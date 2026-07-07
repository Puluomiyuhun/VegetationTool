#include "Nodes.h"
#include <imgui.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
// 打开系统文件选择对话框，返回选中的绝对路径；取消则返回空串
static std::string openImageFileDialog() {
    char szFile[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn))
        return std::string(szFile);
    return std::string();
}
#else
static std::string openImageFileDialog() { return std::string(); }
#endif

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

    // 每张贴图一行：标签在上，只读路径框 + 浏览(...) + 清除(x)
    auto texRow = [&](const char* label, const char* id, std::string& path) {
        ImGui::TextDisabled("%s", label);
        char buf[512];
        strncpy(buf, path.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;

        // 路径框只读（避免手输错误路径），点击浏览按钮选择文件
        ImGui::SetNextItemWidth(-56.0f);  // 给右侧两个按钮留位
        ImGui::InputText(id, buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);

        ImGui::SameLine();
        char browseBtn[16]; snprintf(browseBtn, sizeof(browseBtn), "...%s", id);
        if (ImGui::Button(browseBtn)) {
            std::string sel = openImageFileDialog();
            if (!sel.empty()) { path = sel; changed = true; }
        }

        ImGui::SameLine();
        char clrBtn[16]; snprintf(clrBtn, sizeof(clrBtn), "x%s", id);
        if (ImGui::Button(clrBtn)) {
            if (!path.empty()) { path.clear(); changed = true; }
        }
    };

    texRow("BaseColor",                "##bc", m.baseColorTex);
    texRow("Roughness(R)/Metallic(G)", "##ro", m.roughnessTex);
    texRow("Normal Map",               "##nr", m.normalTex);
    if (hasSSS) {
        // 叶片：不透明度遮罩 + alpha 剔除阈值
        texRow("Opacity Mask (R)",     "##op", m.opacityTex);
        changed |= ImGui::SliderFloat("Alpha Cutoff", &m.alphaCutoff, 0.0f, 1.0f);
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
        changed |= ImGui::SliderFloat("Base Flare",   &params.baseFlare,   1.0f, 3.0f);
        changed |= ImGui::SliderFloat("Taper Power",  &params.taperPow,    0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Noise Amount", &params.noiseAmount, 0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",   &params.noiseFreq,   0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",        &params.gnarl,       0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Joint Count",  &params.jointCount,  0, 20);
        changed |= ImGui::SliderFloat("Joint Bulge",  &params.jointBulge,  0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Sides",        &params.sides,       3, 16);
        changed |= ImGui::SliderInt  ("Length Segs",  &params.lengthSegs,  2, 24);
        changed |= ImGui::SliderFloat("UV Tiling U",  &params.uvTilingU,   0.1f, 20.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",  &params.uvTilingV,   0.1f, 20.0f);
        changed |= ImGui::SliderInt  ("Seed",         &params.seed,        0, 999);
    }
    if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 植株在场景中的位置(一个工程内可摆放多棵独立植被)
        changed |= ImGui::SliderFloat("Pos X", &params.posX, -50.0f, 50.0f);
        changed |= ImGui::SliderFloat("Pos Z", &params.posZ, -50.0f, 50.0f);
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
    // Generation：生成规律 —— 生成多少、方位如何分布
    if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* kModeNames[] = {
            "Classic", "Proportional", "Proportional steps", "Absolute", "Absolute steps",
            "Phyllotaxy", "Interval", "Bifurcation", "Flood", "Parent"
        };
        int modeIdx = (int)params.mode;
        if (ImGui::Combo("Mode", &modeIdx, kModeNames, IM_ARRAYSIZE(kModeNames))) {
            params.mode = (BranchMode)modeIdx;
            changed = true;
        }
        if (params.mode == BranchMode::Interval) {
            // Interval(竹节式): 枝条只长在固定间距的节上
            changed |= ImGui::SliderFloat("Interval Spacing", &params.intervalSpacing, 0.02f, 0.5f);
            changed |= ImGui::SliderInt  ("Branches / Node",  &params.branchesPerNode, 1, 6);
        } else {
            changed |= ImGui::SliderInt  ("Branch Count",  &params.branchCount,  1, 8);
        }
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Region Start",  &params.regionStart,  0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Region End",    &params.regionEnd,    0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Size Falloff",  &params.sizeFalloff,  0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    // Spine：单根枝条形态 —— 长度、粗细、下垂、弯曲
    if (ImGui::CollapsingHeader("Spine", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length Ratio",  &params.lengthRatio,  0.1f, 1.0f);
        changed |= ImGui::SliderFloat("Radius Scale",   &params.radiusScale,  0.1f, 2.0f);
        changed |= ImGui::SliderFloat("End Ratio",      &params.endRatio,     0.01f, 1.0f);
        changed |= ImGui::SliderFloat("Base Flare",     &params.baseFlare,    1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Taper Power",    &params.taperPow,     0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  10.0f, 90.0f);
        changed |= ImGui::SliderFloat("Gravity",       &params.gravity,      0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Noise Amount",   &params.noiseAmount,  0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",     &params.noiseFreq,    0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",          &params.gnarl,        0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Joint Count",    &params.jointCount,   0, 20);
        changed |= ImGui::SliderFloat("Joint Bulge",    &params.jointBulge,   0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Sides",         &params.sides,        3, 12);
        changed |= ImGui::SliderInt  ("Length Segs",   &params.lengthSegs,   2, 16);
        changed |= ImGui::SliderFloat("UV Tiling U",   &params.uvTilingU,    0.1f, 20.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",   &params.uvTilingV,    0.1f, 20.0f);
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
    // Generation：生成规律
    if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt  ("Twig Count",    &params.twigCount,    1, 10);
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Region Start",  &params.regionStart,  0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Region End",    &params.regionEnd,    0.0f, 1.0f);
        changed |= ImGui::Checkbox   ("Alternating",   &params.alternating);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    // Spine：单根细枝形态
    if (ImGui::CollapsingHeader("Spine", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length Ratio",  &params.lengthRatio,  0.1f, 1.0f);
        changed |= ImGui::SliderFloat("Radius Scale",   &params.radiusScale,  0.1f, 2.0f);
        changed |= ImGui::SliderFloat("End Ratio",      &params.endRatio,     0.01f, 1.0f);
        changed |= ImGui::SliderFloat("Base Flare",     &params.baseFlare,    1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Taper Power",    &params.taperPow,     0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  10.0f, 90.0f);
        changed |= ImGui::SliderFloat("Gravity",       &params.gravity,      0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Noise Amount",   &params.noiseAmount,  0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",     &params.noiseFreq,    0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",          &params.gnarl,        0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Sides",         &params.sides,        3, 8);
        changed |= ImGui::SliderInt  ("Length Segs",   &params.lengthSegs,   2, 12);
        changed |= ImGui::SliderFloat("UV Tiling U",   &params.uvTilingU,    0.1f, 20.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",   &params.uvTilingV,    0.1f, 20.0f);
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
        changed |= ImGui::SliderFloat("Leaf Aspect",    &params.leafAspect,    0.1f, 1.5f);
        changed |= ImGui::SliderFloat("Normal Jitter",  &params.normalJitter,  0.0f, 1.0f);
        changed |= ImGui::Checkbox   ("Planar (fern)",  &params.planar);
        changed |= ImGui::SliderFloat("Size Falloff",   &params.sizeFalloff,   0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",           &params.seed,          0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_leaf");
        changed |= drawMaterial(params.material, true);  // SSS for leaves
        ImGui::PopID();
    }
    return changed;
}

// ---------- RootsNode ----------
RootsNode::RootsNode() { type = NodeType::Roots; }

bool RootsNode::drawProperties() {
    bool changed = false;
    // Generation：生成规律 —— 根条数、方位分布
    if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt  ("Root Count",    &params.rootCount,    1, 16);
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    // Spine：单根根系形态 —— 长度、粗细、张开、下扎
    if (ImGui::CollapsingHeader("Spine", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length",       &params.length,      0.5f, 10.0f);
        changed |= ImGui::SliderFloat("Radius Scale", &params.radiusScale, 0.1f, 2.0f);
        changed |= ImGui::SliderFloat("End Ratio",    &params.endRatio,    0.01f, 0.5f);
        changed |= ImGui::SliderFloat("Taper Power",  &params.taperPow,    0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Base Flare",   &params.baseFlare,   1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Collar Sink",  &params.collarSink,  0.0f, 0.8f);
        changed |= ImGui::SliderFloat("Spread Angle", &params.spreadAngle, 30.0f, 150.0f);
        changed |= ImGui::SliderFloat("Gravity",      &params.gravity,     0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Noise Amount", &params.noiseAmount, 0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",   &params.noiseFreq,   0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",        &params.gnarl,       0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Joint Count",  &params.jointCount,  0, 20);
        changed |= ImGui::SliderFloat("Joint Bulge",  &params.jointBulge,  0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Sides",        &params.sides,       3, 12);
        changed |= ImGui::SliderInt  ("Length Segs",  &params.lengthSegs,  2, 20);
        changed |= ImGui::SliderFloat("UV Tiling U",  &params.uvTilingU,   0.1f, 20.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",  &params.uvTilingV,   0.1f, 20.0f);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_roots");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- SpineNode ----------
SpineNode::SpineNode() { type = NodeType::Spine; }

bool SpineNode::drawProperties() {
    bool changed = false;
    // Generation：叶轴条数、方位分布
    if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt  ("Spine Count",   &params.spineCount,   1, 12);
        changed |= ImGui::SliderFloat("Rotate Offset", &params.rotateOffset, 0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Region Start",  &params.regionStart,  0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Region End",    &params.regionEnd,    0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 999);
    }
    // Spine：单条叶轴形态
    if (ImGui::CollapsingHeader("Spine", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Length Ratio",  &params.lengthRatio,  0.1f, 1.5f);
        changed |= ImGui::SliderFloat("Radius Scale",  &params.radiusScale,  0.05f, 1.0f);
        changed |= ImGui::SliderFloat("End Ratio",     &params.endRatio,     0.01f, 1.0f);
        changed |= ImGui::SliderFloat("Taper Power",   &params.taperPow,     0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  10.0f, 90.0f);
        changed |= ImGui::SliderFloat("Gravity",       &params.gravity,      0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Noise Amount",  &params.noiseAmount,  0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",    &params.noiseFreq,    0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",         &params.gnarl,        0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Sides",         &params.sides,        3, 10);
        changed |= ImGui::SliderInt  ("Length Segs",   &params.lengthSegs,   3, 24);
        changed |= ImGui::SliderFloat("UV Tiling U",   &params.uvTilingU,    0.1f, 10.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",   &params.uvTilingV,    0.1f, 10.0f);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_spine");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- FrondNode ----------
FrondNode::FrondNode() { type = NodeType::Frond; }

bool FrondNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Width",        &params.width,        0.02f, 2.0f);
        changed |= ImGui::SliderFloat("Width Base",   &params.widthBase,    0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Width Tip",    &params.widthTip,     0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Profile Power",&params.profilePow,   0.2f, 3.0f);
        changed |= ImGui::SliderFloat("Curl",         &params.curl,         -1.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Segs Per Side",&params.segsPerSide,  1, 6);
        changed |= ImGui::Checkbox   ("Serrate",      &params.serrate);
        changed |= ImGui::SliderFloat("Serrate Depth",&params.serrateDepth, 0.0f, 0.8f);
        changed |= ImGui::SliderInt  ("Seed",         &params.seed,         0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_frond");
        changed |= drawMaterial(params.material, true);  // SSS for leaves
        ImGui::PopID();
    }
    return changed;
}
