#include "NodeEditorPanel.h"
#include "graph/Nodes.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <unordered_map>
#include <algorithm>

namespace ned = ax::NodeEditor;

ImVec4 NodeEditorPanel::nodeColor(NodeType type) {
    switch (type) {
        case NodeType::Trunk:       return {0.45f, 0.28f, 0.12f, 1.0f};
        case NodeType::Roots:       return {0.30f, 0.20f, 0.14f, 1.0f};
        case NodeType::Branch:      return {0.35f, 0.22f, 0.08f, 1.0f};
        case NodeType::Twig:        return {0.28f, 0.18f, 0.05f, 1.0f};
        case NodeType::LeafCluster: return {0.15f, 0.45f, 0.08f, 1.0f};
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
        for (const auto& [id, node] : graph.nodes())
            ned::SetNodePosition((ned::NodeId)id,
                ImVec2(node->editorPos.x, node->editorPos.y));
        m_firstFrame = false;
    }

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
            if (ned::AcceptDeletedItem())
                graph.removeNode((NodeId)deletedNodeId.Get());
    }
    ned::EndDelete();

    // 选中检测
    std::vector<ned::NodeId> sel(ned::GetSelectedObjectCount());
    int count = ned::GetSelectedNodes(sel.data(), (int)sel.size());
    selectedNodeId = (count > 0) ? (NodeId)sel[0].Get() : INVALID_NODE;

    // 上一帧粘贴出的新节点：本帧摆位并选中。
    // 必须在“同步 editorPos”循环之前处理：新节点首帧尚未被编辑器定位(默认0,0)，
    // 若先跑同步循环会把粘贴时算好的 editorPos 覆盖成 0,0，导致所有节点叠在一起。
    if (!m_pastePendingSelect.empty()) {
        ned::ClearSelection();
        for (NodeId nid : m_pastePendingSelect) {
            if (auto* n = graph.getNode(nid))
                ned::SetNodePosition((ned::NodeId)nid, ImVec2(n->editorPos.x, n->editorPos.y));
            ned::SelectNode((ned::NodeId)nid, true);
        }
        m_pastePendingSelect.clear();
    }

    // 每帧同步节点位置到 editorPos（支持拖动后持久化）
    for (auto& [id, node] : graph.nodes()) {
        ImVec2 pos = ned::GetNodePosition((ned::NodeId)id);
        node->editorPos = {pos.x, pos.y};
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
    if (ImGui::BeginPopupContextWindow()) {
        ImVec2 mousePos = ImGui::GetMousePosOnOpeningCurrentPopup();
        glm::vec2 canvasPos = {mousePos.x, mousePos.y};

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
