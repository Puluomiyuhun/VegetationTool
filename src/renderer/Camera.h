#pragma once
#include <glm/glm.hpp>

class OrbitCamera {
public:
    OrbitCamera();

    void onMouseDrag(float dx, float dy);     // 左键旋转
    void onMiddleDrag(float dx, float dy);    // 中键平移
    void onScroll(float delta);               // 滚轮缩放
    // 右键飞行: 按相机局部方向平移(fwd=前后, right=左右, up=世界上下), 单位=世界距离。
    void flyMove(float fwd, float right, float up);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
    glm::vec3 position() const;

    void reset();

    float azimuth   = 45.0f;
    float elevation = 30.0f;
    float distance  = 12.0f;
    glm::vec3 target = {0.0f, 2.0f, 0.0f};
    float fov       = 45.0f;
};
