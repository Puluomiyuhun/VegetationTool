#pragma once
#include <glad/glad.h>

class Framebuffer {
public:
    void create(int width, int height);
    void resize(int width, int height);
    void bind() const;
    void unbind() const;
    void destroy();

    GLuint colorTexture() const { return m_colorTex; }
    int    width()  const { return m_width; }
    int    height() const { return m_height; }
    bool   valid()  const { return m_fbo != 0; }

private:
    GLuint m_fbo      = 0;
    GLuint m_colorTex = 0;
    GLuint m_depthRbo = 0;
    int    m_width    = 0;
    int    m_height   = 0;

    void allocate();
};
