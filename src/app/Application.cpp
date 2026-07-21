#include "Application.h"
#include "graph/Nodes.h"
#include "io/ProjectIO.h"
#include <nlohmann/json.hpp>
#include <stb_image_write.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <cstring>

// 程序化生成一个风格化的树形图标（绿色圆冠 + 棕色树干），设置为窗口图标。
// 无需外部资源文件，避免打包依赖。
static void setWindowIcon(GLFWwindow* win) {
    auto makeImage = [](int S, std::vector<unsigned char>& px) {
        px.assign(S * S * 4, 0);
        float cx = S * 0.5f;
        float canopyCy = S * 0.40f;
        float canopyR  = S * 0.34f;
        // 树干矩形
        float trunkW = S * 0.14f;
        float trunkTop = S * 0.55f, trunkBot = S * 0.92f;
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float fx = x + 0.5f, fy = y + 0.5f;
                unsigned char r=0,g=0,b=0,a=0;
                // 圆冠（带上深下浅的渐变）
                float d = std::sqrt((fx-cx)*(fx-cx) + (fy-canopyCy)*(fy-canopyCy));
                if (d <= canopyR) {
                    float t = fy / (float)S;             // 越往下越亮
                    r = (unsigned char)(40  + t*35);
                    g = (unsigned char)(120 + t*70);
                    b = (unsigned char)(40  + t*25);
                    a = 255;
                }
                // 树干（覆盖在圆冠之上）
                if (fx >= cx-trunkW*0.5f && fx <= cx+trunkW*0.5f &&
                    fy >= trunkTop && fy <= trunkBot) {
                    r = 96; g = 60; b = 30; a = 255;
                }
                int i = (y*S + x)*4;
                px[i]=r; px[i+1]=g; px[i+2]=b; px[i+3]=a;
            }
        }
    };
    std::vector<unsigned char> px32, px16;
    makeImage(32, px32);
    makeImage(16, px16);
    GLFWimage imgs[2];
    imgs[0].width = 32; imgs[0].height = 32; imgs[0].pixels = px32.data();
    imgs[1].width = 16; imgs[1].height = 16; imgs[1].pixels = px16.data();
    glfwSetWindowIcon(win, 2, imgs);
}

bool Application::init(int width, int height) {
    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(width, height, "SlowTree", nullptr, nullptr);
    if (!m_window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    setWindowIcon(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCb);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "GLAD load failed\n");
        return false;
    }

    try {
        m_renderer.init();
    } catch (const std::exception& e) {
        fprintf(stderr, "Renderer init error: %s\n", e.what());
        return false;
    }

    m_framebuffer.create(1, 1);
    m_ui.init(m_window);

    // 构建默认模板树并立即生成
    m_graph.buildDefaultTemplate();
    m_meshData = m_generator.generate(m_graph);
    m_renderer.uploadTreeMesh(m_meshData);
    m_graph.clearDirty();

    // 启动内嵌 HTTP 控制服务(供外部 AI/MCP 调用), 仅监听本地回环。
    m_api.start(8765, [this](const std::string& method, const ApiServer::Json& params) {
        return handleApiCommand(method, params);
    });

    return true;
}

void Application::run() {
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // 窗口最小化时帧缓冲为 0x0, 若继续走 ImGui 渲染会在零尺寸下重算并保存
        // dock 布局, 恢复时导致停靠窗口错乱。此时挂起渲染, 等待窗口恢复。
        int fbw, fbh;
        glfwGetFramebufferSize(m_window, &fbw, &fbh);
        if (fbw == 0 || fbh == 0) {
            glfwWaitEvents();
            continue;
        }

        update();
        render();
        glfwSwapBuffers(m_window);
    }
}

