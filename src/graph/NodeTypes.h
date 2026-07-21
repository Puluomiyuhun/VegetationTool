#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// io/MeshImport.h 里定义; 这里只前向声明, 供导入节点缓存已加载网格(避免头文件循环包含)。
struct ImportedMesh;

// 枚举序列化为 int, 末尾追加新类型以保持旧工程兼容(切勿在中间插入)。
enum class NodeType { Trunk, Roots, Branch, Twig, LeafCluster, Spine, Frond, Export, Custom,
                      ImportTrunk, ImportLeaf, Scatter };

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
    float sizeFalloff   = 0.0f;  // 沿父级向上的长度衰减[0,1]: 越靠上的枝条越短(0=不衰减)
    float noiseAmount  = 30.0f;  // 样条噪声扰动强度(度)
    float noiseFreq    = 3.0f;   // 噪声频率
    float gnarl        = 10.0f;  // 螺旋扭曲总角度(度)
    int   jointCount   = 0;      // 竹节数(0=无节): 用于竹类分枝的周期性膨大
    float jointBulge   = 0.0f;   // 竹节膨大幅度(半径倍增比例)
    int   branchCount  = 4;
    // Interval 模式(竹节式): 在父级[regionStart,regionEnd]内每隔 intervalSpacing(归一化间距)
    // 设一个"节", 每个节上环绕生长 branchesPerNode 根枝条(竹枝只长在竹节上)。
    float intervalSpacing  = 0.12f; // 相邻节的归一化间距(占父级长度比例)
    int   branchesPerNode  = 2;     // 每个节上环绕生长的枝条数
    int   sides        = 6;
    int   lengthSegs   = 6;
    int   seed         = 2;
    float uvTilingU    = 1.0f;   // 树皮纹理沿枝条周向的平铺次数
    float uvTilingV    = 2.0f;   // 树皮纹理沿枝条长度的平铺次数
    // ---- Variance(每根实例随机偏移, 绝对±范围; 0=关闭, 与该参数同单位) ----
    float lengthRatioVar = 0.0f; // 长度比例抖动 ±
    float radiusScaleVar = 0.0f; // start半径比例抖动 ±
    float endRatioVar    = 0.0f; // 末端锥度抖动 ±
    float spreadAngleVar = 0.0f; // 张开角抖动 ±(度)
    float gravityVar     = 0.0f; // 重力下垂抖动 ±
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
    // ---- Variance(每根实例随机偏移, 绝对±范围; 0=关闭) ----
    float lengthRatioVar = 0.0f;
    float radiusScaleVar = 0.0f;
    float endRatioVar    = 0.0f;
    float spreadAngleVar = 0.0f;
    float gravityVar     = 0.0f;
    MaterialParams material = {{0.28f,0.16f,0.07f}, 0.9f, 0.0f, 0.6f, 0.0f};
};

struct LeafClusterParams {
    int         leafCount     = 20;
    float       clusterRadius = 0.35f;
    float       leafSize      = 0.22f;
    float       leafAspect    = 0.65f;  // 叶片宽/高比: 小值=细长叶(竹叶/蕨类小叶), 大值=宽叶
    float       normalJitter  = 0.35f;
    float       normalSoften  = 0.0f;   // 叶片法线软化[0,1]: 0=平面卡片法线, 1=从叶簇轴心指向叶片的球形法线(树冠体积感/柔和受光)
    bool        planar        = false;  // true=平面羽状排列(蕨类叶轴两侧), false=黄金角3D辐射
    float       sizeFalloff   = 0.0f;   // 沿叶轴向梢部的缩小比例[0,1]: 蕨类小叶向尖端渐小
    int         seed          = 4;
    MaterialParams material = {{0.15f,0.48f,0.06f}, 0.75f, 0.0f, 0.4f, 0.45f};

    // ---- 轮廓裁剪网格(对标 SpeedTree Mesh Cutout) ----
    // 用一张贴合叶子剪影的三角网格代替整块四边形卡片, 大幅减少透明像素的 overdraw。
    // 点坐标为叶片局部 UV 空间[0,1](与四边形卡片相同的 U=横向, V=纵向, V=0 底 V=1 顶),
    // 生成时映射到每张叶卡的平面。cutoutTris 为三角形索引(每3个一组)。
    bool                   useCutout = false;    // 启用轮廓网格(需 cutoutTris 非空, 否则退回四边形)
    std::vector<glm::vec2> cutoutPoints;         // 三角化后完整顶点(致密边界+内部撒点)
    std::vector<uint32_t>  cutoutTris;           // 三角形索引
    std::vector<glm::vec2> cutoutRing;           // 可编辑边界环(仅编辑器用, 不参与生成)
    bool                   requestEditCutout = false;  // 瞬时: UI 点击"编辑轮廓"后置 true, 打开编辑器后清零
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
    // ---- Variance(每根实例随机偏移, 绝对±范围; 0=关闭) ----
    float lengthVar      = 0.0f; // 根长度抖动 ±(世界单位)
    float radiusScaleVar = 0.0f;
    float endRatioVar    = 0.0f;
    float spreadAngleVar = 0.0f; // 张开角抖动 ±(度)
    float gravityVar     = 0.0f;
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
    // ---- Variance(每根实例随机偏移, 绝对±范围; 0=关闭) ----
    float lengthRatioVar = 0.0f;
    float radiusScaleVar = 0.0f;
    float endRatioVar    = 0.0f;
    float spreadAngleVar = 0.0f; // 抬起角抖动 ±(度)
    float gravityVar     = 0.0f;
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

