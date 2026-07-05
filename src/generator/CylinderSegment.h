#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cstdint>
#include <random>

// 圆柱段的一个截面环（改名避免与imgui内部Ring冲突）
struct BranchRing {
    glm::vec3 center;
    float     radius;
    glm::vec3 up;
    glm::vec3 right;
};

class CylinderSegment {
public:
    // 平滑贝塞尔曲线环列
    static std::vector<BranchRing> buildCurvedRings(
        glm::vec3 p0, glm::vec3 p1, glm::vec3 p2,
        float radiusStart, float radiusEnd,
        int lengthSegs);

    // 折段弯曲环列（苍劲老树风格）
    // bendCount: 折弯段数（0=直线）
    // bendAngle: 每段最大折角（度），越大越苍劲
    static std::vector<BranchRing> buildKinkedRings(
        glm::vec3 origin, glm::vec3 dir, float length,
        float radiusStart, float radiusEnd,
        int lengthSegs, int bendCount, float bendAngle,
        std::mt19937& rng);

    static void buildCap(
        const BranchRing& bottom, const BranchRing& top,
        int sides,
        std::vector<float>&    outVerts,
        std::vector<uint32_t>& outIdx,
        uint32_t               baseIdx);

    static void build(
        const std::vector<BranchRing>& rings,
        int sides,
        std::vector<float>&    outVerts,
        std::vector<uint32_t>& outIdx);

private:
    static glm::vec3 bezier2(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float t);
    static glm::vec3 bezierTangent2(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, float t);
    static glm::vec3 perpendicular(glm::vec3 dir);
    static glm::vec3 ptfTransport(glm::vec3 right, glm::vec3 prevUp, glm::vec3 newUp);
};
