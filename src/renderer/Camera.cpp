#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

OrbitCamera::OrbitCamera() {}

void OrbitCamera::onMouseDrag(float dx, float dy) {
    azimuth   -= dx * 0.4f;   // 反转水平，符合直觉（向右拖 = 视角向右转）
    elevation += dy * 0.4f;   // 反转垂直
    elevation  = std::clamp(elevation, -89.0f, 89.0f);
}

void OrbitCamera::onMiddleDrag(float dx, float dy) {
    float s = distance * 0.001f;
    glm::vec3 right = glm::normalize(glm::cross(
        glm::vec3(0,1,0), position() - target));
    glm::vec3 up    = glm::vec3(0, 1, 0);
    target -= right * (dx * s) - up * (dy * s);
}

void OrbitCamera::onScroll(float delta) {
    distance -= delta * 0.5f;
    distance  = std::clamp(distance, 1.0f, 100.0f);
}

glm::vec3 OrbitCamera::position() const {
    float az = glm::radians(azimuth);
    float el = glm::radians(elevation);
    return target + glm::vec3(
        distance * std::cos(el) * std::sin(az),
        distance * std::sin(el),
        distance * std::cos(el) * std::cos(az)
    );
}

glm::mat4 OrbitCamera::viewMatrix() const {
    return glm::lookAt(position(), target, glm::vec3(0, 1, 0));
}

glm::mat4 OrbitCamera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, 0.05f, 500.0f);
}

void OrbitCamera::reset() {
    azimuth   = 45.0f;
    elevation = 30.0f;
    distance  = 12.0f;
    target    = {0.0f, 2.0f, 0.0f};
}