    // ---- 轮廓裁剪网格(对标 SpeedTree Mesh Cutout) ----
    // 与 LeafCluster 相同: 用贴合叶形的三角网格代替整块叶带矩形, 减少透明像素 overdraw。
    // 点坐标为叶带局部 UV[0,1](U=横向 lateral, V=沿脊线 t, V=0 基部 V=1 梢部),
    // 生成时映射到叶带曲面。cutoutTris 为三角形索引(每3个一组)。
    bool                   useCutout = false;    // 启用轮廓网格(需 cutoutTris 非空, 否则退回整片叶带)
    std::vector<glm::vec2> cutoutPoints;         // 三角化后完整顶点(致密边界+内部撒点)
    std::vector<uint32_t>  cutoutTris;           // 三角形索引
    std::vector<glm::vec2> cutoutRing;           // 可编辑边界环(仅编辑器用, 不参与生成)
    bool                   requestEditCutout = false;  // 瞬时: UI 点击"编辑轮廓"后置 true, 打开编辑器后清零
};

// Export：模型导出节点。输入连接图中任意一个节点。不参与几何生成。
// 两种导出模式:
//  - exportWhole=true : 从上游节点向上追溯到根 Trunk, 导出完整整株模型。
//  - exportWhole=false: 以上游节点为"标本根", 导出该节点+其全部下游子枝叶组成的
//    竖直向上标本(根部在原点, 主枝沿 +Y 挺立), 供 UE5.8 PCG 程序化种树用作枝叶标本。
struct ExportParams {
    std::string path = "tree_export.obj";  // 导出文件路径
    int         format = 0;                // 导出格式: 0=OBJ(+mtl); 1=FBX(ASCII); 2=USD(Nanite 程序集)
    // 导出模式: 0=当前节点及下游(竖直标本); 1=整株(追溯到根 Trunk); 2=当前节点及上游(祖先链)
    int         exportMode = 0;
    int         specimenCount = 1;         // 标本模式: 用不同随机种子生成的变体数量
    bool        singleFile = true;         // true=全部并排合到一个 obj; false=每变体一个文件(_序号后缀)
    float       specimenSpacing = 3.0f;    // 单文件并排时相邻标本的间距(沿 X 轴)
    bool        requestExport = false;     // 瞬时标志: UI 点击后置 true, 导出完成清零
};

// Custom：脚本自定义枝条节点(对标并超越 SpeedTree)。用户用 Lua 编写 generate(ctx) 函数，
// 返回一个枝条数组，每根枝条给出 {t, azimuth, elevation, length, radius, endRatio}，
// 引擎沿用与 Branch 相同的圆柱/枝领/子节点递归管线生成几何。脚本只产出参数(不碰内存)，
// 因此安全且能自动接入风力/拾取/高亮/导出。
// ctx 字段: count(用户可调整数), parentLen, parentRadius(附着半径), depth, seed,
//           以及工具函数 rand()/rand(a,b)/noise(x)。
inline const char* kDefaultCustomScript =
    "-- generate(ctx) 返回枝条数组\n"
    "-- 每根枝条: {t=附着位置[0,1], azimuth=方位角(度), elevation=仰角(度),\n"
    "--            length=长度, radius=半径倍数, endRatio=末端锥度}\n"
    "function generate(ctx)\n"
    "  local out = {}\n"
    "  for i = 0, ctx.count - 1 do\n"
    "    local f = i / math.max(1, ctx.count - 1)\n"
    "    out[#out+1] = {\n"
    "      t         = 0.2 + 0.75 * f,\n"
    "      azimuth   = i * 137.5,\n"
    "      elevation = 50.0,\n"
    "      length    = ctx.parentLen * 0.5 * (1.0 - 0.3 * f),\n"
    "      radius    = 1.0,\n"
    "      endRatio  = 0.25,\n"
    "    }\n"
    "  end\n"
    "  return out\n"
    "end\n";

