#include "ProjectIO.h"
#include "graph/NodeGraph.h"
#include "graph/Nodes.h"
#include "renderer/Renderer.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

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
        o << "planar "        << (p.planar ? 1 : 0) << '\n';
        o << "sizeFalloff "   << p.sizeFalloff   << '\n';
        o << "seed "          << p.seed          << '\n';
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
        writeMaterial(o, p.material);
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
        readMaterial(kv, p.material); break;
    }
    case NodeType::LeafCluster: {
        auto& p = static_cast<LeafClusterNode*>(n)->params;
        p.leafCount=getI(kv,"leafCount",p.leafCount); p.clusterRadius=getF(kv,"clusterRadius",p.clusterRadius);
        p.leafSize=getF(kv,"leafSize",p.leafSize); p.normalJitter=getF(kv,"normalJitter",p.normalJitter);
        p.leafAspect=getF(kv,"leafAspect",p.leafAspect);
        p.planar=getI(kv,"planar",p.planar?1:0)!=0; p.sizeFalloff=getF(kv,"sizeFalloff",p.sizeFalloff);
        p.seed=getI(kv,"seed",p.seed);
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
        readMaterial(kv, p.material); break;
    }
    case NodeType::Frond: {
        auto& p = static_cast<FrondNode*>(n)->params;
        p.width=getF(kv,"width",p.width); p.widthBase=getF(kv,"widthBase",p.widthBase);
        p.widthTip=getF(kv,"widthTip",p.widthTip); p.profilePow=getF(kv,"profilePow",p.profilePow);
        p.curl=getF(kv,"curl",p.curl); p.segsPerSide=getI(kv,"segsPerSide",p.segsPerSide);
        p.serrate=getI(kv,"serrate",p.serrate?1:0)!=0; p.serrateDepth=getF(kv,"serrateDepth",p.serrateDepth);
        p.seed=getI(kv,"seed",p.seed);
        readMaterial(kv, p.material); break;
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

} // namespace ProjectIO
