#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProjection;
uniform float uOutlineWidth;  // 沿法线外扩距离(世界单位)

void main()
{
    vec3 p = aPos + normalize(aNormal) * uOutlineWidth;
    gl_Position = uViewProjection * vec4(p, 1.0);
}
