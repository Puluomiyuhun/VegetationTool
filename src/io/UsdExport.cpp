// USD (.usda) 导出: 把 SlowTree 生成的树网格写成 UE 可直接导入的 Nanite Assembly。
//
// 目标结构(staticMesh Nanite Assembly, 见 docs/UE_NANITE_ASSEMBLY_USD_DESIGN.md §2):
//   /Root                       (Xform, NaniteAssemblyRootAPI, meshType="staticMesh")
//     ├─ TrunkMesh              (Mesh)          ← 所有枝干批次合并的 base 几何(一个顶点不动)
//     └─ Leaf_<i>_Instancer     (PointInstancer)← 每个叶批次一个: 单片叶原型 + 每叶 transform
//          └─ Protos            (Scope)
//               └─ Proto        (Mesh)          ← 该批次的单片叶原型(内嵌几何)
//
// 内存关键: 叶批次里成百上千片叶卡在引擎里只存 1 份原型网格, 其余是 (position,
// orientation, scale) 实例 —— 这正是用户要的"枝叶实例化省内存"。
//
// 坐标: SlowTree 为 Y-up 米制, USD 亦 Y-up 米制(upAxis="Y", metersPerUnit=1),
// 交给 UE 的 USD 导入器统一转 Z-up/厘米, 不在此手动换算(见设计文档 §2.3)。
//
// 叶片提取假设: 每片叶 = 4 顶点四边形(LeafCluster/Frond 默认路径)。四边形的
// center/right/up/半宽半高可从烘焙顶点精确反解, 得到实例 transform, 原型为单位四边形。
// 非四边形(如 cutout 轮廓网格)批次无法这样实例化, 退化为把其几何并入 base 网格(仍正确, 只是不省内存)。

#include "ProjectIO.h"
#include "renderer/Renderer.h"    // TreeMeshData / MeshBatch
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <fstream>
#include <sstream>
#include <cctype>
#include <vector>
#include <cmath>

namespace {

// 由正交基(列向量 right/up/normal)构造四元数(w,x,y,z)。
glm::quat quatFromBasis(const glm::vec3& r, const glm::vec3& u, const glm::vec3& n) {
    glm::mat3 m(r, u, n);   // 列即基向量
    return glm::normalize(glm::quat_cast(m));
}

// 一片叶的实例数据
struct LeafInstance {
    glm::vec3 pos;    // 四边形中心(= pivot)
    glm::quat rot;    // 朝向(right,up,normal)
    glm::vec3 scale;  // (半宽, 半高, 1) —— 原型为单位四边形[-1,1]
};

// 尝试把一个叶批次解析成 N 片四边形实例。成功返回 true 并填 out。
// 约定顶点发射顺序(见 buildLeafCluster): 每叶 4 顶点 v0=左下 v1=右下 v2=右上 v3=左上,
// 索引 6 个(两三角)。批次顶点 stride=16, 前 3 为 pos, 3-5 为 normal。
bool parseLeafQuads(const MeshBatch& b, std::vector<LeafInstance>& out) {
    const int stride = 16;
    size_t vCount = b.vertices.size() / stride;
    if (vCount < 4 || vCount % 4 != 0) return false;
    size_t leafN = vCount / 4;
    if (b.indices.size() != leafN * 6) return false;   // 非纯四边形(可能是 cutout)

    auto P = [&](size_t vi) {
        const float* v = &b.vertices[vi * stride];
        return glm::vec3(v[0], v[1], v[2]);
    };
    out.reserve(leafN);
    for (size_t j = 0; j < leafN; ++j) {
        glm::vec3 p0 = P(j*4+0), p1 = P(j*4+1), p2 = P(j*4+2), p3 = P(j*4+3);
        glm::vec3 center = (p0 + p1 + p2 + p3) * 0.25f;
        // right*hw = ((p1-p0)+(p2-p3))/4 ; up*hs = ((p3-p0)+(p2-p1))/4
        glm::vec3 rVec = ((p1 - p0) + (p2 - p3)) * 0.25f;
        glm::vec3 uVec = ((p3 - p0) + (p2 - p1)) * 0.25f;
        float hw = glm::length(rVec), hs = glm::length(uVec);
        if (hw < 1e-6f || hs < 1e-6f) return false;
        glm::vec3 right = rVec / hw;
        glm::vec3 up    = uVec / hs;
        glm::vec3 nrm   = glm::normalize(glm::cross(right, up));
        LeafInstance li;
        li.pos = center;
        li.rot = quatFromBasis(right, up, nrm);
        li.scale = glm::vec3(hw, hs, 1.0f);
        out.push_back(li);
    }
    return true;
}

// 把一个批次的三角网追加到合并的 base 网格缓冲(points/counts/indices/normals/uv)。
void appendToBase(const MeshBatch& b,
                  std::vector<glm::vec3>& pts, std::vector<glm::vec3>& nrms,
                  std::vector<glm::vec2>& uvs, std::vector<int>& faceCounts,
                  std::vector<int>& faceIdx) {
    int stride = b.isLeaf ? 16 : 10;
    size_t vCount = b.vertices.size() / stride;
    int base = (int)pts.size();
    for (size_t i = 0; i < vCount; ++i) {
        const float* v = &b.vertices[i * stride];
        pts.emplace_back(v[0], v[1], v[2]);
        nrms.emplace_back(v[3], v[4], v[5]);
        uvs.emplace_back(v[6], v[7]);
    }
    for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
        faceCounts.push_back(3);
        faceIdx.push_back(base + (int)b.indices[i]);
        faceIdx.push_back(base + (int)b.indices[i+1]);
        faceIdx.push_back(base + (int)b.indices[i+2]);
    }
}

