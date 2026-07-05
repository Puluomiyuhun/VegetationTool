#include "Application.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdio>

bool Application::init(int width, int height) {
    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(width, height, "VegetationTool", nullptr, nullptr);
    if (!m_window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
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
    if (m_graph.isDirty()) {
        m_meshData = m_generator.generate(m_graph);
        m_renderer.uploadTreeMesh(m_meshData);
        m_graph.clearDirty();
    }
}

void Application::render() {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
