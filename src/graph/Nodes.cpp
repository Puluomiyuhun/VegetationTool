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
// 网格保存对话框：返回选中的保存路径(自动补扩展名)；取消返回空串。fbx=true 时过滤 FBX。
static std::string saveMeshFileDialog(const std::string& initial, bool fbx) {
    char szFile[1024] = {0};
    strncpy(szFile, initial.c_str(), sizeof(szFile)-1);
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrFilter = fbx ? "FBX (ASCII)\0*.fbx\0All\0*.*\0"
                          : "Wavefront OBJ\0*.obj\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = fbx ? "fbx" : "obj";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn))
        return std::string(szFile);
    return std::string();
}
#else
static std::string openImageFileDialog() { return std::string(); }
static std::string saveMeshFileDialog(const std::string&, bool) { return std::string(); }
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
        changed |= ImGui::SliderFloat("Normal Soften",  &params.normalSoften,  0.0f, 1.0f);
        changed |= ImGui::Checkbox   ("Planar (fern)",  &params.planar);
        changed |= ImGui::SliderFloat("Size Falloff",   &params.sizeFalloff,   0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",           &params.seed,          0, 999);
    }
    if (ImGui::CollapsingHeader("Cutout Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 轮廓裁剪网格: 用贴合剪影的三角网格代替四边形卡片, 降低透明像素 overdraw。
        changed |= ImGui::Checkbox("Use Cutout Mesh", &params.useCutout);
        if (ImGui::Button("Edit Cutout Mesh...")) params.requestEditCutout = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%d pts / %d tris)",
            (int)params.cutoutPoints.size(), (int)params.cutoutTris.size() / 3);
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
    if (ImGui::CollapsingHeader("Cutout Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 轮廓裁剪网格: 用贴合剪影的三角网格代替整片叶带矩形, 降低透明像素 overdraw。
        changed |= ImGui::Checkbox("Use Cutout Mesh", &params.useCutout);
        if (ImGui::Button("Edit Cutout Mesh...")) params.requestEditCutout = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%d pts / %d tris)",
            (int)params.cutoutPoints.size(), (int)params.cutoutTris.size() / 3);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_frond");
        changed |= drawMaterial(params.material, true);  // SSS for leaves
        ImGui::PopID();
    }
    return changed;
}

// ---------- ExportNode ----------
ExportNode::ExportNode() { type = NodeType::Export; }

