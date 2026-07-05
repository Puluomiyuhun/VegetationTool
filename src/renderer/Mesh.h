#pragma once
#include <glad/glad.h>
#include <vector>

class Mesh {
public:
    // attribSizes: e.g. {3,3} = pos(3)+normal(3), {3,3,4} = pos+normal+color
    void create(const std::vector<float>&    vertices,
                const std::vector<uint32_t>& indices,
                const std::vector<int>&      attribSizes);

    void update(const std::vector<float>&    vertices,
                const std::vector<uint32_t>& indices);

    void draw(GLenum mode = GL_TRIANGLES) const;
    void destroy();

    bool valid() const { return m_vao != 0; }
    int  indexCount() const { return m_indexCount; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    int    m_indexCount = 0;
};
