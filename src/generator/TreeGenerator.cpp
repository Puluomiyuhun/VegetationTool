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

// SpeedTree 式"焊接(weld)"过渡：把子枝基部一圈的每个顶点单独投影到父级表面，
// 得到自然的鞍形交线(而非平圆盘)，再在枝条截面圈与该投影圈之间铺 weldSegs 段
// 环列做平滑(smoothstep→两端相切)过渡；上/下侧沿父级轴向做"蹼状"延展，
// 令枝腋处形成真实枝领而非突兀的吸盘。
void TreeGenerator::appendCollar(MeshBatch& batch,
    glm::vec3 parentC, glm::vec3 parentA, float parentR,
    glm::vec3 childBase, glm::vec3 childDir, glm::vec3 childRight,
    float startR, float baseFlare, int sides,
    float uvTiling, float branchTotalLen,
    const std::vector<BranchRing>* trunkRings,
    float collarSink)
{
    if (baseFlare <= 1.0f || parentR <= 1e-5f) return;
    float pi2 = glm::two_pi<float>();
    glm::vec3 A   = glm::normalize(parentA);
    glm::vec3 up  = glm::normalize(childDir);
    glm::vec3 fwd = glm::normalize(glm::cross(up, childRight));

    const int weldSegs = 4;                          // 过渡环数(越多越圆滑)
    float flareMax  = startR * (baseFlare - 1.0f);   // 向外扩张量(径向)
    // 上/下"蹼"沿父级轴向的延展量(下侧枝腋略大, 更接近真实枝领)。
    // 用 startR 设上限：baseFlare 较大(如树根 2.5)时不至于把裙边沿轴拉成过长尖刺，
    // 同时仍保留足够延展避免交界生硬(盒子感)。竖直枝 startR 小、上限自然不触发。
    float upperSpread = std::min(flareMax * 1.2f, startR * 0.9f);
    float lowerSpread = std::min(flareMax * 1.6f, startR * 1.2f);
    float landingR    = parentR * 0.99f;             // 末端略沉入父级表面, 被父级面遮住→消除悬空缝隙
    float uTiling  = std::max(1.0f, std::round(uvTiling));
    float vPerUnit = (branchTotalLen > 1e-5f) ? uvTiling / branchTotalLen : 0.0f;

    // 当传入父级(树干)rings时: 裙边"落点"半径随树干在该高度的真实半径变化(而非常数)，
    // 使根盘外圈/上缘随树干锥度+树盘外扩起伏贴合，避免树干变细处外圈悬空(圆盘外圈贴不上)。
    // 只有 Roots 会传 trunkRings，Branch/Twig 传 nullptr → 行为与之前完全一致。
    auto landingAt = [&](float axial) -> float {
        if (!trunkRings || trunkRings->size() < 2) return landingR;
        const auto& tr = *trunkRings;
        float a0 = glm::dot(tr.front().center - parentC, A);
        for (size_t i = 1; i < tr.size(); ++i) {
            float a1 = glm::dot(tr[i].center - parentC, A);
            if (axial <= a1 || i + 1 == tr.size()) {
                float denom = (a1 - a0);
                float f = (std::abs(denom) > 1e-5f) ? (axial - a0) / denom : 0.0f;
                f = std::clamp(f, 0.0f, 1.0f);
                return glm::mix(tr[i-1].radius, tr[i].radius, f) * 0.99f;
            }
            a0 = a1;
        }
        return landingR;
    };

    auto smooth = [](float x){ x = std::clamp(x, 0.0f, 1.0f); return x*x*(3.0f-2.0f*x); };

    int rv = sides + 1;
    uint32_t base = (uint32_t)(batch.vertices.size() / 8);

    for (int k = 0; k <= weldSegs; ++k) {
        float u    = (float)k / (float)weldSegs;
        float su   = smooth(u);
        float R    = startR + flareMax * su;   // 半径沿过渡外扩
        float proj = su;                       // 向父级表面投影比例(两端导数≈0→相切)
        for (int j = 0; j <= sides; ++j) {
            float a = (float)j / (float)sides * pi2;
            glm::vec3 localDir = childRight * std::cos(a) + fwd * std::sin(a);
            glm::vec3 p = childBase + localDir * R;      // 枝条截面上的点(外扩)

            // 逐顶点投影到父级圆柱表面(→自然鞍形), 上/下侧按 spread 沿轴延展成蹼
            glm::vec3 v        = p - parentC;
            float     axialRaw = glm::dot(v, A);
            float     s        = glm::dot(localDir, A);  // 上(+)/下(-)侧因子[-1,1]
            float     axial    = axialRaw + (s >= 0.0f ? upperSpread : lowerSpread) * s;
            glm::vec3 radial   = v - A * axialRaw;
            float     rl       = glm::length(radial);
            glm::vec3 rdir     = (rl > 1e-5f) ? radial / rl : localDir;
            // 落点半径按 collarSink 向树干内收，收陷幅度随 su(距圆心距离,0中心→1外圈)加大，
            // 使外圈更深地陷入树干实体、被树皮遮住，消除外圈悬空缝隙。
            float landR = landingAt(axial) * (1.0f - collarSink * su);
            glm::vec3 onTrunk  = parentC + A * axial + rdir * landR;

            glm::vec3 pos    = glm::mix(p, onTrunk, proj);          // 顶端贴枝条, 底端落父级
            glm::vec3 normal = glm::normalize(glm::mix(localDir, rdir, proj));
            float vCoord = -glm::length(pos - (childBase + localDir * startR)) * vPerUnit;
            float uCoord = (float)j / (float)sides * uTiling;
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, normal.x,normal.y,normal.z, uCoord, vCoord});
        }
    }
    // k=0 圈(枝条基部) → k=weldSegs 圈(父级表面), 环环相连成焊接带
    for (int k = 0; k < weldSegs; ++k) {
        for (int j = 0; j < sides; ++j) {
            uint32_t a0 = base + k*rv + j,     a1 = base + k*rv + j + 1;
            uint32_t b0 = base + (k+1)*rv + j, b1 = base + (k+1)*rv + j + 1;
            // k 圈→k+1 圈是"逆生长方向"(向父级投影)前进，故绕序相对
            // appendCylinder 翻转，使外表面朝外(否则正面被剔除→看到背面)
            batch.indices.insert(batch.indices.end(), {a0,b0,a1, a1,b0,b1});
        }
    }
}

