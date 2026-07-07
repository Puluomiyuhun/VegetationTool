#include "NodeEditorPanel.h"
#include "graph/Nodes.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <unordered_map>
#include <algorithm>
#include <cstring>

namespace ned = ax::NodeEditor;

ImVec4 NodeEditorPanel::nodeColor(NodeType type) {
    switch (type) {
        case NodeType::Trunk:       return {0.45f, 0.28f, 0.12f, 1.0f};
        case NodeType::Roots:       return {0.30f, 0.20f, 0.14f, 1.0f};
        case NodeType::Branch:      return {0.35f, 0.22f, 0.08f, 1.0f};
        case NodeType::Twig:        return {0.28f, 0.18f, 0.05f, 1.0f};
        case NodeType::LeafCluster: return {0.15f, 0.45f, 0.08f, 1.0f};
        case NodeType::Spine:       return {0.20f, 0.40f, 0.16f, 1.0f};
        case NodeType::Frond:       return {0.14f, 0.50f, 0.10f, 1.0f};
    }
    return {0.3f,0.3f,0.3f,1.0f};
}

void NodeEditorPanel::init() {
    ned::Config cfg;
    cfg.SettingsFile = nullptr;
    m_ctx = ned::CreateEditor(&cfg);
}

void NodeEditorPanel::shutdown() {
    if (m_ctx) { ned::DestroyEditor(m_ctx); m_ctx = nullptr; }
}

// 绘制注释框: 用 ned::Group 生成一个可拖动/可缩放的框, 框住其覆盖范围内的节点;
// 顶部用 BeginGroupHint 画一条随缩放浮动的大标题条, 双击可就地编辑标题文字。
void NodeEditorPanel::drawComment(CommentFrame& c) {
    const float alpha = 0.75f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ned::PushStyleColor(ned::StyleColor_NodeBg,     ImColor(120, 180, 90, 64));
    ned::PushStyleColor(ned::StyleColor_NodeBorder, ImColor(180, 220, 140, 96));

    ned::BeginNode((ned::NodeId)c.id);
    ImGui::PushID((int)c.id);
    ImGui::TextUnformatted(c.text.c_str());
    ned::Group(ImVec2(c.size.x, c.size.y));
    ImGui::PopID();
    ned::EndNode();

    ned::PopStyleColor(2);
    ImGui::PopStyleVar();

    // 记录被 node-editor 反馈的实际大小(用户拖拽缩放后)
    ImVec2 sz = ned::GetNodeSize((ned::NodeId)c.id);
    if (sz.x > 1.0f && sz.y > 1.0f) c.size = {sz.x, sz.y};

    // 顶部浮动标题条 + 双击编辑
    if (ned::BeginGroupHint((ned::NodeId)c.id)) {
        int bgAlpha = (int)(ImGui::GetStyle().Alpha * 255);
        ImVec2 min = ned::GetGroupMin();
        ImGui::SetCursorScreenPos(min - ImVec2(-8, ImGui::GetTextLineHeightWithSpacing() + 4));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(c.text.c_str());
        ImGui::EndGroup();

        ImVec2 tl = ImGui::GetItemRectMin();
        ImVec2 br = ImGui::GetItemRectMax();
        tl.x -= 8; tl.y -= 4; br.x += 8; br.y += 4;
        auto* dl = ned::GetHintBackgroundDrawList();
        dl->AddRectFilled(tl, br, IM_COL32(120, 180, 90, 64 * bgAlpha / 255), 4.0f);
        dl->AddRect(tl, br, IM_COL32(180, 220, 140, 128 * bgAlpha / 255), 4.0f);
    }
    ned::EndGroupHint();
}

void NodeEditorPanel::drawNode(const TreeNode* node) {
    ned::PushStyleColor(ned::StyleColor_NodeBg,
        nodeColor(node->getType()));

    // 用节点ID隔离ImGui命名空间，防止多节点label相同导致的ID冲突
    ImGui::PushID((int)node->id);

    ned::BeginNode((ned::NodeId)node->id);

    ImGui::TextUnformatted(node->getLabel());
    ImGui::Spacing();

    // 输入Pin
    for (const auto& pin : node->inputPins) {
        ned::BeginPin((ned::PinId)pin.id, ned::PinKind::Input);
        ImGui::PushID((int)pin.id);
        ImGui::TextUnformatted("-> In");
        ImGui::PopID();
        ned::EndPin();
    }

    ImGui::SameLine();

    // 输出Pin
    ned::BeginPin((ned::PinId)node->outputPin.id, ned::PinKind::Output);
    ImGui::PushID((int)node->outputPin.id);
    ImGui::TextUnformatted("Out ->");
    ImGui::PopID();
    ned::EndPin();

    ned::EndNode();

    ImGui::PopID();  // node->id

    ned::PopStyleColor();
}

