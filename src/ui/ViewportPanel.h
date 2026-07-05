#pragma once
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Framebuffer.h"

class ViewportPanel {
public:
    void render(Renderer& renderer, OrbitCamera& camera,
                Framebuffer& fb, bool& wireframe);

private:
    float m_prevMouseX = 0;
    float m_prevMouseY = 0;
    bool  m_firstMouse = true;
};
