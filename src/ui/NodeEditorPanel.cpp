#include "NodeEditorPanel.h"
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ned = ax::NodeEditor;

ImVec4 NodeEditorPanel::nodeColor(NodeType type) {
    switch (type) {
        case NodeType::Trunk:       return {0.45f, 0.28f, 0.12f, 1.0f};
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
    if (ned::BeginCreate()) {
        ned::PinId fromId, toId;
        if (ned::QueryNewLink(&fromId, &toId)) {
            if (ned::AcceptNewItem()) {
                graph.addLink((PinId)fromId.Get(), (PinId)toId.Get());
            }
        }
        ned::EndCreate();
    }

    if (ned::BeginDelete()) {
        ned::LinkId deletedLinkId;
        while (ned::QueryDeletedLink(&deletedLinkId))
            if (ned::AcceptDeletedItem())
                graph.removeLink((LinkId)deletedLinkId.Get());

        ned::NodeId deletedNodeId;
        while (ned::QueryDeletedNode(&deletedNodeId))
            if (ned::AcceptDeletedItem())
                graph.removeNode((NodeId)deletedNodeId.Get());

        ned::EndDelete();
    }

    // 选中检测
    std::vector<ned::NodeId> sel(ned::GetSelectedObjectCount());
    int count = ned::GetSelectedNodes(sel.data(), (int)sel.size());
    selectedNodeId = (count > 0) ? (NodeId)sel[0].Get() : INVALID_NODE;

    // 每帧同步节点位置到 editorPos（支持拖动后持久化）
    for (auto& [id, node] : graph.nodes()) {
        ImVec2 pos = ned::GetNodePosition((ned::NodeId)id);
        node->editorPos = {pos.x, pos.y};
    }

    // 右键菜单
    handleContextMenu(graph);

    ned::End();
    ned::SetCurrentEditor(nullptr);
}

void NodeEditorPanel::handleContextMenu(NodeGraph& graph) {
    ned::Suspend();
    if (ImGui::BeginPopupContextWindow()) {
        ImVec2 mousePos = ImGui::GetMousePosOnOpeningCurrentPopup();
        glm::vec2 canvasPos = {mousePos.x, mousePos.y};

        if (ImGui::MenuItem("Add Trunk"))
            graph.addNode(NodeType::Trunk, canvasPos);
        if (ImGui::MenuItem("Add Branch"))
            graph.addNode(NodeType::Branch, canvasPos);
        if (ImGui::MenuItem("Add Twig"))
            graph.addNode(NodeType::Twig, canvasPos);
        if (ImGui::MenuItem("Add Leaf Cluster"))
            graph.addNode(NodeType::LeafCluster, canvasPos);

        ImGui::Separator();
        if (ImGui::MenuItem("Reset to Default"))
            graph.buildDefaultTemplate();

        ImGui::EndPopup();
    }
    ned::Resume();
}
