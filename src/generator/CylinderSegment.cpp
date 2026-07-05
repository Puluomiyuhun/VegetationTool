#include "CylinderSegment.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// ---- 私有工具 ----
glm::vec3 CylinderSegment::bezier2(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float t) {
    float u = 1.0f - t;
    return u*u*p0 + 2*u*t*p1 + t*t*p2;
}

glm::vec3 CylinderSegment::bezierTangent2(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float t) {
    float u = 1.0f - t;
    return 2*u*(p1-p0) + 2*t*(p2-p1);
}

glm::vec3 CylinderSegment::perpendicular(glm::vec3 dir) {
    glm::vec3 ref = (std::abs(dir.y) < 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    return glm::normalize(glm::cross(ref, dir));
}

// Parallel Transport Frame：将right从prevUp旋转到newUp
glm::vec3 CylinderSegment::ptfTransport(glm::vec3 right, glm::vec3 prevUp, glm::vec3 newUp) {
    glm::vec3 rotAxis = glm::cross(prevUp, newUp);
    float sinA = glm::length(rotAxis);
    if (sinA < 1e-5f) return right;
    rotAxis /= sinA;
    float cosA = glm::dot(prevUp, newUp);
    return glm::normalize(
        right * cosA
        + glm::cross(rotAxis, right) * sinA
        + rotAxis * glm::dot(rotAxis, right) * (1.0f - cosA)
    );
}

// ---- 平滑贝塞尔曲线 ----
std::vector<BranchRing> CylinderSegment::buildCurvedRings(
    glm::vec3 p0, glm::vec3 p1, glm::vec3 p2,
    float radiusStart, float radiusEnd,
    int lengthSegs)
{
    std::vector<BranchRing> rings;
    rings.reserve(lengthSegs + 1);

    glm::vec3 initTangent = glm::normalize(bezierTangent2(p0,p1,p2, 0.0001f));
    glm::vec3 right = perpendicular(initTangent);

    for (int i = 0; i <= lengthSegs; ++i) {
        float t = (float)i / (float)lengthSegs;
        BranchRing ring;
        ring.center = bezier2(p0, p1, p2, t);
        ring.radius = glm::mix(radiusStart, radiusEnd, t);
        ring.up     = glm::normalize(bezierTangent2(p0, p1, p2,
                          std::clamp(t, 0.0001f, 0.9999f)));
        if (i > 0)
            right = ptfTransport(right, rings.back().up, ring.up);
        ring.right = right;
        rings.push_back(ring);
    }
    return rings;
}

// ---- 折段弯曲（苍劲老树风格）----
// 将枝干分成 bendCount+1 段，每段末端做一次硬折弯，
// 折弯方向随机（偏向侧面/下方），产生苍劲的折角效果。
std::vector<BranchRing> CylinderSegment::buildKinkedRings(
    glm::vec3 origin, glm::vec3 dir, float length,
    float radiusStart, float radiusEnd,
    int lengthSegs, int bendCount, float bendAngle,
    std::mt19937& rng)
{
    if (bendCount <= 0) {
        // 无折弯：直线
        glm::vec3 right = perpendicular(dir);
        std::vector<BranchRing> rings;
        for (int i = 0; i <= lengthSegs; ++i) {
            float t = (float)i / (float)lengthSegs;
            rings.push_back({origin + dir * (length * t),
                             glm::mix(radiusStart, radiusEnd, t),
                             dir, right});
        }
        return rings;
    }

    std::uniform_real_distribution<float> angleDist(bendAngle * 0.5f, bendAngle);
    std::uniform_real_distribution<float> axDist(-1.0f, 1.0f);

    // 每折弯段的环数
    int totalSeg   = bendCount + 1;
    int segsPerSeg = std::max(1, lengthSegs / totalSeg);
    float segLen   = length / (float)totalSeg;

    std::vector<BranchRing> rings;
    glm::vec3 curOrigin = origin;
    glm::vec3 curDir    = glm::normalize(dir);
    glm::vec3 curRight  = perpendicular(curDir);
    float accumLen = 0.0f;

    for (int k = 0; k < totalSeg; ++k) {
        bool isLast = (k == totalSeg - 1);
        // 最后一段用剩余环数
        int segs = isLast ? (lengthSegs - k * segsPerSeg) : segsPerSeg;
        if (segs <= 0) segs = 1;

        for (int i = (k == 0 ? 0 : 1); i <= segs; ++i) {
            float frac = (float)i / (float)segs;
            float t = std::clamp((accumLen + segLen * frac) / length, 0.0f, 1.0f);
            BranchRing ring;
            ring.center = curOrigin + curDir * (segLen * frac);
            ring.radius = glm::mix(radiusStart, radiusEnd, t);
            ring.up     = curDir;
            ring.right  = curRight;
            rings.push_back(ring);
        }
        accumLen += segLen;
        curOrigin = rings.back().center;

        if (!isLast) {
            // 折弯：随机侧向轴，带轻微下垂偏向
            glm::vec3 sideAxis = glm::normalize(
                glm::vec3(axDist(rng), 0.0f, axDist(rng)));
            // 混入少量重力方向（让枝干向下弯）
            glm::vec3 bendAxis = glm::normalize(sideAxis + glm::vec3(0,-0.3f,0));
            // 保证折弯轴与枝干方向不平行
            if (std::abs(glm::dot(bendAxis, curDir)) > 0.98f)
                bendAxis = perpendicular(curDir);

            float angle = angleDist(rng);
            float rad = glm::radians(angle);
            float c = std::cos(rad), s = std::sin(rad);
            glm::vec3 newDir = curDir * c
                             + glm::cross(bendAxis, curDir) * s
                             + bendAxis * glm::dot(bendAxis, curDir) * (1.0f - c);
            newDir = glm::normalize(newDir);
            curRight = ptfTransport(curRight, curDir, newDir);
            curDir = newDir;
        }
    }
    return rings;
}

// ---- buildCap ----
void CylinderSegment::buildCap(
    const BranchRing& bottom, const BranchRing& top,
    int sides,
    std::vector<float>&    outVerts,
    std::vector<uint32_t>& outIdx,
    uint32_t               baseIdx)
{
    float pi2 = glm::two_pi<float>();
    int   vBase = (int)outVerts.size() / 6;

    auto pushVert = [&](glm::vec3 pos, glm::vec3 normal) {
        outVerts.push_back(pos.x); outVerts.push_back(pos.y); outVerts.push_back(pos.z);
        outVerts.push_back(normal.x); outVerts.push_back(normal.y); outVerts.push_back(normal.z);
    };

    for (int ring = 0; ring < 2; ++ring) {
        const BranchRing& r = (ring == 0) ? bottom : top;
        for (int j = 0; j <= sides; ++j) {
            float angle = (float)j / (float)sides * pi2;
            glm::vec3 localDir = r.right * std::cos(angle)
                               + glm::cross(r.up, r.right) * std::sin(angle);
            glm::vec3 pos    = r.center + localDir * r.radius;
            glm::vec3 normal = glm::normalize(localDir);
            pushVert(pos, normal);
        }
    }

    int ringVerts = sides + 1;
    for (int j = 0; j < sides; ++j) {
        uint32_t b0 = baseIdx + vBase + j;
        uint32_t b1 = baseIdx + vBase + j + 1;
        uint32_t t0 = baseIdx + vBase + ringVerts + j;
        uint32_t t1 = baseIdx + vBase + ringVerts + j + 1;
        outIdx.insert(outIdx.end(), {b0, b1, t0, b1, t1, t0});
    }
}

// ---- build ----
void CylinderSegment::build(
    const std::vector<BranchRing>& rings,
    int sides,
    std::vector<float>&    outVerts,
    std::vector<uint32_t>& outIdx)
{
    if (rings.size() < 2) return;
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        buildCap(rings[i], rings[i+1], sides, outVerts, outIdx, 0);
    }
}
