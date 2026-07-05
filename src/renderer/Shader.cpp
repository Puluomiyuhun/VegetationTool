#include "Shader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, 512, nullptr, buf);
        throw std::runtime_error(std::string("Shader compile error: ") + buf);
    }
    return s;
}

void Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    std::string vs = readFile(vertPath);
    std::string fs = readFile(fragPath);
    loadFromSource(vs.c_str(), fs.c_str());
}

void Shader::loadFromSource(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER,   vertSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    GLint ok;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(id, 512, nullptr, buf);
        throw std::runtime_error(std::string("Program link error: ") + buf);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Shader::use() const { glUseProgram(id); }
void Shader::destroy()   { if (id) { glDeleteProgram(id); id = 0; } }

void Shader::setMat4(const char* name, const float* value) const {
    glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, value);
}
void Shader::setVec3(const char* name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(id, name), x, y, z);
}
void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(id, name), x, y, z, w);
}
void Shader::setInt(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(id, name), value);
}
void Shader::setFloat(const char* name, float value) const {
    glUniform1f(glGetUniformLocation(id, name), value);
}
