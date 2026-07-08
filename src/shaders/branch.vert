#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec2 aWind;   // x=weight(尖端权重), y=phase(每枝相位)

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// ---- 顶点风力 ----
uniform int   uWindEnabled;
uniform float uWindTime;
uniform vec3  uWindDir;            // 归一化水平风向
uniform float uWindStrength;       // 总倍增
uniform float uWindGlobalStrength; // 全局摆动幅度
uniform float uWindGlobalFreq;
uniform float uWindBranchStrength; // 枝条颤动幅度
uniform float uWindBranchFreq;
uniform float uWindTreeHeight;     // 树高(世界空间)

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vViewPos;
out vec2 vUV;

// 对物体空间顶点 p 施加全局摆动 + 枝条颤动。weight = 枝条尖端权重, phase = 每枝相位。
vec3 applyWind(vec3 p, float weight, float phase) {
    if (uWindEnabled == 0) return p;
    float t = uWindTime;
    // 1) 全局摆动: 按高度比 hr^2 加权, 整树随风缓慢摇摆
    float hr = clamp(p.y / max(uWindTreeHeight, 0.001), 0.0, 1.0);
    float gsw = sin(t * uWindGlobalFreq) * 0.7 + sin(t * uWindGlobalFreq * 0.37 + 1.3) * 0.3;
    p += uWindDir * gsw * (hr * hr) * uWindGlobalStrength * uWindStrength;
    // 2) 枝条颤动: 每枝相位不同, 尖端(weight大)摆动明显
    float bsw = sin(t * uWindBranchFreq + phase);
    p += uWindDir * bsw * weight * uWindBranchStrength * uWindStrength;
    return p;
}

void main()
{
    vec3 pos = applyWind(aPos, aWind.x, aWind.y);
    vec4 worldPos  = uModel * vec4(pos, 1.0);
    vFragPos       = worldPos.xyz;
    vNormal        = mat3(transpose(inverse(uModel))) * aNormal;
    vViewPos       = vec3(inverse(uView)[3]);
    vUV            = aUV;
    gl_Position    = uProjection * uView * worldPos;
}