bool ExportNode::drawProperties() {
    ImGui::TextWrapped("把上游连接的节点导出为三维网格 (含材质与贴图)。");
    ImGui::Spacing();

    // 导出格式
    ImGui::TextDisabled("导出格式");
    int prevFormat = params.format;
    ImGui::RadioButton("OBJ (+ .mtl 材质)", &params.format, 0);
    ImGui::SameLine();
    ImGui::RadioButton("FBX (含材质/贴图)", &params.format, 1);
    if (params.format != prevFormat) {
        // 切换格式时同步规整路径扩展名
        const char* want = params.format == 1 ? ".fbx" : ".obj";
        size_t dot = params.path.find_last_of('.');
        size_t slash = params.path.find_last_of("/\\");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            params.path = params.path.substr(0, dot);
        params.path += want;
    }
    ImGui::Spacing();

    ImGui::TextDisabled("导出模式");
    ImGui::RadioButton("当前节点及下游 (竖直标本)", &params.exportMode, 0);
    ImGui::RadioButton("整株 (追溯到根 Trunk)",      &params.exportMode, 1);
    ImGui::RadioButton("当前节点及上游 (祖先链)",     &params.exportMode, 2);
    ImGui::Spacing();

    if (params.exportMode == 1) {
        ImGui::TextDisabled("从上游节点向上追溯到根 Trunk, 导出完整整株。");
    } else if (params.exportMode == 2) {
        ImGui::TextWrapped("从上游节点沿输入链一路追溯到根, 只导出这条祖先链上的"
                           "节点(不含它们的其它分支/叶), 保持在场景中的原始位姿。");
    } else {
        ImGui::TextWrapped("以上游节点为标本根, 导出该节点及其全部下游子枝叶组成的"
                           "竖直标本 (根部在原点, 主枝沿 +Y 挺立), 供 UE5 PCG 用。");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt("标本数量", &params.specimenCount, 1, 50);
        ImGui::TextDisabled("每个标本用不同随机种子生成一个形态变体。");
        int layout = params.singleFile ? 0 : 1;
        ImGui::RadioButton("合并到一个文件 (并排)", &layout, 0);
        ImGui::SameLine();
        ImGui::RadioButton("每个一个文件 (_序号)", &layout, 1);
        params.singleFile = (layout == 0);
        if (params.singleFile && params.specimenCount > 1) {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("并排间距", &params.specimenSpacing, 0.5f, 20.0f, "%.1f");
        }
    }
    ImGui::Spacing();

    // 路径(可编辑)
    char buf[1024];
    strncpy(buf, params.path.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    ImGui::TextDisabled("Output file");
    ImGui::SetNextItemWidth(-56.0f);
    if (ImGui::InputText("##exportpath", buf, sizeof(buf)))
        params.path = buf;
    ImGui::SameLine();
    if (ImGui::Button("...##exportbrowse")) {
        std::string sel = saveMeshFileDialog(params.path, params.format == 1);
        if (!sel.empty()) params.path = sel;
    }

    ImGui::Spacing();
    if (ImGui::Button(params.format == 1 ? "Export FBX" : "Export OBJ", ImVec2(-1, 0)))
        params.requestExport = true;   // 由 Application 每帧检查并执行

    // 属性变更不需要重建网格, 恒返回 false
    return false;
}

// ---------- CustomNode ----------
CustomNode::CustomNode() {
    type = NodeType::Custom;
    if (params.script.empty()) params.script = kDefaultCustomScript;
}

bool CustomNode::drawProperties() {
    bool changed = false;
    if (params.script.empty()) params.script = kDefaultCustomScript;

    if (ImGui::CollapsingHeader("Script (Lua)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("编写 generate(ctx) 返回枝条数组。ctx: count/parentLen/"
                           "parentRadius/depth/seed, 工具 rand()/rand(a,b)/noise(x)。");
        ImGui::TextDisabled("每根枝条: {t, azimuth, elevation, length, radius, endRatio}");
        ImGui::Spacing();

        // 多行脚本编辑框(std::string 回调式增长)
        ImVec2 boxSize(-1.0f, 220.0f);
        if (ImGui::InputTextMultiline("##customscript", &params.script[0],
                params.script.capacity() + 1, boxSize,
                ImGuiInputTextFlags_CallbackResize,
                [](ImGuiInputTextCallbackData* d) -> int {
                    auto* s = static_cast<std::string*>(d->UserData);
                    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                        s->resize(d->BufTextLen);
                        d->Buf = &(*s)[0];
                    }
                    return 0;
                }, &params.script)) {
            changed = true;
        }

        if (ImGui::Button("Apply / Rebuild", ImVec2(-1, 0)))
            changed = true;   // 触发上层重建

        // 上次执行的错误(由 TreeGenerator 写回)红色显示
        if (!params.lastError.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
            ImGui::TextWrapped("脚本错误: %s", params.lastError.c_str());
            ImGui::PopStyleColor();
        }
    }

    // Geometry：单根枝条的截面/锥度/扰动/风格(脚本只给逻辑, 这里控形态)
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt  ("Count",        &params.count,       0, 200);
        changed |= ImGui::SliderFloat("Base Flare",   &params.baseFlare,   1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Taper Power",  &params.taperPow,    0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Gravity",      &params.gravity,     0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Noise Amount", &params.noiseAmount, 0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Noise Freq",   &params.noiseFreq,   0.5f, 8.0f);
        changed |= ImGui::SliderFloat("Gnarl",        &params.gnarl,       0.0f, 90.0f);
        changed |= ImGui::SliderInt  ("Sides",        &params.sides,       3, 12);
        changed |= ImGui::SliderInt  ("Length Segs",  &params.lengthSegs,  2, 16);
        changed |= ImGui::SliderFloat("UV Tiling U",  &params.uvTilingU,   0.1f, 20.0f);
        changed |= ImGui::SliderFloat("UV Tiling V",  &params.uvTilingV,   0.1f, 20.0f);
        changed |= ImGui::SliderInt  ("Seed",         &params.seed,        0, 999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_custom");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}