void Application::update() {
    // 执行外部 API 累积的命令(在主线程, NodeGraph/OpenGL 非线程安全)。
    m_api.drain();

    // 选中节点变化时也需重新生成(高亮几何随选中节点走)
    if (m_graph.isDirty() || m_selectedNode != m_lastHlNode) {
        m_meshData = m_generator.generate(m_graph, m_selectedNode);
        m_renderer.uploadTreeMesh(m_meshData);
        m_graph.clearDirty();
        m_lastHlNode = m_selectedNode;
    }

    // 处理 Export 节点导出请求: 按模式导出整株或竖直标本, 写出 OBJ。
    for (const auto& [id, node] : m_graph.nodes()) {
        if (node->getType() != NodeType::Export) continue;
        auto* ex = static_cast<ExportNode*>(node.get());
        if (!ex->params.requestExport) continue;
        ex->params.requestExport = false;

        // 按 format 选择导出器, 并把路径扩展名规整为 .obj / .fbx / .usda。
        const int fmt = ex->params.format;   // 0=OBJ 1=FBX 2=USD
        auto fixExt = [fmt](std::string p) {
            size_t dot = p.find_last_of('.');
            size_t slash = p.find_last_of("/\\");
            std::string want = fmt == 1 ? ".fbx" : (fmt == 2 ? ".usda" : ".obj");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                p = p.substr(0, dot);
            return p + want;
        };
        auto exportMesh = [fmt](const TreeMeshData& mesh, const std::string& p) {
            return fmt == 1 ? ProjectIO::exportFBX(mesh, p)
                 : fmt == 2 ? ProjectIO::exportUSD(mesh, p)
                            : ProjectIO::exportOBJ(mesh, p);
        };

        // 上游节点 = 其输出连到本 Export 输入的节点(遍历各节点的 children 反查)
        NodeId upstreamId = INVALID_NODE;
        for (const auto& [pid, pnode] : m_graph.nodes()) {
            for (const TreeNode* c : m_graph.childrenOf(pid))
                if (c->id == id) { upstreamId = pid; break; }
            if (upstreamId != INVALID_NODE) break;
        }
        if (upstreamId == INVALID_NODE) {
            std::fprintf(stderr, "[Export] Export 节点未连接上游节点, 跳过导出\n");
            continue;
        }

        if (ex->params.exportMode == 1) {
            // 整株: 从上游节点沿输入链向上追溯到根 Trunk / ImportTrunk
            NodeId cur = upstreamId, rootTrunk = INVALID_NODE;
            for (int guard = 0; guard < 64 && cur != INVALID_NODE; ++guard) {
                TreeNode* cn = m_graph.getNode(cur);
                if (!cn) break;
                if (cn->getType() == NodeType::Trunk ||
                    cn->getType() == NodeType::ImportTrunk) { rootTrunk = cur; break; }
                NodeId parent = INVALID_NODE;   // 找 cur 的父(其 children 含 cur)
                for (const auto& [pid, pnode] : m_graph.nodes()) {
                    for (const TreeNode* c : m_graph.childrenOf(pid))
                        if (c->id == cur) { parent = pid; break; }
                    if (parent != INVALID_NODE) break;
                }
                cur = parent;
            }
            if (rootTrunk == INVALID_NODE) {
                std::fprintf(stderr, "[Export] 追溯不到根 Trunk, 跳过整株导出\n");
                continue;
            }
            TreeMeshData mesh = m_generator.generateSubtree(m_graph, rootTrunk);
            std::string outPath = fixExt(ex->params.path);
            bool ok = exportMesh(mesh, outPath);
            std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                         ok ? "OK" : "FAILED", outPath.c_str());
            continue;
        }

        if (ex->params.exportMode == 2) {
            // 当前节点及上游: 只导出从上游节点到根 Trunk 这条祖先链(各节点仅一根实例)
            TreeMeshData mesh = m_generator.generateChain(m_graph, upstreamId);
            if (mesh.batches.empty()) {
                std::fprintf(stderr, "[Export] 祖先链追溯不到根 Trunk, 跳过导出\n");
                continue;
            }
            bool ok = exportMesh(mesh, fixExt(ex->params.path));
            std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                         ok ? "OK" : "FAILED", fixExt(ex->params.path).c_str());
            continue;
        }

        // 标本模式: 上游节点作为竖直主枝, 连同下游子枝叶一起导出。
        // specimenCount 个变体(不同随机种子); singleFile=合并到一个 obj 并沿 X 并排,
        // 否则每个变体各写一个带 _N 后缀的文件。
        int count = std::max(1, ex->params.specimenCount);
        if (ex->params.singleFile) {
            TreeMeshData merged;
            for (int i = 0; i < count; ++i) {
                TreeMeshData v = m_generator.generateSpecimen(m_graph, upstreamId, i);
                float xoff = (float)i * ex->params.specimenSpacing;
                for (auto& b : v.batches) {
                    size_t stride = b.isLeaf ? 16 : 10;   // 顶点浮点步长
                    for (size_t k = 0; k + stride <= b.vertices.size(); k += stride)
                        b.vertices[k] += xoff;             // 仅平移 pos.x
                    merged.batches.push_back(std::move(b));
                }
            }
            std::string outPath = fixExt(ex->params.path);
            bool ok = exportMesh(merged, outPath);
            std::fprintf(ok ? stdout : stderr, "[Export] %s (x%d) -> %s\n",
                         ok ? "OK" : "FAILED", count, outPath.c_str());
        } else {
            // 在扩展名前插入 _N: fern.obj -> fern_0.obj / fern_1.obj ...
            std::string base = fixExt(ex->params.path), ext;
            size_t dot = base.find_last_of('.');
            size_t slash = base.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
                ext  = base.substr(dot);
                base = base.substr(0, dot);
            }
            for (int i = 0; i < count; ++i) {
                TreeMeshData v = m_generator.generateSpecimen(m_graph, upstreamId, i);
                std::string fn = base + "_" + std::to_string(i) + ext;
                bool ok = exportMesh(v, fn);
                std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                             ok ? "OK" : "FAILED", fn.c_str());
            }
        }
    }
}