// ---- 主入口 ----
TreeMeshData TreeGenerator::generate(NodeGraph& graph) {
    TreeMeshData data;
    m_out = &data;
    // 一个工程内可有多棵植被：每个"无输入连线的 Trunk"都是一株独立植物，
    // 各自按自身 posX/posZ 摆放到场景中。
    for (const auto& [id, node] : graph.nodes()) {
        if (node->getType() != NodeType::Trunk) continue;
        // 只处理根 Trunk（输入未连接的），作为独立植株标志
        bool hasInput = false;
        for (const auto& pin : node->inputPins)
            if (graph.linkFromPin(pin.id) != INVALID_LINK) { hasInput = true; break; }
        if (hasInput) continue;

        const auto& tp = static_cast<const TrunkNode*>(node.get())->params;
        glm::vec3 base = {tp.posX, 0.0f, tp.posZ};
        processNode(graph, node.get(), nullptr, base, {0,1,0}, 1.0f, 0);
    }
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
    case NodeType::Spine:
        if (parentRings && !parentRings->empty())
            buildSpine(static_cast<const SpineNode*>(node),
                       graph, *parentRings, parentLen, depth);
        break;
    case NodeType::Frond:
        buildFrond(static_cast<const FrondNode*>(node), parentRings, origin, dir);
        break;
    }
}

