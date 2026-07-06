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
    glm::vec3& outPos, glm::vec3& outDir, glm::vec3& outRight, float& outRadius)
{
    if (rings.empty()) return;
    if (rings.size() == 1) {
        outPos    = rings[0].center;
        outDir    = rings[0].up;
        outRight  = rings[0].right;
        outRadius = rings[0].radius;
        return;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    float fi   = t * (float)(rings.size() - 1);
    int   lo   = (int)fi;
    int   hi   = std::min(lo + 1, (int)rings.size() - 1);
    float frac = fi - (float)lo;

    outPos    = glm::mix(rings[lo].center, rings[hi].center, frac);
    outDir    = glm::normalize(glm::mix(rings[lo].up,    rings[hi].up,    frac));
    outRight  = glm::normalize(glm::mix(rings[lo].right, rings[hi].right, frac));
    outRadius = glm::mix(rings[lo].radius, rings[hi].radius, frac);
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
                                    const std::vector<BranchRing>& rings, int sides,
                                    float uvTiling) {
    if (rings.size() < 2) return;
    float pi2 = glm::two_pi<float>();

    // 估算总圆柱长度（用于V轴UV缩放）
    float totalLen = 0.0f;
    for (size_t i = 1; i < rings.size(); ++i)
        totalLen += glm::length(rings[i].center - rings[i-1].center);
    if (totalLen < 1e-5f) totalLen = 1.0f;

    float vAccum = 0.0f;
    // U 沿圆周方向平铺：为保证接缝无缝，取整数次（至少1次）
    float uTiling = std::max(1.0f, std::round(uvTiling));
    for (size_t ri = 0; ri + 1 < rings.size(); ++ri) {
        const BranchRing& bot = rings[ri];
        const BranchRing& top = rings[ri+1];
        float segLen = glm::length(top.center - bot.center);
        // V 沿长度平铺 uvTiling 次，避免整段被单张纹理纵向拉伸
        float vBot = (vAccum / totalLen) * uvTiling;
        float vTop = ((vAccum + segLen) / totalLen) * uvTiling;
        vAccum += segLen;

        // 每个顶点: pos(3)+normal(3)+uv(2) = 8 floats
        uint32_t base = (uint32_t)(batch.vertices.size() / 8);
        for (int ring = 0; ring < 2; ++ring) {
            const BranchRing& r = (ring == 0) ? bot : top;
            float vCoord = (ring == 0) ? vBot : vTop;
            for (int j = 0; j <= sides; ++j) {
                float angle = (float)j / (float)sides * pi2;
                float uCoord = (float)j / (float)sides * uTiling;
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

// 生成枝领裙边：内圈=子枝基部真实一圈，外圈=沿径向外扩后投影到父级表面的一圈。
// 两圈之间用四边形连成一圈"吸"在父级表面的过渡裙，彻底消除穿模。
void TreeGenerator::appendCollar(MeshBatch& batch,
    glm::vec3 parentC, glm::vec3 parentA, float parentR,
    glm::vec3 childBase, glm::vec3 childDir, glm::vec3 childRight,
    float startR, float baseFlare, int sides,
    float uvTiling, float branchTotalLen)
{
    if (baseFlare <= 1.0f || parentR <= 1e-5f) return;
    float pi2 = glm::two_pi<float>();
    glm::vec3 childUp = glm::normalize(childDir);
    glm::vec3 fwd     = glm::cross(childUp, childRight);

    // 外圈外扩宽度：限制在父级半径范围内，否则底盘会绕过树干、边缘悬空出头
    float flareWidth = std::min(startR * (baseFlare - 1.0f), parentR * 0.95f);
    float outerR     = startR + flareWidth;
    // childBase 在父级轴上的投影位置，用于把裙边压扁、紧贴附着点
    float baseAxial  = glm::dot(childBase - parentC, parentA);

    // UV：与枝干本体保持一致，避免接缝处压缩/错位
    // U 沿圆周取整数次平铺（同 appendCylinder）；
    // V 按裙边真实物理长度换算，令纹理密度与枝干本体相同(uvTiling/totalLen)
    float uTiling = std::max(1.0f, std::round(uvTiling));
    float vPerUnit = (branchTotalLen > 1e-5f) ? uvTiling / branchTotalLen : 0.0f;

    // 先算出外圈顶点位置，用于按内外圈物理间距推出 V
    uint32_t base = (uint32_t)(batch.vertices.size() / 8);
    int rv = sides + 1;

    for (int ring = 0; ring < 2; ++ring) {
        for (int j = 0; j <= sides; ++j) {
            float angle = (float)j / (float)sides * pi2;
            glm::vec3 localDir = childRight * std::cos(angle) + fwd * std::sin(angle);
            glm::vec3 innerPos = childBase + localDir * startR;
            glm::vec3 pos;
            float vCoord;
            if (ring == 0) {
                // 内圈：子枝基部真实一圈，V=0 与枝干本体基部对齐
                pos    = innerPos;
                vCoord = 0.0f;
            } else {
                // 外圈：外扩后投影到父级表面，并把轴向位置向附着点收拢(裙边压扁)
                glm::vec3 flared = childBase + localDir * outerR;
                glm::vec3 v      = flared - parentC;
                // 让裙边更多地沿父级表面延展(0.85)而非过度压扁，闭合下侧缝隙
                float     axial  = glm::mix(baseAxial, glm::dot(v, parentA), 0.85f);
                glm::vec3 radial = v - parentA * glm::dot(v, parentA);
                float len = glm::length(radial);
                glm::vec3 rdir = (len > 1e-5f) ? radial / len : localDir;
                pos = parentC + parentA * axial + rdir * parentR;
                // 外圈朝枝干基部下方，V 取负，长度按真实间距缩放（不再硬编码 0→1）
                float collarLen = glm::length(pos - innerPos);
                vCoord = -collarLen * vPerUnit;
            }
            glm::vec3 normal = glm::normalize(localDir);
            float uCoord = (float)j / (float)sides * uTiling;
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, normal.x,normal.y,normal.z,
                 uCoord, vCoord});
        }
    }
    for (int j = 0; j < sides; ++j) {
        uint32_t i0 = base + j,      i1 = base + j + 1;
        uint32_t o0 = base + rv + j, o1 = base + rv + j + 1;
        // 外圈在父级表面、内圈在子枝基部，缠成裙边（双面朝外）
        batch.indices.insert(batch.indices.end(), {i0,o0,i1, i1,o0,o1});
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
    case NodeType::Roots:
        if (parentRings && !parentRings->empty())
            buildRoots(static_cast<const RootsNode*>(node), *parentRings);
        break;
    case NodeType::LeafCluster:
        buildLeafCluster(static_cast<const LeafClusterNode*>(node), parentRings, origin, dir);
        break;
    }
}

// ---- Trunk ----
std::vector<BranchRing> TreeGenerator::buildTrunk(
    const TrunkNode* node, glm::vec3 origin, glm::vec3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed);

    auto rings = CylinderSegment::buildNaturalRings(
        origin, dir, p.length,
        p.startRadius * p.baseFlare, p.endRadius,
        p.lengthSegs, p.noiseAmount, p.noiseFreq,
        p.gnarl, p.taperPow, 0.0f, rng);

    if (!rings.empty()) rings[0].radius = p.startRadius * p.baseFlare;

    auto& batch = getBatch(p.material, false);
    appendCylinder(batch, rings, p.sides, p.uvTiling);
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
        // 从父节点rings按attachT取附着点，限制在[regionStart,regionEnd]区间内，
        // 父级太底部/太顶部区域不长枝条
        float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
        float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
        if (re < rs) std::swap(rs, re);
        float attachT = (p.branchCount > 1)
            ? glm::mix(rs, re, (float)i / (float)(p.branchCount-1))
            : glm::mix(rs, re, 0.5f);

        glm::vec3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, attachT, attachPos, attachDir, attachRight, attachRadius);

        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);

        // 以attachDir为轴向、attachRight为参考，计算分支方向
        glm::vec3 branchDir = rotateAroundAxis(attachDir, attachRight, el);
        branchDir = rotateAroundAxis(branchDir, attachDir, az);

        // 向下引导：把初始方向朝地面(0,-1,0)偏转，越靠父级底部(attachT越小)偏转越大
        if (p.downAngle > 0.0f) {
            float droop = p.downAngle * (1.0f - attachT);
            branchDir = glm::normalize(glm::mix(branchDir, glm::vec3(0,-1,0), glm::clamp(droop, 0.0f, 1.0f)));
        }

        float thisLen = branchLen * jitterLen(rng);
        // start半径贴合父级附着点，end按自身锥度收缩
        float startR = attachRadius * p.radiusScale;
        float endR   = startR * p.endRatio;

        // 枝条从父级“表面”发出（轴心 + 径向×父半径），而非从轴心穿出
        glm::vec3 radial = branchDir - attachDir * glm::dot(branchDir, attachDir);
        glm::vec3 surfacePos = (glm::length(radial) > 1e-4f)
            ? attachPos + glm::normalize(radial) * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, branchDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, p.gravity, rng);

        auto& batch = getBatch(p.material, false);
        appendCylinder(batch, rings, p.sides, p.uvTiling);
        // 枝领：把基部一圈投影到父级表面，形成贴合过渡裙
        if (!rings.empty())
            appendCollar(batch, attachPos, attachDir, attachRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTiling, thisLen);

        // 子节点从branch末端（折弯后的真实末端）出发
        glm::vec3 tip      = rings.empty() ? surfacePos + branchDir * thisLen : rings.back().center;
        glm::vec3 tipDir   = rings.empty() ? branchDir : rings.back().up;

        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                // 叶簇沿整根枝条均匀生长：把本枝 rings 传下去
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
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
    float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
    float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
    if (re < rs) std::swap(rs, re);
    std::uniform_real_distribution<float> attachDist(rs, re);

    float baseAz = 0.0f;
    for (int i = 0; i < p.twigCount; ++i) {
        float az = p.alternating
                   ? (i % 2 == 0 ? baseAz : baseAz + 180.0f) + jitter(rng)
                   : i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);
        if (p.alternating) baseAz += p.rotateOffset;

        float t = attachDist(rng);
        glm::vec3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, t, attachPos, attachDir, attachRight, attachRadius);

        glm::vec3 twigDir = rotateAroundAxis(attachDir, attachRight, el);
        twigDir = rotateAroundAxis(twigDir, attachDir, az);

        // 向下引导：把初始方向朝地面(0,-1,0)偏转，越靠父级底部(t越小)偏转越大
        if (p.downAngle > 0.0f) {
            float droop = p.downAngle * (1.0f - t);
            twigDir = glm::normalize(glm::mix(twigDir, glm::vec3(0,-1,0), glm::clamp(droop, 0.0f, 1.0f)));
        }

        float thisLen = twigLen * jitterLen(rng);
        // start半径贴合父级附着点，end按自身锥度收缩
        float startR = attachRadius * p.radiusScale;
        float endR   = startR * p.endRatio;

        // 细枝从父级“表面”发出，而非从轴心穿出
        glm::vec3 radial = twigDir - attachDir * glm::dot(twigDir, attachDir);
        glm::vec3 surfacePos = (glm::length(radial) > 1e-4f)
            ? attachPos + glm::normalize(radial) * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, twigDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, p.gravity, rng);

        auto& batch = getBatch(p.material, false);
        appendCylinder(batch, rings, p.sides, p.uvTiling);
        // 枝领：把基部一圈投影到父级表面，形成贴合过渡裙
        if (!rings.empty())
            appendCollar(batch, attachPos, attachDir, attachRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTiling, thisLen);

        glm::vec3 tip    = rings.empty() ? surfacePos + twigDir * thisLen : rings.back().center;
        glm::vec3 tipDir = rings.empty() ? twigDir : rings.back().up;

        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                // 叶簇沿整根细枝均匀生长：把本枝 rings 传下去
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, attachPos, twigDir, thisLen, depth+1);
        }
    }
}

