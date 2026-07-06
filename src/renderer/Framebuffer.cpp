#include "Framebuffer.h"
#include <stdexcept>

void Framebuffer::create(int width, int height) {
    m_width  = width;
    m_height = height;
    allocate();
}

void Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    destroy();
    m_width  = width;
    m_height = height;
    allocate();
}

void Framebuffer::setSamples(int samples) {
    if (samples < 1) samples = 1;
    if (samples == m_samples) return;
    m_samples = samples;
    if (m_width > 0 && m_height > 0) {
        destroy();
        allocate();
    }
}

void Framebuffer::allocate() {
    // 供 ImGui 显示的普通(单采样)颜色纹理
    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height,
                 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (m_samples > 1) {
        // 多重采样渲染目标：颜色 + 深度 用多重采样 renderbuffer
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenRenderbuffers(1, &m_msaaColorRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGB8,
                                         m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, m_msaaColorRbo);

        glGenRenderbuffers(1, &m_depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples,
                                         GL_DEPTH24_STENCIL8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_depthRbo);

        // resolve FBO：把 MSAA 结果 blit 到普通纹理供显示
        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_colorTex, 0);
    } else {
        // 单采样：纹理直接作为颜色附件
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_colorTex, 0);

        glGenRenderbuffers(1, &m_depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_depthRbo);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() const {
    // 多重采样：把 MSAA 颜色缓冲 resolve(下采样) 到普通纹理
    if (m_samples > 1 && m_resolveFbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
        glBlitFramebuffer(0, 0, m_width, m_height,
                          0, 0, m_width, m_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::destroy() {
    if (m_fbo)         { glDeleteFramebuffers(1,  &m_fbo);         m_fbo         = 0; }
    if (m_resolveFbo)  { glDeleteFramebuffers(1,  &m_resolveFbo);  m_resolveFbo  = 0; }
    if (m_colorTex)    { glDeleteTextures(1,      &m_colorTex);    m_colorTex    = 0; }
    if (m_depthRbo)    { glDeleteRenderbuffers(1, &m_depthRbo);    m_depthRbo    = 0; }
    if (m_msaaColorRbo){ glDeleteRenderbuffers(1, &m_msaaColorRbo);m_msaaColorRbo= 0; }
}
