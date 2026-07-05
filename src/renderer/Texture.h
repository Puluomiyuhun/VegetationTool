#pragma once
#include <glad/glad.h>
#include <string>

class Texture {
public:
    // 从文件加载，sRGB=true时用sRGB内部格式（basecolor贴图）
    bool loadFromFile(const std::string& path, bool sRGB = false);
    void destroy();

    void bind(int unit = 0) const;
    bool valid() const { return m_id != 0; }
    GLuint id()   const { return m_id; }

    const std::string& path() const { return m_path; }

private:
    GLuint      m_id   = 0;
    std::string m_path;
};
