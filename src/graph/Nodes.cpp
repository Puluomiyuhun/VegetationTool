#include "Nodes.h"
#include "../io/MeshImport.h"
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
// 网格保存对话框：返回选中的保存路径(自动补扩展名)；取消返回空串。fmt: 0=OBJ 1=FBX 2=USD。
static std::string saveMeshFileDialog(const std::string& initial, int fmt) {
    char szFile[1024] = {0};
    strncpy(szFile, initial.c_str(), sizeof(szFile)-1);
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrFilter = fmt == 1 ? "FBX (ASCII)\0*.fbx\0All\0*.*\0"
                    : fmt == 2 ? "USD (ASCII)\0*.usda\0All\0*.*\0"
                               : "Wavefront OBJ\0*.obj\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = fmt == 1 ? "fbx" : (fmt == 2 ? "usda" : "obj");
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn))
        return std::string(szFile);
    return std::string();
}
// FBX 文件选择对话框：返回选中路径；取消返回空串。
static std::string openFbxFileDialog() {
    char szFile[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrFilter = "FBX\0*.fbx;*.FBX\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn))
        return std::string(szFile);
    return std::string();
}
#else
static std::string openImageFileDialog() { return std::string(); }
static std::string saveMeshFileDialog(const std::string&, int) { return std::string(); }
static std::string openFbxFileDialog() { return std::string(); }
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

// 公共：带 Variance 的浮点滑条。主滑条 + 同行一个窄的 "± 抖动" 滑条。
// var 为绝对范围(与主值同单位), 0=关闭。生成时每根实例在 [value-var, value+var] 采样。
static bool sliderFloatVar(const char* label, float* value, float* var,
                           float vmin, float vmax, float varMax,
                           const char* fmt = "%.3f") {
    bool changed = false;
    ImGui::PushID(label);
    float full = ImGui::CalcItemWidth();
    float varW = full * 0.34f;
    ImGui::SetNextItemWidth(full - varW - ImGui::GetStyle().ItemInnerSpacing.x);
    changed |= ImGui::SliderFloat("##v", value, vmin, vmax, fmt);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(varW);
    // 抖动幅度: 显示为 ±X, 拖到 0 即关闭
    changed |= ImGui::SliderFloat("##var", var, 0.0f, varMax, "\xC2\xB1%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Variance: each instance randomizes within value \xC2\xB1 this");
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
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
        changed |= sliderFloatVar("Length Ratio", &params.lengthRatio, &params.lengthRatioVar, 0.1f, 1.0f, 0.5f);
        changed |= sliderFloatVar("Radius Scale", &params.radiusScale, &params.radiusScaleVar, 0.1f, 2.0f, 1.0f);
        changed |= sliderFloatVar("End Ratio",    &params.endRatio,    &params.endRatioVar,    0.01f, 1.0f, 0.5f);
        changed |= ImGui::SliderFloat("Base Flare",     &params.baseFlare,    1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Taper Power",    &params.taperPow,     0.5f, 4.0f);
        changed |= sliderFloatVar("Spread Angle", &params.spreadAngle, &params.spreadAngleVar, 10.0f, 90.0f, 45.0f, "%.1f");
        changed |= sliderFloatVar("Gravity",      &params.gravity,     &params.gravityVar,     0.0f, 1.0f, 0.5f);
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
        changed |= sliderFloatVar("Length Ratio", &params.lengthRatio, &params.lengthRatioVar, 0.1f, 1.0f, 0.5f);
        changed |= sliderFloatVar("Radius Scale", &params.radiusScale, &params.radiusScaleVar, 0.1f, 2.0f, 1.0f);
        changed |= sliderFloatVar("End Ratio",    &params.endRatio,    &params.endRatioVar,    0.01f, 1.0f, 0.5f);
        changed |= ImGui::SliderFloat("Base Flare",     &params.baseFlare,    1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Taper Power",    &params.taperPow,     0.5f, 4.0f);
        changed |= sliderFloatVar("Spread Angle", &params.spreadAngle, &params.spreadAngleVar, 10.0f, 90.0f, 45.0f, "%.1f");
        changed |= sliderFloatVar("Gravity",      &params.gravity,     &params.gravityVar,     0.0f, 1.0f, 0.5f);
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
        changed |= sliderFloatVar("Length",      &params.length,      &params.lengthVar,      0.5f, 10.0f, 3.0f, "%.2f");
        changed |= sliderFloatVar("Radius Scale",&params.radiusScale, &params.radiusScaleVar, 0.1f, 2.0f, 1.0f);
        changed |= sliderFloatVar("End Ratio",   &params.endRatio,    &params.endRatioVar,    0.01f, 0.5f, 0.25f);
        changed |= ImGui::SliderFloat("Taper Power",  &params.taperPow,    0.5f, 4.0f);
        changed |= ImGui::SliderFloat("Base Flare",   &params.baseFlare,   1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Collar Sink",  &params.collarSink,  0.0f, 0.8f);
        changed |= sliderFloatVar("Spread Angle",&params.spreadAngle, &params.spreadAngleVar, 30.0f, 150.0f, 45.0f, "%.1f");
        changed |= sliderFloatVar("Gravity",     &params.gravity,     &params.gravityVar,     0.0f, 1.0f, 0.5f);
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
        changed |= sliderFloatVar("Length Ratio", &params.lengthRatio, &params.lengthRatioVar, 0.1f, 1.5f, 0.5f);
        changed |= sliderFloatVar("Radius Scale", &params.radiusScale, &params.radiusScaleVar, 0.05f, 1.0f, 0.5f);
        changed |= sliderFloatVar("End Ratio",    &params.endRatio,    &params.endRatioVar,    0.01f, 1.0f, 0.5f);
        changed |= ImGui::SliderFloat("Taper Power",   &params.taperPow,     0.5f, 4.0f);
        changed |= sliderFloatVar("Spread Angle", &params.spreadAngle, &params.spreadAngleVar, 10.0f, 90.0f, 45.0f, "%.1f");
        changed |= sliderFloatVar("Gravity",      &params.gravity,     &params.gravityVar,     0.0f, 1.0f, 0.5f);
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
    ImGui::SameLine();
    ImGui::RadioButton("USD (Nanite 程序集)", &params.format, 2);
    if (params.format != prevFormat) {
        // 切换格式时同步规整路径扩展名
        const char* want = params.format == 1 ? ".fbx" : (params.format == 2 ? ".usda" : ".obj");
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
        std::string sel = saveMeshFileDialog(params.path, params.format);
        if (!sel.empty()) params.path = sel;
    }

    ImGui::Spacing();
    const char* btnLabel = params.format == 1 ? "Export FBX"
                         : (params.format == 2 ? "Export USD" : "Export OBJ");
    if (ImGui::Button(btnLabel, ImVec2(-1, 0)))
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

// ---------- 导入节点公共: FBX 路径选择行 + 加载状态显示 ----------
// 返回是否需要重新加载(路径变更或用户点了 Reload)。changed 反映是否需重建网格。
static bool drawFbxPathRow(std::string& fbxPath, bool& requestReload,
                           const std::string& loadError,
                           const std::shared_ptr<ImportedMesh>& cached) {
    bool reload = false;
    ImGui::TextDisabled("FBX 文件");
    char buf[1024];
    strncpy(buf, fbxPath.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    ImGui::SetNextItemWidth(-84.0f);
    ImGui::InputText("##fbxpath", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("...##fbxbrowse")) {
        std::string sel = openFbxFileDialog();
        if (!sel.empty() && sel != fbxPath) { fbxPath = sel; reload = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload##fbx") && !fbxPath.empty()) reload = true;

    if (reload) requestReload = true;

    // 状态: 错误红色, 成功显示顶点/三角/骨骼数
    if (!loadError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f,0.35f,0.30f,1.0f));
        ImGui::TextWrapped("加载失败: %s", loadError.c_str());
        ImGui::PopStyleColor();
    } else if (cached && cached->ok) {
        ImGui::TextColored(ImVec4(0.5f,0.85f,0.5f,1.0f),
            "顶点 %zu / 三角 %zu / 骨骼 %zu",
            cached->vertexCount(), cached->triangleCount(), cached->bones.size());
    } else if (!fbxPath.empty()) {
        ImGui::TextDisabled("(尚未加载, 点 Reload 或调整参数)");
    }
    return reload;
}

// ---------- ImportTrunkNode ----------
ImportTrunkNode::ImportTrunkNode() { type = NodeType::ImportTrunk; }

bool ImportTrunkNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("导入 SpeedTree 带骨骼枝干 FBX, 作为一株的根。骨骼链会换算成"
                           "附着环供下游 Scatter 沿其撒叶。");
        ImGui::Spacing();
        changed |= drawFbxPathRow(params.fbxPath, params.requestReload,
                                  params.loadError, params.cached);
    }
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Scale", &params.scale, 0.01f, 10.0f);
        changed |= ImGui::SliderFloat("Pos X", &params.posX, -20.0f, 20.0f);
        changed |= ImGui::SliderFloat("Pos Z", &params.posZ, -20.0f, 20.0f);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_impTrunk");
        changed |= drawMaterial(params.material, false);
        ImGui::PopID();
    }
    return changed;
}

// ---------- ImportLeafNode ----------
ImportLeafNode::ImportLeafNode() { type = NodeType::ImportLeaf; }

bool ImportLeafNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("导入枝叶单体 FBX(一小片叶网格), 用于预览。散布请用 Scatter 节点。");
        ImGui::Spacing();
        changed |= drawFbxPathRow(params.fbxPath, params.requestReload,
                                  params.loadError, params.cached);
    }
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Scale", &params.scale, 0.01f, 10.0f);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_impLeaf");
        changed |= drawMaterial(params.material, true);
        ImGui::PopID();
    }
    return changed;
}

