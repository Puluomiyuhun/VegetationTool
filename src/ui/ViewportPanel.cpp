#include "ViewportPanel.h"
#include <imgui.h>
#include <glad/glad.h>

void ViewportPanel::render(Renderer& renderer, OrbitCamera& camera,
                            Framebuffer& fb, bool& wireframe)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("Viewport");

    ImVec2 size = ImGui::GetContentRegionAvail();
    int w = (int)size.x;
    int h = (int)size.y;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    // 自适应Framebuffer尺寸
    if (w != fb.width() || h != fb.height()) {
        fb.resize(w, h);
    }

    // 渲染到离屏FBO
    fb.bind();

    // 调整宽高比后渲染
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(camera, aspect, wireframe);
    fb.unbind();

    // 显示到ImGui
    ImGui::Image(
        (ImTextureID)(intptr_t)fb.colorTexture(),
        size,
        ImVec2(0,1), ImVec2(1,0)  // 上下翻转UV（OpenGL坐标系）
    );

    // 鼠标输入（仅在视口被hover时处理）
    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGuiIO& io = ImGui::GetIO();
        float dx = io.MouseDelta.x;
        float dy = io.MouseDelta.y;

        if (io.MouseDown[0])  camera.onMouseDrag(dx, dy);
        if (io.MouseDown[2])  camera.onMiddleDrag(dx, dy);
        if (io.MouseWheel != 0) camera.onScroll(io.MouseWheel);
    }

    // 工具栏（左上角overlay）
    ImVec2 overlayPos = ImGui::GetItemRectMin();
    ImGui::SetNextWindowPos({overlayPos.x + 8, overlayPos.y + 8});
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::SetNextWindowSize({140, 0});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##ViewportOverlay", nullptr, flags)) {
        ImGui::Checkbox("Wireframe", &wireframe);
        if (ImGui::Button("Reset Camera")) camera.reset();
        // 抗锯齿 (MSAA) 采样数
        const char* aaItems[] = {"Off", "2x", "4x", "8x"};
        const int   aaSamples[] = {1, 2, 4, 8};
        int cur = 0;
        for (int i = 0; i < 4; ++i) if (aaSamples[i] == fb.samples()) cur = i;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##AA", &cur, aaItems, 4))
            fb.setSamples(aaSamples[cur]);
    }
    ImGui::End();

    ImGui::End();
    ImGui::PopStyleVar();
}
