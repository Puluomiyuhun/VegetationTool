#include "LeafCutoutEditor.h"
#include "graph/Nodes.h"
#include <imgui.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <unordered_map>

// 轮廓网格参数的通用访问器: LeafCluster 与 Frond 拥有同名的 cutout 字段。
// 返回指向目标节点各字段的指针, 让编辑器无需关心具体节点类型。
namespace {
struct CutoutAccess {
    bool*                    useCutout = nullptr;
    std::vector<glm::vec2>*  cutoutPoints = nullptr;
    std::vector<uint32_t>*   cutoutTris = nullptr;
    std::vector<glm::vec2>*  cutoutRing = nullptr;
    MaterialParams*          material = nullptr;
    bool valid() const { return useCutout != nullptr; }
};

CutoutAccess getCutout(TreeNode* n) {
    CutoutAccess a;
    if (!n) return a;
    if (n->getType() == NodeType::LeafCluster) {
        auto& p = static_cast<LeafClusterNode*>(n)->params;
        a.useCutout = &p.useCutout; a.cutoutPoints = &p.cutoutPoints;
        a.cutoutTris = &p.cutoutTris; a.cutoutRing = &p.cutoutRing;
        a.material = &p.material;
    } else if (n->getType() == NodeType::Frond) {
        auto& p = static_cast<FrondNode*>(n)->params;
        a.useCutout = &p.useCutout; a.cutoutPoints = &p.cutoutPoints;
        a.cutoutTris = &p.cutoutTris; a.cutoutRing = &p.cutoutRing;
        a.material = &p.material;
    }
    return a;
}
} // namespace