void Application::render() {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 顶点风力动画时间(秒), 供风力着色器驱动
    m_renderer.windTime = (float)glfwGetTime();

    m_ui.beginFrame();
    m_ui.render(m_graph, m_selectedNode,
                m_renderer, m_camera,
                m_framebuffer, m_wireframe);
    m_ui.endFrame();
}

void Application::shutdown() {
    m_api.stop();
    m_ui.shutdown();
    m_renderer.shutdown();
    m_framebuffer.destroy();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Application::framebufferResizeCb(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
}

// ---- 外部 API 命令分发(主线程执行) ----
// 约定: 抛出 std::runtime_error 会被上层包成 { ok:false, error }。

// 节点类型 <-> 名称 双向映射(API 用可读名, 也兼容传入整数序号)。
static const char* nodeTypeName(NodeType t) {
    switch (t) {
        case NodeType::Trunk:       return "Trunk";
        case NodeType::Roots:       return "Roots";
        case NodeType::Branch:      return "Branch";
        case NodeType::Twig:        return "Twig";
        case NodeType::LeafCluster: return "LeafCluster";
        case NodeType::Spine:       return "Spine";
        case NodeType::Frond:       return "Frond";
        case NodeType::Export:      return "Export";
        case NodeType::Custom:      return "Custom";
    }
    return "Unknown";
}

static bool parseNodeType(const ApiServer::Json& v, NodeType& out) {
    if (v.is_number_integer()) {
        int i = v.get<int>();
        if (i < 0 || i > (int)NodeType::Custom) return false;
        out = (NodeType)i;
        return true;
    }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        for (int i = 0; i <= (int)NodeType::Custom; ++i)
            if (s == nodeTypeName((NodeType)i)) { out = (NodeType)i; return true; }
    }
    return false;
}

// base64 编码(截图以 data-uri 形式返回给 MCP/AI)。
static std::string base64Encode(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? tbl[(v >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? tbl[v & 0x3F]        : '=');
    }
    return out;
}

// stb_image_write 内存写回调: 把 PNG 字节追加到 vector。
static void pngToVector(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<unsigned char>*>(ctx);
    const auto* p = static_cast<unsigned char*>(data);
    v->insert(v->end(), p, p + size);
}

