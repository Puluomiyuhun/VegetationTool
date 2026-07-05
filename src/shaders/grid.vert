#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 uViewProjection;
uniform vec3 uColor;

out vec3 vColor;

void main()
{
    vColor      = uColor;
    gl_Position = uViewProjection * vec4(aPos, 1.0);
}