// ============================ 几何工具 ============================
namespace {

float cross2(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTri(const glm::vec2& p, const glm::vec2& a,
                const glm::vec2& b, const glm::vec2& c) {
    float d1 = cross2(p, a, b), d2 = cross2(p, b, c), d3 = cross2(p, c, a);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

// 简单多边形耳切三角化(O(n^2))。ring 视作有序边界环, 输出三角形索引(引用 ring 下标)。
void earClip(const std::vector<glm::vec2>& ring, std::vector<uint32_t>& out) {
    out.clear();
    int n = (int)ring.size();
    if (n < 3) return;

    // 计算有向面积, 统一为逆时针(CCW), 使凸顶点判据 cross>0 成立
    float area = 0.0f;
    for (int i = 0; i < n; ++i) {
        const glm::vec2& a = ring[i];
        const glm::vec2& b = ring[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = (area < 0.0f) ? (n - 1 - i) : i;

    int guard = 0;
    while ((int)idx.size() > 3 && guard++ < 100000) {
        bool clipped = false;
        int m = (int)idx.size();
        for (int i = 0; i < m; ++i) {
            int i0 = idx[(i + m - 1) % m], i1 = idx[i], i2 = idx[(i + 1) % m];
            const glm::vec2& a = ring[i0];
            const glm::vec2& b = ring[i1];
            const glm::vec2& c = ring[i2];
            if (cross2(a, b, c) <= 0.0f) continue;   // 反凸/退化, 不是耳
            bool ok = true;
            for (int j = 0; j < m; ++j) {
                int pj = idx[j];
                if (pj == i0 || pj == i1 || pj == i2) continue;
                if (pointInTri(ring[pj], a, b, c)) { ok = false; break; }
            }
            if (!ok) continue;
            out.push_back((uint32_t)i0);
            out.push_back((uint32_t)i1);
            out.push_back((uint32_t)i2);
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;   // 退化多边形, 放弃剩余
    }
    if ((int)idx.size() == 3) {
        out.push_back((uint32_t)idx[0]);
        out.push_back((uint32_t)idx[1]);
        out.push_back((uint32_t)idx[2]);
    }
}

// 点是否在多边形内部(奇偶射线法)。poly 为有序边界环。
bool pointInPoly(const glm::vec2& p, const std::vector<glm::vec2>& poly) {
    bool in = false;
    int n = (int)poly.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const glm::vec2& a = poly[i];
        const glm::vec2& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12f) + a.x))
            in = !in;
    }
    return in;
}

// 沿边界环按目标边长 seg 插值致密化(不新增角点, 只在长边上补点)。
std::vector<glm::vec2> densifyRing(const std::vector<glm::vec2>& ring, float seg) {
    std::vector<glm::vec2> out;
    int n = (int)ring.size();
    if (n < 2 || seg <= 1e-5f) return ring;
    for (int i = 0; i < n; ++i) {
        const glm::vec2& a = ring[i];
        const glm::vec2& b = ring[(i + 1) % n];
        out.push_back(a);
        float len = glm::length(b - a);
        int sub = (int)std::floor(len / seg);
        for (int k = 1; k <= sub; ++k) {
            float t = (float)k / (float)(sub + 1);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

// Bowyer-Watson Delaunay 三角化(输入点集, 输出三角形索引引用 pts)。
void delaunay(const std::vector<glm::vec2>& pts, std::vector<uint32_t>& outTris) {
    outTris.clear();
    int n = (int)pts.size();
    if (n < 3) return;

    // 包围盒 → 超级三角形(其 3 个顶点追加在 pts 之后, 用局部副本)
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (auto& p : pts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    glm::vec2 c = (lo + hi) * 0.5f;
    float d = std::max(hi.x - lo.x, hi.y - lo.y);
    if (d < 1e-6f) d = 1.0f;
    d *= 20.0f;

    std::vector<glm::vec2> v = pts;
    int s0 = n, s1 = n + 1, s2 = n + 2;
    v.push_back({c.x - d, c.y - d});
    v.push_back({c.x + d, c.y - d});
    v.push_back({c.x,     c.y + d});

    struct Tri { int a, b, c; };
    auto circum = [&](int ia, int ib, int ic, const glm::vec2& p) -> bool {
        const glm::vec2& A = v[ia]; const glm::vec2& B = v[ib]; const glm::vec2& C = v[ic];
        float ax = A.x - p.x, ay = A.y - p.y;
        float bx = B.x - p.x, by = B.y - p.y;
        float cx = C.x - p.x, cy = C.y - p.y;
        float det = (ax * ax + ay * ay) * (bx * cy - cx * by)
                  - (bx * bx + by * by) * (ax * cy - cx * ay)
                  + (cx * cx + cy * cy) * (ax * by - bx * ay);
        // v 已归一化为 CCW? 不保证, 用有向面积符号规整
        float ori = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        return (ori > 0.0f) ? (det > 0.0f) : (det < 0.0f);
    };

    std::vector<Tri> tris;
    tris.push_back({s0, s1, s2});

    for (int i = 0; i < n; ++i) {
        const glm::vec2& p = v[i];
        std::vector<std::pair<int,int>> edges;
        for (int t = (int)tris.size() - 1; t >= 0; --t) {
            if (circum(tris[t].a, tris[t].b, tris[t].c, p)) {
                edges.push_back({tris[t].a, tris[t].b});
                edges.push_back({tris[t].b, tris[t].c});
                edges.push_back({tris[t].c, tris[t].a});
                tris[t] = tris.back();
                tris.pop_back();
            }
        }
        // 去掉共享边(仅保留边界轮廓)
        int m = (int)edges.size();
        std::vector<char> dead(m, 0);
        for (int a = 0; a < m; ++a) {
            for (int b = a + 1; b < m; ++b) {
                if ((edges[a].first == edges[b].first && edges[a].second == edges[b].second) ||
                    (edges[a].first == edges[b].second && edges[a].second == edges[b].first)) {
                    dead[a] = dead[b] = 1;
                }
            }
        }
        for (int e = 0; e < m; ++e)
            if (!dead[e]) tris.push_back({edges[e].first, edges[e].second, i});
    }

    // 剔除含超级三角形顶点的三角形
    for (auto& t : tris) {
        if (t.a >= n || t.b >= n || t.c >= n) continue;
        outTris.push_back((uint32_t)t.a);
        outTris.push_back((uint32_t)t.b);
        outTris.push_back((uint32_t)t.c);
    }
}

// 通用均匀网格: 在 [lo,hi] 内按 seg 六边形撒点, 用 inside() 判定保留点与裁剪三角形。
// boundary 为额外的边界顶点(手动模式传致密化边界环; 自动模式留空, 边缘精度由 seg 决定)。
void buildUniformMesh(const std::function<bool(const glm::vec2&)>& inside,
                      glm::vec2 lo, glm::vec2 hi, float seg,
                      const std::vector<glm::vec2>& boundary,
                      std::vector<glm::vec2>& outV, std::vector<uint32_t>& outT) {
    outV.clear();
    outT.clear();
    std::vector<glm::vec2> pts = boundary;
    float dy = seg * 0.866f;   // 行距 = seg * sin(60°)
    int row = 0;
    for (float y = lo.y + dy; y < hi.y; y += dy, ++row) {
        float xoff = (row & 1) ? seg * 0.5f : 0.0f;
        for (float x = lo.x + xoff; x < hi.x; x += seg) {
            glm::vec2 q(x, y);
            if (inside(q)) pts.push_back(q);
        }
    }
    if (pts.size() < 3) return;

    std::vector<uint32_t> raw;
    delaunay(pts, raw);

    outV = pts;
    for (size_t k = 0; k + 2 < raw.size(); k += 3) {
        glm::vec2 cen = (pts[raw[k]] + pts[raw[k + 1]] + pts[raw[k + 2]]) / 3.0f;
        if (inside(cen)) {
            outT.push_back(raw[k]);
            outT.push_back(raw[k + 1]);
            outT.push_back(raw[k + 2]);
        }
    }
}

float perpDist(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1e-9f) return glm::length(p - a);
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
    glm::vec2 proj = a + ab * t;
    return glm::length(p - proj);
}

void dpRec(const std::vector<glm::vec2>& pts, int i0, int i1,
           float eps, std::vector<char>& keep) {
    if (i1 <= i0 + 1) return;
    float dmax = 0.0f; int split = -1;
    for (int i = i0 + 1; i < i1; ++i) {
        float d = perpDist(pts[i], pts[i0], pts[i1]);
        if (d > dmax) { dmax = d; split = i; }
    }
    if (dmax > eps && split > 0) {
        keep[split] = 1;
        dpRec(pts, i0, split, eps, keep);
        dpRec(pts, split, i1, eps, keep);
    }
}

// 闭合环的 Douglas-Peucker 简化: 以 0 与距其最远点为两个锚, 各弧分别简化。
std::vector<glm::vec2> simplifyLoop(const std::vector<glm::vec2>& loop, float eps) {
    int n = (int)loop.size();
    if (n < 5) return loop;
    int far = 0; float fd = -1.0f;
    for (int i = 1; i < n; ++i) {
        float d = glm::length(loop[i] - loop[0]);
        if (d > fd) { fd = d; far = i; }
    }
    std::vector<char> keep(n, 0);
    keep[0] = 1; keep[far] = 1; keep[n - 1] = 1;
    dpRec(loop, 0, far, eps, keep);
    dpRec(loop, far, n - 1, eps, keep);
    std::vector<glm::vec2> out;
    for (int i = 0; i < n; ++i) if (keep[i]) out.push_back(loop[i]);
    return out;
}

} // namespace

// ============================ 编辑器 ============================
void LeafCutoutEditor::open(NodeId nodeId) {
    m_open = true;
    m_targetId = nodeId;
    m_pendingLoad = true;
    m_loadedPath.clear();
    m_dragIdx = -1;
}

void LeafCutoutEditor::loadFor(NodeGraph& graph) {
    m_ring.clear();
    m_loops.clear();
    m_verts.clear();
    m_tris.clear();
    TreeNode* n = graph.getNode(m_targetId);
    CutoutAccess ca = getCutout(n);
    if (!ca.valid()) { m_open = false; return; }

    // 复制已有边界环(若存在)并三角化; 否则若有已保存网格(自动模式生成)直接载入显示
    m_ring = *ca.cutoutRing;
    if (m_ring.size() >= 3) {
        retriangulate();
    } else if (!ca.cutoutTris->empty() && !ca.cutoutPoints->empty()) {
        m_verts = *ca.cutoutPoints;
        m_tris  = *ca.cutoutTris;
    }

    // 显示贴图: 优先 BaseColor, 否则 Opacity
    std::string disp = !ca.material->baseColorTex.empty() ? ca.material->baseColorTex
                                                          : ca.material->opacityTex;
    if (!disp.empty() && disp != m_loadedPath) {
        // 显示用: 以线性(非 sRGB)方式上传, 否则 ImGui 采样 sRGB 纹理会显得偏暗
        if (m_tex.loadFromFile(disp, false)) {
            m_loadedPath = disp;
        }
    } else if (disp.empty()) {
        m_tex.destroy();
        m_loadedPath.clear();
    }
    // 取图像尺寸(用于宽高比与自动裁剪采样)
    m_texW = m_texH = 0;
    if (!disp.empty()) {
        int w = 0, h = 0, ch = 0;
        if (stbi_info(disp.c_str(), &w, &h, &ch)) { m_texW = w; m_texH = h; }
    }
    m_fitOnLoad = true;
}

// 边界致密化 + 内部六边形撒点 + Delaunay 三角化 + 保留形心在剪影内的三角形。
// 生成均匀近等边网格(对标 SpeedTree), 而非从少数角点辐射的针状耳切网格。
// 自动模式(m_loops 非空)按"偶奇规则"填充多条轮廓环: 可同时覆盖多片叶/分叉,
// 并自动挖掉被包含的孔洞环。手动模式(m_loops 为空)填充单条 m_ring。
void LeafCutoutEditor::retriangulate() {
    m_verts.clear();
    m_tris.clear();

    // 组装轮廓环列表: 自动多环优先, 否则手动单环
    std::vector<std::vector<glm::vec2>> single;
    const std::vector<std::vector<glm::vec2>>* loops = &m_loops;
    if (m_loops.empty()) {
        if (m_ring.size() < 3) return;
        single.push_back(m_ring);
        loops = &single;
    }

    float seg = std::clamp(m_density, 0.01f, 0.5f);
    glm::vec2 lo(1e9f), hi(-1e9f);
    std::vector<glm::vec2> boundary;
    for (const auto& ring : *loops) {
        if (ring.size() < 3) continue;
        for (const auto& p : ring) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        std::vector<glm::vec2> d = densifyRing(ring, seg);
        boundary.insert(boundary.end(), d.begin(), d.end());
    }
    // 偶奇规则: 落在奇数个环内=实心(孔洞环会把内部翻回外部)
    auto inside = [&](const glm::vec2& q) -> bool {
        bool in = false;
        for (const auto& ring : *loops)
            if (ring.size() >= 3 && pointInPoly(q, ring)) in = !in;
        return in;
    };
    buildUniformMesh(inside, lo, hi, seg, boundary, m_verts, m_tris);
    if (m_tris.empty() && m_ring.size() >= 3) { earClip(m_ring, m_tris); m_verts = m_ring; }  // 退化兜底
}

// alpha 自动裁剪: marching squares 描轮廓 → 取面积最大的闭合环 → DP 简化 → 写入 m_ring。
bool LeafCutoutEditor::autoGenerate(const std::string& path) {
    if (path.empty()) return false;
    stbi_set_flip_vertically_on_load(false);   // CPU 采样用图像原始朝向
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    stbi_set_flip_vertically_on_load(true);     // 复原(Texture 加载依赖翻转)
    if (!data) return false;

    // 采样网格分辨率自动取自图像尺寸(上限 256), 无需手动参数
    int G = std::clamp(std::max(w, h), 16, 256);
    auto insideAt = [&](int gx, int gy) -> bool {
        int px = std::clamp((int)((float)gx / (float)G * (float)(w - 1) + 0.5f), 0, w - 1);
        int py = std::clamp((int)((float)gy / (float)G * (float)(h - 1) + 0.5f), 0, h - 1);
        const unsigned char* pix = data + (size_t)(py * w + px) * ch;
        float a;
        if (ch == 4)      a = pix[3] / 255.0f;
        else if (ch == 1) a = pix[0] / 255.0f;
        else              a = (0.299f * pix[0] + 0.587f * pix[1] + 0.114f * pix[2]) / 255.0f;
        return a >= m_threshold;
    };

    // Marching squares → 线段集合(网格角坐标)
    std::vector<std::pair<glm::vec2, glm::vec2>> segs;
    auto addSeg = [&](glm::vec2 a, glm::vec2 b) { segs.push_back({a, b}); };
    for (int cy = 0; cy < G; ++cy) {
        for (int cx = 0; cx < G; ++cx) {
            bool tl = insideAt(cx, cy),     tr = insideAt(cx + 1, cy);
            bool br = insideAt(cx + 1, cy + 1), bl = insideAt(cx, cy + 1);
            glm::vec2 eT((float)cx + 0.5f, (float)cy);
            glm::vec2 eR((float)cx + 1.0f, (float)cy + 0.5f);
            glm::vec2 eB((float)cx + 0.5f, (float)cy + 1.0f);
            glm::vec2 eL((float)cx,        (float)cy + 0.5f);
            bool cT = (tl != tr), cR = (tr != br), cB = (br != bl), cL = (bl != tl);
            int cnt = (int)cT + (int)cR + (int)cB + (int)cL;
            if (cnt == 2) {
                glm::vec2 pv[2]; int k = 0;
                if (cT) pv[k++] = eT;
                if (cR) pv[k++] = eR;
                if (cB) pv[k++] = eB;
                if (cL) pv[k++] = eL;
                addSeg(pv[0], pv[1]);
            } else if (cnt == 4) {
                // 鞍点: 按中心内外决定连接方式
                bool center = ((int)tl + tr + br + bl) >= 3;
                if (center) { addSeg(eT, eR); addSeg(eB, eL); }
                else        { addSeg(eT, eL); addSeg(eR, eB); }
            }
        }
    }
    stbi_image_free(data);
    if (segs.empty()) return false;

    // 顶点去重 + 邻接
    auto keyOf = [](const glm::vec2& p) -> long long {
        long long kx = (long long)std::llround(p.x * 2.0);
        long long ky = (long long)std::llround(p.y * 2.0);
        return (kx << 20) ^ (ky & 0xfffff);
    };
    std::unordered_map<long long, int> pmap;
    std::vector<glm::vec2> pts;
    auto getId = [&](const glm::vec2& p) -> int {
        long long k = keyOf(p);
        auto it = pmap.find(k);
        if (it != pmap.end()) return it->second;
        int id = (int)pts.size();
        pmap[k] = id; pts.push_back(p);
        return id;
    };
    std::vector<std::vector<int>> adj;
    for (auto& s : segs) {
        int a = getId(s.first), b = getId(s.second);
        if ((int)adj.size() < (int)pts.size()) adj.resize(pts.size());
        adj[a].push_back(b);
        adj[b].push_back(a);
    }
    adj.resize(pts.size());

    // 提取所有闭合环, 保留面积占比足够大的(相对最大环), 收进 m_loops(叶片 UV)
    std::vector<char> visited(pts.size(), 0);
    std::vector<std::pair<std::vector<glm::vec2>, float>> found;  // (环, 面积)
    float maxArea = 0.0f;
    for (int start = 0; start < (int)pts.size(); ++start) {
        if (visited[start] || adj[start].empty()) continue;
        std::vector<int> loop;
        int cur = start, prev = -1;
        while (cur != -1 && !visited[cur]) {
            visited[cur] = 1;
            loop.push_back(cur);
            int next = -1;
            for (int nb : adj[cur]) {
                if (nb == prev) continue;
                if (nb == start) { next = -2; break; }   // 闭合
                if (!visited[nb]) { next = nb; break; }
            }
            if (next == -2) break;
            prev = cur; cur = next;
        }
        if (loop.size() < 3) continue;
        std::vector<glm::vec2> poly;
        poly.reserve(loop.size());
        for (int id : loop) poly.push_back(pts[id]);
        float area = 0.0f;
        int m = (int)poly.size();
        for (int i = 0; i < m; ++i)
            area += poly[i].x * poly[(i + 1) % m].y - poly[(i + 1) % m].x * poly[i].y;
        area = std::fabs(area) * 0.5f;
        found.push_back({ std::move(poly), area });
        if (area > maxArea) maxArea = area;
    }
    if (found.empty() || maxArea <= 0.0f) return false;

    // 网格角坐标 → 叶片 UV(V 翻转: 图像顶部=V=1)
    auto toUV = [&](const std::vector<glm::vec2>& poly) {
        std::vector<glm::vec2> uv;
        uv.reserve(poly.size());
        for (auto& p : poly)
            uv.push_back({ std::clamp(p.x / (float)G, 0.0f, 1.0f),
                           std::clamp(1.0f - p.y / (float)G, 0.0f, 1.0f) });
        return uv;
    };

    m_loops.clear();
    m_ring.clear();
    for (auto& [poly, area] : found) {
        if (area < maxArea * 0.02f) continue;             // 丢弃碎片小环
        std::vector<glm::vec2> s = simplifyLoop(poly, m_simplify);
        if (s.size() >= 3) m_loops.push_back(toUV(s));
    }
    if (m_loops.empty()) return false;
    retriangulate();
    return true;
}

void LeafCutoutEditor::applyTo(NodeGraph& graph) {
    TreeNode* n = graph.getNode(m_targetId);
    CutoutAccess ca = getCutout(n);
    if (!ca.valid()) return;
    *ca.cutoutRing   = m_ring;      // 可编辑边界环
    *ca.cutoutPoints = m_verts;     // 三角化后完整顶点(生成用)
    *ca.cutoutTris   = m_tris;
    graph.markDirty();
}

void LeafCutoutEditor::render(NodeGraph& graph) {
    if (!m_open) return;
    if (m_pendingLoad) { loadFor(graph); m_pendingLoad = false; }

    TreeNode* node = graph.getNode(m_targetId);
    CutoutAccess ca = getCutout(node);
    if (!ca.valid()) { m_open = false; return; }

    ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Leaf Cutout Editor", &m_open)) { ImGui::End(); return; }

    ImGui::TextDisabled("目标: [%s] id %u", node->getLabel(), node->id);
    ImGui::TextWrapped("左键点击=沿剪影依次加点勾勒边界; 拖动红点=移动; 右键点=删除; "
                       "滚轮=缩放; 中键拖动=平移。");

    bool useCutout = *ca.useCutout;
    if (ImGui::Checkbox("在树上启用轮廓网格", &useCutout)) {
        *ca.useCutout = useCutout;
        graph.markDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(边界 %d 点 / %d 环 / 网格 %d 顶点 / %d 三角形)",
                        (int)m_ring.size(), (int)m_loops.size(),
                        (int)m_verts.size(), (int)m_tris.size() / 3);

    if (ImGui::Button("清空")) { m_ring.clear(); m_loops.clear(); m_verts.clear(); m_tris.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("重设为四边形")) {
        m_loops.clear();
        m_ring = { {0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f} };
        retriangulate();
    }
    ImGui::SameLine();
    if (ImGui::Button("适配视图")) m_fitOnLoad = true;
    ImGui::SameLine();
    ImGui::Checkbox("显示网格", &m_showMesh);

    ImGui::SeparatorText("网格密度 (Delaunay 均匀三角化)");
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("目标边长", &m_density, 0.02f, 0.4f, "%.3f")) {
        retriangulate();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("越小三角形越密越均匀");

    ImGui::SeparatorText("自动裁剪 (按 alpha)");
    ImGui::SetNextItemWidth(200); ImGui::SliderFloat("阈值", &m_threshold, 0.05f, 0.95f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200); ImGui::SliderFloat("简化", &m_simplify, 0.0f, 6.0f);
    ImGui::SameLine();
    if (ImGui::Button("自动生成")) {
        std::string src = !ca.material->opacityTex.empty() ? ca.material->opacityTex
                                                            : ca.material->baseColorTex;
        if (!autoGenerate(src))
            std::fprintf(stderr, "[Cutout] 自动生成失败(无贴图或无轮廓)\n");
    }

    ImGui::SeparatorText("");
    if (ImGui::Button("应用")) applyTo(graph);
    ImGui::SameLine();
    if (ImGui::Button("应用并关闭")) { applyTo(graph); m_open = false; }
    ImGui::SameLine();
    if (ImGui::Button("关闭")) m_open = false;

    // ---------------- 画布 ----------------
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 32) canvasSize.x = 32;
    if (canvasSize.y < 32) canvasSize.y = 32;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos,
                      ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                      IM_COL32(15, 15, 18, 255));

    ImGui::InvisibleButton("##cutoutcanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    // 图像尺寸(保持宽高比适配画布) × 缩放; imgPos = canvasPos + pan
    float imgAsp = (m_texH > 0) ? (float)m_texW / (float)m_texH : 1.0f;
    float availAsp = canvasSize.x / canvasSize.y;
    ImVec2 baseSize;
    if (imgAsp > availAsp) { baseSize.x = canvasSize.x; baseSize.y = canvasSize.x / imgAsp; }
    else                   { baseSize.y = canvasSize.y; baseSize.x = canvasSize.y * imgAsp; }
    ImVec2 imgSize(baseSize.x * m_zoom, baseSize.y * m_zoom);

    if (m_fitOnLoad) {
        m_zoom = 1.0f;
        imgSize = baseSize;
        m_pan = { (canvasSize.x - imgSize.x) * 0.5f, (canvasSize.y - imgSize.y) * 0.5f };
        m_fitOnLoad = false;
    }
    ImVec2 imgPos(canvasPos.x + m_pan.x, canvasPos.y + m_pan.y);

    auto uvToScreen = [&](const glm::vec2& uv) -> ImVec2 {
        return ImVec2(imgPos.x + uv.x * imgSize.x,
                      imgPos.y + (1.0f - uv.y) * imgSize.y);
    };
    auto screenToUv = [&](const ImVec2& s) -> glm::vec2 {
        return glm::vec2((s.x - imgPos.x) / imgSize.x,
                         1.0f - (s.y - imgPos.y) / imgSize.y);
    };

    // 缩放到光标
    if (hovered && io.MouseWheel != 0.0f) {
        ImVec2 mp = io.MousePos;
        glm::vec2 uvBefore((mp.x - imgPos.x) / imgSize.x, (mp.y - imgPos.y) / imgSize.y);
        m_zoom = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), 0.2f, 30.0f);
        imgSize = ImVec2(baseSize.x * m_zoom, baseSize.y * m_zoom);
        m_pan.x = (mp.x - uvBefore.x * imgSize.x) - canvasPos.x;
        m_pan.y = (mp.y - uvBefore.y * imgSize.y) - canvasPos.y;
        imgPos = ImVec2(canvasPos.x + m_pan.x, canvasPos.y + m_pan.y);
    }
    // 中键平移
    if (hovered && io.MouseDown[2]) { m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y; }

    // 裁剪绘制到画布区域
    dl->PushClipRect(canvasPos,
                     ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    if (m_tex.valid()) {
        dl->AddImage((ImTextureID)(intptr_t)m_tex.id(), imgPos,
                     ImVec2(imgPos.x + imgSize.x, imgPos.y + imgSize.y),
                     ImVec2(0, 1), ImVec2(1, 0));
    } else {
        dl->AddText(ImVec2(canvasPos.x + 12, canvasPos.y + 12), IM_COL32(200, 120, 120, 255),
                    "未设置叶片贴图 (在节点材质里指定 BaseColor / Opacity)");
    }

    // 命中测试(屏幕 8px): 覆盖手动环 m_ring 与自动多环 m_loops
    int hit = -1, hitLoop = -1;   // hitLoop=-1 → m_ring; >=0 → m_loops[hitLoop]
    if (hovered) {
        float best = 8.0f * 8.0f;
        for (int i = 0; i < (int)m_ring.size(); ++i) {
            ImVec2 s = uvToScreen(m_ring[i]);
            float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; hit = i; hitLoop = -1; }
        }
        for (int L = 0; L < (int)m_loops.size(); ++L) {
            for (int i = 0; i < (int)m_loops[L].size(); ++i) {
                ImVec2 s = uvToScreen(m_loops[L][i]);
                float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hit = i; hitLoop = L; }
            }
        }
    }
    auto ringAt = [&](int loop) -> std::vector<glm::vec2>& {
        return (loop < 0) ? m_ring : m_loops[loop];
    };

