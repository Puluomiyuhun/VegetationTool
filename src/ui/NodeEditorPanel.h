#pragma once
#include "graph/NodeGraph.h"
#include <imgui_node_editor.h>
#include <memory>
#include <vector>
#include <utility>

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

    // ---- 复制/粘贴剪贴板 ----
    // 暂存被复制节点的克隆(含参数与相对位置)与它们之间的内部连线
    std::vector<std::unique_ptr<TreeNode>> m_clipboard;
    std::vector<std::pair<int,int>>        m_clipboardEdges; // (parentIdx, childIdx)
    void copySelected(NodeGraph& graph);
    // 粘贴到画布，返回新建节点的 id 列表(供选中高亮)
    std::vector<NodeId> pasteClipboard(NodeGraph& graph, glm::vec2 offset);

    bool m_firstFrame = true;
    // 粘贴后需要在下一帧重新定位/选中的新节点
    std::vector<NodeId> m_pastePendingSelect;
};
