#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;    // per-vertex albedo
layout(location = 4) in vec2 aWind;     // x=weight, y=phase
layout(location = 5) in vec3 aAnchor;   // 叶片锚点(物体空间), 叶片绕此点摆动

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// ---- 顶点风力 ----
uniform int   uWindEnabled;
uniform float uWindTime;
uniform vec3  uWindDir;
uniform float uWindStrength;
uniform float uWindGlobalStrength;
uniform float uWindGlobalFreq;
uniform float uWindBranchStrength;
uniform float uWindBranchFreq;
uniform float uWindLeafStrength;   // 叶片旋转幅度(弧度)
uniform float uWindLeafFreq;
uniform float uWindTreeHeight;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vViewPos;
out vec2 vUV;
out vec3 vAlbedo;

// 全局摆动 + 枝条颤动(与 branch.vert 相同), 施加于任意物体空间点。
vec3 applyWind(vec3 p, float weight, float phase) {
    if (uWindEnabled == 0) return p;
    float t = uWindTime;
    float hr = clamp(p.y / max(uWindTreeHeight, 0.001), 0.0, 1.0);
    float gsw = sin(t * uWindGlobalFreq) * 0.7 + sin(t * uWindGlobalFreq * 0.37 + 1.3) * 0.3;
    p += uWindDir * gsw * (hr * hr) * uWindGlobalStrength * uWindStrength;
    float bsw = sin(t * uWindBranchFreq + phase);
    p += uWindDir * bsw * weight * uWindBranchStrength * uWindStrength;
    return p;
}

// 绕任意单位轴 axis 旋转角 ang 的矩阵(Rodrigues)
mat3 rotAxis(vec3 axis, float ang) {
    float s = sin(ang), c = cos(ang);
    float t = 1.0 - c;
    vec3 a = normalize(axis);
    return mat3(
        t*a.x*a.x + c,      t*a.x*a.y + s*a.z,  t*a.x*a.z - s*a.y,
        t*a.x*a.y - s*a.z,  t*a.y*a.y + c,      t*a.y*a.z + s*a.x,
        t*a.x*a.z + s*a.y,  t*a.y*a.z - s*a.x,  t*a.z*a.z + c);
}

void main()
{
    vec3 pos = aPos;
    vec3 nrm = aNormal;
    if (uWindEnabled != 0) {
        // 每片叶从锚点哈希出两个随机量, 令频率/幅度/相位各不相同 → 打破同质化。
        // 同一叶卡的 4 个顶点共享同一 anchor, 故整片叶运动一致(不会撕裂)。
        float r1 = fract(sin(dot(aAnchor, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float r2 = fract(sin(dot(aAnchor, vec3(39.346,  11.135, 83.155))) * 24634.6345);
        float freqScale = 0.6  + r1 * 0.8;         // [0.6,1.4] 摆速各异 → 持续错相
        float ampScale  = 0.65 + r2 * 0.7;         // [0.65,1.35] 幅度差异
        float ph        = aWind.y + r1 * 6.2831;   // 相位随机化

        // 锚点随全局+枝条风一起位移, 叶片作为整体跟随
        vec3 anchor = applyWind(aAnchor, aWind.x, aWind.y);
        vec3 rel    = aPos - aAnchor;
        // 叶片摆动: 绕 Y(tumble) 与风向(ripple) 做小角度正弦旋转
        float t   = uWindTime;
        float amp = uWindLeafStrength * uWindStrength * ampScale;
        float aY  = sin(t * uWindLeafFreq * freqScale       + ph)       * amp;
        float aR  = sin(t * uWindLeafFreq * freqScale * 1.3 + ph * 1.7) * amp * 0.7;
        mat3 R = rotAxis(uWindDir, aR) * rotAxis(vec3(0,1,0), aY);
        pos = anchor + R * rel;
        nrm = R * aNormal;
    }

    vec4 worldPos = uModel * vec4(pos, 1.0);
    vFragPos      = worldPos.xyz;
    vNormal       = mat3(transpose(inverse(uModel))) * nrm;
    vViewPos      = vec3(inverse(uView)[3]);
    vUV           = aUV;
    vAlbedo       = aColor;
    gl_Position   = uProjection * uView * worldPos;
}