// ---------- ScatterNode ----------
ScatterNode::ScatterNode() { type = NodeType::Scatter; }

bool ScatterNode::drawProperties() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Leaf Prototype Variants", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("导入一个或多个叶原型变体(实例枝)。在父级(ImportTrunk)骨骼的每根末端"
                           "细枝上撒 Count 片叶, 每片均匀随机选取一个变体。导出 USD 时每个变体"
                           "成为一个 PointInstancer: 引擎里只存 1 份原型网格, 省内存。");
        ImGui::Spacing();

        int removeIdx = -1;
        for (int i = 0; i < (int)params.variants.size(); ++i) {
            auto& var = params.variants[i];
            ImGui::PushID(i);
            ImGui::Text("变体 %d", i + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("移除")) removeIdx = i;
            changed |= drawFbxPathRow(var.fbxPath, var.requestReload,
                                      var.loadError, var.cached);
            // 多材质实例枝: 让用户指定哪个子网格是"枝干"(复用父级 Trunk 材质),
            // 其余子网格作叶子(用 FBX 自带材质)。仅当加载出 ≥2 个 part 时显示。
            if (var.cached && var.cached->ok && var.cached->parts.size() >= 2) {
                if (var.trunkPart >= (int)var.cached->parts.size()) var.trunkPart = -1;
                std::string preview = (var.trunkPart < 0)
                    ? std::string("(无, 全部作叶子)")
                    : ("#" + std::to_string(var.trunkPart) + " " + var.cached->parts[var.trunkPart].materialName);
                ImGui::TextDisabled("枝干子网格(复用父级 Trunk 材质)");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##trunkpart", preview.c_str())) {
                    if (ImGui::Selectable("(无, 全部作叶子)", var.trunkPart < 0)) {
                        var.trunkPart = -1; changed = true;
                    }
                    for (int pi = 0; pi < (int)var.cached->parts.size(); ++pi) {
                        const auto& sm = var.cached->parts[pi];
                        std::string lbl = "#" + std::to_string(pi) + " " + sm.materialName
                                        + " (" + std::to_string(sm.indexCount / 3) + " 三角)";
                        if (ImGui::Selectable(lbl.c_str(), var.trunkPart == pi)) {
                            var.trunkPart = pi; changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            params.variants.erase(params.variants.begin() + removeIdx);
            changed = true;
        }
        if (ImGui::Button("+ 添加变体")) {
            params.variants.emplace_back();
            changed = true;
        }
    }
    if (ImGui::CollapsingHeader("Scatter", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* distItems[] = { "随机数量 (Random)", "等距交错 (Even Step)" };
        int dist = (int)params.distribution;
        if (ImGui::Combo("Distribution", &dist, distItems, 2)) {
            params.distribution = (ScatterParams::Distribution)dist;
            changed = true;
        }
        if (params.distribution == ScatterParams::Distribution::EvenStep)
            changed |= ImGui::SliderFloat("Even Spacing", &params.evenSpacing, 0.02f, 0.5f);
        else
            changed |= ImGui::SliderInt  ("Count/Twig",    &params.count,        1, 30);
        changed |= ImGui::SliderFloat("Leaf Scale",    &params.leafScale,    0.05f, 5.0f);
        changed |= ImGui::SliderFloat("Scale Var",     &params.leafScaleVar, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Region Start",  &params.regionStart,  0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Region End",    &params.regionEnd,    0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Spread Angle",  &params.spreadAngle,  0.0f, 90.0f);
        changed |= ImGui::SliderFloat("Spiral Step",   &params.spiralStep,   0.0f, 360.0f);
        changed |= ImGui::SliderFloat("Tip Scale",     &params.tipScale,     0.05f, 3.0f);
        changed |= ImGui::SliderFloat("Normal Jitter", &params.normalJitter, 0.0f, 1.0f);
        changed |= ImGui::SliderInt  ("Seed",          &params.seed,         0, 9999);
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("mat_scatter");
        changed |= drawMaterial(params.material, true);
        ImGui::PopID();
    }
    return changed;
}
