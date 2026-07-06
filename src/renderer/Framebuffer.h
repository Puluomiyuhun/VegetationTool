#pragma once
#include <glad/glad.h>

class Framebuffer {
public:
    void create(int width, int height);
    void resize(int width, int height);
    // 设置 MSAA 采样数(1=关闭)。变化时会重建 FBO。
    void setSamples(int samples);
    int  samples() const { return m_samples; }

    void bind() const;
    void unbind() const;   // 若为多重采样，会把 MSAA 结果 resolve 到普通纹理
    void destroy();

    GLuint colorTexture() const { return m_colorTex; }
    int    width()  const { return m_width; }
    int    height() const { return m_height; }
    bool   valid()  const { return m_fbo != 0; }

private:
    GLuint m_fbo      = 0;   // 主渲染目标(MSAA 时为多重采样 FBO)
    GLuint m_colorTex = 0;   // 供 ImGui 显示的普通纹理(resolve 目标)
    GLuint m_depthRbo = 0;

    // 多重采样附件与 resolve FBO(仅 m_samples>1 时使用)
    GLuint m_msaaColorRbo = 0;
    GLuint m_resolveFbo   = 0;

    int    m_width    = 0;
    int    m_height   = 0;
    int    m_samples  = 4;

    void allocate();
};
