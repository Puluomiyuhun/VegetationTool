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
#include <memory>
#include <unordered_map>

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
    // 高亮几何(仅位置, 3 floats/顶点): 选中节点及其子树的三角网, 用于视口黄色线框叠加。
    std::vector<float>    hlVerts;
    std::vector<uint32_t> hlIdx;
    void clear() { batches.clear(); hlVerts.clear(); hlIdx.clear(); }
};

struct LightingParams {
    glm::vec3 lightDir    = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    glm::vec3 lightColor  = {1.2f, 1.1f, 0.95f};
    float     lightIntensity = 2.2f;   // 主光亮度倍增
    float     ambientStrength = 1.4f;  // 环境光亮度倍增
    float     exposure      = 1.3f;    // 曝光(色调映射前)
    glm::vec3 ambientTop  = {0.3f, 0.45f, 0.6f};
    glm::vec3 ambientBot  = {0.12f, 0.10f, 0.08f};
    // 渐变天空背景(类 SpeedTree)
    glm::vec3 skyTop      = {0.35f, 0.52f, 0.78f};  // 天顶蓝
    glm::vec3 skyHorizon  = {0.78f, 0.76f, 0.70f};  // 地平线暖雾
    glm::vec3 skyGround   = {0.52f, 0.50f, 0.47f};  // 地面灰
    // 阴影(shadow map 自阴影)
    bool  shadowEnabled  = true;
    float shadowStrength = 0.6f;    // 阴影浓度(0=无, 1=全黑)
    float shadowBias     = 0.0025f; // 深度偏移，抑制阴影痤疮(shadow acne)
    float groundShadowStrength = 0.4f; // 地面接收阴影的强度(独立于树自阴影)
    // 地面(用天空渐变着色, 与远端天空融合; 接收阴影压暗)
    bool  groundEnabled  = true;
    float groundAlpha    = 0.85f;   // 地面不透明度: <1 让被遮挡的植被半透明透出
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
        // 贴图改为按路径共享(见 m_texCache)，避免每次重建网格都从磁盘重新解码 → 卡顿。
        std::shared_ptr<Texture> texBaseColor;   // unit 0 — basecolor/albedo
        std::shared_ptr<Texture> texRoughness;   // unit 1 — roughness(R) metallic(G)
        std::shared_ptr<Texture> texNormal;      // unit 2 — tangent-space normal
        std::shared_ptr<Texture> texOpacity;     // unit 3 — opacity mask(R)（叶片alpha剔除）
    };

    std::vector<GpuBatch> m_batches;
    // 贴图缓存：路径(+sRGB标志) → 已加载纹理。重建网格时命中缓存直接复用, 不再重复解码。
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_texCache;
    std::shared_ptr<Texture> getTexture(const std::string& path, bool sRGB);

    Shader m_branchShader;
    Shader m_leafShader;
    Shader m_gridShader;
    Shader m_skyShader;
    Shader m_depthShader;   // 阴影深度 pass
    Shader m_groundShader;  // 地面(接收投影)
    Mesh   m_gridMesh;
    Mesh   m_groundMesh;    // 地面平面
    Mesh   m_hlMesh;        // 选中节点高亮线框(仅位置)
    int    m_hlIndexCount = 0;
    GLuint m_skyVao = 0;   // 空 VAO，用于全屏三角形(顶点由 gl_VertexID 生成)

    // ---- 阴影贴图 ----
    GLuint    m_shadowFbo = 0;
    GLuint    m_shadowTex = 0;
    int       m_shadowSize = 2048;
    glm::mat4 m_lightSpace = glm::mat4(1.0f);
    // 场景包围盒(世界空间)，用于拟合光源正交视锥
    glm::vec3 m_sceneMin = glm::vec3(-5.0f);
    glm::vec3 m_sceneMax = glm::vec3( 5.0f);

    void initShadowMap();
    void computeSceneBounds(const TreeMeshData& data);
    void renderShadowPass();

    void setLightUniforms(Shader& sh);
    void bindBatchTextures(Shader& sh, GpuBatch& gb);
    void buildGrid();
    void buildGround();
    void renderGrid(const glm::mat4& vp);
    void renderGround(const glm::mat4& vp, const glm::vec3& camPos);
    void renderSky(const OrbitCamera& camera, float aspect);
    void renderHighlight(const glm::mat4& vp);   // 选中节点子树黄色线框叠加
};