// 写一个 Mesh prim 体(在给定缩进下), 含 points/faceVertexCounts/faceVertexIndices/normals/st/displayColor。
void writeMeshBody(std::ostream& o, const char* ind,
                   const std::vector<glm::vec3>& pts, const std::vector<glm::vec3>& nrms,
                   const std::vector<glm::vec2>& uvs, const std::vector<int>& faceCounts,
                   const std::vector<int>& faceIdx, const glm::vec3& displayCol) {
    o << ind << "point3f[] points = [";
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) o << ", ";
        o << "(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")";
    }
    o << "]\n";

    o << ind << "int[] faceVertexCounts = [";
    for (size_t i = 0; i < faceCounts.size(); ++i) { if (i) o << ", "; o << faceCounts[i]; }
    o << "]\n";

    o << ind << "int[] faceVertexIndices = [";
    for (size_t i = 0; i < faceIdx.size(); ++i) { if (i) o << ", "; o << faceIdx[i]; }
    o << "]\n";

    o << ind << "normal3f[] normals = [";
    for (size_t i = 0; i < nrms.size(); ++i) {
        if (i) o << ", ";
        o << "(" << nrms[i].x << ", " << nrms[i].y << ", " << nrms[i].z << ")";
    }
    o << "] (interpolation = \"vertex\")\n";

    o << ind << "texCoord2f[] primvars:st = [";
    for (size_t i = 0; i < uvs.size(); ++i) {
        if (i) o << ", ";
        o << "(" << uvs[i].x << ", " << uvs[i].y << ")";
    }
    o << "] (interpolation = \"vertex\")\n";

    o << ind << "color3f[] primvars:displayColor = ["
      << "(" << displayCol.x << ", " << displayCol.y << ", " << displayCol.z << ")]"
      << " (interpolation = \"constant\")\n";
}

// USD 标识符净化: 非字母数字下划线的字符替换为下划线, 保证是合法 prim/joint 路径段。
std::string sanitizeName(const std::string& s) {
    std::string r;
    for (char c : s) r += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    if (r.empty() || std::isdigit((unsigned char)r[0])) r = "j_" + r;
    return r;
}

// 写一个 translation-only 的 USD matrix4d(行主序, 平移在第 4 行)。
void writeTransMat(std::ostream& o, const glm::vec3& t) {
    o << "( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), ("
      << t.x << ", " << t.y << ", " << t.z << ", 1) )";
}

} // namespace

