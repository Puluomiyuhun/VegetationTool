#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "graph/NodeTypes.h"   // MaterialParams

// FBX 导入(基于 ufbx): 把 SpeedTree 导出的 FBX 读成 SlowTree 可用的网格 + 骨架。
// - 枝干: 通常带骨骼/蒙皮, 用于 skeletalMesh USD + 沿骨骼散布叶片。
// - 枝叶单体: 通常无骨骼的一小片叶网格, 作为散布原型。
//
// 坐标: 保持 FBX 原样(经 ufbx 归一到米 + Y-up), 顶点烘到世界空间(geometry_to_world),
// 使叶散布与枝干显示同一坐标系。

// 一根骨骼(rest/bind 位姿)。parent=-1 为根。
struct ImportedBone {
    std::string name;
    int         parent = -1;
    glm::vec3   restPos{0.0f};   // 世界空间静止位置(骨骼节点原点)
    glm::vec3   restDir{0,1,0};  // 指向首个子骨骼的方向(无子=从父指向自身)
    float       length = 0.0f;   // 到首个子骨骼的距离(无子=0)
    int         segment = -1;    // SpeedTree 段号(骨骼名 Bone_N_...); -1=无段号
    bool        leafTwig = false;// 是否为"叶段末端"(该段无子段, 且此骨为该段 End 节点)
    float       radius = 0.0f;   // 该骨处枝干截面半径(由蒙皮顶点到骨轴距离反算, 世界米)
};

struct ImportedMesh {
    // 三角化后逐角(per-corner)顶点, 世界空间, Y-up 米制
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t>  indices;

    // 每顶点最多 4 骨骼影响(供 skeletalMesh 导出与蒙皮参考)。无骨骼时为空。
    std::vector<glm::ivec4> boneIdx;   // 骨骼索引, 空槽 = -1
    std::vector<glm::vec4>  boneWt;    // 权重(已归一)

    std::vector<ImportedBone> bones;   // 骨架(空 = 无骨骼)
    MaterialParams material;           // 贴图路径 / albedo(取首个材质)

    // 按材质拆分的子网格(part): 每个 part 引用 indices 的一段连续区间, 携带该材质。
    // 用于"实例枝含枝干+叶子两个材质"的场景: 让 Scatter 把某个 part 判为枝干(复用父级
    // trunk 材质)、其余为叶子(用自带材质)。至少 1 个 part; 单材质时 parts.size()==1。
    struct SubMesh {
        uint32_t       indexOffset = 0;   // 在 indices 中的起始位置
        uint32_t       indexCount  = 0;   // 该 part 的索引数(三角形数×3)
        MaterialParams material;          // 该子网格材质(贴图/albedo)
        std::string    materialName;      // 材质名(供 UI 显示区分)
    };
    std::vector<SubMesh> parts;

    bool        ok = false;
    std::string error;

    bool hasSkeleton() const { return !bones.empty(); }
    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }
    bool hasLeafTwigs() const {   // 骨架中是否有识别出的叶段末端
        for (const auto& b : bones) if (b.leafTwig) return true; return false;
    }
};

namespace MeshImport {
    // 从 FBX 文件加载。合并文件中所有网格到一个 ImportedMesh。失败时 ok=false 且 error 有说明。
    ImportedMesh loadFBX(const std::string& path);
}
