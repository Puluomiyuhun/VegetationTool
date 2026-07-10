#pragma once
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Framebuffer.h"
#include "graph/NodeGraph.h"
#include "generator/TreeGenerator.h"
#include "ui/UIManager.h"
#include "api/ApiServer.h"
#include <GLFW/glfw3.h>

class Application {
public:
    bool init(int width = 1600, int height = 900);
    void run();
    void shutdown();

private:
    GLFWwindow*    m_window    = nullptr;
    NodeGraph      m_graph;
    TreeGenerator  m_generator;
    TreeMeshData   m_meshData;
    Renderer       m_renderer;
    OrbitCamera    m_camera;
    Framebuffer    m_framebuffer;
    UIManager      m_ui;
    NodeId         m_selectedNode = INVALID_NODE;
    NodeId         m_lastHlNode   = INVALID_NODE;   // 上次生成时的高亮节点(变化则重新生成)
    bool           m_wireframe    = false;

    ApiServer      m_api;          // 内嵌 HTTP 控制服务(供外部 AI/MCP 调用)

    void update();
    void render();

    // API 命令处理器: 在主线程执行, 分发 method 到具体操作, 返回结果 json。
    ApiServer::Json handleApiCommand(const std::string& method, const ApiServer::Json& params);

    static void framebufferResizeCb(GLFWwindow* w, int width, int height);
};
