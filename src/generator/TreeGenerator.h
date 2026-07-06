#pragma once
#include "graph/NodeGraph.h"
#include "graph/Nodes.h"
#include "renderer/Renderer.h"
#include "CylinderSegment.h"
#include <glm/glm.hpp>
#include <random>

class TreeGenerator {
public:
    TreeMeshData generate(NodeGraph& graph);

private:
    TreeMeshData* m_out = nullptr;

    MeshBatch& getBatch(const MaterialParams& mat, bool isLeaf);

    // parentRings: 父节点的环列，Branch/Twig 从中取附着点
    void processNode(
        const NodeGraph& graph,
        const TreeNode*  node,
        const std::vector<BranchRing>* parentRings,  // 父节点rings（nullptr=根）
        glm::vec3        origin,     // 父节点起点（用于无rings时降级）
        glm::vec3        dir,        // 父节点初始方向（降级用）
        float            parentLen,
        int              depth);

    // 返回rings供子节点使用
    std::vector<BranchRing> buildTrunk(const TrunkNode* node,
        glm::vec3 origin, glm::vec3 dir);

    void buildBranches(const BranchNode* node, const NodeGraph& graph,
        const std::vector<BranchRing>& parentRings,
        float parentLen, int depth);

    void buildTwig(const TwigNode* node, const NodeGraph& graph,
        const std::vector<BranchRing>& parentRings,
        float parentLen, int depth);

    void buildLeafCluster(const LeafClusterNode* node,
        glm::vec3 origin, glm::vec3 dir);

    // 从rings按比例t(0-1)插值出附着点、切线方向、right轴、半径
    static void sampleRings(const std::vector<BranchRing>& rings, float t,
        glm::vec3& outPos, glm::vec3& outDir, glm::vec3& outRight,
        float& outRadius);

    static glm::vec3 rotateAroundAxis(glm::vec3 v, glm::vec3 axis, float angleDeg);
    static glm::vec3 perpendicular(glm::vec3 dir);

    void appendCylinder(MeshBatch& batch,
                        const std::vector<BranchRing>& rings, int sides,
                        float uvTiling = 1.0f);

    // 生成"枝领"裙边：把子枝基部一圈外沿顶点沿径向投影到父级圆柱表面，
    // 与子枝第一圈组成一段贴合父级表面的过渡带（消除穿模）。
    // parentC/parentA/parentR: 附着处父级圆柱的中心、轴向、半径
    void appendCollar(MeshBatch& batch,
                      glm::vec3 parentC, glm::vec3 parentA, float parentR,
                      glm::vec3 childBase, glm::vec3 childDir, glm::vec3 childRight,
                      float startR, float baseFlare, int sides,
                      float uvTiling, float branchTotalLen);
};
