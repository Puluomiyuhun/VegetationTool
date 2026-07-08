#include "LeafCutoutEditor.h"
#include "graph/Nodes.h"
#include <imgui.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

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
    m_tris.clear();
    TreeNode* n = graph.getNode(m_targetId);
    if (!n || n->getType() != NodeType::LeafCluster) { m_open = false; return; }
    const auto& p = static_cast<LeafClusterNode*>(n)->params;

    // 复制已有环(若存在)
    m_ring = p.cutoutPoints;
    if (m_ring.size() >= 3) retriangulate();

    // 显示贴图: 优先 BaseColor, 否则 Opacity
    std::string disp = !p.material.baseColorTex.empty() ? p.material.baseColorTex
                                                        : p.material.opacityTex;
    if (!disp.empty() && disp != m_loadedPath) {
        if (m_tex.loadFromFile(disp, true)) {
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

void LeafCutoutEditor::retriangulate() {
    earClip(m_ring, m_tris);
}

bool LeafCutoutEditor::autoGenerate(const std::string& path) {
    if (path.empty()) return false;
    stbi_set_flip_vertically_on_load(false);   // CPU 采样用图像原始朝向
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    stbi_set_flip_vertically_on_load(true);     // 复原(Texture 加载依赖翻转)
    if (!data) return false;

    int G = std::clamp(m_resolution, 16, 512);
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

    // 提取所有环, 保留面积最大者
    std::vector<char> visited(pts.size(), 0);
    std::vector<glm::vec2> best;
    float bestArea = 0.0f;
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
        if (area > bestArea) { bestArea = area; best = poly; }
    }
    if (best.size() < 3) return false;

    best = simplifyLoop(best, m_simplify);

    // 网格角坐标 → 叶片 UV(V 翻转: 图像顶部=V=1)
    m_ring.clear();
    m_ring.reserve(best.size());
    for (auto& p : best)
        m_ring.push_back({ std::clamp(p.x / (float)G, 0.0f, 1.0f),
                           std::clamp(1.0f - p.y / (float)G, 0.0f, 1.0f) });
    retriangulate();
    return true;
}

void LeafCutoutEditor::applyTo(NodeGraph& graph) {
    TreeNode* n = graph.getNode(m_targetId);
    if (!n || n->getType() != NodeType::LeafCluster) return;
    auto& p = static_cast<LeafClusterNode*>(n)->params;
    p.cutoutPoints = m_ring;
    p.cutoutTris   = m_tris;
    graph.markDirty();
}

void LeafCutoutEditor::render(NodeGraph& graph) {
    if (!m_open) return;
    if (m_pendingLoad) { loadFor(graph); m_pendingLoad = false; }

    TreeNode* node = graph.getNode(m_targetId);
    if (!node || node->getType() != NodeType::LeafCluster) { m_open = false; return; }
    auto& params = static_cast<LeafClusterNode*>(node)->params;

    ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Leaf Cutout Editor", &m_open)) { ImGui::End(); return; }

    ImGui::TextDisabled("目标: [%s] id %u", node->getLabel(), node->id);
    ImGui::TextWrapped("左键点击=沿剪影依次加点勾勒边界; 拖动红点=移动; 右键点=删除; "
                       "滚轮=缩放; 中键拖动=平移。");

    bool useCutout = params.useCutout;
    if (ImGui::Checkbox("在树上启用轮廓网格", &useCutout)) {
        params.useCutout = useCutout;
        graph.markDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d 点 / %d 三角形)", (int)m_ring.size(), (int)m_tris.size() / 3);

    if (ImGui::Button("清空")) { m_ring.clear(); m_tris.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("重设为四边形")) {
        m_ring = { {0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f} };
        retriangulate();
    }
    ImGui::SameLine();
    if (ImGui::Button("适配视图")) m_fitOnLoad = true;

    ImGui::SeparatorText("自动裁剪 (按 alpha)");
    ImGui::SetNextItemWidth(150); ImGui::SliderFloat("阈值", &m_threshold, 0.05f, 0.95f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150); ImGui::SliderInt("分辨率", &m_resolution, 24, 256);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150); ImGui::SliderFloat("简化", &m_simplify, 0.0f, 6.0f);
    ImGui::SameLine();
    if (ImGui::Button("自动生成")) {
        std::string src = !params.material.opacityTex.empty() ? params.material.opacityTex
                                                              : params.material.baseColorTex;
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

    // 命中测试(屏幕 8px)
    int hit = -1;
    if (hovered) {
        float best = 8.0f * 8.0f;
        for (int i = 0; i < (int)m_ring.size(); ++i) {
            ImVec2 s = uvToScreen(m_ring[i]);
            float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; hit = i; }
        }
    }

    // 交互
    if (hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hit >= 0) m_dragIdx = hit;
            else {
                glm::vec2 uv = screenToUv(io.MousePos);
                uv.x = std::clamp(uv.x, 0.0f, 1.0f);
                uv.y = std::clamp(uv.y, 0.0f, 1.0f);
                m_ring.push_back(uv);
                retriangulate();
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hit >= 0) {
            m_ring.erase(m_ring.begin() + hit);
            retriangulate();
        }
    }
    if (m_dragIdx >= 0) {
        if (io.MouseDown[0] && m_dragIdx < (int)m_ring.size()) {
            glm::vec2 uv = screenToUv(io.MousePos);
            m_ring[m_dragIdx] = { std::clamp(uv.x, 0.0f, 1.0f), std::clamp(uv.y, 0.0f, 1.0f) };
            retriangulate();
        } else {
            m_dragIdx = -1;
        }
    }

    // 绘制三角形网格(青)
    for (size_t k = 0; k + 2 < m_tris.size(); k += 3) {
        if (m_tris[k] >= m_ring.size() || m_tris[k+1] >= m_ring.size() ||
            m_tris[k+2] >= m_ring.size()) continue;
        ImVec2 a = uvToScreen(m_ring[m_tris[k]]);
        ImVec2 b = uvToScreen(m_ring[m_tris[k+1]]);
        ImVec2 c = uvToScreen(m_ring[m_tris[k+2]]);
        dl->AddTriangle(a, b, c, IM_COL32(60, 220, 220, 180), 1.0f);
    }
    // 绘制边界环(亮青闭合)
    for (size_t i = 0; i < m_ring.size(); ++i) {
        ImVec2 a = uvToScreen(m_ring[i]);
        ImVec2 b = uvToScreen(m_ring[(i + 1) % m_ring.size()]);
        if (m_ring.size() >= 2)
            dl->AddLine(a, b, IM_COL32(90, 255, 255, 230), 1.5f);
    }
    // 绘制顶点(红)
    for (int i = 0; i < (int)m_ring.size(); ++i) {
        ImVec2 s = uvToScreen(m_ring[i]);
        ImU32 col = (i == hit || i == m_dragIdx) ? IM_COL32(255, 230, 60, 255)
                                                 : IM_COL32(235, 60, 50, 255);
        dl->AddCircleFilled(s, 4.0f, col);
        dl->AddCircle(s, 4.0f, IM_COL32(20, 20, 20, 255), 0, 1.0f);
    }

    dl->PopClipRect();
    ImGui::End();
}
