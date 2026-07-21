#pragma once
#include "Shader.h"
#include "Mesh.h"
#include "Texture.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "graph/NodeTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// 单个材质批次（CPU侧）
// branch 顶点格式: pos(3)+normal(3)+uv(2)+wind(2:weight,phase) = 10 floats
// leaf   顶点格式: pos(3)+normal(3)+uv(2)+albedo(3)+wind(2:weight,phase)+anchor(3) = 16 floats
struct MeshBatch {
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    MaterialParams        material;
    bool                  isLeaf = false;
    // 实例化代理: 该 batch 是散布叶的"视口预览烘焙"(几何已在 protos 里实例化导出)。
    // 导出 USD 时必须跳过, 否则每片叶完整几何会被重复并入 base 网格(面数/体积爆炸)。
    bool                  instanced = false;
};

struct TreeMeshData {
    std::vector<MeshBatch> batches;
    // 实例化叶原型(FBX 散布用): 一份原型网格 + 一组每实例 transform。
    // 视口渲染时烘成普通 batch; 导出 USD 时成为 PointInstancer(引擎里只存 1 份原型, 省内存)。
    // bone: 该实例刚性绑定的骨索引(骨骼 Nanite Assembly 用; -1=无骨骼/不绑定)。
    struct ProtoInstance { glm::vec3 pos; glm::quat rot; glm::vec3 scale; int bone = -1; };
    // 原型内的材质分段: idx 的一段连续区间用一种材质。一个实例枝可含"枝干材质段 + 叶子材质段",
    // 但整枝共用一份实例 transform(subs 只切几何/材质, 不切实例)。
    struct ProtoSub { uint32_t idxOffset = 0; uint32_t idxCount = 0; MaterialParams material; };
    struct InstancedProto {
        // 原型网格(局部空间, Y-up 米制): 三角化顶点(pos/normal/uv) + 索引。
        std::vector<glm::vec3> pts;
        std::vector<glm::vec3> nrms;
        std::vector<glm::vec2> uvs;
        std::vector<uint32_t>  idx;
        MaterialParams         material;   // 兼容/主材质(= subs[0].material)
        std::vector<ProtoSub>  subs;       // 按材质分段(≥1); 每段一个 draw call, 共用实例
        std::vector<ProtoInstance> instances;
    };
    std::vector<InstancedProto> protos;
    // ---- 骨骼 Nanite Assembly 导出数据(仅当散布用的枝干带骨架时填充; 无骨骼则留空, 走 staticMesh 导出) ----
    // 一根骨(世界空间 rest 位姿): 位置 + 父索引 + 名字 + 仿真组(0=树干,1=枝,2=细枝叶)。
    struct SkelBone { glm::vec3 pos; int parent; std::string name; int simGroup; };
    struct SkinBase {
        // 树干 base 网格(世界空间, Y-up 米制): pos/normal/uv + 每顶点最多 4 骨蒙皮(索引/权重)。
        std::vector<glm::vec3>  pts;
        std::vector<glm::vec3>  nrms;
        std::vector<glm::vec2>  uvs;
        std::vector<uint32_t>   idx;
        std::vector<glm::ivec4> boneIdx;   // 骨索引, 空槽 = -1
        std::vector<glm::vec4>  boneWt;    // 权重(归一)
        MaterialParams          material;  // 兼容字段(= subs[0].material)
        // 按材质分段(≥1): 每段一个 GeomSubset(familyName=materialBind), UE 导入后成独立材质槽。
        // 单材质时留空, 导出走整块单槽。
        std::vector<ProtoSub>   subs;
    };
    std::vector<SkelBone> skeleton;   // 空 = 无骨架(走 staticMesh 导出)
    SkinBase              skinBase;   // 树干蒙皮 base(skeleton 非空时有效)
    bool hasSkeleton() const { return !skeleton.empty(); }
    // 高亮几何(pos(3)+normal(3), 6 floats/顶点): 选中节点"自身"的三角网,
    // 用于视口描边(沿法线外扩 + 模板缓冲勾勒轮廓)。
    std::vector<float>    hlVerts;
    std::vector<uint32_t> hlIdx;
    // 拾取三角形: 每个三角形记录三个世界坐标顶点 + 所属节点 id, 供鼠标射线拾取。
    struct PickTri { glm::vec3 a, b, c; uint32_t node; };
    std::vector<PickTri>  pickTris;
    void clear() { batches.clear(); protos.clear(); hlVerts.clear(); hlIdx.clear(); pickTris.clear(); skeleton.clear(); skinBase = {}; }
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

// 顶点风力动画参数(类 SpeedTree 顶点风)。三层叠加:
//  1) 全局摆动(Global): 整树随高度比按 hr^2 加权左右摇摆
//  2) 枝条颤动(Branch): 每顶点按烘焙权重+相位做正弦位移(尖端摆动大)
//  3) 叶片摆动(Leaf):   叶片绕锚点(basePos)做小角度旋转(ripple/tumble)
// 全部在顶点着色器完成, CPU 只上传少量 uniform + 时间。阴影 pass 不施加风力(静态)。
struct WindParams {
    bool  enabled        = true;
    float dirAngleDeg    = 30.0f;   // 风向(绕Y轴, 度)
    float strength       = 1.0f;    // 全局强度总倍增
    float globalStrength = 0.18f;   // 全局摆动幅度
    float globalFreq     = 0.9f;    // 全局摆动频率
    float branchStrength = 0.06f;   // 枝条颤动幅度
    float branchFreq     = 2.2f;    // 枝条颤动频率
    float leafStrength   = 0.15f;   // 叶片旋转幅度(弧度)
    float leafFreq       = 5.0f;    // 叶片旋转频率
};

class Renderer {
public:
    void init();
    void shutdown();

