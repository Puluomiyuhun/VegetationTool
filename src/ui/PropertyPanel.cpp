#include "PropertyPanel.h"
#include <imgui.h>

void PropertyPanel::render(NodeId selectedNodeId, NodeGraph& graph) {
    ImGui::Begin("Properties");

    if (selectedNodeId == INVALID_NODE) {
        ImGui::TextDisabled("点击节点选中后可在此编辑参数");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored({0.7f,0.8f,1.0f,1.0f}, "操作说明:");
        ImGui::BulletText("左键单击节点 - 选中");
        ImGui::BulletText("左键拖动节点 - 移动");
        ImGui::BulletText("从Out拖向In - 连线");
        ImGui::BulletText("右键空白处 - 添加节点");
        ImGui::BulletText("右键节点 - 删除节点");
        ImGui::BulletText("Delete键 - 删除选中");
        ImGui::BulletText("滚轮 - 缩放节点图");
        ImGui::End();
        return;
    }

    TreeNode* node = graph.getNode(selectedNodeId);
    if (!node) {
        ImGui::End();
        return;
    }

    ImGui::Text("[ %s ]  (id: %u)", node->getLabel(), node->id);
    ImGui::Separator();
    ImGui::Spacing();

    if (node->drawProperties())
        graph.markDirty();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 快速添加子节点
    ImGui::TextColored({0.7f,0.9f,0.7f,1.0f}, "添加子节点:");
    ImGui::SameLine();
    ImGui::TextDisabled("(自动连线到此节点)");

    auto addChildBtn = [&](const char* label, NodeType type) {
        if (ImGui::Button(label)) {
            NodeId child = graph.addChildNode(selectedNodeId, type);
            (void)child;
            graph.markDirty();
        }
    };

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f,0.22f,0.08f,1.0f));
    addChildBtn("+ Branch", NodeType::Branch);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f,0.14f,0.05f,1.0f));
    addChildBtn("+ Twig", NodeType::Twig);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f,0.42f,0.08f,1.0f));
    addChildBtn("+ Leaf", NodeType::LeafCluster);
    ImGui::PopStyleColor();

    ImGui::End();
}
