#version 330 core

// 实例化叶片顶点着色器: 原型网格(局部空间)按每实例 transform 展开。
// 与 leaf.vert 的风力观感保持一致(散布叶 windW=1.0, phase=基相位+实例序*2.4)。

// ---- 原型网格属性(每顶点) ----
layout(location = 0) in vec3 aPos;      // 原型局部坐标(茎基归零)
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
// ---- 每实例属性(divisor=1) ----
layout(location = 3) in vec3 iPos;      // 实例世界位置(= 锚点 attach)
layout(location = 4) in vec4 iRot;      // 实例朝向四元数(x,y,z,w)
layout(location = 5) in vec3 iScale;    // 实例缩放

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uAlbedo;      // 叶材质统一色(散布叶按材质 albedo)

// ---- 顶点风力(与 leaf.vert 一致) ----
uniform int   uWindEnabled;
uniform float uWindTime;
uniform vec3  uWindDir;
uniform float uWindStrength;
uniform float uWindGlobalStrength;
uniform float uWindGlobalFreq;
uniform float uWindBranchStrength;
uniform float uWindBranchFreq;
uniform float uWindLeafStrength;
uniform float uWindLeafFreq;
uniform float uWindTreeHeight;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vViewPos;
out vec2 vUV;
out vec3 vAlbedo;

// 四元数旋转向量
vec3 qrot(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// 全局摆动 + 枝条颤动(施加于任意物体空间点)
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

mat3 rotAxis(vec3 axis, float ang) {
    float s = sin(ang), c = cos(ang);
    float tt = 1.0 - c;
    vec3 a = normalize(axis);
    return mat3(
        tt*a.x*a.x + c,      tt*a.x*a.y + s*a.z,  tt*a.x*a.z - s*a.y,
        tt*a.x*a.y - s*a.z,  tt*a.y*a.y + c,      tt*a.y*a.z + s*a.x,
        tt*a.x*a.z + s*a.y,  tt*a.y*a.z - s*a.x,  tt*a.z*a.z + c);
}

void main()
{
    // 实例展开: 原型局部 → 缩放 → 旋转 → 平移到锚点
    vec3 rel0 = qrot(iRot, aPos * iScale);   // 相对锚点的偏移
    vec3 pos  = iPos + rel0;                 // 世界坐标
    vec3 nrm  = qrot(iRot, aNormal);
    vec3 anchorObj = iPos;                   // 该片叶锚点(= 实例位置)

    if (uWindEnabled != 0) {
        // 散布叶: windW=1.0, phase=基相位(iPos 哈希) + 实例序*2.4
        float basePhase = fract(sin(dot(iPos, vec3(11.17, 23.31, 41.53))) * 7411.31) * 6.2831
                          + float(gl_InstanceID) * 2.4;
        float r1 = fract(sin(dot(anchorObj, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float r2 = fract(sin(dot(anchorObj, vec3(39.346,  11.135, 83.155))) * 24634.6345);
        float freqScale = 0.6  + r1 * 0.8;
        float ampScale  = 0.65 + r2 * 0.7;
        float ph        = basePhase + r1 * 6.2831;

        vec3 anchor = applyWind(anchorObj, 1.0, basePhase);
        vec3 rel    = rel0;
        float t   = uWindTime;
        float amp = uWindLeafStrength * uWindStrength * ampScale;
        float aY  = sin(t * uWindLeafFreq * freqScale       + ph)       * amp;
        float aR  = sin(t * uWindLeafFreq * freqScale * 1.3 + ph * 1.7) * amp * 0.7;
        mat3 R = rotAxis(uWindDir, aR) * rotAxis(vec3(0,1,0), aY);
        pos = anchor + R * rel;
        nrm = R * nrm;
    }

    vec4 worldPos = uModel * vec4(pos, 1.0);
    vFragPos      = worldPos.xyz;
    vNormal       = mat3(transpose(inverse(uModel))) * nrm;
    vViewPos      = vec3(inverse(uView)[3]);
    vUV           = aUV;
    vAlbedo       = uAlbedo;
    gl_Position   = uProjection * uView * worldPos;
}