namespace ProjectIO {

// 骨骼 Nanite Assembly 导出(meshType="skeletalMesh"): 树干蒙皮 base + 叶片 PointInstancer
// 刚性绑骨 + DynamicWindSkeletonAPI 三组仿真组。仅当 mesh.hasSkeleton() 时调用。
static bool exportSkeletalUSD(const TreeMeshData& mesh, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f.setf(std::ios::fixed);
    f.precision(6);

    const auto& bones = mesh.skeleton;
    const auto& sb = mesh.skinBase;
    size_t N = bones.size();
    if (N == 0 || sb.pts.empty()) return false;

    // 材质收集: UE USD 导入靠 rel material:binding 生成材质槽(displayColor 不算)。
    // 各 mesh 段收集到此, 最后在 Root 内统一写 def Scope "Materials"(UsdPreviewSurface 空壳)。
    struct MatEntry { std::string name; glm::vec3 col; };
    std::vector<MatEntry> mats;
    auto addMat = [&](const std::string& nm, const glm::vec3& col) -> std::string {
        mats.push_back({nm, col});
        return "</Root/Materials/" + nm + ">";
    };

    // 1) 每骨: 唯一段名 + 完整路径(按父链拼接) + 局部/世界平移。
    std::vector<std::string> seg(N), jpath(N);
    for (size_t i = 0; i < N; ++i)
        seg[i] = sanitizeName(bones[i].name.empty() ? ("bone" + std::to_string(i)) : bones[i].name)
               + "_" + std::to_string(i);   // 后缀索引保证唯一
    for (size_t i = 0; i < N; ++i) {
        std::vector<int> chain;
        for (int c = (int)i; c >= 0 && c < (int)N; c = bones[c].parent) chain.push_back(c);
        std::string p;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!p.empty()) p += "/";
            p += seg[*it];
        }
        jpath[i] = p;
    }

    // 2) 头
    f << "#usda 1.0\n(\n";
    f << "    defaultPrim = \"Root\"\n    metersPerUnit = 1\n    upAxis = \"Y\"\n)\n\n";

    // 3) Root: NaniteAssemblyRootAPI, meshType=skeletalMesh, skeleton 指向下方 Skel
    f << "def Xform \"Root\" (\n";
    f << "    prepend apiSchemas = [\"NaniteAssemblyRootAPI\"]\n)\n{\n";
    f << "    uniform token unreal:naniteAssembly:meshType = \"skeletalMesh\"\n";
    f << "    rel unreal:naniteAssembly:skeleton = </Root/TreeSkelRoot/Skel>\n\n";

    // 3a) SkelRoot + Skeleton
    f << "    def SkelRoot \"TreeSkelRoot\"\n    {\n";
    f << "        def Skeleton \"Skel\" (\n";
    f << "            prepend apiSchemas = [\"DynamicWindSkeletonAPI\"]\n        )\n        {\n";

    // skel:joints (路径) / skel:jointNames (短名, 供 bindJoints 匹配)
    f << "            uniform token[] joints = [";
    for (size_t i = 0; i < N; ++i) { if (i) f << ", "; f << "\"" << jpath[i] << "\""; }
    f << "]\n";
    f << "            uniform token[] jointNames = [";
    for (size_t i = 0; i < N; ++i) { if (i) f << ", "; f << "\"" << seg[i] << "\""; }
    f << "]\n";

    // bindTransforms(世界空间平移) / restTransforms(相对父的局部平移)
    f << "            uniform matrix4d[] bindTransforms = [";
    for (size_t i = 0; i < N; ++i) { if (i) f << ", "; writeTransMat(f, bones[i].pos); }
    f << "]\n";
    f << "            uniform matrix4d[] restTransforms = [";
    for (size_t i = 0; i < N; ++i) {
        if (i) f << ", ";
        glm::vec3 local = bones[i].parent >= 0 && bones[i].parent < (int)N
                        ? bones[i].pos - bones[bones[i].parent].pos : bones[i].pos;
        writeTransMat(f, local);
    }
    f << "]\n\n";

    // DynamicWindSkeletonAPI: 三组仿真组(0=树干/1=枝/2=细枝叶)
    f << "            uniform token[] unreal:dynamicWind:jointNames = [";
    for (size_t i = 0; i < N; ++i) { if (i) f << ", "; f << "\"" << seg[i] << "\""; }
    f << "]\n";
    f << "            uniform int[] unreal:dynamicWind:jointSimulationGroups = [";
    for (size_t i = 0; i < N; ++i) { if (i) f << ", "; f << bones[i].simGroup; }
    f << "]\n";
    // 每组风影响(单值组, 组数=3): 树干弱 / 枝中 / 细枝叶强
    f << "            uniform float[] unreal:dynamicWind:simulationGroupInfluences = [0.15, 0.5, 0.9]\n";
    f << "            uniform int[] unreal:dynamicWind:trunkSimulationGroups = [0]\n";
    f << "        }\n\n";

    // 3b) 树干蒙皮 base 网格(标准 UsdSkel skinning)
    f << "        def Mesh \"TrunkBase\" (\n";
    f << "            prepend apiSchemas = [\"SkelBindingAPI\"]\n        )\n        {\n";
    f << "            rel skel:skeleton = </Root/TreeSkelRoot/Skel>\n";
    {
        std::vector<int> fc(sb.idx.size() / 3, 3);
        std::vector<int> fi(sb.idx.begin(), sb.idx.end());
        writeMeshBody(f, "            ", sb.pts, sb.nrms, sb.uvs, fc, fi, sb.material.albedo);
    }
    // skel:jointIndices / jointWeights (elementSize=4)
    f << "            int[] primvars:skel:jointIndices = [";
    for (size_t i = 0; i < sb.boneIdx.size(); ++i) {
        const glm::ivec4& bi = sb.boneIdx[i];
        for (int k = 0; k < 4; ++k) {
            if (i || k) f << ", ";
            int idx = bi[k]; f << (idx >= 0 && idx < (int)N ? idx : 0);   // 空槽→0(权重亦0)
        }
    }
    f << "] (\n                elementSize = 4\n                interpolation = \"vertex\"\n            )\n";
    f << "            float[] primvars:skel:jointWeights = [";
    for (size_t i = 0; i < sb.boneWt.size(); ++i) {
        const glm::vec4& w = sb.boneWt[i];
        const glm::ivec4& bi = sb.boneIdx[i];
        for (int k = 0; k < 4; ++k) {
            if (i || k) f << ", ";
            f << ((bi[k] >= 0 && bi[k] < (int)N) ? w[k] : 0.0f);
        }
    }
    f << "] (\n                elementSize = 4\n                interpolation = \"vertex\"\n            )\n";
    f << "            matrix4d primvars:skel:geomBindTransform = ";
    writeTransMat(f, glm::vec3(0.0f));
    f << "\n";
    // 材质分段 → GeomSubset(familyName=materialBind): UE 导入后每段成独立材质槽。
    // 单材质(subs 为空)时整块绑一个材质。均需真实 material:binding, 否则 UE 只出一个槽。
    if (sb.subs.size() >= 2) {
        f << "            uniform token subsetFamily:materialBind:familyType = \"partition\"\n";
        for (size_t si = 0; si < sb.subs.size(); ++si) {
            const auto& ps = sb.subs[si];
            if (ps.idxCount == 0) continue;
            // GeomSubset 索引 = 面(三角形)序号。段内索引区间 [off, off+cnt) 每 3 个一面。
            uint32_t faceStart = ps.idxOffset / 3;
            uint32_t faceCount = ps.idxCount / 3;
            std::string mp = addMat("Mat_Trunk_" + std::to_string(si), ps.material.albedo);
            f << "            def GeomSubset \"MatSlot_" << si << "\" (\n";
            f << "                prepend apiSchemas = [\"MaterialBindingAPI\"]\n            )\n            {\n";
            f << "                uniform token elementType = \"face\"\n";
            f << "                uniform token familyName = \"materialBind\"\n";
            f << "                int[] indices = [";
            for (uint32_t k = 0; k < faceCount; ++k) { if (k) f << ", "; f << (faceStart + k); }
            f << "]\n";
            f << "                rel material:binding = " << mp << "\n";
            f << "                color3f[] primvars:displayColor = [("
              << ps.material.albedo.x << ", " << ps.material.albedo.y << ", "
              << ps.material.albedo.z << ")] (interpolation = \"constant\")\n";
            f << "            }\n";
        }
    } else {
        std::string mp = addMat("Mat_Trunk", sb.material.albedo);
        f << "            rel material:binding = " << mp << "\n";
    }
    f << "        }\n";
    f << "    }\n\n";

    // 3c) 散布叶 PointInstancer: NaniteAssemblySkelBindingAPI, 每实例刚性绑到所属 leafTwig 骨
    for (size_t pi = 0; pi < mesh.protos.size(); ++pi) {
        const auto& lp = mesh.protos[pi];
        if (lp.instances.empty() || lp.pts.empty() || lp.idx.empty()) continue;
        std::string inst = "ScatterLeaf_" + std::to_string(pi) + "_Instancer";

        f << "    def PointInstancer \"" << inst << "\" (\n";
        f << "        prepend apiSchemas = [\"NaniteAssemblySkelBindingAPI\"]\n    )\n    {\n";
        f << "        rel prototypes = [</Root/" << inst << "/Protos/Proto>]\n";

        f << "        int[] protoIndices = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) { if (j) f << ", "; f << "0"; }
        f << "]\n";

        f << "        point3f[] positions = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& p = lp.instances[j].pos;
            f << "(" << p.x << ", " << p.y << ", " << p.z << ")";
        }
        f << "]\n";

        f << "        quath[] orientations = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::quat& q = lp.instances[j].rot;
            f << "(" << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ")";
        }
        f << "]\n";

        f << "        float3[] scales = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& s = lp.instances[j].scale;
            f << "(" << s.x << ", " << s.y << ", " << s.z << ")";
        }
        f << "]\n";

        // 每实例绑一根骨(elementSize=1): 名字取该 leafTwig 骨短名; 无效→根骨
        f << "        uniform token[] primvars:unreal:naniteAssembly:bindJoints = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            int b = lp.instances[j].bone;
            f << "\"" << ((b >= 0 && b < (int)N) ? seg[b] : seg[0]) << "\"";
        }
        f << "] (elementSize = 1)\n";
        f << "        uniform float[] primvars:unreal:naniteAssembly:bindJointWeights = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) { if (j) f << ", "; f << "1.0"; }
        f << "] (elementSize = 1)\n\n";

        // Proto: 骨骼 Nanite Assembly 要求 PointInstancer 原型自身是 SkeletalMesh
        // (引擎在原型下找 UsdSkelSkeleton 才收集为部件), 故原型 = SkelRoot + 单骨 Skeleton
        // + 蒙皮到该单骨(全权重)的叶网格。每实例再经上方 bindJoints 刚性绑到树干 leafTwig 骨。
        // 含多材质时按材质段(subs)拆成多个 Mesh(共用同一 Skeleton + 顶点/蒙皮)。
        std::string protoPath = "/Root/" + inst + "/Protos/Proto";
        f << "        def Scope \"Protos\"\n        {\n";
        f << "            def SkelRoot \"Proto\"\n            {\n";
        // 单骨 Skeleton(identity rest/bind)
        f << "                def Skeleton \"LeafSkel\"\n                {\n";
        f << "                    uniform token[] joints = [\"leaf\"]\n";
        f << "                    uniform token[] jointNames = [\"leaf\"]\n";
        f << "                    uniform matrix4d[] bindTransforms = ["; writeTransMat(f, glm::vec3(0.0f)); f << "]\n";
        f << "                    uniform matrix4d[] restTransforms = ["; writeTransMat(f, glm::vec3(0.0f)); f << "]\n";
        f << "                }\n";
        // 材质段: subs 为空时整块视为一段。
        struct SegRef { uint32_t off, cnt; glm::vec3 col; };
        std::vector<SegRef> segs;
        if (lp.subs.empty()) segs.push_back({0, (uint32_t)lp.idx.size(), lp.material.albedo});
        else for (const auto& ps : lp.subs) segs.push_back({ps.idxOffset, ps.idxCount, ps.material.albedo});
        for (size_t si = 0; si < segs.size(); ++si) {
            const SegRef& sg = segs[si];
            if (sg.cnt == 0) continue;
            std::vector<int> fc(sg.cnt / 3, 3);
            std::vector<int> fi(lp.idx.begin() + sg.off, lp.idx.begin() + sg.off + sg.cnt);
            std::string geoName = "LeafGeo_" + std::to_string(si);
            std::string mp = addMat("Mat_Leaf" + std::to_string(pi) + "_" + std::to_string(si), sg.col);
            // 叶网格: 蒙皮到 LeafSkel 单骨, 全顶点 index 0 / weight 1(顶点数组整枝共用)
            f << "                def Mesh \"" << geoName << "\" (\n";
            f << "                    prepend apiSchemas = [\"SkelBindingAPI\", \"MaterialBindingAPI\"]\n                )\n                {\n";
            f << "                    rel skel:skeleton = <" << protoPath << "/LeafSkel>\n";
            f << "                    rel material:binding = " << mp << "\n";
            writeMeshBody(f, "                    ", lp.pts, lp.nrms, lp.uvs, fc, fi, sg.col);
            f << "                    int[] primvars:skel:jointIndices = [";
            for (size_t v = 0; v < lp.pts.size(); ++v) { if (v) f << ", "; f << "0"; }
            f << "] (\n                        elementSize = 1\n                        interpolation = \"vertex\"\n                    )\n";
            f << "                    float[] primvars:skel:jointWeights = [";
            for (size_t v = 0; v < lp.pts.size(); ++v) { if (v) f << ", "; f << "1"; }
            f << "] (\n                        elementSize = 1\n                        interpolation = \"vertex\"\n                    )\n";
            f << "                    matrix4d primvars:skel:geomBindTransform = "; writeTransMat(f, glm::vec3(0.0f)); f << "\n";
            f << "                }\n";
        }
        f << "            }\n        }\n";
        f << "    }\n\n";
    }

    // Materials: 每个 mesh 段一个 UsdPreviewSurface 空壳(仅 diffuseColor)。
    // UE 靠这些 material:binding 生成独立材质槽; 具体贴图由用户在 UE 里指定。
    f << "    def Scope \"Materials\"\n    {\n";
    for (const auto& m : mats) {
        f << "        def Material \"" << m.name << "\"\n        {\n";
        f << "            token outputs:surface.connect = </Root/Materials/" << m.name << "/Surface.outputs:surface>\n";
        f << "            def Shader \"Surface\"\n            {\n";
        f << "                uniform token info:id = \"UsdPreviewSurface\"\n";
        f << "                color3f inputs:diffuseColor = (" << m.col.x << ", " << m.col.y << ", " << m.col.z << ")\n";
        f << "                token outputs:surface\n";
        f << "            }\n";
        f << "        }\n";
    }
    f << "    }\n";

    f << "}\n";

    // 配套 DynamicWind JSON: UE 5.8 不会在 USD 导入时自动读 DynamicWindSkeletonAPI,
    // 需手动用 ImportDynamicWindSkeletalDataFromFile 选此 json 应用到 SkeletalMesh。
    // 字段名对齐 FDynamicWindSkeletalImportData(JsonObjectStringToUStruct 按 UPROPERTY 名解析)。
    // JointName 必须等于 UE 导入后的骨名(= USD 的 jointNames = seg[i])。
    {
        std::string jp = path;
        size_t dot = jp.find_last_of('.');
        size_t slash = jp.find_last_of("/\\");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            jp = jp.substr(0, dot);
        jp += "_wind.json";
        std::ofstream j(jp);
        if (j) {
            j.setf(std::ios::fixed); j.precision(3);
            const float infl[3] = {0.15f, 0.5f, 0.9f};   // 与 USD simulationGroupInfluences 一致
            j << "{\n    \"Joints\": [\n";
            for (size_t i = 0; i < N; ++i) {
                j << "        { \"JointName\": \"" << seg[i]
                  << "\", \"SimulationGroupIndex\": " << bones[i].simGroup << " }"
                  << (i + 1 < N ? "," : "") << "\n";
            }
            j << "    ],\n    \"SimulationGroups\": [\n";
            for (int g = 0; g < 3; ++g) {
                j << "        { \"bUseDualInfluence\": false, \"Influence\": " << infl[g]
                  << ", \"MinInfluence\": 0.0, \"MaxInfluence\": 0.0, \"ShiftTop\": 0.0, \"bIsTrunkGroup\": "
                  << (g == 0 ? "true" : "false") << " }" << (g < 2 ? "," : "") << "\n";
            }
            j << "    ],\n    \"bIsGroundCover\": false,\n    \"GustAttenuation\": 0.0\n}\n";
        }
    }

    return true;
}

