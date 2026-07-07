#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

enum class NodeType { Trunk, Roots, Branch, Twig, LeafCluster, Spine, Frond };

// Branch 分布模式(对标 SpeedTree Generation Mode)。目前仅实现 Classic(=现有分布行为)，
// 其余模式先占位保留枚举与序号, 待后续逐个实现。
enum class BranchMode {
    Classic = 0,        // 现有行为: 固定条数, 黄金角方位 + [regionStart,regionEnd]区间均匀附着
    Proportional,       // 数量正比父级长度
    ProportionalSteps,  // 同上, 数量取整数档
    Absolute,           // 指定绝对数量
    AbsoluteSteps,      // 绝对数量的阶跃版
    Phyllotaxy,         // 叶序(黄金角螺旋)
    Interval,           // 沿父级固定间距
    Bifurcation,        // 末端二叉分叉(Y形)
    Flood,              // 表面泛生(藤蔓/苔藓)
    Parent              // 继承父级分布
};

// PBR 材质参数（每个节点独立设置）
struct MaterialParams {
    glm::vec3 albedo      = {0.45f, 0.28f, 0.12f};
    float     roughness   = 0.8f;
    float     metallic    = 0.0f;
    float     aoStrength  = 0.6f;
    float     sssStrength = 0.0f;  // 次表面散射（叶片用）

    // 贴图路径（空字符串=不使用）
    std::string baseColorTex;   // 替代 albedo
    std::string roughnessTex;   // R通道=roughness, G通道=metallic（可选）
    std::string normalTex;      // 切线空间法线贴图
    std::string opacityTex;     // 不透明度遮罩(R通道)，用于叶片alpha剔除
    float       alphaCutoff = 0.5f;  // alpha剔除阈值：低于此值的片元被丢弃
};

struct TrunkParams {
    float length      = 5.0f;
    float startRadius = 0.35f;
    float endRadius   = 0.12f;
    float baseFlare   = 1.4f;
    float posX        = 0.0f;   // 植株在场景中的位置X(用于一个工程内摆放多棵植被)
    float posZ        = 0.0f;   // 植株在场景中的位置Z
    float noiseAmount = 25.0f;  // 样条噪声扰动强度(度): 让枝干自然扭曲而非笔直
    float noiseFreq   = 2.5f;   // 噪声频率: 越大扭动越频繁
    float gnarl       = 15.0f;  // 螺旋扭曲总角度(度): 树皮沿长度旋拧的苍劲感
    float taperPow    = 1.6f;   // 锥度曲线幂(1=线性, >1基部饱满/末端尖锐)
    int   jointCount  = 0;      // 竹节数(0=无节): >0 时沿长度周期性膨大, 配合直竿做竹子
    float jointBulge  = 0.0f;   // 竹节膨大幅度(半径倍增比例)
    int   sides       = 8;
    int   lengthSegs  = 10;
    int   seed        = 1;
    float uvTilingU   = 1.0f;   // 树皮纹理沿枝干周向的平铺次数
    float uvTilingV   = 3.0f;   // 树皮纹理沿枝干长度的平铺次数
    MaterialParams material = {{0.38f,0.22f,0.10f}, 0.85f, 0.0f, 0.5f, 0.0f};
};