    void uploadTreeMesh(const TreeMeshData& data);
    void render(const OrbitCamera& camera, float aspect, bool wireframe);

    // 屏幕拾取: ndc∈[-1,1] 的鼠标坐标, 返回命中三角形所属节点 id(未命中=INVALID_NODE)。
    uint32_t pickNode(const OrbitCamera& camera, float aspect,
                      float ndcX, float ndcY) const;

    LightingParams lighting;
    WindParams     wind;
    float          windTime = 0.0f;  // 每帧由 Application 更新为 glfwGetTime()

    // 后处理抗锯齿模式。专治叶片 alpha-test 硬边在交融处的锯齿/闪烁。
    //   None: 仅靠 MSAA(对 alpha-test 边缘无效)
    //   FXAA: 单帧边缘模糊, 锐利无拖影, 亚像素闪烁抑制弱
    //   TAA : 逐帧亚像素抖动 + 深度重投影历史混合, 静态最干净, 运动时可能轻微拖影
    enum class AAMode { None = 0, FXAA = 1, TAA = 2 };
    AAMode aaMode = AAMode::TAA;
    void  resetTaaHistory() { m_taaHaveHistory = false; }

    // 骨骼可视化: 把导入枝干的骨架画成线段, 每级(按父链深度)一种颜色, 方便查看绑定。
    // 关深度测试, 骨骼恒浮于网格之上。仅当 skinBase 有骨架时有内容。
    bool showSkeleton = false;

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

    // 实例化叶批次: 一份原型网格(VAO 内含 proto VBO + EBO + 每实例 VBO), 用 glDrawElementsInstanced
    // 一次画完所有实例。相比逐顶点烘焙, CPU 生成量与 GPU 上传量都降到 (1份原型 + 实例数组)。
    // 每实例数据布局(10 float): pos(3) + rot四元数(4) + scale(3)。
    struct GpuInstancedBatch {
        GLuint vao = 0, vboProto = 0, ebo = 0, vboInst = 0;
        int    instanceCount = 0;
        // 每材质段一个 draw call(共用同一 VAO/实例 VBO): 索引偏移/数量 + 材质 + 贴图。
        struct Sub {
            int indexOffset = 0;   // 以索引个数计
            int indexCount  = 0;
            MaterialParams material;
            std::shared_ptr<Texture> texBaseColor;
            std::shared_ptr<Texture> texRoughness;
            std::shared_ptr<Texture> texNormal;
            std::shared_ptr<Texture> texOpacity;
        };
        std::vector<Sub> subs;
        void destroy();
    };

