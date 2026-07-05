#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

enum class NodeType { Trunk, Branch, Twig, LeafCluster };

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
};

struct TrunkParams {
    float length      = 5.0f;
    float startRadius = 0.35f;
    float endRadius   = 0.12f;
    float taper       = 0.7f;
    float baseFlare   = 1.4f;
    float curvature   = 0.06f;  // 保留（用于贝塞尔模式）
    float bendAngle   = 18.0f;  // 折段弯曲：每段最大折角(度)
    int   bendCount   = 3;      // 折弯段数（0=不弯，越多越苍劲）
    int   sides       = 8;
    int   lengthSegs  = 8;
    int   seed        = 1;
    MaterialParams material = {{0.38f,0.22f,0.10f}, 0.85f, 0.0f, 0.5f, 0.0f};
};

struct BranchParams {
    float lengthRatio  = 0.55f;
    float startRadius  = 0.08f;
    float endRadius    = 0.02f;
    float curvature    = 0.08f;
    float spreadAngle  = 50.0f;
    float rotateOffset = 137.5f;
    float gravity      = 0.18f;
    float bendAngle    = 22.0f;  // 折段弯曲最大折角
    int   bendCount    = 2;      // 折弯段数
    int   branchCount  = 4;
    int   sides        = 6;
    int   lengthSegs   = 4;
    int   seed         = 2;
    MaterialParams material = {{0.32f,0.18f,0.08f}, 0.88f, 0.0f, 0.55f, 0.0f};
};

struct TwigParams {
    float lengthRatio  = 0.4f;
    float startRadius  = 0.025f;
    float endRadius    = 0.006f;
    float curvature    = 0.12f;
    float spreadAngle  = 65.0f;
    float rotateOffset = 137.5f;
    float gravity      = 0.25f;
    float bendAngle    = 28.0f;  // 折段弯曲最大折角
    int   bendCount    = 2;      // 折弯段数
    int   twigCount    = 5;
    int   sides        = 5;
    int   lengthSegs   = 3;
    bool  alternating  = true;
    int   seed         = 3;
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
