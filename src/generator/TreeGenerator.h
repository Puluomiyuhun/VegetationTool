#pragma once
#include "graph/NodeGraph.h"
#include "graph/Nodes.h"
#include "renderer/Renderer.h"
#include "CylinderSegment.h"
#include <glm/glm.hpp>
#include <random>
#include <set>

class TreeGenerator {
public:
    // hlNode: 需要在视口高亮的节点(该节点及其子树的几何会被镜像到 hlVerts/hlIdx)
    TreeMeshData generate(NodeGraph& graph, NodeId hlNode = INVALID_NODE);

    // 只生成 trunkId 这一株 Trunk 的整棵树网格(供 Export 节点导出单株模型使用)。
    TreeMeshData generateSubtree(NodeGraph& graph, NodeId trunkId);

    // 生成 rootId 节点及其全部下游子枝叶组成的"竖直标本": 该节点作为一根主枝
    // 从原点(0,0,0)沿 +Y 挺直生长(仅一根实例、无重力偏转、无枝领), 子节点相对其
    // 正常生长。供 UE5 PCG 程序化种树使用的枝叶标本批量导出。
    // seedOffset: 叠加到所有随机种子上, 用同一节点生成互不相同的形态变体。
    TreeMeshData generateSpecimen(NodeGraph& graph, NodeId rootId, int seedOffset = 0);

    // 生成 nodeId 及其上游祖先链(从根 Trunk 一路到该节点): 只生成这条链上的节点,
    // 每个多实例节点只长一根(链上代表实例), 不含旁支/叶。保持场景原始位姿。
    // 供"当前节点及上游"导出使用(如从 Trunk 导出只得一根干)。
    TreeMeshData generateChain(NodeGraph& graph, NodeId nodeId);

private:
    TreeMeshData* m_out = nullptr;
    NodeId        m_hlNode = INVALID_NODE;   // 被选中的高亮节点
    bool          m_hlCapture = false;       // 当前是否正在生成被选中节点"自身"的几何
    NodeId        m_curNode = INVALID_NODE;  // 当前正在生成几何的节点(供拾取三角标记归属)
    // 标本模式: 生成 m_specimenRoot 时把它当作竖直挺立的主枝(原点+Y, 单实例, 无重力/枝领)。
    NodeId        m_specimenRoot = INVALID_NODE;
    // 叠加到所有随机种子上的偏移(标本变体用), 非标本导出时为 0 不影响正常生成。
    int           m_specimenSeedOffset = 0;
    // 标本参考父级尺寸(方案B): 由 measureSpecimenParent 在真实整株里测得, 令标本的
    // 粗细/长细比与编辑器所见一致; 测不到时保留默认回退值。
    float         m_specParentLen    = 4.0f;
    float         m_specParentRadius = 0.3f;
    // 测量遍历: 命中该节点首个实例时记录其 parentLen/attachRadius, 命中后置位不再重复。
    NodeId        m_measureTarget = INVALID_NODE;
    bool          m_measureDone   = false;
    // 祖先链模式(导出"当前及上游"): 只生成 m_chainNodes 内的节点, 且各多实例节点只长一根。
    bool          m_chainMode  = false;
    std::set<NodeId> m_chainNodes;
    // 顶点风力烘焙: 当前节点的风力基权重与相位, 由 processNode 按节点类型/id 设置。
    float         m_windW = 0.0f;     // 该节点枝条风力基权重(尖端处再乘 tRing)
    float         m_windPhase = 0.0f; // 该节点相位偏移(按节点 id 哈希, 令相邻枝条不同步)

    MeshBatch& getBatch(const MaterialParams& mat, bool isLeaf);

    // 方案B 标本测量: 在真实整株里跑一遍, 捕获 targetId 首个实例的父级长度/附着半径,
    // 结果写入 m_specParentLen / m_specParentRadius。
    void measureSpecimenParent(NodeGraph& graph, NodeId targetId);
    // 每次向 batch 追加几何后调用: 把 [iFrom,end) 的三角登记到拾取表(附带 m_curNode);
    // 若 m_hlCapture 为真, 再把 [vFrom,end) 顶点(pos+normal)镜像到高亮描边缓冲。
    void afterAppend(const MeshBatch& batch, size_t vFrom, size_t iFrom);

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

    void buildRoots(const RootsNode* node,
        const std::vector<BranchRing>& parentRings);

    void buildLeafCluster(const LeafClusterNode* node,
        const std::vector<BranchRing>* parentRings,
        glm::vec3 origin, glm::vec3 dir);

    // Spine：像细枝一样生成一条弯曲叶轴中心线，渲染成细茎并把 rings 传给 Frond 子节点
    void buildSpine(const SpineNode* node, const NodeGraph& graph,
        const std::vector<BranchRing>& parentRings,
        float parentLen, int depth);

    // Frond：沿父级(Spine)rings 铺一条连续带状蕨叶网格，宽度按叶形轮廓变化
    void buildFrond(const FrondNode* node,
        const std::vector<BranchRing>* parentRings,
        glm::vec3 origin, glm::vec3 dir);

    // 从rings按比例t(0-1)插值出附着点、切线方向、right轴、半径
    static void sampleRings(const std::vector<BranchRing>& rings, float t,
        glm::vec3& outPos, glm::vec3& outDir, glm::vec3& outRight,
        float& outRadius);

    static glm::vec3 rotateAroundAxis(glm::vec3 v, glm::vec3 axis, float angleDeg);
    static glm::vec3 perpendicular(glm::vec3 dir);

    void appendCylinder(MeshBatch& batch,
                        const std::vector<BranchRing>& rings, int sides,
                        float uvTilingU = 1.0f, float uvTilingV = 1.0f);

    // 生成"枝领"裙边：把子枝基部一圈外沿顶点沿径向投影到父级圆柱表面，
    // 与子枝第一圈组成一段贴合父级表面的过渡带（消除穿模）。
    // parentC/parentA/parentR: 附着处父级圆柱的中心、轴向、半径
    void appendCollar(MeshBatch& batch,
                      glm::vec3 parentC, glm::vec3 parentA, float parentR,
                      glm::vec3 childBase, glm::vec3 childDir, glm::vec3 childRight,
                      float startR, float baseFlare, int sides,
                      float uvTilingU, float uvTilingV, float branchTotalLen,
                      const std::vector<BranchRing>* trunkRings = nullptr,
                      float collarSink = 0.0f);
};
