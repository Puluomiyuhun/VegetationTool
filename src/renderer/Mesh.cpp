#include "Mesh.h"
#include <stdexcept>

void Mesh::create(const std::vector<float>&    vertices,
                  const std::vector<uint32_t>& indices,
                  const std::vector<int>&      attribSizes)
{
    destroy();
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint32_t),
                 indices.data(), GL_DYNAMIC_DRAW);

    // 计算总步长
    int stride = 0;
    for (int s : attribSizes) stride += s;
    stride *= sizeof(float);

    int offset = 0;
    for (int i = 0; i < (int)attribSizes.size(); ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, attribSizes[i], GL_FLOAT, GL_FALSE,
                              stride, (void*)(intptr_t)(offset * sizeof(float)));
        offset += attribSizes[i];
    }

    glBindVertexArray(0);
    m_indexCount = (int)indices.size();
}

void Mesh::update(const std::vector<float>&    vertices,
                  const std::vector<uint32_t>& indices)
{
    if (!m_vao) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(uint32_t),
                 indices.data(), GL_DYNAMIC_DRAW);
    m_indexCount = (int)indices.size();
}

void Mesh::draw(GLenum mode) const {
    if (!m_vao || m_indexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(mode, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::destroy() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;
}
