#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

enum class NodeType { Trunk, Roots, Branch, Twig, LeafCluster };

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
    float noiseAmount = 25.0f;  // 样条噪声扰动强度(度): 让枝干自然扭曲而非笔直
    float noiseFreq   = 2.5f;   // 噪声频率: 越大扭动越频繁
    float gnarl       = 15.0f;  // 螺旋扭曲总角度(度): 树皮沿长度旋拧的苍劲感
    float taperPow    = 1.6f;   // 锥度曲线幂(1=线性, >1基部饱满/末端尖锐)
    int   sides       = 8;
    int   lengthSegs  = 10;
    int   seed        = 1;
    float uvTiling    = 3.0f;   // 树皮纹理沿枝干长度的平铺次数
    MaterialParams material = {{0.38f,0.22f,0.10f}, 0.85f, 0.0f, 0.5f, 0.0f};
};

struct BranchParams {
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
    int   branchCount  = 4;
    int   sides        = 6;
    int   lengthSegs   = 6;
    int   seed         = 2;
    float uvTiling     = 2.0f;   // 树皮纹理沿枝条长度的平铺次数
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
    float uvTiling     = 1.0f;   // 树皮纹理沿细枝长度的平铺次数
    MaterialParams material = {{0.28f,0.16f,0.07f}, 0.9f, 0.0f, 0.6f, 0.0f};
};

struct LeafClusterParams {
    int         leafCount     = 20;
    float       clusterRadius = 0.35f;
    float       leafSize      = 0.22f;
    float       normalJitter  = 0.35f;
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
    float spreadAngle = 78.0f;  // 起始张开角(度): 接近90=先水平铺开
    float droop       = 0.75f;  // 下扎强度: 沿长度逐渐转向地下
    float rotateOffset= 137.5f; // 各根方位偏移(度)
    float noiseAmount = 40.0f;  // 样条噪声扰动强度(度)
    float noiseFreq   = 3.0f;   // 噪声频率
    float gnarl       = 12.0f;  // 螺旋扭曲总角度(度)
    int   sides       = 6;
    int   lengthSegs  = 10;
    int   seed        = 5;
    float uvTiling    = 3.0f;
    MaterialParams material = {{0.30f,0.19f,0.10f}, 0.9f, 0.0f, 0.55f, 0.0f};
};
