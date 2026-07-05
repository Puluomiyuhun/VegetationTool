#pragma once
#include "NodeEditorPanel.h"
#include "ViewportPanel.h"
#include "PropertyPanel.h"
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Framebuffer.h"
#include "graph/NodeGraph.h"
#include <GLFW/glfw3.h>

class UIManager {
public:
    void init(GLFWwindow* window);
    void shutdown();

    void beginFrame();
    void render(NodeGraph& graph, NodeId& selectedNodeId,
                Renderer& renderer, OrbitCamera& camera,
                Framebuffer& fb, bool& wireframe);
    void endFrame();

private:
    NodeEditorPanel m_nodeEditor;
    ViewportPanel   m_viewport;
    PropertyPanel   m_property;

    void setupDockspace();
};
