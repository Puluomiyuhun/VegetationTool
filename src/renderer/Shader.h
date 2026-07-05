#pragma once
#include <glad/glad.h>
#include <string>

class Shader {
public:
    GLuint id = 0;

    void loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    void loadFromSource(const char* vertSrc, const char* fragSrc);
    void use() const;
    void destroy();

    void setMat4(const char* name, const float* value) const;
    void setVec3(const char* name, float x, float y, float z) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;

private:
    GLuint compile(GLenum type, const char* src);
};