// ---- Trunk ----
std::vector<BranchRing> TreeGenerator::buildTrunk(
    const TrunkNode* node, glm::vec3 origin, glm::vec3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed);

    // 主干锥度从 startRadius 起(不再把 baseFlare 乘进整条锥度)，
    // 树盘(根盘)改为在基部局部外扩、沿 flareHeight 高度用 smoothstep 平滑收敛，
    // 避免过去"整段变粗 + 底部硬折"的不自然过渡。
    auto rings = CylinderSegment::buildNaturalRings(
        origin, dir, p.length,
        p.startRadius, p.endRadius,
        p.lengthSegs, p.noiseAmount, p.noiseFreq,
        p.gnarl, p.taperPow, 0.0f, rng,
        p.jointCount, p.jointBulge);

    float flareExtra = p.startRadius * (p.baseFlare - 1.0f);
    const float fh = 0.18f;  // 树盘(根盘)过渡高度: 基部外扩沿长度平滑收敛的比例(固定)
    if (flareExtra > 0.0f && p.lengthSegs > 0) {
        for (size_t i = 0; i < rings.size(); ++i) {
            float t = (float)i / (float)p.lengthSegs;
            if (t >= fh) break;
            float u = t / fh;                       // 0(基部)→1(过渡顶)
            float w = 1.0f - (3.0f*u*u - 2.0f*u*u*u); // 1-smoothstep: 两端导数为0→无折痕
            rings[i].radius += flareExtra * w;
        }
    }

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
            p.gnarl, p.taperPow, p.gravity, rng,
            p.jointCount, p.jointBulge);

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

        // 以树干轴为参考，先抬到 spreadAngle 再绕轴旋方位(spreadAngle>90 即朝下俯冲入地)
        glm::vec3 rootDir = rotateAroundAxis(baseDir, baseRight, el);
        rootDir = rotateAroundAxis(rootDir, baseDir, az);
        rootDir = glm::normalize(rootDir);

        float thisLen = p.length * jitterLen(rng);
        float startR  = baseRadius * p.radiusScale;
        float endR    = startR * p.endRatio;

        // 从树干“表面”发出，而非从轴心穿出；再沿径向回沉一小段嵌入树干实体，
        // 使根基与树干实体互相重叠(而非仅贴表面)，从根本上消除近水平粗根的接缝空隙。
        // collar 仍从此嵌入点向外平滑过渡，只影响 Roots，不改动 Branch。
        glm::vec3 radial = rootDir - baseDir * glm::dot(rootDir, baseDir);
        glm::vec3 surfacePos = basePos;
        if (glm::length(radial) > 1e-4f) {
            glm::vec3 rdir = glm::normalize(radial);
            float sink = std::min(startR * 0.6f, baseRadius * 0.5f);
            surfacePos = basePos + rdir * (baseRadius - sink);
        }

        // gravity 作为“重力”参数传入，使根沿长度持续向下弯扎入地；节参数用于周期性膨大
        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, rootDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, p.gravity, rng,
            p.jointCount, p.jointBulge);

        appendCylinder(batch, rings, p.sides, p.uvTiling);
        // 根领：接壤处裙边贴合树干表面(传入树干rings→落点半径随树干高度真实起伏)
        if (!rings.empty())
            appendCollar(batch, basePos, baseDir, baseRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTiling, thisLen,
                         &parentRings, p.collarSink);
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
        float     sizeT = 0.5f;   // 沿叶轴位置(0=基部,1=梢部)，用于 sizeFalloff
        if (haveRings) {
            float t = (p.leafCount > 1) ? (float)i / (float)(p.leafCount - 1) : 0.5f;
            t = glm::clamp(t + (rot01(rng) - 0.5f) / (float)p.leafCount, 0.0f, 1.0f);
            sizeT = t;
            sampleRings(*parentRings, t, axisPos, axisDir, axisRight, axisRadius);
        } else {
            sizeT = (p.leafCount > 1) ? (float)i / (float)(p.leafCount - 1) : 0.5f;
            axisPos = origin;
            axisDir = glm::normalize(dir);
            axisRight = perpendicular(axisDir);
        }
        axisDir = glm::normalize(axisDir);
        glm::vec3 fwd = glm::normalize(glm::cross(axisDir, axisRight));

        // 外扩方向：平面模式=小叶交替伸向叶轴两侧(蕨类)，否则黄金角3D辐射
        glm::vec3 outDir;
        if (p.planar) {
            float side = (i % 2 == 0) ? 1.0f : -1.0f;
            float wob  = (rot01(rng) - 0.5f) * 0.25f;  // 轻微抖动避免完全规则
            outDir = glm::normalize(axisRight * side + fwd * wob);
        } else {
            float az = goldenAngle * (float)i + (rot01(rng) - 0.5f) * 0.7f;
            outDir = glm::normalize(axisRight * std::cos(az) + fwd * std::sin(az));
        }

        // 叶片生长方向：以垂直外扩为主，略向枝梢方向前倾 + 抖动
        glm::vec3 leafUp = glm::normalize(outDir + axisDir * (0.25f + jitter(rng)));

        // 叶基贴在枝条表面，叶片向外伸展。sizeFalloff 令小叶向梢部渐小(蕨类)
        glm::vec3 basePos = axisPos + outDir * axisRadius;
        float sizeScale = 1.0f - p.sizeFalloff * sizeT;
        float hs = p.leafSize * 0.5f * sizeScale;
        float hw = hs * p.leafAspect;   // 宽高比: 小值=细长叶(竹叶/蕨类小叶)
        glm::vec3 pos = basePos + leafUp * ((hs + p.clusterRadius * 0.4f) * radJit(rng));

        // 叶面朝向：平面模式统一朝叶轴平面法线(共面平铺)，否则随机旋转
        glm::vec3 leafRight, leafFwd;
        if (p.planar) {
            leafFwd   = fwd;
            leafRight = glm::normalize(glm::cross(leafUp, leafFwd));
            leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));
        } else {
            glm::vec3 tmp = (std::abs(leafUp.y) < 0.95f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
            leafRight = glm::normalize(glm::cross(leafUp, tmp));
            leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));
            float rot = rot01(rng) * pi2;
            leafRight = glm::normalize(leafRight * std::cos(rot) + leafFwd * std::sin(rot));
            leafFwd   = glm::normalize(glm::cross(leafRight, leafUp));
        }

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

