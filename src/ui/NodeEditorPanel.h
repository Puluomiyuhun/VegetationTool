#pragma once
#include "graph/NodeGraph.h"
#include <imgui_node_editor.h>

namespace ned = ax::NodeEditor;

class NodeEditorPanel {
public:
    void init();
    void shutdown();
    void render(NodeGraph& graph, NodeId& selectedNodeId);

    // 读取工程后调用：强制下一帧按各节点 editorPos 重新摆放
    void requestReposition() { m_firstFrame = true; }

private:
    ned::EditorContext* m_ctx = nullptr;

    // 节点颜色
    static ImVec4 nodeColor(NodeType type);
    void drawNode(const TreeNode* node);
    void handleContextMenu(NodeGraph& graph);

    bool m_firstFrame = true;
};
