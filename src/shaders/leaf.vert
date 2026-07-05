#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;    // per-vertex albedo

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vViewPos;
out vec2 vUV;
out vec3 vAlbedo;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos      = worldPos.xyz;
    vNormal       = mat3(transpose(inverse(uModel))) * aNormal;
    vViewPos      = vec3(inverse(uView)[3]);
    vUV           = aUV;
    vAlbedo       = aColor;
    gl_Position   = uProjection * uView * worldPos;
}
