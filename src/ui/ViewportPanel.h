#pragma once
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Framebuffer.h"
#include "graph/NodeGraph.h"

class ViewportPanel {
public:
    void render(Renderer& renderer, OrbitCamera& camera,
                Framebuffer& fb, bool& wireframe, NodeId& selectedNodeId);

private:
    float m_prevMouseX = 0;
    float m_prevMouseY = 0;
    bool  m_firstMouse = true;
};