// 导出 USD Nanite Assembly(.usda 文本)。返回是否成功。
bool exportUSD(const TreeMeshData& mesh, const std::string& path) {
    // 带骨架且有蒙皮 base(散布用枝干为 SpeedTree 骨骼 FBX): 走骨骼 Nanite Assembly,
    // 兼容 DynamicWind 骨骼风效。原生生成树只有骨架(可视化用)但无蒙皮 base,
    // 此时走下面的静态网格导出路径, 不进骨骼分支(否则 exportSkeletalUSD 因 skinBase 空而失败)。
    if (mesh.hasSkeleton() && !mesh.skinBase.pts.empty())
        return exportSkeletalUSD(mesh, path);

    std::ofstream f(path);
    if (!f) return false;
    f.setf(std::ios::fixed);
    f.precision(6);

    // 1) 分拣批次: 枝干 -> base; 叶(四边形) -> 各自 instancer; 无法实例化的叶 -> 并入 base。
    std::vector<glm::vec3> basePts, baseNrms;
    std::vector<glm::vec2> baseUvs;
    std::vector<int> baseFaceCounts, baseFaceIdx;
    glm::vec3 baseCol(0.35f, 0.22f, 0.10f);

    struct LeafProto {
        std::vector<LeafInstance> instances;
        glm::vec3 color;
    };
    std::vector<LeafProto> leafProtos;

    for (const auto& b : mesh.batches) {
        if (b.vertices.empty() || b.indices.empty()) continue;
        if (b.instanced) continue;   // 散布叶的视口预览烘焙: 已在 protos 里实例化导出, 跳过
        if (!b.isLeaf) {
            appendToBase(b, basePts, baseNrms, baseUvs, baseFaceCounts, baseFaceIdx);
            baseCol = b.material.albedo;   // 用最后一个枝干材质做整体 displayColor
        } else {
            std::vector<LeafInstance> insts;
            if (parseLeafQuads(b, insts) && !insts.empty()) {
                leafProtos.push_back({std::move(insts), b.material.albedo});
            } else {
                // cutout 或异形叶: 无法实例化, 并入 base(几何原样保留, 只是不省内存)
                appendToBase(b, basePts, baseNrms, baseUvs, baseFaceCounts, baseFaceIdx);
            }
        }
    }

    if (basePts.empty()) {
        // Nanite Assembly base 必须有几何; 无枝干则无法导出
        std::fprintf(stderr, "[USD] 无枝干几何, 无法导出 Nanite Assembly base 网格\n");
        return false;
    }

    // 2) 头
    f << "#usda 1.0\n";
    f << "(\n";
    f << "    defaultPrim = \"Root\"\n";
    f << "    metersPerUnit = 1\n";
    f << "    upAxis = \"Y\"\n";
    f << ")\n\n";

    // 3) Root: 应用 NaniteAssemblyRootAPI, meshType=staticMesh
    f << "def Xform \"Root\" (\n";
    f << "    prepend apiSchemas = [\"NaniteAssemblyRootAPI\"]\n";
    f << ")\n{\n";
    f << "    uniform token unreal:naniteAssembly:meshType = \"staticMesh\"\n\n";

    // 3a) base 树干网格
    f << "    def Mesh \"TrunkMesh\"\n    {\n";
    writeMeshBody(f, "        ", basePts, baseNrms, baseUvs, baseFaceCounts, baseFaceIdx, baseCol);
    f << "    }\n\n";

    // 3b) 每个叶原型 -> 一个 PointInstancer(单原型) + 内嵌单位叶四边形
    // 单位叶四边形: [-1,-1]..[1,1], 法线 +Z, uv 覆盖整张叶贴图
    for (size_t pi = 0; pi < leafProtos.size(); ++pi) {
        const LeafProto& lp = leafProtos[pi];
        std::string inst = "Leaf_" + std::to_string(pi) + "_Instancer";

        f << "    def PointInstancer \"" << inst << "\"\n    {\n";

        // prototypes 关系: 指向下面 Protos/Proto
        f << "        rel prototypes = [</Root/" << inst << "/Protos/Proto>]\n";

        // protoIndices: 全 0(单一原型)
        f << "        int[] protoIndices = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) { if (j) f << ", "; f << "0"; }
        f << "]\n";

        // positions
        f << "        point3f[] positions = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& p = lp.instances[j].pos;
            f << "(" << p.x << ", " << p.y << ", " << p.z << ")";
        }
        f << "]\n";

        // orientations(quath, USD 顺序 = (w,x,y,z))
        f << "        quath[] orientations = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::quat& q = lp.instances[j].rot;
            f << "(" << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ")";
        }
        f << "]\n";

        // scales
        f << "        float3[] scales = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& s = lp.instances[j].scale;
            f << "(" << s.x << ", " << s.y << ", " << s.z << ")";
        }
        f << "]\n\n";

        // Protos 组 + 单位叶原型
        f << "        def Scope \"Protos\"\n        {\n";
        f << "            def Mesh \"Proto\"\n            {\n";
        std::vector<glm::vec3> qp = {
            {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}
        };
        std::vector<glm::vec3> qn = {
            {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}
        };
        std::vector<glm::vec2> quv = {
            {0,0}, {1,0}, {1,1}, {0,1}
        };
        std::vector<int> qc = {3, 3};
        std::vector<int> qi = {0,1,2, 0,2,3};
        writeMeshBody(f, "                ", qp, qn, quv, qc, qi, lp.color);
        f << "            }\n";
        f << "        }\n";

        f << "    }\n\n";
    }

    // 3c) 导入散布叶原型 -> 每组一个 PointInstancer, Proto 内嵌导入的叶网格(自包含)
    for (size_t pi = 0; pi < mesh.protos.size(); ++pi) {
        const auto& lp = mesh.protos[pi];
        if (lp.instances.empty() || lp.pts.empty() || lp.idx.empty()) continue;
        std::string inst = "ScatterLeaf_" + std::to_string(pi) + "_Instancer";

        f << "    def PointInstancer \"" << inst << "\"\n    {\n";
        f << "        rel prototypes = [</Root/" << inst << "/Protos/Proto>]\n";

        f << "        int[] protoIndices = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) { if (j) f << ", "; f << "0"; }
        f << "]\n";

        f << "        point3f[] positions = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& p = lp.instances[j].pos;
            f << "(" << p.x << ", " << p.y << ", " << p.z << ")";
        }
        f << "]\n";

        f << "        quath[] orientations = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::quat& q = lp.instances[j].rot;
            f << "(" << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ")";
        }
        f << "]\n";

        f << "        float3[] scales = [";
        for (size_t j = 0; j < lp.instances.size(); ++j) {
            if (j) f << ", ";
            const glm::vec3& s = lp.instances[j].scale;
            f << "(" << s.x << ", " << s.y << ", " << s.z << ")";
        }
        f << "]\n\n";

        // Proto: 内嵌导入的叶网格几何(局部空间, 重心归零)。含多材质时按材质段拆多个 Mesh。
        f << "        def Scope \"Protos\"\n        {\n";
        f << "            def Xform \"Proto\"\n            {\n";
        struct SegRef { uint32_t off, cnt; glm::vec3 col; };
        std::vector<SegRef> segs;
        if (lp.subs.empty()) segs.push_back({0, (uint32_t)lp.idx.size(), lp.material.albedo});
        else for (const auto& ps : lp.subs) segs.push_back({ps.idxOffset, ps.idxCount, ps.material.albedo});
        for (size_t si = 0; si < segs.size(); ++si) {
            const SegRef& sg = segs[si];
            if (sg.cnt == 0) continue;
            std::vector<int> fc(sg.cnt / 3, 3);
            std::vector<int> fi(lp.idx.begin() + sg.off, lp.idx.begin() + sg.off + sg.cnt);
            f << "                def Mesh \"Geo_" << si << "\"\n                {\n";
            writeMeshBody(f, "                    ", lp.pts, lp.nrms, lp.uvs, fc, fi, sg.col);
            f << "                }\n";
        }
        f << "            }\n";
        f << "        }\n";
        f << "    }\n\n";
    }

    f << "}\n";
    return true;
}

} // namespace ProjectIO
