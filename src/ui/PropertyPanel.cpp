#include "PropertyPanel.h"
#include <imgui.h>

void PropertyPanel::render(NodeId selectedNodeId, NodeGraph& graph) {
    ImGui::Begin("Properties");

    if (selectedNodeId == INVALID_NODE) {
        ImGui::TextDisabled("Click a node to edit its parameters here.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored({0.7f,0.8f,1.0f,1.0f}, "Controls:");
        ImGui::BulletText("Left-click node       - Select");
        ImGui::BulletText("Left-drag node        - Move");
        ImGui::BulletText("Drag Out -> In        - Connect");
        ImGui::BulletText("Right-click canvas    - Add node");
        ImGui::BulletText("Right-click node      - Delete node");
        ImGui::BulletText("Delete key            - Delete selected");
        ImGui::BulletText("Scroll wheel          - Zoom graph");
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

    // Add child node shortcuts
    ImGui::TextColored({0.7f,0.9f,0.7f,1.0f}, "Add Child:");
    ImGui::SameLine();
    ImGui::TextDisabled("(auto-wired to this node)");

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

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f,0.20f,0.14f,1.0f));
    addChildBtn("+ Roots", NodeType::Roots);
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
