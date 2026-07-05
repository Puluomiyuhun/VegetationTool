#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vViewPos;
out vec2 vUV;

void main()
{
    vec4 worldPos  = uModel * vec4(aPos, 1.0);
    vFragPos       = worldPos.xyz;
    vNormal        = mat3(transpose(inverse(uModel))) * aNormal;
    vViewPos       = vec3(inverse(uView)[3]);
    vUV            = aUV;
    gl_Position    = uProjection * uView * worldPos;
}
