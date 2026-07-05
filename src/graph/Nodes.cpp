#include "Nodes.h"
#include <imgui.h>

// 公共：绘制 MaterialParams 滑条，返回是否有变化
static bool drawMaterial(MaterialParams& m, bool hasSSS = false) {
    bool changed = false;
    changed |= ImGui::ColorEdit3("Albedo", &m.albedo.x);
    changed |= ImGui::SliderFloat("Roughness",  &m.roughness,  0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Metallic",   &m.metallic,   0.0f, 1.0f);
    changed |= ImGui::SliderFloat("AO Strength",&m.aoStrength, 0.0f, 1.0f);
    if (hasSSS)
        changed |= ImGui::SliderFloat("SSS Strength",&m.sssStrength, 0.0f, 1.0f);

    ImGui::Spacing();
    ImGui::TextColored({0.8f,0.8f,0.5f,1.0f}, "Textures (drag file path):");

    static char bcBuf[512], roBuf[512], nrBuf[512];
    // basecolor
    strncpy(bcBuf, m.baseColorTex.c_str(), sizeof(bcBuf)-1); bcBuf[sizeof(bcBuf)-1]=0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##bc", bcBuf, sizeof(bcBuf))) {
        m.baseColorTex = bcBuf; changed = true;
    }
    ImGui::SameLine(0,0); ImGui::TextDisabled(" BaseColor");
    if (!m.baseColorTex.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x##bc")) { m.baseColorTex.clear(); changed = true; }
    }

    // roughness/metallic
    strncpy(roBuf, m.roughnessTex.c_str(), sizeof(roBuf)-1); roBuf[sizeof(roBuf)-1]=0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##ro", roBuf, sizeof(roBuf))) {
        m.roughnessTex = roBuf; changed = true;
    }
    ImGui::SameLine(0,0); ImGui::TextDisabled(" Roughness(R)/Metallic(G)");
    if (!m.roughnessTex.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x##ro")) { m.roughnessTex.clear(); changed = true; }
    }

    // normal
    strncpy(nrBuf, m.normalTex.c_str(), sizeof(nrBuf)-1); nrBuf[sizeof(nrBuf)-1]=0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##nr", nrBuf, sizeof(nrBuf))) {
        m.normalTex = nrBuf; changed = true;
    }
    ImGui::SameLine(0,0); ImGui::TextDisabled(" Normal Map");
    if (!m.normalTex.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x##nr")) { m.normalTex.clear(); changed = true; }
    }
    return changed;
}

// ---------- TrunkNode ----------
TrunkNode::TrunkNode() { type = NodeType::Trunk; }

bool TrunkNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length",       &params.length,      1.0f, 15.0f);
        changed |= ImGui::SliderFloat("Start Radius", &params.startRadius, 0.05f, 1.0f);
        changed |= ImGui::SliderFloat("End Radius",   &params.endRadius,   0.01f, 0.5f);
        changed |= ImGui::SliderFloat("Taper",        &params.taper,       0.3f, 1.0f);
        changed |= ImGui::SliderFloat("Base Flare",   &params.baseFlare,   1.0f, 3.0f);
        changed |= ImGui::SliderInt  ("Bend Count",   &params.bendCount,   0, 6);
        changed |= ImGui::SliderFloat("Bend Angle",   &params.bendAngle,   0.0f, 60.0f);
        changed |= ImGui::SliderInt  ("Sides",        &params.sides,       3, 16);
        changed |= ImGui::SliderInt  ("Length Segs",  &params.lengthSegs,  2, 16);
        changed |= ImGui::SliderInt  ("Seed",         &params.seed,        0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_trunk");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- BranchNode ----------
BranchNode::BranchNode() { type = NodeType::Branch; }

bool BranchNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length Ratio",  &params.lengthRatio,  0.1f, 1.0f);
        changed |= ImGui::SliderFloat("Start Radius",  &params.startRadius,  0.01f, 0.3f);
        changed |= ImGui::SliderFloat("End Radius",    &params.endRadius,    0.002f, 0.1f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  10.0f, 90.0f);
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Gravity",       &params.gravity,      0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Bend Count",    &params.bendCount,    0, 5);
        changed |= ImGui::SliderFloat("Bend Angle",    &params.bendAngle,    0.0f, 60.0f);
        changed |= ImGui::SliderInt  ("Branch Count",  &params.branchCount,  1, 8);
        changed |= ImGui::SliderInt  ("Sides",         &params.sides,        3, 12);
        changed |= ImGui::SliderInt  ("Length Segs",   &params.lengthSegs,   2, 8);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_branch");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- TwigNode ----------
TwigNode::TwigNode() { type = NodeType::Twig; }

bool TwigNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length Ratio",  &params.lengthRatio,  0.1f, 1.0f);
        changed |= ImGui::SliderFloat("Start Radius",  &params.startRadius,  0.005f, 0.1f);
        changed |= ImGui::SliderFloat("End Radius",    &params.endRadius,    0.001f, 0.05f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  10.0f, 90.0f);
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Gravity",       &params.gravity,      0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Bend Count",    &params.bendCount,    0, 5);
        changed |= ImGui::SliderFloat("Bend Angle",    &params.bendAngle,    0.0f, 60.0f);
        changed |= ImGui::SliderInt  ("Twig Count",    &params.twigCount,    1, 10);
        changed |= ImGui::SliderInt  ("Sides",         &params.sides,        3, 8);
        changed |= ImGui::SliderInt  ("Length Segs",   &params.lengthSegs,   2, 6);
        changed |= ImGui::Checkbox   ("Alternating",   &params.alternating);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_twig");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- LeafClusterNode ----------
LeafClusterNode::LeafClusterNode() { type = NodeType::LeafCluster; }

bool LeafClusterNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt  ("Leaf Count",     &params.leafCount,     4, 64);
        changed |= ImGui::SliderFloat("Cluster Radius", &params.clusterRadius, 0.05f, 1.0f);
        changed |= ImGui::SliderFloat("Leaf Size",      &params.leafSize,      0.02f, 0.8f);
        changed |= ImGui::SliderFloat("Normal Jitter",  &params.normalJitter,  0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",           &params.seed,          0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_leaf");
        changed |= drawMaterial(params.material, true);  // SSS for leaves
        ImGui::PopID();
    }
    return changed;
}
