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

    // 处理 Export 节点导出请求: 找到其上游 Trunk, 生成该株树网格, 写出 OBJ。
    for (const auto& [id, node] : m_graph.nodes()) {
        if (node->getType() != NodeType::Export) continue;
        auto* ex = static_cast<ExportNode*>(node.get());
        if (!ex->params.requestExport) continue;
        ex->params.requestExport = false;

        // 上游节点 = 其输出连到本 Export 输入的节点(遍历各节点的 children 反查)
        NodeId trunkId = INVALID_NODE;
        for (const auto& [pid, pnode] : m_graph.nodes()) {
            for (const TreeNode* c : m_graph.childrenOf(pid))
                if (c->id == id) { trunkId = pid; break; }
            if (trunkId != INVALID_NODE) break;
        }
        TreeNode* trunk = m_graph.getNode(trunkId);
        if (!trunk || trunk->getType() != NodeType::Trunk) {
            std::fprintf(stderr, "[Export] 未连接 Trunk, 跳过导出\n");
            continue;
        }
        TreeMeshData mesh = m_generator.generateSubtree(m_graph, trunkId);
        bool ok = ProjectIO::exportOBJ(mesh, ex->params.path);
        std::fprintf(ok ? stdout : stderr, "[Export] %s -> %s\n",
                     ok ? "OK" : "FAILED", ex->params.path.c_str());
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