// ---- Roots ----
// 从树干“基部”一圈向外辐射铺开，再借 droop(下扎)沿长度逐渐转向地下，
// 末端按锥度收细成尖。方位用黄金角分布，接壤处套用枝领裙边贴合树干。
void TreeGenerator::buildRoots(
    const RootsNode* node, const std::vector<BranchRing>& parentRings)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + 500);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    std::uniform_real_distribution<float> jitterLen(0.8f, 1.25f);

    // 树干基部环(t=0)：附着中心、轴向、half、半径
    glm::vec3 basePos, baseDir, baseRight;
    float     baseRadius = 1.0f;
    sampleRings(parentRings, 0.0f, basePos, baseDir, baseRight, baseRadius);

    auto& batch = getBatch(p.material, false);

    for (int i = 0; i < p.rootCount; ++i) {
        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);  // 接近90°=先近水平铺开

        // 以树干轴为参考，先抬到 spreadAngle 再绕轴旋方位
        glm::vec3 rootDir = rotateAroundAxis(baseDir, baseRight, el);
        rootDir = rotateAroundAxis(rootDir, baseDir, az);
        rootDir = glm::normalize(rootDir);

        float thisLen = p.length * jitterLen(rng);
        float startR  = baseRadius * p.radiusScale;
        float endR    = startR * p.endRatio;

        // 从树干“表面”发出，而非从轴心穿出
        glm::vec3 radial = rootDir - baseDir * glm::dot(rootDir, baseDir);
        glm::vec3 surfacePos = (glm::length(radial) > 1e-4f)
            ? basePos + glm::normalize(radial) * baseRadius
            : basePos;

        // droop 作为“重力”参数传入，使根沿长度持续向下弯扎入地
        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, rootDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, p.droop, rng);

        appendCylinder(batch, rings, p.sides, p.uvTiling);
        // 根领：接壤处裙边贴合树干表面
        if (!rings.empty())
            appendCollar(batch, basePos, baseDir, baseRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTiling, thisLen);
    }
}

