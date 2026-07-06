#include "UIManager.h"
#include "io/ProjectIO.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
// 工程文件的打开/保存对话框（.vtree）。save=true 时为“另存为”。
static std::string projectFileDialog(bool save) {
    char szFile[1024] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrFilter = "Vegetation Tree (*.vtree)\0*.vtree\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt  = "vtree";
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn)) return std::string(szFile);
    } else {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) return std::string(szFile);
    }
    return std::string();
}
#else
static std::string projectFileDialog(bool) { return std::string(); }
#endif


// 专业深色主题 (类 Blender/Unreal 风格)
static void applyProfessionalTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding     = ImVec2(8, 8);
    s.FramePadding      = ImVec2(6, 4);
    s.ItemSpacing       = ImVec2(8, 5);
    s.ItemInnerSpacing  = ImVec2(5, 4);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowRounding    = 4.0f;
    s.ChildRounding     = 3.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 3.0f;

    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    ImVec4* c = s.Colors;
    // ---- 背景 ----
    c[ImGuiCol_WindowBg]           = ImVec4(0.118f, 0.122f, 0.133f, 1.00f);  // #1E1F22
    c[ImGuiCol_ChildBg]            = ImVec4(0.102f, 0.106f, 0.114f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.141f, 0.145f, 0.157f, 0.98f);

    // ---- 边框 ----
    c[ImGuiCol_Border]             = ImVec4(0.247f, 0.251f, 0.271f, 1.00f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // ---- 标题栏 ----
    c[ImGuiCol_TitleBg]            = ImVec4(0.086f, 0.090f, 0.098f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.153f, 0.157f, 0.173f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.086f, 0.090f, 0.098f, 1.00f);

    // ---- 菜单栏 ----
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.082f, 0.086f, 0.094f, 1.00f);

    // ---- 滚动条 ----
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.082f, 0.086f, 0.094f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.250f, 0.255f, 0.275f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.310f, 0.315f, 0.340f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.400f, 0.408f, 0.440f, 1.00f);

    // ---- 控件框 ----
    c[ImGuiCol_FrameBg]            = ImVec4(0.165f, 0.169f, 0.184f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.220f, 0.224f, 0.243f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.275f, 0.280f, 0.302f, 1.00f);

    // ---- 强调色 (蓝青) ----
    c[ImGuiCol_CheckMark]          = ImVec4(0.388f, 0.714f, 1.000f, 1.00f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.318f, 0.604f, 0.918f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.388f, 0.714f, 1.000f, 1.00f);

    // ---- 按钮 ----
    c[ImGuiCol_Button]             = ImVec4(0.220f, 0.224f, 0.243f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.318f, 0.604f, 0.918f, 0.80f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.318f, 0.604f, 0.918f, 1.00f);

    // ---- Header (CollapsingHeader) ----
    c[ImGuiCol_Header]             = ImVec4(0.220f, 0.224f, 0.243f, 1.00f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.270f, 0.510f, 0.780f, 0.70f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.318f, 0.604f, 0.918f, 1.00f);

    // ---- 分割线 ----
    c[ImGuiCol_Separator]          = ImVec4(0.247f, 0.251f, 0.271f, 1.00f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.318f, 0.604f, 0.918f, 0.80f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.388f, 0.714f, 1.000f, 1.00f);

    // ---- Resize ----
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.318f, 0.604f, 0.918f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.318f, 0.604f, 0.918f, 0.70f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.388f, 0.714f, 1.000f, 1.00f);

    // ---- Tab ----
    c[ImGuiCol_Tab]                = ImVec4(0.141f, 0.145f, 0.157f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.270f, 0.510f, 0.780f, 0.80f);
    c[ImGuiCol_TabActive]          = ImVec4(0.208f, 0.392f, 0.596f, 1.00f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.118f, 0.122f, 0.133f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.165f, 0.310f, 0.471f, 1.00f);

    // ---- Docking ----
    c[ImGuiCol_DockingPreview]     = ImVec4(0.318f, 0.604f, 0.918f, 0.50f);
    c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.086f, 0.090f, 0.098f, 1.00f);

    // ---- Plot ----
    c[ImGuiCol_PlotLines]          = ImVec4(0.388f, 0.714f, 1.000f, 1.00f);
    c[ImGuiCol_PlotHistogram]      = ImVec4(0.318f, 0.604f, 0.918f, 1.00f);

    // ---- 文字 ----
    c[ImGuiCol_Text]               = ImVec4(0.882f, 0.890f, 0.910f, 1.00f);
    c[ImGuiCol_TextDisabled]       = ImVec4(0.450f, 0.458f, 0.490f, 1.00f);
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.318f, 0.604f, 0.918f, 0.35f);

    // ---- Modal ----
    c[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
}

void UIManager::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyProfessionalTheme();

    // 加载字体：优先微软雅黑（支持中文），fallback 到内置字体
    // 基础 ASCII 范围 + CJK 统一表意文字
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 1;

    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",    // 微软雅黑
        "C:/Windows/Fonts/simhei.ttf",  // 黑体
        "C:/Windows/Fonts/simsun.ttc",  // 宋体
    };

    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        font = io.Fonts->AddFontFromFileTTF(
            path, 15.0f, &fontCfg,
            io.Fonts->GetGlyphRangesChineseFull()
        );
        if (font) break;
    }
    if (!font) {
        io.Fonts->AddFontDefault();
    }
    // 注意: 新版 ImGui 由后端自动构建字体图集(RendererHasTextures)，
    // 不可再手动调用 io.Fonts->Build()，否则会触发断言崩溃。

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

    // Menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Tree"))
                graph.buildDefaultTemplate();
            ImGui::Separator();
            if (ImGui::MenuItem("Open Project...")) {
                std::string path = projectFileDialog(false);
                if (!path.empty()) {
                    if (ProjectIO::load(graph, path)) {
                        selectedNodeId = INVALID_NODE;
                        m_nodeEditor.requestReposition();
                    }
                }
            }
            if (ImGui::MenuItem("Save Project...")) {
                std::string path = projectFileDialog(true);
                if (!path.empty()) ProjectIO::save(graph, path);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tree")) {
            if (ImGui::MenuItem("Reset to Default"))
                graph.buildDefaultTemplate();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Wireframe", nullptr, &wireframe);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Left: Node Graph
    ImGui::Begin("Node Graph");
    m_nodeEditor.render(graph, selectedNodeId);
    ImGui::End();

    // Right: Properties
    m_property.render(selectedNodeId, graph);

    // Lighting panel
    ImGui::Begin("Lighting");
    {
        auto& L = renderer.lighting;
        ImGui::SeparatorText("Direction");
        ImGui::SliderFloat("X##ld", &L.lightDir.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Y##ld", &L.lightDir.y,  0.0f, 1.0f);
        ImGui::SliderFloat("Z##ld", &L.lightDir.z, -1.0f, 1.0f);
        ImGui::SeparatorText("Colors");
        ImGui::ColorEdit3("Light Color",    &L.lightColor.x);
        ImGui::ColorEdit3("Sky Ambient",    &L.ambientTop.x);
        ImGui::ColorEdit3("Ground Ambient", &L.ambientBot.x);
    }
    ImGui::End();

    // Viewport
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
