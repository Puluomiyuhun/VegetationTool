#pragma once
#include "graph/NodeGraph.h"
#include <imgui_node_editor.h>
#include <memory>
#include <vector>
#include <utility>
#include <unordered_set>

namespace ned = ax::NodeEditor;

class NodeEditorPanel {
public:
    void init();
    void shutdown();
    void render(NodeGraph& graph, NodeId& selectedNodeId);

    // 读取工程后调用：强制下一帧按各节点 editorPos 重新摆放
    void requestReposition() { m_firstFrame = true; m_positioned.clear(); }

private:
    ned::EditorContext* m_ctx = nullptr;

    // 节点颜色
    static ImVec4 nodeColor(NodeType type);
    void drawNode(const TreeNode* node);
    void drawComment(CommentFrame& c);   // 绘制注释框(Group)及顶部可编辑标题
    void handleContextMenu(NodeGraph& graph);

    // ---- 复制/粘贴剪贴板 ----
    // 暂存被复制节点的克隆(含参数与相对位置)与它们之间的内部连线
    std::vector<std::unique_ptr<TreeNode>> m_clipboard;
    std::vector<std::pair<int,int>>        m_clipboardEdges; // (parentIdx, childIdx)
    void copySelected(NodeGraph& graph);
    // 粘贴到画布，返回新建节点的 id 列表(供选中高亮)
    std::vector<NodeId> pasteClipboard(NodeGraph& graph, glm::vec2 offset);

    bool m_firstFrame = true;
    // 上一帧对外报告的选中节点: 用于检测外部(视口拾取)改动 selectedNodeId, 从而把
    // 选中同步进 node-editor(否则编辑器每帧用自己的选中覆盖掉视口拾取结果)。
    NodeId m_lastReportedSel = INVALID_NODE;
    // 粘贴后需要在下一帧重新定位/选中的新节点
    std::vector<NodeId> m_pastePendingSelect;
    // 已被 node-editor 定位过的节点集合：新节点(不在此集合)先按 editorPos 定位，
    // 定位后才纳入“每帧同步位置”逻辑，避免新节点首帧 GetNodePosition 返回(0,0)覆盖坐标。
    std::unordered_set<NodeId> m_positioned;
    // 画布空间下的右键新增位置(由背景右键菜单记录)
    glm::vec2 m_contextCanvasPos = {0.0f, 0.0f};
    // 正在编辑标题的注释框 id(双击进入)，INVALID_NODE 表示无
    NodeId m_editingComment = INVALID_NODE;
    char   m_editBuf[256] = {0};
    bool   m_editJustOpened = false;
};
