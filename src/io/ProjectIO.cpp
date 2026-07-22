#include "ProjectIO.h"
#include "graph/NodeGraph.h"
#include "graph/Nodes.h"
#include "renderer/Renderer.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <cstdio>

namespace {

// Base64 编解码: 自定义节点脚本是多行文本, 编成单行存入行式 .vtree(避免破坏解析)。
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64encode(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c; bits += 8;
        while (bits >= 0) { out.push_back(kB64[(val >> bits) & 0x3F]); bits -= 6; }
    }
    if (bits > -6) out.push_back(kB64[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
std::string b64decode(const std::string& in) {
    static int T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) T[i] = -1;
                 for (int i = 0; i < 64; ++i) T[(unsigned char)kB64[i]] = i; init = true; }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] < 0) continue;   // 跳过 '=' 与空白
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

// ---------------- 写出 ----------------
void writeMaterial(std::ostream& o, const MaterialParams& m) {
    o << "mat.albedo "      << m.albedo.x << ' ' << m.albedo.y << ' ' << m.albedo.z << '\n';
    o << "mat.roughness "   << m.roughness   << '\n';
    o << "mat.metallic "    << m.metallic    << '\n';
    o << "mat.aoStrength "  << m.aoStrength  << '\n';
    o << "mat.sssStrength " << m.sssStrength << '\n';
    o << "mat.alphaCutoff " << m.alphaCutoff << '\n';
    // 字符串放最后：value 取整行剩余(允许空格路径)。空串也写出以占位
    o << "mat.baseColorTex " << m.baseColorTex << '\n';
    o << "mat.roughnessTex " << m.roughnessTex << '\n';
    o << "mat.normalTex "    << m.normalTex    << '\n';
    o << "mat.opacityTex "   << m.opacityTex   << '\n';
}

// 光照/场景设置块：save 时若传入非空 lighting 则写出 LIGHTING...ENDLIGHTING。
void writeLighting(std::ostream& o, const LightingParams& L) {
    o << "LIGHTING\n";
    o << "lightDir "        << L.lightDir.x   << ' ' << L.lightDir.y   << ' ' << L.lightDir.z   << '\n';
    o << "lightColor "      << L.lightColor.x << ' ' << L.lightColor.y << ' ' << L.lightColor.z << '\n';
    o << "lightIntensity "  << L.lightIntensity  << '\n';
    o << "ambientStrength " << L.ambientStrength << '\n';
    o << "exposure "        << L.exposure        << '\n';
    o << "ambientTop "      << L.ambientTop.x  << ' ' << L.ambientTop.y  << ' ' << L.ambientTop.z  << '\n';
    o << "ambientBot "      << L.ambientBot.x  << ' ' << L.ambientBot.y  << ' ' << L.ambientBot.z  << '\n';
    o << "skyTop "          << L.skyTop.x      << ' ' << L.skyTop.y      << ' ' << L.skyTop.z      << '\n';
    o << "skyHorizon "      << L.skyHorizon.x  << ' ' << L.skyHorizon.y  << ' ' << L.skyHorizon.z  << '\n';
    o << "skyGround "       << L.skyGround.x   << ' ' << L.skyGround.y   << ' ' << L.skyGround.z   << '\n';
    o << "shadowEnabled "   << (L.shadowEnabled ? 1 : 0) << '\n';
    o << "shadowStrength "  << L.shadowStrength  << '\n';
    o << "shadowBias "      << L.shadowBias      << '\n';
    o << "groundShadowStrength " << L.groundShadowStrength << '\n';
    o << "groundEnabled "   << (L.groundEnabled ? 1 : 0) << '\n';
    o << "groundAlpha "     << L.groundAlpha     << '\n';
    o << "ENDLIGHTING\n";
}

