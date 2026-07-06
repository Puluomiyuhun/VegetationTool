#version 330 core

// 阴影深度 pass：把几何体从光源视角渲染到深度贴图。
// branch(8 floats) 与 leaf(11 floats) 顶点前三个属性一致(pos+normal+uv)，
// 这里只用 aPos，UV 用于叶片 alpha 剔除。
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uLightSpace;
uniform mat4 uModel;

out vec2 vUV;

void main()
{
    vUV = aUV;
    gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
}
