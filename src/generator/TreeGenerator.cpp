#include "TreeGenerator.h"
#include "CylinderSegment.h"
#include "graph/Nodes.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <random>
#include <algorithm>

static constexpr int MAX_DEPTH = 6;

// ---- 工具 ----
glm::vec3 TreeGenerator::perpendicular(glm::vec3 dir) {
    glm::vec3 ref = (std::abs(dir.y) < 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    return glm::normalize(glm::cross(ref, dir));
}

glm::vec3 TreeGenerator::rotateAroundAxis(glm::vec3 v, glm::vec3 axis, float angleDeg) {
    float rad = glm::radians(angleDeg);
    float c   = std::cos(rad), s = std::sin(rad);
    return v*c + glm::cross(axis, v)*s + axis*glm::dot(axis, v)*(1-c);
}

// 按比例 t∈[0,1] 从 rings 中插值位置、切线方向、right轴
void TreeGenerator::sampleRings(const std::vector<BranchRing>& rings, float t,
    glm::vec3& outPos, glm::vec3& outDir, glm::vec3& outRight)
{
    if (rings.empty()) return;
    if (rings.size() == 1) {
        outPos   = rings[0].center;
        outDir   = rings[0].up;
        outRight = rings[0].right;
        return;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    float fi   = t * (float)(rings.size() - 1);
    int   lo   = (int)fi;
    int   hi   = std::min(lo + 1, (int)rings.size() - 1);
    float frac = fi - (float)lo;

    outPos   = glm::mix(rings[lo].center, rings[hi].center, frac);
    outDir   = glm::normalize(glm::mix(rings[lo].up,    rings[hi].up,    frac));
    outRight = glm::normalize(glm::mix(rings[lo].right, rings[hi].right, frac));
}

MeshBatch& TreeGenerator::getBatch(const MaterialParams& mat, bool isLeaf) {
    for (auto& b : m_out->batches) {
        if (b.isLeaf == isLeaf &&
            glm::distance(b.material.albedo, mat.albedo) < 0.01f)
            return b;
    }
    MeshBatch nb;
    nb.material = mat;
    nb.isLeaf   = isLeaf;
    m_out->batches.push_back(std::move(nb));
    return m_out->batches.back();
}

void TreeGenerator::appendCylinder(MeshBatch& batch,
                                    const std::vector<BranchRing>& rings, int sides) {
    if (rings.size() < 2) return;
    float pi2 = glm::two_pi<float>();

    // 估算总圆柱长度（用于V轴UV缩放）
    float totalLen = 0.0f;
    for (size_t i = 1; i < rings.size(); ++i)
        totalLen += glm::length(rings[i].center - rings[i-1].center);
    if (totalLen < 1e-5f) totalLen = 1.0f;

    float vAccum = 0.0f;
    for (size_t ri = 0; ri + 1 < rings.size(); ++ri) {
        const BranchRing& bot = rings[ri];
        const BranchRing& top = rings[ri+1];
        float segLen = glm::length(top.center - bot.center);
        float vBot = vAccum / totalLen;
        float vTop = (vAccum + segLen) / totalLen;
        vAccum += segLen;

        // 每个顶点: pos(3)+normal(3)+uv(2) = 8 floats
        uint32_t base = (uint32_t)(batch.vertices.size() / 8);
        for (int ring = 0; ring < 2; ++ring) {
            const BranchRing& r = (ring == 0) ? bot : top;
            float vCoord = (ring == 0) ? vBot : vTop;
            for (int j = 0; j <= sides; ++j) {
                float angle = (float)j / (float)sides * pi2;
                float uCoord = (float)j / (float)sides;
                glm::vec3 localDir = r.right * std::cos(angle)
                                   + glm::cross(r.up, r.right) * std::sin(angle);
                glm::vec3 pos    = r.center + localDir * r.radius;
                glm::vec3 normal = glm::normalize(localDir);
                batch.vertices.insert(batch.vertices.end(),
                    {pos.x,pos.y,pos.z, normal.x,normal.y,normal.z, uCoord, vCoord});
            }
        }
        int rv = sides + 1;
        for (int j = 0; j < sides; ++j) {
            uint32_t b0 = base + j,      b1 = base + j + 1;
            uint32_t t0 = base + rv + j, t1 = base + rv + j + 1;
            batch.indices.insert(batch.indices.end(), {b0,b1,t0, b1,t1,t0});
        }
    }
}

// ---- 主入口 ----
TreeMeshData TreeGenerator::generate(NodeGraph& graph) {
    TreeMeshData data;
    m_out = &data;
    TreeNode* root = graph.rootNode();
    if (!root) return data;
    processNode(graph, root, nullptr, {0,0,0}, {0,1,0}, 1.0f, 0);
    m_out = nullptr;
    return data;
}

void TreeGenerator::processNode(
    const NodeGraph& graph, const TreeNode* node,
    const std::vector<BranchRing>* parentRings,
    glm::vec3 origin, glm::vec3 dir,
    float parentLen, int depth)
{
    if (!node || depth > MAX_DEPTH) return;
    switch (node->getType()) {
    case NodeType::Trunk: {
        auto rings = buildTrunk(static_cast<const TrunkNode*>(node), origin, dir);
        float trunkLen = static_cast<const TrunkNode*>(node)->params.length;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, origin, dir, trunkLen, depth+1);
        break;
    }
    case NodeType::Branch:
        if (parentRings && !parentRings->empty())
            buildBranches(static_cast<const BranchNode*>(node),
                          graph, *parentRings, parentLen, depth);
        break;
    case NodeType::Twig:
        if (parentRings && !parentRings->empty())
            buildTwig(static_cast<const TwigNode*>(node),
                      graph, *parentRings, parentLen, depth);
        break;
    case NodeType::LeafCluster:
        buildLeafCluster(static_cast<const LeafClusterNode*>(node), origin, dir);
        break;
    }
}

// ---- Trunk ----
std::vector<BranchRing> TreeGenerator::buildTrunk(
    const TrunkNode* node, glm::vec3 origin, glm::vec3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed);

    auto rings = CylinderSegment::buildKinkedRings(
        origin, dir, p.length,
        p.startRadius * p.baseFlare, p.endRadius,
        p.lengthSegs, p.bendCount, p.bendAngle, rng);

    if (!rings.empty()) rings[0].radius = p.startRadius * p.baseFlare;

    auto& batch = getBatch(p.material, false);
    appendCylinder(batch, rings, p.sides);
    return rings;
}

