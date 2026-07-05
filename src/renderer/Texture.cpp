#include "Texture.h"
#include <stb_image.h>
#include <cstdio>

bool Texture::loadFromFile(const std::string& path, bool sRGB) {
    destroy();
    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    if (!data) return false;

    GLenum internalFmt, fmt;
    if (ch == 4) { fmt = GL_RGBA; internalFmt = sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8; }
    else if (ch == 3) { fmt = GL_RGB;  internalFmt = sRGB ? GL_SRGB8       : GL_RGB8;  }
    else if (ch == 1) { fmt = GL_RED;  internalFmt = GL_R8; }
    else { stbi_image_free(data); return false; }

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    m_path = path;
    return true;
}

void Texture::destroy() {
    if (m_id) { glDeleteTextures(1, &m_id); m_id = 0; }
    m_path.clear();
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}