ApiServer::Json Application::handleApiCommand(const std::string& method,
                                             const ApiServer::Json& params) {
    using Json = ApiServer::Json;

    auto reqId = [&](const char* key) -> NodeId {
        if (!params.contains(key) || !params[key].is_number_integer())
            throw std::runtime_error(std::string("missing integer param: ") + key);
        return (NodeId)params[key].get<int64_t>();
    };
    auto nodeInfo = [&](const TreeNode* n) -> Json {
        return Json{
            {"id",    n->id},
            {"type",  (int)n->getType()},
            {"typeName", nodeTypeName(n->getType())},
            {"label", n->getLabel()},
            {"x",     n->editorPos.x},
            {"y",     n->editorPos.y},
        };
    };

    if (method == "ping") {
        return Json{{"pong", true}, {"app", "SlowTree"}};
    }

    if (method == "graph.list") {
        // 返回图中全部节点(id/type/label/编辑器坐标)与连线。
        Json nodes = Json::array();
        for (const auto& [id, node] : m_graph.nodes())
            nodes.push_back(nodeInfo(node.get()));
        Json links = Json::array();
        for (const Link& l : m_graph.links()) {
            // 附带 fromNode/toNode 便于 AI 理解拓扑(pin 反查 owner)。
            NodeId fromNode = INVALID_NODE, toNode = INVALID_NODE;
            for (const auto& [id, node] : m_graph.nodes()) {
                if (node->outputPin.id == l.fromPin) fromNode = id;
                for (const auto& p : node->inputPins)
                    if (p.id == l.toPin) toNode = id;
            }
            links.push_back({{"id", l.id}, {"fromPin", l.fromPin}, {"toPin", l.toPin},
                             {"fromNode", fromNode}, {"toNode", toNode}});
        }
        return Json{{"nodes", nodes}, {"links", links}, {"selected", m_selectedNode}};
    }

    if (method == "node.add") {
        NodeType t;
        if (!params.contains("type") || !parseNodeType(params["type"], t))
            throw std::runtime_error("invalid or missing 'type'");
        float x = params.value("x", 0.0f), y = params.value("y", 0.0f);
        NodeId id = m_graph.addNode(t, {x, y});
        return Json{{"id", id}};
    }

    if (method == "node.remove") {
        NodeId id = reqId("id");
        if (!m_graph.getNode(id)) throw std::runtime_error("node not found");
        m_graph.removeNode(id);
        if (m_selectedNode == id) m_selectedNode = INVALID_NODE;
        return Json{{"removed", id}};
    }

    if (method == "node.addChild") {
        NodeId parent = reqId("parentId");
        NodeType t;
        if (!params.contains("type") || !parseNodeType(params["type"], t))
            throw std::runtime_error("invalid or missing 'type'");
        NodeId id = m_graph.addChildNode(parent, t);
        if (id == INVALID_NODE) throw std::runtime_error("addChild failed (bad parent?)");
        return Json{{"id", id}};
    }

    if (method == "link.add") {
        // 支持两种形式: {fromNode,toNode}(推荐, 用节点 id) 或 {fromPin,toPin}(底层 pin id)。
        PinId fromPin = INVALID_PIN, toPin = INVALID_PIN;
        if (params.contains("fromNode") && params.contains("toNode")) {
            TreeNode* a = m_graph.getNode((NodeId)params["fromNode"].get<int64_t>());
            TreeNode* b = m_graph.getNode((NodeId)params["toNode"].get<int64_t>());
            if (!a || !b) throw std::runtime_error("fromNode/toNode not found");
            if (b->inputPins.empty()) throw std::runtime_error("toNode has no input pin");
            fromPin = a->outputPin.id;
            toPin   = b->inputPins[0].id;
        } else {
            fromPin = (PinId)reqId("fromPin");
            toPin   = (PinId)reqId("toPin");
        }
        LinkId lid = m_graph.addLink(fromPin, toPin);
        return Json{{"id", lid}};
    }

    if (method == "link.remove") {
        LinkId id = (LinkId)reqId("id");
        m_graph.removeLink(id);
        return Json{{"removed", id}};
    }

    if (method == "node.getParams") {
        NodeId id = reqId("id");
        TreeNode* n = m_graph.getNode(id);
        if (!n) throw std::runtime_error("node not found");
        auto kv = ProjectIO::nodeParamsToMap(n);
        Json p = Json::object();
        for (const auto& [k, v] : kv) p[k] = v;
        Json out = nodeInfo(n);
        out["params"] = p;
        return out;
    }

    if (method == "node.setParams") {
        NodeId id = reqId("id");
        TreeNode* n = m_graph.getNode(id);
        if (!n) throw std::runtime_error("node not found");
        if (!params.contains("params") || !params["params"].is_object())
            throw std::runtime_error("missing object 'params'");
        std::map<std::string, std::string> kv;
        for (auto it = params["params"].begin(); it != params["params"].end(); ++it) {
            // 值统一转成字符串(数字/布尔/字符串都接受), 复用 .vtree 的解析。
            const Json& v = it.value();
            kv[it.key()] = v.is_string() ? v.get<std::string>() : v.dump();
        }
        ProjectIO::applyNodeParams(n, kv);
        m_graph.markDirty();   // 触发主循环下一帧重生成网格
        return Json{{"updated", id}, {"applied", (int)kv.size()}};
    }

    if (method == "node.select") {
        NodeId id = reqId("id");
        if (id != INVALID_NODE && !m_graph.getNode(id))
            throw std::runtime_error("node not found");
        m_selectedNode = id;
        return Json{{"selected", id}};
    }

    if (method == "project.new") {
        m_graph.clear();
        m_selectedNode = INVALID_NODE;
        return Json{{"ok", true}};
    }

    if (method == "project.buildDefault") {
        m_graph.clear();
        m_graph.buildDefaultTemplate();
        m_graph.markDirty();
        m_selectedNode = INVALID_NODE;
        return Json{{"ok", true}};
    }

    if (method == "project.save") {
        std::string path = params.value("path", std::string());
        if (path.empty()) throw std::runtime_error("missing 'path'");
        bool ok = ProjectIO::save(m_graph, path, &m_renderer.lighting);
        if (!ok) throw std::runtime_error("save failed");
        return Json{{"saved", path}};
    }

    if (method == "project.load") {
        std::string path = params.value("path", std::string());
        if (path.empty()) throw std::runtime_error("missing 'path'");
        bool ok = ProjectIO::load(m_graph, path, &m_renderer.lighting);
        if (!ok) throw std::runtime_error("load failed");
        m_graph.markDirty();
        m_selectedNode = INVALID_NODE;
        return Json{{"loaded", path}};
    }

    if (method == "export.trigger") {
        // 触发一个 Export 节点导出(下一帧 update() 中执行)。可指定 id, 否则用首个 Export 节点。
        NodeId id = INVALID_NODE;
        if (params.contains("id")) id = (NodeId)params["id"].get<int64_t>();
        ExportNode* ex = nullptr;
        for (const auto& [nid, node] : m_graph.nodes()) {
            if (node->getType() != NodeType::Export) continue;
            if (id == INVALID_NODE || nid == id) {
                ex = static_cast<ExportNode*>(node.get());
                id = nid;
                break;
            }
        }
        if (!ex) throw std::runtime_error("no matching Export node");
        if (params.contains("path")) ex->params.path = params["path"].get<std::string>();
        ex->params.requestExport = true;
        return Json{{"triggered", id}, {"path", ex->params.path}};
    }

    if (method == "render.screenshot") {
        // 抓取视口离屏 FBO 的颜色纹理(上一帧渲染结果)。
        // 在主线程执行, GL 上下文有效。可写文件(path)和/或返回 base64(默认返回)。
        if (!m_framebuffer.valid())
            throw std::runtime_error("framebuffer not ready");
        int w = m_framebuffer.width(), h = m_framebuffer.height();
        if (w <= 0 || h <= 0) throw std::runtime_error("framebuffer empty");

        std::vector<unsigned char> pixels((size_t)w * h * 4);
        glBindTexture(GL_TEXTURE_2D, m_framebuffer.colorTexture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        // OpenGL 纹理原点在左下, 图像原点在左上 -> 垂直翻转。
        std::vector<unsigned char> flipped((size_t)w * h * 4);
        size_t row = (size_t)w * 4;
        for (int y = 0; y < h; ++y)
            std::memcpy(&flipped[(size_t)y * row],
                        &pixels[(size_t)(h - 1 - y) * row], row);

        std::string path = params.value("path", std::string());
        bool wantB64 = params.value("base64", path.empty());  // 未指定则: 有路径写文件, 否则回 base64

        Json out{{"width", w}, {"height", h}};
        if (!path.empty()) {
            if (!stbi_write_png(path.c_str(), w, h, 4, flipped.data(), (int)row))
                throw std::runtime_error("write png failed");
            out["path"] = path;
        }
        if (wantB64) {
            std::vector<unsigned char> png;
            stbi_write_png_to_func(pngToVector, &png, w, h, 4, flipped.data(), (int)row);
            out["mime"] = "image/png";
            out["base64"] = base64Encode(png.data(), png.size());
        }
        return out;
    }

    throw std::runtime_error("unknown method: " + method);
}