void writeNode(std::ostream& o, const TreeNode* n) {
    o << "NODE " << n->id << ' ' << (int)n->getType()
      << ' ' << n->editorPos.x << ' ' << n->editorPos.y << '\n';

    switch (n->getType()) {
    case NodeType::Trunk: {
        const auto& p = static_cast<const TrunkNode*>(n)->params;
        o << "length "      << p.length      << '\n';
        o << "startRadius " << p.startRadius << '\n';
        o << "endRadius "   << p.endRadius   << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "posX "        << p.posX        << '\n';
        o << "posZ "        << p.posZ        << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "jointCount "  << p.jointCount  << '\n';
        o << "jointBulge "  << p.jointBulge  << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Roots: {
        const auto& p = static_cast<const RootsNode*>(n)->params;
        o << "rootCount "   << p.rootCount   << '\n';
        o << "length "      << p.length      << '\n';
        o << "radiusScale " << p.radiusScale << '\n';
        o << "endRatio "    << p.endRatio    << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "collarSink "  << p.collarSink  << '\n';
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "jointCount "  << p.jointCount  << '\n';
        o << "jointBulge "  << p.jointBulge  << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        o << "lengthVar "      << p.lengthVar      << '\n';
        o << "radiusScaleVar " << p.radiusScaleVar << '\n';
        o << "endRatioVar "    << p.endRatioVar    << '\n';
        o << "spreadAngleVar " << p.spreadAngleVar << '\n';
        o << "gravityVar "     << p.gravityVar     << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Branch: {
        const auto& p = static_cast<const BranchNode*>(n)->params;
        o << "mode "        << (int)p.mode   << '\n';
        o << "lengthRatio " << p.lengthRatio << '\n';
        o << "radiusScale " << p.radiusScale << '\n';
        o << "endRatio "    << p.endRatio    << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "regionStart " << p.regionStart << '\n';
        o << "sizeFalloff " << p.sizeFalloff << '\n';
        o << "regionEnd "   << p.regionEnd   << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "branchCount " << p.branchCount << '\n';
        o << "intervalSpacing " << p.intervalSpacing << '\n';
        o << "branchesPerNode " << p.branchesPerNode << '\n';
        o << "jointCount "  << p.jointCount  << '\n';
        o << "jointBulge "  << p.jointBulge  << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        o << "lengthRatioVar " << p.lengthRatioVar << '\n';
        o << "radiusScaleVar " << p.radiusScaleVar << '\n';
        o << "endRatioVar "    << p.endRatioVar    << '\n';
        o << "spreadAngleVar " << p.spreadAngleVar << '\n';
        o << "gravityVar "     << p.gravityVar     << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Twig: {
        const auto& p = static_cast<const TwigNode*>(n)->params;
        o << "lengthRatio " << p.lengthRatio << '\n';
        o << "radiusScale " << p.radiusScale << '\n';
        o << "endRatio "    << p.endRatio    << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "regionStart " << p.regionStart << '\n';
        o << "regionEnd "   << p.regionEnd   << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "twigCount "   << p.twigCount   << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "alternating " << (p.alternating ? 1 : 0) << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        o << "lengthRatioVar " << p.lengthRatioVar << '\n';
        o << "radiusScaleVar " << p.radiusScaleVar << '\n';
        o << "endRatioVar "    << p.endRatioVar    << '\n';
        o << "spreadAngleVar " << p.spreadAngleVar << '\n';
        o << "gravityVar "     << p.gravityVar     << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::LeafCluster: {
        const auto& p = static_cast<const LeafClusterNode*>(n)->params;
        o << "leafCount "     << p.leafCount     << '\n';
        o << "clusterRadius " << p.clusterRadius << '\n';
        o << "leafSize "      << p.leafSize      << '\n';
        o << "leafAspect "    << p.leafAspect    << '\n';
        o << "normalJitter "  << p.normalJitter  << '\n';
        o << "normalSoften "  << p.normalSoften  << '\n';
        o << "planar "        << (p.planar ? 1 : 0) << '\n';
        o << "sizeFalloff "   << p.sizeFalloff   << '\n';
        o << "seed "          << p.seed          << '\n';
        o << "useCutout "     << (p.useCutout ? 1 : 0) << '\n';
        if (!p.cutoutPoints.empty()) {
            o << "cutoutPoints " << p.cutoutPoints.size();
            for (const auto& q : p.cutoutPoints) o << ' ' << q.x << ' ' << q.y;
            o << '\n';
        }
        if (!p.cutoutTris.empty()) {
            o << "cutoutTris " << p.cutoutTris.size();
            for (uint32_t t : p.cutoutTris) o << ' ' << t;
            o << '\n';
        }
        if (!p.cutoutRing.empty()) {
            o << "cutoutRing " << p.cutoutRing.size();
            for (const auto& q : p.cutoutRing) o << ' ' << q.x << ' ' << q.y;
            o << '\n';
        }
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Spine: {
        const auto& p = static_cast<const SpineNode*>(n)->params;
        o << "lengthRatio " << p.lengthRatio << '\n';
        o << "radiusScale " << p.radiusScale << '\n';
        o << "endRatio "    << p.endRatio    << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "regionStart " << p.regionStart << '\n';
        o << "regionEnd "   << p.regionEnd   << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "spineCount "  << p.spineCount  << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        o << "lengthRatioVar " << p.lengthRatioVar << '\n';
        o << "radiusScaleVar " << p.radiusScaleVar << '\n';
        o << "endRatioVar "    << p.endRatioVar    << '\n';
        o << "spreadAngleVar " << p.spreadAngleVar << '\n';
        o << "gravityVar "     << p.gravityVar     << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Frond: {
        const auto& p = static_cast<const FrondNode*>(n)->params;
        o << "width "       << p.width       << '\n';
        o << "widthBase "   << p.widthBase   << '\n';
        o << "widthTip "    << p.widthTip    << '\n';
        o << "profilePow "  << p.profilePow  << '\n';
        o << "curl "        << p.curl        << '\n';
        o << "segsPerSide " << p.segsPerSide << '\n';
        o << "serrate "     << (p.serrate ? 1 : 0) << '\n';
        o << "serrateDepth "<< p.serrateDepth<< '\n';
        o << "seed "        << p.seed        << '\n';
        o << "useCutout "     << (p.useCutout ? 1 : 0) << '\n';
        if (!p.cutoutPoints.empty()) {
            o << "cutoutPoints " << p.cutoutPoints.size();
            for (const auto& q : p.cutoutPoints) o << ' ' << q.x << ' ' << q.y;
            o << '\n';
        }
        if (!p.cutoutTris.empty()) {
            o << "cutoutTris " << p.cutoutTris.size();
            for (uint32_t t : p.cutoutTris) o << ' ' << t;
            o << '\n';
        }
        if (!p.cutoutRing.empty()) {
            o << "cutoutRing " << p.cutoutRing.size();
            for (const auto& q : p.cutoutRing) o << ' ' << q.x << ' ' << q.y;
            o << '\n';
        }
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Export: {
        const auto& p = static_cast<const ExportNode*>(n)->params;
        o << "exportMode " << p.exportMode << '\n';
        o << "format " << p.format << '\n';
        o << "specimenCount " << p.specimenCount << '\n';
        o << "singleFile " << (p.singleFile ? 1 : 0) << '\n';
        o << "specimenSpacing " << p.specimenSpacing << '\n';
        // path 放最后: value 取整行剩余(允许含空格路径)
        o << "path " << p.path << '\n';
        break;
    }
    case NodeType::Custom: {
        const auto& p = static_cast<const CustomNode*>(n)->params;
        o << "count "       << p.count       << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTilingU "   << p.uvTilingU   << '\n';
        o << "uvTilingV "   << p.uvTilingV   << '\n';
        writeMaterial(o, p.material);
        // 脚本 base64 编码为单行(放最后)
        o << "scriptB64 " << b64encode(p.script) << '\n';
        break;
    }
    case NodeType::ImportTrunk: {
        const auto& p = static_cast<const ImportTrunkNode*>(n)->params;
        o << "scale " << p.scale << '\n';
        o << "posX "  << p.posX  << '\n';
        o << "posZ "  << p.posZ  << '\n';
        writeMaterial(o, p.material);
        o << "fbxPath " << p.fbxPath << '\n';   // 放最后: 允许含空格路径
        break;
    }
    case NodeType::ImportLeaf: {
        const auto& p = static_cast<const ImportLeafNode*>(n)->params;
        o << "scale " << p.scale << '\n';
        writeMaterial(o, p.material);
        o << "fbxPath " << p.fbxPath << '\n';
        break;
    }
    case NodeType::Scatter: {
        const auto& p = static_cast<const ScatterNode*>(n)->params;
        o << "distribution " << (int)p.distribution << '\n';
        o << "evenSpacing "  << p.evenSpacing  << '\n';
        o << "count "        << p.count        << '\n';
        o << "leafScale "    << p.leafScale    << '\n';
        o << "leafScaleVar " << p.leafScaleVar << '\n';
        o << "regionStart "  << p.regionStart  << '\n';
        o << "regionEnd "    << p.regionEnd    << '\n';
        o << "spreadAngle "  << p.spreadAngle  << '\n';
        o << "tuck "         << p.tuck         << '\n';
        o << "spiralStep "   << p.spiralStep   << '\n';
        o << "tipScale "     << p.tipScale     << '\n';
        o << "normalJitter " << p.normalJitter << '\n';
        o << "seed "         << p.seed         << '\n';
        writeMaterial(o, p.material);
        o << "variantCount " << p.variants.size() << '\n';
        for (size_t i = 0; i < p.variants.size(); ++i) {
            o << "variant" << i << " " << p.variants[i].fbxPath << '\n';
            o << "variantTrunkPart" << i << " " << p.variants[i].trunkPart << '\n';
        }
        break;
    }
    }
    o << "ENDNODE\n";
}

// ---------------- 读入 ----------------
using KV = std::unordered_map<std::string, std::string>;

float getF(const KV& kv, const char* k, float def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}
int getI(const KV& kv, const char* k, int def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
glm::vec3 getV3(const KV& kv, const char* k, glm::vec3 def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    std::istringstream ss(it->second);
    glm::vec3 v = def; ss >> v.x >> v.y >> v.z; return v;
}
std::string getS(const KV& kv, const char* k) {
    auto it = kv.find(k); return it != kv.end() ? it->second : std::string();
}

void readMaterial(const KV& kv, MaterialParams& m) {
    m.albedo       = getV3(kv, "mat.albedo",      m.albedo);
    m.roughness    = getF (kv, "mat.roughness",   m.roughness);
    m.metallic     = getF (kv, "mat.metallic",    m.metallic);
    m.aoStrength   = getF (kv, "mat.aoStrength",  m.aoStrength);
    m.sssStrength  = getF (kv, "mat.sssStrength", m.sssStrength);
    m.alphaCutoff  = getF (kv, "mat.alphaCutoff", m.alphaCutoff);
    m.baseColorTex = getS (kv, "mat.baseColorTex");
    m.roughnessTex = getS (kv, "mat.roughnessTex");
    m.normalTex    = getS (kv, "mat.normalTex");
    m.opacityTex   = getS (kv, "mat.opacityTex");
}

void readLighting(const KV& kv, LightingParams& L) {
    L.lightDir        = getV3(kv, "lightDir",        L.lightDir);
    L.lightColor      = getV3(kv, "lightColor",      L.lightColor);
    L.lightIntensity  = getF (kv, "lightIntensity",  L.lightIntensity);
    L.ambientStrength = getF (kv, "ambientStrength", L.ambientStrength);
    L.exposure        = getF (kv, "exposure",        L.exposure);
    L.ambientTop      = getV3(kv, "ambientTop",      L.ambientTop);
    L.ambientBot      = getV3(kv, "ambientBot",      L.ambientBot);
    L.skyTop          = getV3(kv, "skyTop",          L.skyTop);
    L.skyHorizon      = getV3(kv, "skyHorizon",      L.skyHorizon);
    L.skyGround       = getV3(kv, "skyGround",       L.skyGround);
    L.shadowEnabled   = getI (kv, "shadowEnabled",   L.shadowEnabled ? 1 : 0) != 0;
    L.shadowStrength  = getF (kv, "shadowStrength",  L.shadowStrength);
    L.shadowBias      = getF (kv, "shadowBias",      L.shadowBias);
    L.groundShadowStrength = getF (kv, "groundShadowStrength", L.groundShadowStrength);
    L.groundEnabled   = getI (kv, "groundEnabled",   L.groundEnabled ? 1 : 0) != 0;
    L.groundAlpha     = getF (kv, "groundAlpha",     L.groundAlpha);
}

void applyParams(TreeNode* n, const KV& kv) {
    switch (n->getType()) {
    case NodeType::Trunk: {
        auto& p = static_cast<TrunkNode*>(n)->params;
        p.length=getF(kv,"length",p.length); p.startRadius=getF(kv,"startRadius",p.startRadius);
        p.endRadius=getF(kv,"endRadius",p.endRadius); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.posX=getF(kv,"posX",p.posX); p.posZ=getF(kv,"posZ",p.posZ);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.taperPow=getF(kv,"taperPow",p.taperPow);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        readMaterial(kv, p.material); break;
    }
    case NodeType::Roots: {
        auto& p = static_cast<RootsNode*>(n)->params;
        p.rootCount=getI(kv,"rootCount",p.rootCount); p.length=getF(kv,"length",p.length);
        p.radiusScale=getF(kv,"radiusScale",p.radiusScale); p.endRatio=getF(kv,"endRatio",p.endRatio);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.collarSink=getF(kv,"collarSink",p.collarSink);
        p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.gravity=getF(kv,"gravity",getF(kv,"droop",p.gravity)); // droop 为旧字段名, 向后兼容
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount);
        p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq); p.gnarl=getF(kv,"gnarl",p.gnarl);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthVar=getF(kv,"lengthVar",p.lengthVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Branch: {
        auto& p = static_cast<BranchNode*>(n)->params;
        p.mode=(BranchMode)getI(kv,"mode",(int)p.mode);
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.sizeFalloff=getF(kv,"sizeFalloff",p.sizeFalloff);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.branchCount=getI(kv,"branchCount",p.branchCount);
        p.intervalSpacing=getF(kv,"intervalSpacing",p.intervalSpacing);
        p.branchesPerNode=getI(kv,"branchesPerNode",p.branchesPerNode);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Twig: {
        auto& p = static_cast<TwigNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.twigCount=getI(kv,"twigCount",p.twigCount);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.alternating=getI(kv,"alternating",p.alternating?1:0)!=0;
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::LeafCluster: {
        auto& p = static_cast<LeafClusterNode*>(n)->params;
        p.leafCount=getI(kv,"leafCount",p.leafCount); p.clusterRadius=getF(kv,"clusterRadius",p.clusterRadius);
        p.leafSize=getF(kv,"leafSize",p.leafSize); p.normalJitter=getF(kv,"normalJitter",p.normalJitter);
        p.normalSoften=getF(kv,"normalSoften",p.normalSoften);
        p.leafAspect=getF(kv,"leafAspect",p.leafAspect);
        p.planar=getI(kv,"planar",p.planar?1:0)!=0; p.sizeFalloff=getF(kv,"sizeFalloff",p.sizeFalloff);
        p.seed=getI(kv,"seed",p.seed);
        p.useCutout=getI(kv,"useCutout",p.useCutout?1:0)!=0;
        {
            std::string sp = getS(kv, "cutoutPoints");
            if (!sp.empty()) {
                std::istringstream ss(sp); size_t cnt = 0; ss >> cnt;
                p.cutoutPoints.clear();
                for (size_t i = 0; i < cnt; ++i) { glm::vec2 q; ss >> q.x >> q.y; p.cutoutPoints.push_back(q); }
            }
            std::string st = getS(kv, "cutoutTris");
            if (!st.empty()) {
                std::istringstream ss(st); size_t cnt = 0; ss >> cnt;
                p.cutoutTris.clear();
                for (size_t i = 0; i < cnt; ++i) { uint32_t t = 0; ss >> t; p.cutoutTris.push_back(t); }
            }
            std::string sr = getS(kv, "cutoutRing");
            if (!sr.empty()) {
                std::istringstream ss(sr); size_t cnt = 0; ss >> cnt;
                p.cutoutRing.clear();
                for (size_t i = 0; i < cnt; ++i) { glm::vec2 q; ss >> q.x >> q.y; p.cutoutRing.push_back(q); }
            }
        }
        readMaterial(kv, p.material); break;
    }
    case NodeType::Spine: {
        auto& p = static_cast<SpineNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.taperPow=getF(kv,"taperPow",p.taperPow);
        p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle); p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset);
        p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.spineCount=getI(kv,"spineCount",p.spineCount);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Frond: {
        auto& p = static_cast<FrondNode*>(n)->params;
        p.width=getF(kv,"width",p.width); p.widthBase=getF(kv,"widthBase",p.widthBase);
        p.widthTip=getF(kv,"widthTip",p.widthTip); p.profilePow=getF(kv,"profilePow",p.profilePow);
        p.curl=getF(kv,"curl",p.curl); p.segsPerSide=getI(kv,"segsPerSide",p.segsPerSide);
        p.serrate=getI(kv,"serrate",p.serrate?1:0)!=0; p.serrateDepth=getF(kv,"serrateDepth",p.serrateDepth);
        p.seed=getI(kv,"seed",p.seed);
        p.useCutout=getI(kv,"useCutout",p.useCutout?1:0)!=0;
        {
            std::string sp = getS(kv, "cutoutPoints");
            if (!sp.empty()) {
                std::istringstream ss(sp); size_t cnt = 0; ss >> cnt;
                p.cutoutPoints.clear();
                for (size_t i = 0; i < cnt; ++i) { glm::vec2 q; ss >> q.x >> q.y; p.cutoutPoints.push_back(q); }
            }
            std::string st = getS(kv, "cutoutTris");
            if (!st.empty()) {
                std::istringstream ss(st); size_t cnt = 0; ss >> cnt;
                p.cutoutTris.clear();
                for (size_t i = 0; i < cnt; ++i) { uint32_t t = 0; ss >> t; p.cutoutTris.push_back(t); }
            }
            std::string sr = getS(kv, "cutoutRing");
            if (!sr.empty()) {
                std::istringstream ss(sr); size_t cnt = 0; ss >> cnt;
                p.cutoutRing.clear();
                for (size_t i = 0; i < cnt; ++i) { glm::vec2 q; ss >> q.x >> q.y; p.cutoutRing.push_back(q); }
            }
        }
        readMaterial(kv, p.material); break;
    }
    case NodeType::Export: {
        auto& p = static_cast<ExportNode*>(n)->params;
        // 优先读新键 exportMode; 兼容旧工程: 无 exportMode 时用旧 exportWhole(1=整株→模式1)
        int legacyWhole = getI(kv, "exportWhole", 0);
        p.exportMode = getI(kv, "exportMode", legacyWhole != 0 ? 1 : 0);
        p.format = getI(kv, "format", p.format);
        p.specimenCount = getI(kv, "specimenCount", p.specimenCount);
        p.singleFile = getI(kv, "singleFile", p.singleFile ? 1 : 0) != 0;
        p.specimenSpacing = getF(kv, "specimenSpacing", p.specimenSpacing);
        std::string s = getS(kv, "path");
        if (!s.empty()) p.path = s;
        break;
    }
    case NodeType::Custom: {
        auto& p = static_cast<CustomNode*>(n)->params;
        p.count       = getI(kv, "count",       p.count);
        p.baseFlare   = getF(kv, "baseFlare",   p.baseFlare);
        p.taperPow    = getF(kv, "taperPow",    p.taperPow);
        p.gravity     = getF(kv, "gravity",     p.gravity);
        p.noiseAmount = getF(kv, "noiseAmount", p.noiseAmount);
        p.noiseFreq   = getF(kv, "noiseFreq",   p.noiseFreq);
        p.gnarl       = getF(kv, "gnarl",       p.gnarl);
        p.sides       = getI(kv, "sides",       p.sides);
        p.lengthSegs  = getI(kv, "lengthSegs",  p.lengthSegs);
        p.seed        = getI(kv, "seed",        p.seed);
        p.uvTilingU   = getF(kv, "uvTilingU",   p.uvTilingU);
        p.uvTilingV   = getF(kv, "uvTilingV",   p.uvTilingV);
        readMaterial(kv, p.material);
        std::string b = getS(kv, "scriptB64");
        if (!b.empty()) { std::string sc = b64decode(b); if (!sc.empty()) p.script = sc; }
        // 也接受未编码的原始 script(方便 API/MCP 自动化直接写入)
        std::string raw = getS(kv, "script");
        if (!raw.empty()) p.script = raw;
        break;
    }
    case NodeType::ImportTrunk: {
        auto& p = static_cast<ImportTrunkNode*>(n)->params;
        p.scale = getF(kv, "scale", p.scale);
        p.posX  = getF(kv, "posX",  p.posX);
        p.posZ  = getF(kv, "posZ",  p.posZ);
        readMaterial(kv, p.material);
        std::string s = getS(kv, "fbxPath");
        if (!s.empty()) { p.fbxPath = s; p.requestReload = true; }
        break;
    }
    case NodeType::ImportLeaf: {
        auto& p = static_cast<ImportLeafNode*>(n)->params;
        p.scale = getF(kv, "scale", p.scale);
        readMaterial(kv, p.material);
        std::string s = getS(kv, "fbxPath");
        if (!s.empty()) { p.fbxPath = s; p.requestReload = true; }
        break;
    }
    case NodeType::Scatter: {
        auto& p = static_cast<ScatterNode*>(n)->params;
        p.distribution = (ScatterParams::Distribution)getI(kv, "distribution", (int)p.distribution);
        p.evenSpacing  = getF(kv, "evenSpacing",  p.evenSpacing);
        p.count        = getI(kv, "count",        p.count);
        p.leafScale    = getF(kv, "leafScale",    p.leafScale);
        p.leafScaleVar = getF(kv, "leafScaleVar", p.leafScaleVar);
        p.regionStart  = getF(kv, "regionStart",  p.regionStart);
        p.regionEnd    = getF(kv, "regionEnd",    p.regionEnd);
        p.spreadAngle  = getF(kv, "spreadAngle",  p.spreadAngle);
        p.tuck         = getF(kv, "tuck",         p.tuck);
        p.spiralStep   = getF(kv, "spiralStep",   p.spiralStep);
        p.tipScale     = getF(kv, "tipScale",     p.tipScale);
        p.normalJitter = getF(kv, "normalJitter", p.normalJitter);
        p.seed         = getI(kv, "seed",         p.seed);
        readMaterial(kv, p.material);
        p.variants.clear();
        int vc = getI(kv, "variantCount", -1);
        if (vc >= 0) {
            for (int i = 0; i < vc; ++i) {
                std::string vp = getS(kv, ("variant" + std::to_string(i)).c_str());
                ScatterParams::Variant var;
                var.fbxPath = vp;
                var.trunkPart = getI(kv, ("variantTrunkPart" + std::to_string(i)).c_str(), -1);
                if (!vp.empty()) var.requestReload = true;
                p.variants.push_back(std::move(var));
            }
        } else {
            // 旧格式兼容: 单个 fbxPath → 变体[0]
            std::string s = getS(kv, "fbxPath");
            if (!s.empty()) {
                ScatterParams::Variant var;
                var.fbxPath = s;
                var.requestReload = true;
                p.variants.push_back(std::move(var));
            }
        }
        break;
    }
    }
}

// 解析 .vtree 文本流：首行校验 VEGTOOL，随后逐节点/连线重建图。
// 文件版(load) 与内置默认模板(loadDefaultTemplate) 共用此逻辑。
bool parseStream(NodeGraph& graph, std::istream& f, LightingParams* lighting = nullptr) {
    std::string line;
    if (!std::getline(f, line) || line.rfind("VEGTOOL", 0) != 0) return false;

    graph.clear();

    std::unordered_map<uint32_t, NodeId> idMap;
    std::vector<std::pair<uint32_t,uint32_t>> pendingLinks;

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag; ss >> tag;
        if (tag == "NODE") {
            uint32_t savedId; int typeInt; float px, py;
            ss >> savedId >> typeInt >> px >> py;
            KV kv;
            std::string body;
            while (std::getline(f, body)) {
                if (body == "ENDNODE") break;
                std::istringstream bs(body);
                std::string key; bs >> key;
                std::string val;
                std::getline(bs, val);
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                kv[key] = val;
            }
            NodeId newId = graph.addNode((NodeType)typeInt, {px, py});
            idMap[savedId] = newId;
            if (TreeNode* n = graph.getNode(newId))
                applyParams(n, kv);
        } else if (tag == "LIGHTING") {
            // 收集到 ENDLIGHTING 为止；仅当调用方需要(传入非空)时才应用
            KV kv;
            std::string body;
            while (std::getline(f, body)) {
                if (body == "ENDLIGHTING") break;
                std::istringstream bs(body);
                std::string key; bs >> key;
                std::string val;
                std::getline(bs, val);
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                kv[key] = val;
            }
            if (lighting) readLighting(kv, *lighting);
        } else if (tag == "LINK") {
            uint32_t from, to; ss >> from >> to;
            pendingLinks.emplace_back(from, to);
        } else if (tag == "COMMENT") {
            float px, py, sx, sy;
            ss >> px >> py >> sx >> sy;
            std::string text;
            std::getline(f, text);            // 标题文本(允许含空格)
            std::string end;
            std::getline(f, end);             // 读掉 ENDCOMMENT
            NodeId cid = graph.addComment({px, py});
            if (CommentFrame* c = graph.getComment(cid)) {
                c->text = text;
                c->size = {sx, sy};
            }
        }
    }

    for (auto& [from, to] : pendingLinks) {
        auto fIt = idMap.find(from), tIt = idMap.find(to);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        TreeNode* pn = graph.getNode(fIt->second);
        TreeNode* cn = graph.getNode(tIt->second);
        if (pn && cn && !cn->inputPins.empty())
            graph.addLink(pn->outputPin.id, cn->inputPins[0].id);
    }

    graph.markDirty();
    return true;
}

// 内置默认工程(取自 HelloTree.vtree)——启动 / Reset to Default 时加载。
static const char* kDefaultTemplate = R"VT(VEGTOOL 1
NODE 1 2 554 199
lengthRatio 0.382
radiusScale 1
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 50
rotateOffset 137.5
gravity 0.18
regionStart 0.509
regionEnd 0.95
noiseAmount 30
noiseFreq 3
gnarl 10
branchCount 4
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.509
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex
mat.roughnessTex
mat.normalTex
mat.opacityTex
ENDNODE
NODE 2 0 100 200
length 6.178
startRadius 0.372
endRadius 0.079
baseFlare 1.4
posX 0
posZ 0
noiseAmount 90
noiseFreq 3.272
gnarl 15
taperPow 1.6
sides 8
lengthSegs 16
seed 1
uvTiling 3
mat.albedo 0.38 0.22 0.1
mat.roughness 0.584
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 3 2 282 199
lengthRatio 0.714
radiusScale 0.689
endRatio 0.255
baseFlare 2.05
taperPow 0.729
spreadAngle 33.177
rotateOffset 154.322
gravity 1
regionStart 0.397
regionEnd 0.911
noiseAmount 90
noiseFreq 1.423
gnarl 10
branchCount 12
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.586
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 4 3 682 199
lengthRatio 0.579
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.3
spreadAngle 65
rotateOffset 137.5
gravity 0.25
regionStart 0.204
regionEnd 0.866
noiseAmount 35
noiseFreq 3.5
gnarl 8
twigCount 7
sides 5
lengthSegs 5
alternating 1
seed 3
uvTiling 1
mat.albedo 0.28 0.16 0.07
mat.roughness 0.9
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 5 4 833 199
leafCount 10
clusterRadius 0.05
leafSize 0.189
normalJitter 0.252
seed 4
mat.albedo 0.15 0.48 0.06
mat.roughness 0.883
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 1
mat.alphaCutoff 0.663
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01_Normal_Winter.png
mat.opacityTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01_Opacity.png
ENDNODE
NODE 6 2 417 199
lengthRatio 0.386
radiusScale 0.395
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 50
rotateOffset 137.5
gravity 0.18
regionStart 0.392
regionEnd 0.95
noiseAmount 90
noiseFreq 2.019
gnarl 10
branchCount 8
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.505
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 7 1 282 119
rootCount 5
length 1.413
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.864
noiseAmount 90
noiseFreq 1.668
gnarl 17.467
sides 6
lengthSegs 10
seed 33
uvTiling 0.472
mat.albedo 0.3 0.19 0.1
mat.roughness 0.561
mat.metallic 0
mat.aoStrength 0.55
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
LINK 1 4
LINK 2 3
LINK 2 7
LINK 3 6
LINK 4 5
LINK 6 1
)VT";

} // namespace

namespace ProjectIO {

bool save(const NodeGraph& graph, const std::string& path,
          const LightingParams* lighting) {
    std::ofstream f(path);
    if (!f) return false;
    f << "VEGTOOL 1\n";
    for (const auto& [id, node] : graph.nodes())
        writeNode(f, node.get());
    // 连线以 (父节点id -> 子节点id) 记录，读回时用各自的 output/input Pin 重连
    for (const auto& [id, node] : graph.nodes()) {
        for (const TreeNode* child : graph.childrenOf(id))
            f << "LINK " << id << ' ' << child->id << '\n';
    }
    // 注释框(编辑器标注)：位置/尺寸写在首行，标题文本单独一行(允许含空格)
    for (const auto& c : graph.comments()) {
        f << "COMMENT " << c.editorPos.x << ' ' << c.editorPos.y
          << ' ' << c.size.x << ' ' << c.size.y << '\n';
        f << c.text << '\n';
        f << "ENDCOMMENT\n";
    }
    // 光照/场景设置(可选)一并写入
    if (lighting) writeLighting(f, *lighting);
    return true;
}

bool load(NodeGraph& graph, const std::string& path, LightingParams* lighting) {
    std::ifstream f(path);
    if (!f) return false;
    return parseStream(graph, f, lighting);
}

void loadDefaultTemplate(NodeGraph& graph) {
    std::istringstream ss(kDefaultTemplate);
    parseStream(graph, ss);
}

// ---- 单节点参数 kv 接口 ----
std::map<std::string, std::string> nodeParamsToMap(const TreeNode* n) {
    std::map<std::string, std::string> out;
    if (!n) return out;
    // 复用 writeNode 输出, 剥掉 NODE 头行与 ENDNODE, 按 "key value..." 拆分
    std::ostringstream os;
    writeNode(os, n);
    std::istringstream is(os.str());
    std::string line;
    bool first = true;
    while (std::getline(is, line)) {
        if (first) { first = false; continue; }   // 跳过 "NODE id type x y"
        if (line == "ENDNODE") break;
        std::istringstream ls(line);
        std::string key; ls >> key;
        std::string val; std::getline(ls, val);
        if (!val.empty() && val[0] == ' ') val.erase(0, 1);
        out[key] = val;
    }
    return out;
}

void applyNodeParams(TreeNode* n, const std::map<std::string, std::string>& kv) {
    if (!n) return;
    KV m(kv.begin(), kv.end());   // applyParams 用 unordered_map
    applyParams(n, m);
}

// ---- 导出辅助 ----
namespace {

// 取路径的文件名(去目录)。用于 mtllib / 贴图相对引用。
std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}
// 去掉扩展名(保留目录)。
std::string stripExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot);
    return path;
}
// 收集批次里出现的贴图, 拷贝到 OBJ/FBX 所在目录旁, 返回"输出目录内的相对文件名"。
// 已拷过的用缓存避免重复。拷贝失败(源不存在)则原样返回源路径, 让 DCC 自行定位。
struct TexCopier {
    std::filesystem::path outDir;
    std::unordered_map<std::string, std::string> cache;
    std::string operator()(const std::string& src) {
        if (src.empty()) return {};
        auto it = cache.find(src);
        if (it != cache.end()) return it->second;
        std::string result = src;
        std::error_code ec;
        std::filesystem::path s(src);
        if (std::filesystem::exists(s, ec)) {
            std::filesystem::path dst = outDir / s.filename();
            std::filesystem::copy_file(
                s, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) result = s.filename().string();
        }
        cache[src] = result;
        return result;
    }
};

} // namespace

// branch 顶点 stride=10 (pos3,nrm3,uv2,wind2); leaf 顶点 stride=16 (pos3,nrm3,uv2,albedo3,wind2,anchor3)。
// 只取 pos/nrm/uv 三段写出, 其余(风力/albedo/anchor)丢弃。OBJ 索引 1-based 且全局累加。
// 同时写出同名 .mtl: 每个批次一份材质(albedo/roughness/贴图), 贴图拷到 obj 旁并相对引用。
bool exportOBJ(const TreeMeshData& mesh, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;

    std::string mtlPath = stripExt(path) + ".mtl";
    std::string mtlName = baseName(mtlPath);
    std::ofstream mf(mtlPath);
    bool haveMtl = (bool)mf;

    std::error_code ec;
    TexCopier copyTex{ std::filesystem::path(path).parent_path() };

    f << "# Exported by VegetationTool\n";
    if (haveMtl) f << "mtllib " << mtlName << '\n';

    size_t vBase = 0;  // 已写出的顶点数(OBJ 索引全局累加)
    int batchIdx = 0;
    for (const auto& batch : mesh.batches) {
        if (batch.vertices.empty() || batch.indices.empty()) continue;
        int stride = batch.isLeaf ? 16 : 10;
        size_t vCount = batch.vertices.size() / stride;
        const MaterialParams& m = batch.material;

        std::string matName = (batch.isLeaf ? "leaf_mat_" : "branch_mat_")
                            + std::to_string(batchIdx);
        if (haveMtl) {
            mf << "newmtl " << matName << '\n';
            mf << "Kd " << m.albedo.x << ' ' << m.albedo.y << ' ' << m.albedo.z << '\n';
            mf << "Ka 0 0 0\n";
            mf << "Ks " << m.metallic << ' ' << m.metallic << ' ' << m.metallic << '\n';
            // OBJ 光泽度 Ns: 由粗糙度粗略反推(粗糙越低 -> 高光越锐)
            mf << "Ns " << (2.0f + (1.0f - m.roughness) * (1.0f - m.roughness) * 900.0f) << '\n';
            std::string bc = copyTex(m.baseColorTex);
            if (!bc.empty()) mf << "map_Kd " << bc << '\n';
            std::string nm = copyTex(m.normalTex);
            if (!nm.empty()) mf << "map_Bump " << nm << '\n';
            std::string op = copyTex(m.opacityTex);
            if (!op.empty()) mf << "map_d " << op << '\n';
            std::string rg = copyTex(m.roughnessTex);
            if (!rg.empty()) mf << "map_Ns " << rg << '\n';
            mf << '\n';
        }

        f << "g " << (batch.isLeaf ? "leaf_" : "branch_") << batchIdx << '\n';
        if (haveMtl) f << "usemtl " << matName << '\n';
        for (size_t i = 0; i < vCount; ++i) {
            const float* v = &batch.vertices[i * stride];
            f << "v "  << v[0] << ' ' << v[1] << ' ' << v[2] << '\n';
        }
        for (size_t i = 0; i < vCount; ++i) {
            const float* v = &batch.vertices[i * stride];
            f << "vn " << v[3] << ' ' << v[4] << ' ' << v[5] << '\n';
        }
        for (size_t i = 0; i < vCount; ++i) {
            const float* v = &batch.vertices[i * stride];
            f << "vt " << v[6] << ' ' << v[7] << '\n';
        }
        // 面: OBJ 用 1-based 且带全局偏移的 v/vt/vn
        for (size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
            uint32_t a = (uint32_t)(vBase + batch.indices[i]   + 1);
            uint32_t b = (uint32_t)(vBase + batch.indices[i+1] + 1);
            uint32_t c = (uint32_t)(vBase + batch.indices[i+2] + 1);
            f << "f " << a << '/' << a << '/' << a << ' '
                      << b << '/' << b << '/' << b << ' '
                      << c << '/' << c << '/' << c << '\n';
        }
        vBase += vCount;
        ++batchIdx;
    }
    (void)ec;
    return true;
}

// ---- FBX (ASCII 7.4) 导出 ----
// 生成一个 Blender / UE / Maya 均可导入的 ASCII FBX: 每个批次 = 一份 Geometry + Model + Material。
// 法线/UV 按 ByPolygonVertex + Direct 逐多边形顶点展开(最稳妥, 各 DCC 通吃)。
// 贴图作为 Texture 对象连到材质的 DiffuseColor/NormalMap/TransparentColor 属性上。
bool exportFBX(const TreeMeshData& mesh, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f.setf(std::ios::fixed);
    f.precision(6);

    TexCopier copyTex{ std::filesystem::path(path).parent_path() };

    // 先过滤出有效批次
    std::vector<const MeshBatch*> batches;
    for (const auto& b : mesh.batches)
        if (!b.vertices.empty() && !b.indices.empty()) batches.push_back(&b);

    // 统一分配 int64 唯一 id
    int64_t nextId = 1000000;
    auto newId = [&]() { return nextId++; };

    struct Obj { int64_t geo, model, mat; std::vector<std::pair<int64_t,std::string>> texs; };
    std::vector<Obj> objs;

    // 头
    f << "; FBX 7.4.0 project file\n; Exported by VegetationTool\n\n";
    f << "FBXHeaderExtension:  {\n"
         "\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n"
         "\tCreator: \"VegetationTool\"\n}\n";
    f << "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
         "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
         "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
         "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
         "\t}\n}\n";

    // Definitions
    int nTex = 0;
    for (const auto* b : batches) {
        const MaterialParams& m = b->material;
        for (const std::string* t : {&m.baseColorTex, &m.normalTex, &m.opacityTex, &m.roughnessTex})
            if (!t->empty()) ++nTex;
    }
    int total = 1 + (int)batches.size()*3 + nTex;
    f << "Definitions:  {\n\tVersion: 100\n\tCount: " << total << "\n";
    f << "\tObjectType: \"GlobalSettings\" {\n\t\tCount: 1\n\t}\n";
    f << "\tObjectType: \"Geometry\" {\n\t\tCount: " << batches.size() << "\n\t}\n";
    f << "\tObjectType: \"Model\" {\n\t\tCount: " << batches.size() << "\n\t}\n";
    f << "\tObjectType: \"Material\" {\n\t\tCount: " << batches.size() << "\n\t}\n";
    if (nTex) f << "\tObjectType: \"Texture\" {\n\t\tCount: " << nTex << "\n\t}\n";
    f << "}\n";

    // Objects
    f << "Objects:  {\n";
    int idx = 0;
    for (const auto* bp : batches) {
        const MeshBatch& b = *bp;
        Obj o; o.geo = newId(); o.model = newId(); o.mat = newId();
        int stride = b.isLeaf ? 16 : 10;
        size_t vCount = b.vertices.size() / stride;
        std::string tag = (b.isLeaf ? "leaf_" : "branch_") + std::to_string(idx);

        // Geometry
        f << "\tGeometry: " << o.geo << ", \"Geometry::" << tag << "\", \"Mesh\" {\n";
        // Vertices(控制点)
        f << "\t\tVertices: *" << vCount*3 << " {\n\t\t\ta: ";
        for (size_t i = 0; i < vCount; ++i) {
            const float* v = &b.vertices[i*stride];
            if (i) f << ',';
            f << v[0] << ',' << v[1] << ',' << v[2];
        }
        f << "\n\t\t}\n";
        // PolygonVertexIndex(每三角形末索引取反标记多边形结束)
        size_t triCount = b.indices.size()/3;
        f << "\t\tPolygonVertexIndex: *" << triCount*3 << " {\n\t\t\ta: ";
        for (size_t i = 0; i+2 < b.indices.size(); i += 3) {
            if (i) f << ',';
            f << b.indices[i] << ',' << b.indices[i+1] << ',' << (-(int)b.indices[i+2]-1);
        }
        f << "\n\t\t}\n";
        // 法线: ByPolygonVertex / Direct
        f << "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
             "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
             "\t\t\tReferenceInformationType: \"Direct\"\n";
        f << "\t\t\tNormals: *" << triCount*3*3 << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i+2 < b.indices.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                const float* v = &b.vertices[b.indices[i+k]*stride];
                if (i||k) f << ',';
                f << v[3] << ',' << v[4] << ',' << v[5];
            }
        }
        f << "\n\t\t\t}\n\t\t}\n";
        // UV: ByPolygonVertex / Direct
        f << "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n"
             "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
             "\t\t\tReferenceInformationType: \"Direct\"\n";
        f << "\t\t\tUV: *" << triCount*3*2 << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i+2 < b.indices.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                const float* v = &b.vertices[b.indices[i+k]*stride];
                if (i||k) f << ',';
                f << v[6] << ',' << v[7];
            }
        }
        f << "\n\t\t\t}\n\t\t}\n";
        // 材质: AllSame -> 索引 0
        f << "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
             "\t\t\tMappingInformationType: \"AllSame\"\n"
             "\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
             "\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n";
        f << "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
             "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
             "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
             "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
             "\t\t}\n";
        f << "\t}\n";

        // Model
        f << "\tModel: " << o.model << ", \"Model::" << tag << "\", \"Mesh\" {\n"
             "\t\tVersion: 232\n\t\tProperties70:  {\n"
             "\t\t\tP: \"InheritType\", \"enum\", \"\", \"\",1\n"
             "\t\t\tP: \"DefaultAttributeIndex\", \"int\", \"Integer\", \"\",0\n"
             "\t\t}\n\t\tShading: T\n\t\tCulling: \"CullingOff\"\n\t}\n";

        // Material
        const MaterialParams& m = b.material;
        f << "\tMaterial: " << o.mat << ", \"Material::" << tag << "\", \"\" {\n"
             "\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n\t\tMultiLayer: 0\n"
             "\t\tProperties70:  {\n";
        f << "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\","
          << m.albedo.x << ',' << m.albedo.y << ',' << m.albedo.z << "\n";
        f << "\t\t\tP: \"SpecularFactor\", \"Number\", \"\", \"A\"," << m.metallic << "\n";
        f << "\t\t\tP: \"ShininessExponent\", \"Number\", \"\", \"A\","
          << (2.0f + (1.0f - m.roughness)*(1.0f - m.roughness)*900.0f) << "\n";
        f << "\t\t}\n\t}\n";

        // Textures
        struct TexSlot { const std::string* src; const char* prop; const char* label; };
        TexSlot slots[] = {
            {&m.baseColorTex, "DiffuseColor",     "diffuse"},
            {&m.normalTex,    "NormalMap",        "normal"},
            {&m.opacityTex,   "TransparentColor", "opacity"},
            {&m.roughnessTex, "ShininessExponent","rough"},
        };
        for (const auto& s : slots) {
            if (s.src->empty()) continue;
            std::string rel = copyTex(*s.src);
            int64_t tid = newId();
            std::string tname = tag + "_" + s.label;
            f << "\tTexture: " << tid << ", \"Texture::" << tname << "\", \"\" {\n"
                 "\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n"
                 "\t\tTextureName: \"Texture::" << tname << "\"\n"
                 "\t\tProperties70:  {\n\t\t\tP: \"UVSet\", \"KString\", \"\", \"\", \"UVMap\"\n\t\t}\n"
                 "\t\tFileName: \"" << *s.src << "\"\n"
                 "\t\tRelativeFilename: \"" << rel << "\"\n\t}\n";
            o.texs.push_back({tid, s.prop});
        }

        objs.push_back(std::move(o));
        ++idx;
    }
    f << "}\n";

    // Connections
    f << "Connections:  {\n";
    for (const auto& o : objs) {
        f << "\tC: \"OO\"," << o.model << ",0\n";           // Model -> RootNode
        f << "\tC: \"OO\"," << o.geo   << ',' << o.model << "\n"; // Geometry -> Model
        f << "\tC: \"OO\"," << o.mat   << ',' << o.model << "\n"; // Material -> Model
        for (const auto& [tid, prop] : o.texs)
            f << "\tC: \"OP\"," << tid << ',' << o.mat << ", \"" << prop << "\"\n"; // Texture -> Material prop
    }
    f << "}\n";
    return true;
}

} // namespace ProjectIO