// ---- Spine ----
// 蕨叶叶轴：像细枝(Twig)一样从父级 rings 附着点发出一条受 gravity 下垂、noise/gnarl
// 扰动的弯曲中心线，渲染成细茎，并把这条 rings 传给 Frond 子节点沿其铺连续叶带。
void TreeGenerator::buildSpine(
    const SpineNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    float spineLen = parentLen * p.lengthRatio;
    std::mt19937 rng(p.seed + depth * 61);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    std::uniform_real_distribution<float> jitterLen(0.85f, 1.15f);

    float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
    float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
    if (re < rs) std::swap(rs, re);

    for (int i = 0; i < p.spineCount; ++i) {
        float attachT = (p.spineCount > 1)
            ? glm::mix(rs, re, (float)i / (float)(p.spineCount - 1))
            : glm::mix(rs, re, 0.5f);

        glm::vec3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, attachT, attachPos, attachDir, attachRight, attachRadius);

        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + jitter(rng);
        glm::vec3 spineDir = rotateAroundAxis(attachDir, attachRight, el);
        spineDir = rotateAroundAxis(spineDir, attachDir, az);

        float thisLen = spineLen * jitterLen(rng);
        float startR  = attachRadius * p.radiusScale;
        float endR    = startR * p.endRatio;

        // 从父级表面发出
        glm::vec3 radial = spineDir - attachDir * glm::dot(spineDir, attachDir);
        glm::vec3 surfacePos = (glm::length(radial) > 1e-4f)
            ? attachPos + glm::normalize(radial) * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, spineDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, p.gravity, rng);

        auto& batch = getBatch(p.material, false);
        appendCylinder(batch, rings, p.sides, p.uvTiling);

        // Frond 子节点沿整条叶轴 rings 铺叶带
        glm::vec3 tip    = rings.empty() ? surfacePos + spineDir * thisLen : rings.back().center;
        glm::vec3 tipDir = rings.empty() ? spineDir : rings.back().up;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
    }
}