// ---- Branch ----
void TreeGenerator::buildBranches(
    const BranchNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    float branchLen = parentLen * p.lengthRatio;
    std::mt19937 rng(p.seed + depth * 100);
    std::uniform_real_distribution<float> jitter(-5.0f, 5.0f);
    std::uniform_real_distribution<float> jitterLen(0.85f, 1.15f);

    for (int i = 0; i < p.branchCount; ++i) {
        // 从父节点rings按attachT取附着点、切线、right
        float attachT = (p.branchCount > 1)
            ? glm::mix(0.2f, 0.95f, (float)i / (float)(p.branchCount-1))
            : 0.6f;

        glm::vec3 attachPos, attachDir, attachRight;
        sampleRings(parentRings, attachT, attachPos, attachDir, attachRight);

        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);

        // 以attachDir为轴向、attachRight为参考，计算分支方向
        glm::vec3 branchDir = rotateAroundAxis(attachDir, attachRight, el);
        branchDir = rotateAroundAxis(branchDir, attachDir, az);
        branchDir = glm::normalize(glm::mix(branchDir, glm::vec3(0,-1,0), p.gravity*0.25f));

        float thisLen = branchLen * jitterLen(rng);

        auto rings = CylinderSegment::buildKinkedRings(
            attachPos, branchDir, thisLen,
            p.startRadius, p.endRadius,
            p.lengthSegs, p.bendCount, p.bendAngle, rng);

        auto& batch = getBatch(p.material, false);
        appendCylinder(batch, rings, p.sides);

        // 子节点从branch末端（折弯后的真实末端）出发
        glm::vec3 tip      = rings.empty() ? attachPos + branchDir * thisLen : rings.back().center;
        glm::vec3 tipDir   = rings.empty() ? branchDir : rings.back().up;

        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                processNode(graph, child, nullptr, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, attachPos, branchDir, thisLen, depth+1);
        }
    }
}

