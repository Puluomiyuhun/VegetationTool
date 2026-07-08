#pragma once
#include "graph/NodeGraph.h"
#include "renderer/Texture.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// SpeedTree "Mesh Cutout" 风格的叶片轮廓编辑器(独立窗口)。
// 在叶片贴图上手动打点勾勒剪影(或按 alpha 自动裁出), 三角化成贴合叶形的网格,
// 代替整块四边形卡片以降低透明像素 overdraw。数据以单条有序边界环(ring)存储,
// 三角形由 ring 做耳切(ear clipping)得到。坐标为叶片局部 UV[0,1](V=0 底, V=1 顶)。
class LeafCutoutEditor {
public:
    void open(NodeId nodeId);         // 请求打开并绑定目标 LeafCluster 节点
    bool isOpen() const { return m_open; }
    void render(NodeGraph& graph);    // 每帧绘制(仅当打开)

private:
    bool   m_open = false;
    NodeId m_targetId = INVALID_NODE;
    bool   m_pendingLoad = false;      // open() 后延迟到 render 里(需要 graph)载入

    Texture     m_tex;                // 显示用 GL 纹理
    std::string m_loadedPath;         // 已加载的贴图路径
    int         m_texW = 0, m_texH = 0;

    // 工作副本, Apply 时写回节点参数
    std::vector<glm::vec2> m_ring;    // 可编辑的有序边界环(手动模式: 用户打点/拖动)
    std::vector<std::vector<glm::vec2>> m_loops;  // 自动模式的多条轮廓环(含孔洞); 非空=自动模式
    std::vector<glm::vec2> m_verts;   // 三角化后的完整顶点(致密边界+内部撒点)
    std::vector<uint32_t>  m_tris;    // 三角形索引(每3个, 引用 m_verts)

    int       m_dragIdx = -1;
    int       m_dragLoop = -1;         // 拖动的环: -1=m_ring, >=0=m_loops[该下标]
    float     m_zoom = 1.0f;
    glm::vec2 m_pan  = {0.0f, 0.0f};   // 图像相对画布的平移(像素)
    bool      m_fitOnLoad = true;

    // 自动裁剪参数
    float m_threshold = 0.5f;
    float m_simplify  = 1.5f;          // Douglas-Peucker 容差(网格像素)
    float m_density   = 0.08f;         // 网格三角化的目标边长(叶片 UV), 越小越密
    bool  m_showMesh  = true;          // 显示/隐藏三角网格与顶点

    void loadFor(NodeGraph& graph);            // 载入目标节点贴图 + 工作副本
    void retriangulate();                       // 边界致密+内部撒点+Delaunay 三角化
    bool autoGenerate(const std::string& path); // alpha 自动裁出最大轮廓环
    void applyTo(NodeGraph& graph);             // 写回参数并标脏
};
