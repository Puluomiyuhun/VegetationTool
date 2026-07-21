// FBX 导入实现(ufbx)。见 MeshImport.h 说明。
#include "MeshImport.h"
#include <ufbx.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <algorithm>
#include <cstring>

namespace {

glm::vec3 toGlm(ufbx_vec3 v) { return glm::vec3((float)v.x, (float)v.y, (float)v.z); }
glm::vec2 toGlm(ufbx_vec2 v) { return glm::vec2((float)v.x, (float)v.y); }

// 从 ufbx 材质取贴图路径与基色, 填入 MaterialParams。
void fillMaterialFrom(const ufbx_material* m, MaterialParams& out) {
    if (!m) return;

    // 基色(FBX 传统 Phong 或 PBR base_color)
    if (m->pbr.base_color.has_value) {
        ufbx_vec4 c = m->pbr.base_color.value_vec4;
        out.albedo = glm::vec3((float)c.x, (float)c.y, (float)c.z);
    } else if (m->fbx.diffuse_color.has_value) {
        ufbx_vec3 c = m->fbx.diffuse_color.value_vec3;
        out.albedo = glm::vec3((float)c.x, (float)c.y, (float)c.z);
    }

    // 贴图: 优先 PBR 通道, 回退传统通道。取绝对 filename。
    auto texOf = [](const ufbx_material_map& map) -> std::string {
        if (map.texture && map.texture->filename.length > 0)
            return std::string(map.texture->filename.data, map.texture->filename.length);
        return {};
    };
    std::string base = texOf(m->pbr.base_color);
    if (base.empty()) base = texOf(m->fbx.diffuse_color);
    if (!base.empty()) out.baseColorTex = base;

    std::string nrm = texOf(m->pbr.normal_map);
    if (nrm.empty()) nrm = texOf(m->fbx.normal_map);
    if (!nrm.empty()) out.normalTex = nrm;

    std::string op = texOf(m->pbr.opacity);
    if (op.empty()) op = texOf(m->fbx.transparency_color);
    if (!op.empty()) out.opacityTex = op;

    std::string rg = texOf(m->pbr.roughness);
    if (!rg.empty()) out.roughnessTex = rg;
}

} // namespace

namespace MeshImport {

ImportedMesh loadFBX(const std::string& path) {
    ImportedMesh out;

    ufbx_load_opts opts = {};
    opts.generate_missing_normals = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;   // 统一到 Y-up 右手
    opts.target_unit_meters = 1.0f;                   // 归一到米

    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        out.ok = false;
        out.error = std::string("ufbx 加载失败: ") + err.description.data;
        return out;
    }