struct BranchParams {
    BranchMode mode = BranchMode::Classic;  // 分布模式(目前仅 Classic 生效)
    float lengthRatio  = 0.55f;
    float radiusScale  = 1.0f;   // start半径 = 父级附着点半径 × 此比例(1.0=完全贴合)
    float endRatio     = 0.25f;  // 末端半径 = 自身start半径 × 此比例(锥度)
    float baseFlare    = 2.2f;   // 枝领裙边外扩倍数(1=无枝领, 越大裙边越宽)
    float taperPow     = 1.5f;   // 锥度曲线幂(1=线性, >1基部饱满/末端尖锐)
    float spreadAngle  = 50.0f;
    float rotateOffset = 137.5f;
    float gravity      = 0.18f;
    float regionStart  = 0.2f;   // 在父级[start,end]区间内生长, 此区间外的枝条剔除
    float regionEnd    = 0.95f;
    float noiseAmount  = 30.0f;  // 样条噪声扰动强度(度)
    float noiseFreq    = 3.0f;   // 噪声频率
    float gnarl        = 10.0f;  // 螺旋扭曲总角度(度)
    int   jointCount   = 0;      // 竹节数(0=无节): 用于竹类分枝的周期性膨大
    float jointBulge   = 0.0f;   // 竹节膨大幅度(半径倍增比例)
    int   branchCount  = 4;
    int   sides        = 6;
    int   lengthSegs   = 6;
    int   seed         = 2;
    float uvTilingU    = 1.0f;   // 树皮纹理沿枝条周向的平铺次数
    float uvTilingV    = 2.0f;   // 树皮纹理沿枝条长度的平铺次数
    MaterialParams material = {{0.32f,0.18f,0.08f}, 0.88f, 0.0f, 0.55f, 0.0f};
};

struct TwigParams {
    float lengthRatio  = 0.4f;
    float radiusScale  = 1.0f;   // start半径 = 父级附着点半径 × 此比例
    float endRatio     = 0.25f;  // 末端半径 = 自身start半径 × 此比例
    float baseFlare    = 1.8f;   // 枝领裙边外扩倍数
    float taperPow     = 1.3f;   // 锥度曲线幂
    float spreadAngle  = 65.0f;
    float rotateOffset = 137.5f;
    float gravity      = 0.25f;
    float regionStart  = 0.2f;   // 在父级[start,end]区间内生长, 此区间外的细枝剔除
    float regionEnd    = 0.95f;
    float noiseAmount  = 35.0f;  // 样条噪声扰动强度(度)
    float noiseFreq    = 3.5f;   // 噪声频率
    float gnarl        = 8.0f;   // 螺旋扭曲总角度(度)
    int   twigCount    = 5;
    int   sides        = 5;
    int   lengthSegs   = 5;
    bool  alternating  = true;
    int   seed         = 3;
    float uvTilingU    = 1.0f;   // 树皮纹理沿细枝周向的平铺次数
    float uvTilingV    = 1.0f;   // 树皮纹理沿细枝长度的平铺次数
    MaterialParams material = {{0.28f,0.16f,0.07f}, 0.9f, 0.0f, 0.6f, 0.0f};
};

struct LeafClusterParams {
    int         leafCount     = 20;
    float       clusterRadius = 0.35f;
    float       leafSize      = 0.22f;
    float       leafAspect    = 0.65f;  // 叶片宽/高比: 小值=细长叶(竹叶/蕨类小叶), 大值=宽叶
    float       normalJitter  = 0.35f;
    bool        planar        = false;  // true=平面羽状排列(蕨类叶轴两侧), false=黄金角3D辐射
    float       sizeFalloff   = 0.0f;   // 沿叶轴向梢部的缩小比例[0,1]: 蕨类小叶向尖端渐小
    int         seed          = 4;
    MaterialParams material = {{0.15f,0.48f,0.06f}, 0.75f, 0.0f, 0.4f, 0.45f};
};

// 根系：从树干基部向外辐射铺开，再向下弯曲扎入地下，末端收细成尖。
struct RootsParams {
    int   rootCount   = 7;      // 主根条数
    float length      = 3.5f;   // 每条根总长度
    float radiusScale = 0.9f;   // 根基半径 = 树干基部半径 × 此比例
    float endRatio    = 0.08f;  // 末端半径比例(收细成尖)
    float taperPow    = 1.8f;   // 锥度曲线幂(基部饱满、末端尖锐)
    float baseFlare   = 2.5f;   // 根基与树干接壤处的枝领裙边外扩倍数
    float collarSink  = 0.3f;   // 根盘外圈向树干内收陷的幅度(0=不收,越大外圈越往里陷,随距圆心距离加大)
    float spreadAngle = 78.0f;  // 起始张开角(度): 0=贴树干向上, 90=水平铺开, >90=朝下俯冲入地
    float gravity     = 0.75f;  // 下扎强度(沿长度逐渐转向地下, 等价于枝条gravity)
    float rotateOffset= 137.5f; // 各根方位偏移(度)
    float noiseAmount = 40.0f;  // 样条噪声扰动强度(度)
    float noiseFreq   = 3.0f;   // 噪声频率
    float gnarl       = 12.0f;  // 螺旋扭曲总角度(度)
    int   jointCount  = 0;      // 节数(0=无节): 用于根部周期性膨大
    float jointBulge  = 0.0f;   // 节膨大幅度(半径倍增比例)
    int   sides       = 6;
    int   lengthSegs  = 10;
    int   seed        = 5;
    float uvTilingU   = 1.0f;
    float uvTilingV   = 3.0f;
    MaterialParams material = {{0.30f,0.19f,0.10f}, 0.9f, 0.0f, 0.55f, 0.0f};
};

