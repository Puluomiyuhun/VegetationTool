#include "UIManager.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

void UIManager::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    // 深色主题微调
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding  = 4.0f;
    style.FrameRounding   = 3.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_nodeEditor.init();
}

void UIManager::shutdown() {
    m_nodeEditor.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UIManager::render(NodeGraph& graph, NodeId& selectedNodeId,
                        Renderer& renderer, OrbitCamera& camera,
                        Framebuffer& fb, bool& wireframe)
{
    setupDockspace();

    // 菜单栏
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                graph.buildDefaultTemplate();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tree")) {
            if (ImGui::MenuItem("Reset Template"))
                graph.buildDefaultTemplate();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // 左侧：节点编辑器
    ImGui::Begin("Node Graph");
    m_nodeEditor.render(graph, selectedNodeId);
    ImGui::End();

    // 右侧：属性面板
    m_property.render(selectedNodeId, graph);

    // 光照控制面板
    ImGui::Begin("Lighting");
    {
        auto& L = renderer.lighting;
        ImGui::Text("Light Direction");
        ImGui::SliderFloat("Light X", &L.lightDir.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Light Y", &L.lightDir.y,  0.0f, 1.0f);
        ImGui::SliderFloat("Light Z", &L.lightDir.z, -1.0f, 1.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Light Color",   &L.lightColor.x);
        ImGui::ColorEdit3("Ambient Top",   &L.ambientTop.x);
        ImGui::ColorEdit3("Ambient Bottom",&L.ambientBot.x);
    }
    ImGui::End();

    // 中央：视口
    m_viewport.render(renderer, camera, fb, wireframe);
}

void UIManager::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::setupDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("DockSpace", nullptr, flags);
    ImGui::PopStyleVar(2);

    ImGuiID dockId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0,0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}