void NodeEditorPanel::render(NodeGraph& graph, NodeId& selectedNodeId) {
    ned::SetCurrentEditor(m_ctx);
    ned::Begin("NodeEditor", ImVec2(0, 0));

    // 首帧设置节点位置
    if (m_firstFrame) {
        for (const auto& [id, node] : graph.nodes()) {
            ned::SetNodePosition((ned::NodeId)id,
                ImVec2(node->editorPos.x, node->editorPos.y));
            m_positioned.insert(id);
        }
        for (auto& c : graph.comments()) {
            ned::SetNodePosition((ned::NodeId)c.id, ImVec2(c.editorPos.x, c.editorPos.y));
            ned::SetGroupSize((ned::NodeId)c.id, ImVec2(c.size.x, c.size.y));
            m_positioned.insert(c.id);
        }
        m_firstFrame = false;
    }

    // 先画注释框(位于节点之下, 作为背景标注), 再画节点
    for (auto& c : graph.comments())
        drawComment(c);

    // 绘制所有节点
    for (const auto& [id, node] : graph.nodes())
        drawNode(node.get());

    // 绘制所有连线
    for (const auto& link : graph.links())
        ned::Link((ned::LinkId)link.id,
                  (ned::PinId)link.fromPin,
                  (ned::PinId)link.toPin);

    // ---- 交互处理 ----
    // 注意: BeginCreate/BeginDelete 即使返回 false 也会置内部 m_InActive=true,
    // 因此 EndCreate/EndDelete 必须无条件调用(在 if 之外), 否则下一帧断言崩溃。
    if (ned::BeginCreate()) {
        ned::PinId fromId, toId;
        if (ned::QueryNewLink(&fromId, &toId)) {
            if (ned::AcceptNewItem()) {
                graph.addLink((PinId)fromId.Get(), (PinId)toId.Get());
            }
        }
    }
    ned::EndCreate();

    if (ned::BeginDelete()) {
        ned::LinkId deletedLinkId;
        while (ned::QueryDeletedLink(&deletedLinkId))
            if (ned::AcceptDeletedItem())
                graph.removeLink((LinkId)deletedLinkId.Get());

        ned::NodeId deletedNodeId;
        while (ned::QueryDeletedNode(&deletedNodeId))
            if (ned::AcceptDeletedItem()) {
                NodeId nid = (NodeId)deletedNodeId.Get();
                // 注释框走独立 ID 段, 需路由到 removeComment; 否则按普通节点删除
                if (graph.isComment(nid)) {
                    graph.removeComment(nid);
                    m_positioned.erase(nid);
                } else {
                    graph.removeNode(nid);
                }
            }
    }
    ned::EndDelete();

    // 选中检测
    // 若外部(视口拾取)改动了 selectedNodeId(与上一帧对外报告值不同), 先把该选中同步进
    // node-editor, 否则下面读回编辑器选中会把视口拾取结果覆盖掉。
    if (selectedNodeId != m_lastReportedSel) {
        ned::ClearSelection();
        if (selectedNodeId != INVALID_NODE)
            ned::SelectNode((ned::NodeId)selectedNodeId, false);
    }
    std::vector<ned::NodeId> sel(ned::GetSelectedObjectCount());
    int count = ned::GetSelectedNodes(sel.data(), (int)sel.size());
    selectedNodeId = (count > 0) ? (NodeId)sel[0].Get() : INVALID_NODE;
    m_lastReportedSel = selectedNodeId;

    // 上一帧粘贴出的新节点：本帧摆位并选中。
    if (!m_pastePendingSelect.empty()) {
        ned::ClearSelection();
        for (NodeId nid : m_pastePendingSelect) {
            if (auto* n = graph.getNode(nid)) {
                ned::SetNodePosition((ned::NodeId)nid, ImVec2(n->editorPos.x, n->editorPos.y));
                m_positioned.insert(nid);
            }
            ned::SelectNode((ned::NodeId)nid, true);
        }
        m_pastePendingSelect.clear();
    }

    // 每帧同步节点位置到 editorPos（支持拖动后持久化）。
    // 新增节点(右键/加子节点/加载)首帧尚未被编辑器定位：先按其 editorPos 定位，
    // 加入 m_positioned 后再参与“读回位置”，否则 GetNodePosition 返回(0,0)会把坐标覆盖掉。
    for (auto& [id, node] : graph.nodes()) {
        if (m_positioned.find(id) == m_positioned.end()) {
            ned::SetNodePosition((ned::NodeId)id, ImVec2(node->editorPos.x, node->editorPos.y));
            m_positioned.insert(id);
            continue;
        }
        ImVec2 pos = ned::GetNodePosition((ned::NodeId)id);
        node->editorPos = {pos.x, pos.y};
    }

    // 注释框位置/尺寸同步（同节点逻辑）：新注释框首帧先按 editorPos+size 定位，
    // 之后每帧读回位置持久化到 editorPos。
    for (auto& c : graph.comments()) {
        if (m_positioned.find(c.id) == m_positioned.end()) {
            ned::SetNodePosition((ned::NodeId)c.id, ImVec2(c.editorPos.x, c.editorPos.y));
            ned::SetGroupSize((ned::NodeId)c.id, ImVec2(c.size.x, c.size.y));
            m_positioned.insert(c.id);
            continue;
        }
        ImVec2 pos = ned::GetNodePosition((ned::NodeId)c.id);
        c.editorPos = {pos.x, pos.y};
    }

    // 复制/粘贴快捷键（Ctrl+C / Ctrl+V）。仅在编辑器获得焦点时响应。
    if (ned::GetSelectedObjectCount() >= 0) {
        ImGuiIO& io = ImGui::GetIO();
        bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            copySelected(graph);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            auto ids = pasteClipboard(graph, {40.0f, 40.0f});
            m_pastePendingSelect = ids;
        }
    }

    // 双击注释框 → 进入标题编辑（打开 InputText 弹窗）。
    ned::NodeId dbl = ned::GetDoubleClickedNode();
    if (dbl && graph.isComment((NodeId)dbl.Get())) {
        m_editingComment = (NodeId)dbl.Get();
        if (auto* c = graph.getComment(m_editingComment)) {
            std::strncpy(m_editBuf, c->text.c_str(), sizeof(m_editBuf) - 1);
            m_editBuf[sizeof(m_editBuf) - 1] = '\0';
        }
        m_editJustOpened = true;
    }
    if (m_editingComment != INVALID_NODE) {
        ned::Suspend();
        if (m_editJustOpened) {
            ImGui::OpenPopup("EditCommentTitle");
            m_editJustOpened = false;
        }
        if (ImGui::BeginPopup("EditCommentTitle")) {
            ImGui::TextUnformatted("Comment");
            ImGui::SetNextItemWidth(240.0f);
            ImGui::SetKeyboardFocusHere();
            bool done = ImGui::InputText("##ctext", m_editBuf, sizeof(m_editBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
            if (auto* c = graph.getComment(m_editingComment))
                c->text = m_editBuf;
            if (done) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        } else {
            m_editingComment = INVALID_NODE;
        }
        ned::Resume();
    }

    // 右键菜单
    handleContextMenu(graph);

    ned::End();
    ned::SetCurrentEditor(nullptr);
}

// 复制当前选中的节点到剪贴板：克隆参数并记录它们之间的内部连线（相对索引）
void NodeEditorPanel::copySelected(NodeGraph& graph) {
    std::vector<ned::NodeId> sel(ned::GetSelectedObjectCount());
    int count = ned::GetSelectedNodes(sel.data(), (int)sel.size());
    if (count <= 0) return;

    m_clipboard.clear();
    m_clipboardEdges.clear();

    // 原节点 id -> 剪贴板索引
    std::unordered_map<NodeId, int> idToIdx;
    for (int i = 0; i < count; ++i) {
        NodeId nid = (NodeId)sel[i].Get();
        auto* node = graph.getNode(nid);
        if (!node) continue;
        idToIdx[nid] = (int)m_clipboard.size();
        m_clipboard.push_back(node->clone());
    }

    // 记录选区内部的连线（父输出 -> 子输入 两端都在选区中）
    for (const auto& link : graph.links()) {
        // 找 fromPin/toPin 所属节点
        NodeId fromNode = INVALID_NODE, toNode = INVALID_NODE;
        for (const auto& [id, node] : graph.nodes()) {
            if (node->outputPin.id == link.fromPin) fromNode = id;
            for (const auto& p : node->inputPins)
                if (p.id == link.toPin) toNode = id;
        }
        auto fIt = idToIdx.find(fromNode);
        auto tIt = idToIdx.find(toNode);
        if (fIt != idToIdx.end() && tIt != idToIdx.end())
            m_clipboardEdges.push_back({fIt->second, tIt->second});
    }
}

// 把剪贴板内容粘贴到画布：新建同类型节点、灌入参数、按 offset 平移、重建内部连线
std::vector<NodeId> NodeEditorPanel::pasteClipboard(NodeGraph& graph, glm::vec2 offset) {
    std::vector<NodeId> newIds;
    if (m_clipboard.empty()) return newIds;

    // 剪贴板索引 -> 新建节点 id
    std::vector<NodeId> idxToNew(m_clipboard.size(), INVALID_NODE);
    for (size_t i = 0; i < m_clipboard.size(); ++i) {
        const TreeNode* src = m_clipboard[i].get();
        glm::vec2 pos = src->editorPos + offset;
        NodeId nid = graph.addNode(src->getType(), pos);
        auto* dst = graph.getNode(nid);
        if (dst) dst->copyParamsFrom(src);
        idxToNew[i] = nid;
        newIds.push_back(nid);
    }

    // 重建内部连线
    for (const auto& e : m_clipboardEdges) {
        auto* parent = graph.getNode(idxToNew[e.first]);
        auto* child  = graph.getNode(idxToNew[e.second]);
        if (parent && child && !child->inputPins.empty())
            graph.addLink(parent->outputPin.id, child->inputPins[0].id);
    }
    return newIds;
}

void NodeEditorPanel::handleContextMenu(NodeGraph& graph) {
    ned::Suspend();
    // node-editor 专用的背景右键检测：命中空白画布时打开新增菜单，
    // 并把当前鼠标屏幕坐标转换为画布坐标，作为新节点落点(否则用屏幕坐标会偏得很远)。
    if (ned::ShowBackgroundContextMenu()) {
        ImVec2 canvas = ned::ScreenToCanvas(ImGui::GetMousePos());
        m_contextCanvasPos = {canvas.x, canvas.y};
        ImGui::OpenPopup("NodeCanvasContextMenu");
    }

    if (ImGui::BeginPopup("NodeCanvasContextMenu")) {
        glm::vec2 canvasPos = m_contextCanvasPos;

        if (ImGui::MenuItem("Add Trunk"))
            graph.addNode(NodeType::Trunk, canvasPos);
        if (ImGui::MenuItem("Add Roots"))
            graph.addNode(NodeType::Roots, canvasPos);
        if (ImGui::MenuItem("Add Branch"))
            graph.addNode(NodeType::Branch, canvasPos);
        if (ImGui::MenuItem("Add Twig"))
            graph.addNode(NodeType::Twig, canvasPos);
        if (ImGui::MenuItem("Add Leaf Cluster"))
            graph.addNode(NodeType::LeafCluster, canvasPos);
        if (ImGui::MenuItem("Add Spine"))
            graph.addNode(NodeType::Spine, canvasPos);
        if (ImGui::MenuItem("Add Frond"))
            graph.addNode(NodeType::Frond, canvasPos);

        ImGui::Separator();
        if (ImGui::MenuItem("Add Comment"))
            graph.addComment(canvasPos);

        ImGui::Separator();
        bool hasSel = ned::GetSelectedObjectCount() > 0;
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSel))
            copySelected(graph);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) {
            // 粘贴到鼠标位置：以剪贴板首节点为基准对齐到光标
            glm::vec2 off = m_clipboard.empty()
                ? glm::vec2(40.0f, 40.0f)
                : canvasPos - m_clipboard.front()->editorPos;
            m_pastePendingSelect = pasteClipboard(graph, off);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Reset to Default"))
            graph.buildDefaultTemplate();

        ImGui::EndPopup();
    }
    ned::Resume();
}
