#include "Application.h"
#include "graph/Nodes.h"
#include "io/ProjectIO.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdio>
#include <vector>
#include <cmath>

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
            // 整株: 从上游节点沿输入链向上追溯到根 Trunk
            NodeId cur = upstreamId, rootTrunk = INVALID_NODE;
            for (int guard = 0; guard < 64 && cur != INVALID_NODE; ++guard) {
                TreeNode* cn = m_graph.getNode(cur);
                if (!cn) break;
                if (cn->getType() == NodeType::Trunk) { rootTrunk = cur; break; }
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
            bool ok = ProjectIO::exportOBJ(mesh, ex->params.path);
            std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                         ok ? "OK" : "FAILED", ex->params.path.c_str());
            continue;
        }

        if (ex->params.exportMode == 2) {
            // 当前节点及上游: 只导出从上游节点到根 Trunk 这条祖先链(各节点仅一根实例)
            TreeMeshData mesh = m_generator.generateChain(m_graph, upstreamId);
            if (mesh.batches.empty()) {
                std::fprintf(stderr, "[Export] 祖先链追溯不到根 Trunk, 跳过导出\n");
                continue;
            }
            bool ok = ProjectIO::exportOBJ(mesh, ex->params.path);
            std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                         ok ? "OK" : "FAILED", ex->params.path.c_str());
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
            bool ok = ProjectIO::exportOBJ(merged, ex->params.path);
            std::fprintf(ok ? stdout : stderr, "[Export] %s (x%d) -> %s\n",
                         ok ? "OK" : "FAILED", count, ex->params.path.c_str());
        } else {
            // 在扩展名前插入 _N: fern.obj -> fern_0.obj / fern_1.obj ...
            std::string base = ex->params.path, ext;
            size_t dot = base.find_last_of('.');
            size_t slash = base.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
                ext  = base.substr(dot);
                base = base.substr(0, dot);
            }
            for (int i = 0; i < count; ++i) {
                TreeMeshData v = m_generator.generateSpecimen(m_graph, upstreamId, i);
                std::string fn = base + "_" + std::to_string(i) + ext;
                bool ok = ProjectIO::exportOBJ(v, fn);
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
    m_ui.shutdown();
    m_renderer.shutdown();
    m_framebuffer.destroy();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Application::framebufferResizeCb(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
}