struct CustomParams {
    std::string script;          // Lua 脚本(含 generate 函数)。空则用默认模板。
    int   count        = 6;      // 传给脚本 ctx.count 的整数(枝条数/迭代次数)
    // 几何管线参数(与 Branch 一致): 脚本只给逻辑，这些控制截面/锥度/扰动/风格。
    float baseFlare    = 2.2f;   // 枝领裙边外扩倍数
    float taperPow     = 1.5f;   // 锥度曲线幂
    float gravity      = 0.18f;  // 重力下垂
    float noiseAmount  = 30.0f;  // 样条噪声扰动强度(度)
    float noiseFreq    = 3.0f;   // 噪声频率
    float gnarl        = 10.0f;  // 螺旋扭曲总角度(度)
    int   sides        = 6;      // 截面边数
    int   lengthSegs   = 6;      // 长度细分
    int   seed         = 8;      // 随机种子(传给脚本 ctx.seed)
    float uvTilingU    = 1.0f;
    float uvTilingV    = 2.0f;
    MaterialParams material = {{0.32f,0.18f,0.08f}, 0.88f, 0.0f, 0.55f, 0.0f};

    mutable std::string lastError;       // 瞬时: 最近一次脚本执行的错误信息(空=成功), 仅UI显示用
};

// ---- FBX 导入 / 散布(SpeedTree → SlowTree → UE USD 桥接) ----
//
// ImportTrunk: 作为一株的"根"节点(等价 Trunk 但几何来自导入 FBX)。加载带骨骼的枝干网格,
//   原样渲染并把骨骼链换算成 rings 供下游 Scatter 沿其撒叶。
// ImportLeaf:  加载一小片"枝叶单体"网格作为散布原型(独立预览用, 也可被 Scatter 引用)。
// Scatter:     ImportTrunk 的子节点。沿父级 rings(骨骼链)撒 N 个叶实例, 每实例记录
//   (position, orientation, scale), 引用一份叶原型网格。导出 USD 时成为 PointInstancer(省内存)。
//
// 加载态: fbxPath 变更或 requestReload 置位时, Application/Generator 调 MeshImport::loadFBX
//   填充 cached(shared_ptr<ImportedMesh>); 生成/渲染/导出复用同一份, 不重复解码磁盘。

struct ImportTrunkParams {
    std::string fbxPath;                 // 枝干 FBX 绝对路径(空=未导入)
    float       scale       = 1.0f;      // 整体缩放(FBX 单位→场景单位)
    float       posX        = 0.0f;      // 场景摆放 X(多株并存)
    float       posZ        = 0.0f;      // 场景摆放 Z
    MaterialParams material = {{0.38f,0.22f,0.10f}, 0.85f, 0.0f, 0.5f, 0.0f};

    // 运行态缓存(不序列化): 已加载网格 + 加载状态。requestReload 由 UI 置位。
    mutable std::shared_ptr<ImportedMesh> cached;
    mutable std::string loadError;       // 空=成功
    mutable bool        requestReload = false;
};

struct ImportLeafParams {
    std::string fbxPath;                 // 叶单体 FBX 绝对路径(空=未导入)
    float       scale       = 1.0f;      // 整体缩放
    MaterialParams material = {{0.15f,0.48f,0.06f}, 0.75f, 0.0f, 0.4f, 0.45f};

    mutable std::shared_ptr<ImportedMesh> cached;
    mutable std::string loadError;
    mutable bool        requestReload = false;
};

struct ScatterParams {
    // 叶原型变体: 可导入多个"实例枝"变体, 散布时每根末端细枝均匀随机选一个变体。
    // 各变体共用外层 material。变体列表为空或全部加载失败时不撒叶。
    struct Variant {
        std::string fbxPath;                       // 该变体 FBX 绝对路径
        // 枝干子网格索引: 实例枝含 2 个材质时, 把某个 part 判为"枝干"(复用父级 Trunk 材质),
        // 其余 part 作叶子(用 FBX 自带材质)。-1 = 全部当叶子(单材质/无枝干)。
        int         trunkPart = -1;
        mutable std::shared_ptr<ImportedMesh> cached;
        mutable std::string loadError;
        mutable bool        requestReload = false;
    };
    std::vector<Variant> variants;       // 叶原型变体(散布用的枝叶单体, 均匀随机选取)

    int         count       = 3;         // 每根末端细枝上撒的叶实例数
    float       leafScale    = 1.0f;     // 每片叶缩放(相对原型)
    float       leafScaleVar = 0.0f;     // 缩放抖动 ±
    float       regionStart  = 0.1f;     // 沿细枝[start,end]区间撒叶
    float       regionEnd     = 1.0f;
    float       spreadAngle   = 55.0f;   // 叶朝向相对枝干径向的抬起抖动上限(度)
    float       normalJitter  = 0.4f;    // 朝向随机抖动强度
    float       spiralStep    = 137.5f;  // 沿枝推进每片叶方位角步进(度), 螺旋式散布生长
    float       tipScale      = 1.0f;    // 近枝尖叶相对缩放(近根=1, 近尖=tipScale; <1 越尖越小)
    int         seed          = 100;
    MaterialParams material = {{0.15f,0.48f,0.06f}, 0.75f, 0.0f, 0.4f, 0.45f};
};
