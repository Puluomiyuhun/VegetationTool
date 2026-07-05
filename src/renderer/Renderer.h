#pragma once
#include "Shader.h"
#include "Mesh.h"
#include "Texture.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "graph/NodeTypes.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// 单个材质批次（CPU侧）
// branch 顶点格式: pos(3)+normal(3)+uv(2) = 8 floats
// leaf   顶点格式: pos(3)+normal(3)+uv(2)+albedo(3) = 11 floats
struct MeshBatch {
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    MaterialParams        material;
    bool                  isLeaf = false;
};

struct TreeMeshData {
    std::vector<MeshBatch> batches;
    void clear() { batches.clear(); }
};

struct LightingParams {
    glm::vec3 lightDir    = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    glm::vec3 lightColor  = {1.2f, 1.1f, 0.95f};
    glm::vec3 ambientTop  = {0.3f, 0.45f, 0.6f};
    glm::vec3 ambientBot  = {0.12f, 0.10f, 0.08f};
};

class Renderer {
public:
    void init();
    void shutdown();

    void uploadTreeMesh(const TreeMeshData& data);
    void render(const OrbitCamera& camera, float aspect, bool wireframe);

    LightingParams lighting;

private:
    struct GpuBatch {
        Mesh           mesh;
        MaterialParams material;
        bool           isLeaf = false;
        Texture        texBaseColor;   // unit 0 — basecolor/albedo
        Texture        texRoughness;   // unit 1 — roughness(R) metallic(G)
        Texture        texNormal;      // unit 2 — tangent-space normal
    };

    std::vector<GpuBatch> m_batches;

    Shader m_branchShader;
    Shader m_leafShader;
    Shader m_gridShader;
    Mesh   m_gridMesh;

    void setLightUniforms(Shader& sh);
    void bindBatchTextures(Shader& sh, GpuBatch& gb);
    void buildGrid();
    void renderGrid(const glm::mat4& vp);
};
