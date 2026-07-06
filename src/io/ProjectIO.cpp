#include "ProjectIO.h"
#include "graph/NodeGraph.h"
#include "graph/Nodes.h"
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
        o << "uvTiling "    << p.uvTiling    << '\n';
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
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "droop "       << p.droop       << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTiling "    << p.uvTiling    << '\n';
        writeMaterial(o, p.material);
        break;
    }
    case NodeType::Branch: {
        const auto& p = static_cast<const BranchNode*>(n)->params;
        o << "lengthRatio " << p.lengthRatio << '\n';
        o << "radiusScale " << p.radiusScale << '\n';
        o << "endRatio "    << p.endRatio    << '\n';
        o << "baseFlare "   << p.baseFlare   << '\n';
        o << "taperPow "    << p.taperPow    << '\n';
        o << "spreadAngle " << p.spreadAngle << '\n';
        o << "downAngle "   << p.downAngle   << '\n';
        o << "rotateOffset "<< p.rotateOffset<< '\n';
        o << "gravity "     << p.gravity     << '\n';
        o << "regionStart " << p.regionStart << '\n';
        o << "regionEnd "   << p.regionEnd   << '\n';
        o << "noiseAmount " << p.noiseAmount << '\n';
        o << "noiseFreq "   << p.noiseFreq   << '\n';
        o << "gnarl "       << p.gnarl       << '\n';
        o << "branchCount " << p.branchCount << '\n';
        o << "jointCount "  << p.jointCount  << '\n';
        o << "jointBulge "  << p.jointBulge  << '\n';
        o << "sides "       << p.sides       << '\n';
        o << "lengthSegs "  << p.lengthSegs  << '\n';
        o << "seed "        << p.seed        << '\n';
        o << "uvTiling "    << p.uvTiling    << '\n';
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
        o << "downAngle "   << p.downAngle   << '\n';
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
        o << "uvTiling "    << p.uvTiling    << '\n';
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
        p.seed=getI(kv,"seed",p.seed); p.uvTiling=getF(kv,"uvTiling",p.uvTiling);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Roots: {
        auto& p = static_cast<RootsNode*>(n)->params;
        p.rootCount=getI(kv,"rootCount",p.rootCount); p.length=getF(kv,"length",p.length);
        p.radiusScale=getF(kv,"radiusScale",p.radiusScale); p.endRatio=getF(kv,"endRatio",p.endRatio);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle); p.droop=getF(kv,"droop",p.droop);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount);
        p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq); p.gnarl=getF(kv,"gnarl",p.gnarl);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed); p.uvTiling=getF(kv,"uvTiling",p.uvTiling);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Branch: {
        auto& p = static_cast<BranchNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.downAngle=getF(kv,"downAngle",p.downAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.branchCount=getI(kv,"branchCount",p.branchCount);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed); p.uvTiling=getF(kv,"uvTiling",p.uvTiling);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Twig: {
        auto& p = static_cast<TwigNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.downAngle=getF(kv,"downAngle",p.downAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.twigCount=getI(kv,"twigCount",p.twigCount);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.alternating=getI(kv,"alternating",p.alternating?1:0)!=0;
        p.seed=getI(kv,"seed",p.seed); p.uvTiling=getF(kv,"uvTiling",p.uvTiling);
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
    }
}

} // namespace

namespace ProjectIO {

bool save(const NodeGraph& graph, const std::string& path) {
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
    return true;
}

bool load(NodeGraph& graph, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    if (!std::getline(f, line) || line.rfind("VEGTOOL", 0) != 0) return false;

    graph.clear();

    // savedId -> 新建的实际NodeId
    std::unordered_map<uint32_t, NodeId> idMap;
    // 待建立的连线(用 savedId 记录)
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
                if (!val.empty() && val[0] == ' ') val.erase(0, 1); // 去掉key后的单空格
                kv[key] = val;
            }
            NodeId newId = graph.addNode((NodeType)typeInt, {px, py});
            idMap[savedId] = newId;
            if (TreeNode* n = graph.getNode(newId))
                applyParams(n, kv);
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

} // namespace ProjectIO