    // 1) 收集全场景骨骼(bone 节点), 建立 node->boneIndex 映射
    std::unordered_map<const ufbx_node*, int> boneMap;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (n->bone) {
            int idx = (int)out.bones.size();
            boneMap[n] = idx;
            ImportedBone b;
            b.name = std::string(n->name.data, n->name.length);
            b.restPos = toGlm(n->node_to_world.cols[3]);   // 世界原点
            out.bones.push_back(b);
        }
    }
    // 填父子关系 + 方向/长度(指向首个也是 bone 的子节点)
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        auto it = boneMap.find(n);
        if (it == boneMap.end()) continue;
        int bi = it->second;
        // 父骨骼(向上找最近的 bone 祖先)
        for (const ufbx_node* p = n->parent; p; p = p->parent) {
            auto pit = boneMap.find(p);
            if (pit != boneMap.end()) { out.bones[bi].parent = pit->second; break; }
        }
        // 首个 bone 子节点决定方向/长度
        for (size_t c = 0; c < n->children.count; ++c) {
            auto cit = boneMap.find(n->children.data[c]);
            if (cit != boneMap.end()) {
                glm::vec3 childPos = out.bones[cit->second].restPos;
                glm::vec3 d = childPos - out.bones[bi].restPos;
                out.bones[bi].length = glm::length(d);
                if (out.bones[bi].length > 1e-6f)
                    out.bones[bi].restDir = d / out.bones[bi].length;
                break;
            }
        }
    }
    // 无子的骨骼: 方向沿用父方向(叶端骨骼)
    for (auto& b : out.bones)
        if (b.length == 0.0f && b.parent >= 0)
            b.restDir = out.bones[b.parent].restDir;

    // 叶段识别(SpeedTree 骨骼命名分段): 骨骼名 "Bone_N_Start/_End" 中 N 为段号。
    // 一段枝由同号 Start/End 组成。段的父段 = 沿骨骼父链向上第一个不同段号。
    // "叶段" = 没有子段的段(枝梢), 叶子只长在叶段上。把叶段的末端骨骼(该段中最深的
    // 那根, 通常是 _End)标记 leafTwig, 供 Scatter 沿 parent->self 撒叶。
    if (!out.bones.empty()) {
        std::vector<int> seg(out.bones.size(), -1);
        for (int i = 0; i < (int)out.bones.size(); ++i) {
            const std::string& nm = out.bones[i].name;
            if (nm.rfind("Bone_", 0) == 0) {
                seg[i] = std::atoi(nm.c_str() + 5);   // Bone_<N>_...
            }
            out.bones[i].segment = seg[i];
        }
        // 段的子段集合: 若某骨的父骨属于不同段, 则该父段拥有此段为子段。
        std::unordered_map<int, std::vector<int>> segChildren;
        std::unordered_map<int, std::vector<int>> segBones;    // 段号 -> 该段所有骨索引
        for (int i = 0; i < (int)out.bones.size(); ++i) {
            int si = seg[i];
            if (si < 0) continue;
            segBones[si].push_back(i);
            int par = out.bones[i].parent;
            if (par >= 0 && seg[par] >= 0 && seg[par] != si)
                segChildren[seg[par]].push_back(si);
        }
        // 叶段 = 无子段。标记叶段中"最深"骨骼(离该段根最远)为 leafTwig。
        for (auto& kv : segBones) {
            int si = kv.first;
            if (!segChildren[si].empty()) continue;   // 非叶段
            // 该段中选距段内起点最远的骨(用骨深度: 到根的父链长度)为末端
            int deepest = -1, deepestDepth = -1;
            for (int bi : kv.second) {
                int d = 0;
                for (int p = out.bones[bi].parent; p >= 0; p = out.bones[p].parent) ++d;
                if (d > deepestDepth) { deepestDepth = d; deepest = bi; }
            }
            if (deepest >= 0 && out.bones[deepest].parent >= 0)
                out.bones[deepest].leafTwig = true;
        }
    }

    // 2) 遍历所有网格实例, 三角化, 烘到世界空间累积。按材质分组到 parts:
    //    同一材质(ufbx_material 指针)的三角形聚到一个 part(可跨多个 mesh)。
    struct PartAccum { const ufbx_material* mat; std::vector<uint32_t> idx; };
    std::vector<PartAccum> partAccum;
    std::unordered_map<const ufbx_material*, int> matToPart;
    auto partOf = [&](const ufbx_material* m) -> int {
        auto it = matToPart.find(m);
        if (it != matToPart.end()) return it->second;
        int pi = (int)partAccum.size();
        matToPart[m] = pi;
        partAccum.push_back({m, {}});
        return pi;
    };

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        if (mesh->instances.count == 0) continue;
        const ufbx_node* node = mesh->instances.data[0];
        ufbx_matrix geoToWorld = node->geometry_to_world;
        ufbx_matrix normalMat = ufbx_matrix_for_normals(&geoToWorld);

        // 蒙皮变形器(取首个)
        const ufbx_skin_deformer* skin =
            mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;

        std::vector<uint32_t> triIdx(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            ufbx_face face = mesh->faces.data[fi];
            // 该面所属材质 → part。face_material 给出本 mesh 的材质槽号。
            const ufbx_material* faceMat = nullptr;
            if (mesh->materials.count > 0) {
                uint32_t slot = (fi < mesh->face_material.count) ? mesh->face_material.data[fi] : 0;
                if (slot < mesh->materials.count) faceMat = mesh->materials.data[slot];
            }
            int pi = partOf(faceMat);

            uint32_t nTri = ufbx_triangulate_face(triIdx.data(), triIdx.size(), mesh, face);
            for (uint32_t t = 0; t < nTri * 3; ++t) {
                uint32_t index = triIdx[t];   // mesh 的 corner index
                uint32_t vtx = mesh->vertex_indices.data[index];  // 逻辑顶点索引

                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                ufbx_vec3 n = mesh->vertex_normal.exists
                            ? ufbx_get_vertex_vec3(&mesh->vertex_normal, index)
                            : ufbx_vec3{0,1,0};
                ufbx_vec2 uv = mesh->vertex_uv.exists
                             ? ufbx_get_vertex_vec2(&mesh->vertex_uv, index)
                             : ufbx_vec2{0,0};

                glm::vec3 wp = toGlm(ufbx_transform_position(&geoToWorld, p));
                glm::vec3 wn = glm::normalize(toGlm(ufbx_transform_direction(&normalMat, n)));

                uint32_t newIdx = (uint32_t)out.positions.size();
                out.positions.push_back(wp);
                out.normals.push_back(wn);
                out.uvs.push_back(toGlm(uv));
                partAccum[pi].idx.push_back(newIdx);   // 索引归入所属 part

                // 蒙皮权重(逐逻辑顶点, 取最大 4 个)
                if (skin && !out.bones.empty()) {
                    glm::ivec4 bidx(-1);
                    glm::vec4  bwt(0.0f);
                    if (vtx < skin->vertices.count) {
                        ufbx_skin_vertex sv = skin->vertices.data[vtx];
                        uint32_t take = std::min<uint32_t>(sv.num_weights, 4);
                        float wsum = 0.0f;
                        for (uint32_t w = 0; w < take; ++w) {
                            ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                            const ufbx_skin_cluster* cl =
                                skin->clusters.data[sw.cluster_index];
                            int mapped = -1;
                            if (cl->bone_node) {
                                auto bit = boneMap.find(cl->bone_node);
                                if (bit != boneMap.end()) mapped = bit->second;
                            }
                            bidx[w] = mapped;
                            bwt[w]  = (float)sw.weight;
                            wsum += (float)sw.weight;
                        }
                        if (wsum > 1e-6f) bwt /= wsum;   // 归一
                    }
                    out.boneIdx.push_back(bidx);
                    out.boneWt.push_back(bwt);
                }
            }
        }
    }

    // 把各 part 的索引拼接进 out.indices, 记录区间 + 材质到 out.parts。
    for (auto& pa : partAccum) {
        ImportedMesh::SubMesh sm;
        sm.indexOffset = (uint32_t)out.indices.size();
        sm.indexCount  = (uint32_t)pa.idx.size();
        fillMaterialFrom(pa.mat, sm.material);
        if (pa.mat) sm.materialName = std::string(pa.mat->name.data, pa.mat->name.length);
        out.indices.insert(out.indices.end(), pa.idx.begin(), pa.idx.end());
        out.parts.push_back(std::move(sm));
    }
    // 兼容: material 仍取首个 part 材质。
    if (!out.parts.empty()) out.material = out.parts[0].material;

    ufbx_free_scene(scene);

    // 3) 逐骨截面半径: 把每个蒙皮顶点归到其主导骨骼(最大权重), 累计顶点到骨轴
    // (restPos, restDir 无限直线)的垂距, 取均值作为该骨处枝干半径。供 Scatter
    // 把叶从骨骼中轴沿辐射方向推到枝干表面。
    if (!out.bones.empty() && out.boneIdx.size() == out.positions.size()) {
        std::vector<float> rSum(out.bones.size(), 0.0f);
        std::vector<int>   rCnt(out.bones.size(), 0);
        for (size_t i = 0; i < out.positions.size(); ++i) {
            const glm::ivec4& bi = out.boneIdx[i];
            const glm::vec4&  bw = out.boneWt[i];
            int dom = -1; float best = 0.0f;
            for (int k = 0; k < 4; ++k)
                if (bi[k] >= 0 && bw[k] > best) { best = bw[k]; dom = bi[k]; }
            if (dom < 0) continue;
            const ImportedBone& b = out.bones[dom];
            glm::vec3 d = out.positions[i] - b.restPos;
            glm::vec3 ax = b.restDir;
            float axl2 = glm::dot(ax, ax);
            float perp = (axl2 > 1e-8f)
                       ? glm::length(d - ax * (glm::dot(d, ax) / axl2))
                       : glm::length(d);
            rSum[dom] += perp; rCnt[dom] += 1;
        }
        for (size_t b = 0; b < out.bones.size(); ++b)
            if (rCnt[b] > 0) out.bones[b].radius = rSum[b] / (float)rCnt[b];
        // 无蒙皮顶点的骨(如叶段末端 End 常无独立权重): 继承父骨半径的一半, 逐级传播。
        for (size_t b = 0; b < out.bones.size(); ++b) {
            if (out.bones[b].radius > 0.0f) continue;
            int par = out.bones[b].parent;
            if (par >= 0 && out.bones[par].radius > 0.0f)
                out.bones[b].radius = out.bones[par].radius * 0.6f;
        }
    }

    if (out.positions.empty()) {
        out.ok = false;
        out.error = "FBX 中没有可用网格几何";
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace MeshImport