    std::vector<GpuBatch> m_batches;
    std::vector<GpuInstancedBatch> m_instBatches;   // 散布叶实例化批次
    // 贴图缓存：路径(+sRGB标志) → 已加载纹理。重建网格时命中缓存直接复用, 不再重复解码。
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_texCache;
    std::shared_ptr<Texture> getTexture(const std::string& path, bool sRGB);

    Shader m_branchShader;
    Shader m_leafShader;
    Shader m_leafInstShader;   // 散布叶实例化渲染
    Shader m_gridShader;
    Shader m_skyShader;
    Shader m_depthShader;   // 阴影深度 pass
    Shader m_groundShader;  // 地面(接收投影)
    Shader m_outlineShader; // 选中描边(沿法线外扩纯色)
    Shader m_taaShader;     // TAA resolve(全屏)
    Shader m_fxaaShader;    // FXAA(全屏)
    Shader m_presentShader; // 呈现/拷贝(全屏)
    Shader m_boneShader;    // 骨骼线段(逐顶点颜色)
    Mesh   m_gridMesh;
    Mesh   m_groundMesh;    // 地面平面
    Mesh   m_hlMesh;        // 选中节点高亮描边(pos+normal)
    int    m_hlIndexCount = 0;
    Mesh   m_boneMesh;      // 骨骼线段(pos+color); 每帧上传变化不大, 随 uploadTreeMesh 重建
    int    m_boneVertCount = 0;
    std::vector<TreeMeshData::PickTri> m_pickTris;  // 鼠标射线拾取用的三角形
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

    // ---- TAA ----
    // scene FBO: 单采样离屏, 存本帧场景颜色(HDR-ish RGB16F)+深度纹理(供重投影)。
    // history[2]: ping-pong 历史颜色。resolve 时读 prev, 写 cur, 呈现后交换。
    GLuint m_taaSceneFbo   = 0;
    GLuint m_taaColorTex   = 0;   // 本帧场景颜色
    GLuint m_taaDepthTex   = 0;   // 本帧深度(采样用)
    GLuint m_taaHistFbo[2] = {0, 0};
    GLuint m_taaHistTex[2] = {0, 0};
    int    m_taaW = 0, m_taaH = 0;
    int    m_taaCur = 0;          // 当前写入的 history 槽
    int    m_taaFrame = 0;        // 帧计数(驱动 Halton 抖动序列)
    bool   m_taaHaveHistory = false;
    glm::mat4 m_taaPrevViewProj = glm::mat4(1.0f);
    GLuint m_fsVao = 0;           // 全屏三角空 VAO
    void ensureTaaTargets(int w, int h);   // 按视口尺寸(重)建 TAA 目标
    void destroyTaaTargets();

    void setLightUniforms(Shader& sh);
    void setWindUniforms(Shader& sh);
    void bindBatchTextures(Shader& sh, GpuBatch& gb);
    // 场景内容绘制(天空/网格/枝叶/地面/高亮), proj 可含 TAA 抖动, view/camera 用于天空与地面。
    void renderSceneContent(const OrbitCamera& camera, float aspect,
                            const glm::mat4& proj, const glm::mat4& view, bool wireframe);
    void buildGrid();
    void buildGround();
    void renderGrid(const glm::mat4& vp);
    void renderGround(const glm::mat4& vp, const glm::vec3& camPos);
    void renderSky(const OrbitCamera& camera, float aspect);
    void renderHighlight(const glm::mat4& vp);   // 选中节点子树黄色线框叠加
    void renderSkeleton(const glm::mat4& vp);    // 骨骼线段(每级一色)
    void buildSkeletonMesh(const TreeMeshData& data);  // 由 skeleton 生成线段+颜色
};