// Spine：蕨叶/羽叶的"叶轴"引导线。像细枝一样生成一条可弯曲的中心样条(受 gravity 下垂、
// noise/gnarl 扰动)，渲染成一根细茎，并把 rings 传给 Frond 沿其铺开连续叶带。
struct SpineParams {
    float lengthRatio  = 0.6f;   // 叶轴长度 = 父级长度 × 此比例
    float radiusScale  = 0.4f;   // 叶轴基部半径 = 父级附着点半径 × 此比例(叶轴通常细)
    float endRatio     = 0.2f;   // 末端半径比例(向梢收细)
    float taperPow     = 1.2f;   // 锥度曲线幂
    float spreadAngle  = 55.0f;  // 相对父级的抬起角(度)
    float rotateOffset = 137.5f; // 各叶轴方位偏移(度)
    float gravity      = 0.35f;  // 沿长度的下垂弧度(蕨叶自然下弯)
    float regionStart  = 0.1f;   // 在父级[start,end]区间内生长
    float regionEnd    = 0.95f;
    float noiseAmount  = 12.0f;  // 样条噪声扰动强度(度)
    float noiseFreq    = 2.0f;   // 噪声频率
    float gnarl        = 5.0f;   // 螺旋扭曲总角度(度)
    int   spineCount   = 5;      // 生成的叶轴条数
    int   sides        = 5;      // 茎截面边数
    int   lengthSegs   = 12;     // 长度细分(越多脊线越平滑, Frond 叶带也越圆顺)
    int   seed         = 6;
    float uvTilingU    = 1.0f;
    float uvTilingV    = 1.0f;
    MaterialParams material = {{0.22f,0.42f,0.10f}, 0.7f, 0.0f, 0.45f, 0.2f};
};

// Frond：沿父级(Spine)脊线生成一条连续带状叶片网格。宽度沿长度按叶形轮廓变化
// (基部窄→中部最宽→梢部收尖)，左右外扩成一整片羽叶/蕨叶，贴 alpha 叶纹理。
// 区别于 LeafCluster 的离散小卡片: Frond 是"沿曲线拉伸的连续叶带"。
struct FrondParams {
    float width       = 0.5f;   // 叶带最大半宽(单侧外扩)
    float widthBase   = 0.15f;  // 基部宽度比例[0,1]: 叶轴根部的相对宽度
    float widthTip    = 0.0f;   // 梢部宽度比例[0,1]: 叶轴尖端的相对宽度(0=收成尖)
    float profilePow  = 0.6f;   // 叶形轮廓幂: 控制最宽处的位置与饱满度(小=靠基部饱满)
    float curl        = 0.0f;   // 沿脊线的横向卷曲(叶面向上/下弯, 值域[-1,1])
    int   segsPerSide = 1;      // 每侧横向细分段数(1=左右各一片, 大=叶缘更平滑)
    bool  serrate     = false;  // 叶缘锯齿(羽状复叶的裂片感)
    float serrateDepth= 0.25f;  // 锯齿深度比例
    int   seed        = 7;
    MaterialParams material = {{0.16f,0.50f,0.07f}, 0.72f, 0.0f, 0.4f, 0.5f};
};
