#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vFragPos;
out vec3 vNormal;

void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vFragPos   = world.xyz;
    vNormal    = mat3(uModel) * aNormal;
    gl_Position = uViewProjection * world;
}