// ---- Twig ----
void TreeGenerator::buildTwig(
    const TwigNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    float twigLen = parentLen * p.lengthRatio;
    std::mt19937 rng(p.seed + depth * 77);
    std::uniform_real_distribution<float> jitter(-8.0f, 8.0f);
    std::uniform_real_distribution<float> jitterLen(0.8f, 1.2f);
    std::uniform_real_distribution<float> attachDist(0.2f, 0.95f);

    float baseAz = 0.0f;
    for (int i = 0; i < p.twigCount; ++i) {
        float az = p.alternating
                   ? (i % 2 == 0 ? baseAz : baseAz + 180.0f) + jitter(rng)
                   : i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);
        if (p.alternating) baseAz += p.rotateOffset;

        float t = attachDist(rng);
        glm::vec3 attachPos, attachDir, attachRight;
        sampleRings(parentRings, t, attachPos, attachDir, attachRight);

        glm::vec3 twigDir = rotateAroundAxis(attachDir, attachRight, el);
        twigDir = rotateAroundAxis(twigDir, attachDir, az);
        twigDir = glm::normalize(glm::mix(twigDir, glm::vec3(0,-1,0), p.gravity*0.25f));

        float thisLen = twigLen * jitterLen(rng);

        auto rings = CylinderSegment::buildKinkedRings(
            attachPos, twigDir, thisLen,
            p.startRadius, p.endRadius,
            p.lengthSegs, p.bendCount, p.bendAngle, rng);

        auto& batch = getBatch(p.material, false);
        appendCylinder(batch, rings, p.sides);

        glm::vec3 tip    = rings.empty() ? attachPos + twigDir * thisLen : rings.back().center;
        glm::vec3 tipDir = rings.empty() ? twigDir : rings.back().up;

        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                processNode(graph, child, nullptr, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, attachPos, twigDir, thisLen, depth+1);
        }
    }
}

// ---- LeafCluster ----
void TreeGenerator::buildLeafCluster(
    const LeafClusterNode* node, glm::vec3 origin, glm::vec3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed);
    std::uniform_real_distribution<float> jitter(-p.normalJitter, p.normalJitter);
    std::uniform_real_distribution<float> radJit(0.65f, 1.35f);
    std::uniform_real_distribution<float> rotJit(0.0f, 360.0f);

    float goldenAngle = glm::radians(137.508f);
    auto& batch = getBatch(p.material, true);
    glm::vec3 col = p.material.albedo;

    for (int i = 0; i < p.leafCount; ++i) {
        float phi   = std::acos(1.0f - (float)(i+0.5f) / (float)p.leafCount);
        float theta = goldenAngle * (float)i;
        glm::vec3 n = glm::normalize(glm::vec3(
            std::sin(phi)*std::cos(theta) + jitter(rng),
            std::abs(std::cos(phi)) + jitter(rng) * 0.5f,
            std::sin(phi)*std::sin(theta) + jitter(rng)
        ));
        glm::vec3 pos = origin + n * (p.clusterRadius * radJit(rng));

        glm::vec3 leafUp    = glm::normalize(glm::mix(n, glm::vec3(0,1,0), 0.5f));
        glm::vec3 leafRight = glm::normalize(glm::cross(leafUp, glm::vec3(0,0,1)));
        if (glm::length(leafRight) < 0.01f)
            leafRight = glm::normalize(glm::cross(leafUp, glm::vec3(1,0,0)));
        glm::vec3 leafFwd = glm::normalize(glm::cross(leafRight, leafUp));

        float rot = glm::radians(rotJit(rng));
        leafRight = leafRight*std::cos(rot) + leafFwd*std::sin(rot);
        leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));

        float hs = p.leafSize * 0.5f;
        float hw = hs * 0.65f;

        // 4顶点，带UV: 左下(0,0) 右下(1,0) 右上(1,1) 左上(0,1)
        struct LV { glm::vec3 pos; float u, v; };
        LV lv[4] = {
            {pos - leafRight*hw - leafUp*hs, 0.f, 0.f},
            {pos + leafRight*hw - leafUp*hs, 1.f, 0.f},
            {pos + leafRight*hw + leafUp*hs, 1.f, 1.f},
            {pos - leafRight*hw + leafUp*hs, 0.f, 1.f},
        };
        // 每顶点: pos(3)+normal(3)+uv(2)+albedo(3) = 11 floats
        uint32_t base = (uint32_t)(batch.vertices.size() / 11);
        for (auto& lvi : lv) {
            batch.vertices.insert(batch.vertices.end(),
                {lvi.pos.x,lvi.pos.y,lvi.pos.z,
                 leafFwd.x,leafFwd.y,leafFwd.z,
                 lvi.u, lvi.v,
                 col.r,col.g,col.b});
        }
        batch.indices.insert(batch.indices.end(),
            {base,base+1,base+2, base,base+2,base+3});
    }
}