// ---- Frond ----
// 沿父级(Spine)rings 生成一条连续带状蕨叶：每个 ring 处沿其 right 轴向左右外扩，
// 半宽按叶形轮廓(基部窄→中部最宽→梢部收尖)变化，curl 令叶面沿 up 方向卷曲。
// 与 LeafCluster 的离散小卡片不同：Frond 是"沿曲线拉伸的连续叶片网格"。
void TreeGenerator::buildFrond(
    const FrondNode* node,
    const std::vector<BranchRing>* parentRings,
    glm::vec3 origin, glm::vec3 dir)
{
    if (!parentRings || parentRings->size() < 2) return;
    const auto& p = node->params;
    const auto& rings = *parentRings;
    auto& batch = getBatch(p.material, true);
    glm::vec3 col = p.material.albedo;

    int nSeg = (int)rings.size() - 1;         // 沿脊线段数
    int cols = std::max(1, p.segsPerSide);     // 每侧横向细分
    int totalCols = cols * 2 + 1;              // 左...中...右 顶点列数
    float pi = glm::pi<float>();

    // 半宽轮廓: t=0 用 widthBase, t=1 用 widthTip, 中部按 profilePow 抬到最大(=width)
    auto halfWidthAt = [&](float t) -> float {
        float base = glm::mix(p.widthBase, p.widthTip, t);   // 端点线性过渡
        float bump = std::pow(std::sin(pi * std::clamp(t,0.0f,1.0f)),
                              std::max(0.05f, p.profilePow)); // 中部隆起
        return p.width * (base + (1.0f - base) * bump);
    };

    // 逐 ring 生成一排横向顶点(共 rings.size() 行 × totalCols 列)
    std::vector<uint32_t> rowBase(rings.size());
    for (size_t ri = 0; ri < rings.size(); ++ri) {
        const BranchRing& r = rings[ri];
        float t  = (rings.size() > 1) ? (float)ri / (float)(rings.size()-1) : 0.0f;
        float hw = halfWidthAt(t);
        glm::vec3 rightAxis = glm::normalize(r.right);
        glm::vec3 upAxis    = glm::normalize(r.up);
        // 叶面法线 ≈ 脊线 up × right (垂直于叶带平面)
        glm::vec3 faceN = glm::normalize(glm::cross(rightAxis, upAxis));
        float vCoord = t;

        rowBase[ri] = (uint32_t)(batch.vertices.size() / 11);
        for (int c = 0; c < totalCols; ++c) {
            float lateral = ((float)c / (float)(totalCols-1)) * 2.0f - 1.0f; // [-1,1]
            float x = lateral * hw;
            // curl: 叶面沿两侧向 up 方向卷起(|lateral|^2 越靠边卷得越多)
            float lift = p.curl * hw * lateral * lateral;
            // 锯齿: 叶缘按脊线位置周期性内缩，形成羽状裂片
            float edge = 1.0f;
            if (p.serrate && cols >= 1 && std::abs(std::abs(lateral)-1.0f) < 1e-3f)
                edge = 1.0f - p.serrateDepth * (0.5f + 0.5f*std::sin((float)ri*2.3f));
            glm::vec3 pos = r.center + rightAxis * (x * edge) + faceN * lift;
            float uCoord = (float)c / (float)(totalCols-1);
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, faceN.x,faceN.y,faceN.z, uCoord, vCoord,
                 col.r,col.g,col.b});
        }
    }

    // 相邻两行之间铺四边形(双面: 叶片两面都可见, 各生成一组三角形)
    for (int ri = 0; ri < nSeg; ++ri) {
        uint32_t b0 = rowBase[ri];
        uint32_t b1 = rowBase[ri+1];
        for (int c = 0; c < totalCols-1; ++c) {
            uint32_t v00 = b0+c,   v01 = b0+c+1;
            uint32_t v10 = b1+c,   v11 = b1+c+1;
            batch.indices.insert(batch.indices.end(),
                {v00,v01,v11, v00,v11,v10});   // 正面
            batch.indices.insert(batch.indices.end(),
                {v00,v11,v01, v00,v10,v11});   // 背面
        }
    }
}