    // 交互
    if (hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hit >= 0) { m_dragIdx = hit; m_dragLoop = hitLoop; }
            else {
                m_loops.clear();   // 空白处打点 → 切回单环手动模式
                glm::vec2 uv = screenToUv(io.MousePos);
                uv.x = std::clamp(uv.x, 0.0f, 1.0f);
                uv.y = std::clamp(uv.y, 0.0f, 1.0f);
                m_ring.push_back(uv);
                retriangulate();
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hit >= 0) {
            auto& r = ringAt(hitLoop);
            if (hit < (int)r.size()) r.erase(r.begin() + hit);
            if (hitLoop >= 0 && r.size() < 3)          // 环点太少则整环删除
                m_loops.erase(m_loops.begin() + hitLoop);
            retriangulate();
        }
    }
    if (m_dragIdx >= 0) {
        std::vector<glm::vec2>* r = nullptr;
        if (m_dragLoop < 0) r = &m_ring;
        else if (m_dragLoop < (int)m_loops.size()) r = &m_loops[m_dragLoop];
        if (io.MouseDown[0] && r && m_dragIdx < (int)r->size()) {
            glm::vec2 uv = screenToUv(io.MousePos);
            (*r)[m_dragIdx] = { std::clamp(uv.x, 0.0f, 1.0f), std::clamp(uv.y, 0.0f, 1.0f) };
            retriangulate();
        } else {
            m_dragIdx = -1; m_dragLoop = -1;
        }
    }

    // 绘制三角形网格(青) —— 可切换显示
    if (m_showMesh) {
        for (size_t k = 0; k + 2 < m_tris.size(); k += 3) {
            if (m_tris[k] >= m_verts.size() || m_tris[k+1] >= m_verts.size() ||
                m_tris[k+2] >= m_verts.size()) continue;
            ImVec2 a = uvToScreen(m_verts[m_tris[k]]);
            ImVec2 b = uvToScreen(m_verts[m_tris[k+1]]);
            ImVec2 c = uvToScreen(m_verts[m_tris[k+2]]);
            dl->AddTriangle(a, b, c, IM_COL32(60, 220, 220, 180), 1.0f);
        }
    }
    // 绘制边界环(亮青闭合): 手动单环 + 自动多环
    for (size_t i = 0; i < m_ring.size(); ++i) {
        ImVec2 a = uvToScreen(m_ring[i]);
        ImVec2 b = uvToScreen(m_ring[(i + 1) % m_ring.size()]);
        if (m_ring.size() >= 2)
            dl->AddLine(a, b, IM_COL32(90, 255, 255, 230), 1.5f);
    }
    for (const auto& ring : m_loops) {
        int rn = (int)ring.size();
        for (int i = 0; i < rn; ++i)
            dl->AddLine(uvToScreen(ring[i]), uvToScreen(ring[(i + 1) % rn]),
                        IM_COL32(90, 255, 255, 200), 1.5f);
    }
    // 绘制顶点(红): 手动环 + 自动多环都可拖动/删除
    auto drawVerts = [&](const std::vector<glm::vec2>& ring, int loop) {
        for (int i = 0; i < (int)ring.size(); ++i) {
            ImVec2 s = uvToScreen(ring[i]);
            bool hl = (i == hit && loop == hitLoop) || (i == m_dragIdx && loop == m_dragLoop);
            ImU32 col = hl ? IM_COL32(255, 230, 60, 255) : IM_COL32(235, 60, 50, 255);
            dl->AddCircleFilled(s, 4.0f, col);
            dl->AddCircle(s, 4.0f, IM_COL32(20, 20, 20, 255), 0, 1.0f);
        }
    };
    drawVerts(m_ring, -1);
    for (int L = 0; L < (int)m_loops.size(); ++L) drawVerts(m_loops[L], L);

    dl->PopClipRect();
    ImGui::End();
}