// ---- LeafCluster ----
// 叶片沿父级枝条(twig)整根均匀生长；每片叶以"垂直于枝条轴向"的随机外扩方向为
// 生长方向(略带向枝梢前倾)，叶面法线再在垂直于生长方向的平面内随机旋转，
// 避免所有叶片朝同一方向而显假。
void TreeGenerator::buildLeafCluster(
    const LeafClusterNode* node,
    const std::vector<BranchRing>* parentRings,
    glm::vec3 origin, glm::vec3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed);
    std::uniform_real_distribution<float> jitter(-p.normalJitter, p.normalJitter);
    std::uniform_real_distribution<float> radJit(0.7f, 1.3f);
    std::uniform_real_distribution<float> rot01(0.0f, 1.0f);

    float goldenAngle = glm::radians(137.508f);
    float pi2 = glm::two_pi<float>();
    auto& batch = getBatch(p.material, true);
    glm::vec3 col = p.material.albedo;

    bool haveRings = (parentRings && !parentRings->empty());

    for (int i = 0; i < p.leafCount; ++i) {
        // 附着点：沿整根枝条均匀分布(带轻微抖动)，而非全挤在末端
        glm::vec3 axisPos, axisDir, axisRight;
        float     axisRadius = 0.0f;
        if (haveRings) {
            float t = (p.leafCount > 1) ? (float)i / (float)(p.leafCount - 1) : 0.5f;
            t = glm::clamp(t + (rot01(rng) - 0.5f) / (float)p.leafCount, 0.0f, 1.0f);
            sampleRings(*parentRings, t, axisPos, axisDir, axisRight, axisRadius);
        } else {
            axisPos = origin;
            axisDir = glm::normalize(dir);
            axisRight = perpendicular(axisDir);
        }
        axisDir = glm::normalize(axisDir);

        // 绕枝条轴的随机方位(黄金角+抖动)，构造垂直于枝条方向的外扩方向
        float az = goldenAngle * (float)i + (rot01(rng) - 0.5f) * 0.7f;
        glm::vec3 fwd    = glm::normalize(glm::cross(axisDir, axisRight));
        glm::vec3 outDir = glm::normalize(axisRight * std::cos(az) + fwd * std::sin(az));

        // 叶片生长方向：以垂直外扩为主，略向枝梢方向前倾 + 抖动
        glm::vec3 leafUp = glm::normalize(outDir + axisDir * (0.25f + jitter(rng)));

        // 叶基贴在枝条表面，叶片向外伸展
        glm::vec3 basePos = axisPos + outDir * axisRadius;
        float hs = p.leafSize * 0.5f;
        float hw = hs * 0.65f;
        glm::vec3 pos = basePos + leafUp * ((hs + p.clusterRadius * 0.4f) * radJit(rng));

        // 叶面朝向：在垂直于 leafUp 的平面内随机选一个法线朝向
        glm::vec3 tmp = (std::abs(leafUp.y) < 0.95f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        glm::vec3 leafRight = glm::normalize(glm::cross(leafUp, tmp));
        glm::vec3 leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));
        float rot = rot01(rng) * pi2;
        leafRight = glm::normalize(leafRight * std::cos(rot) + leafFwd * std::sin(rot));
        leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));

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
