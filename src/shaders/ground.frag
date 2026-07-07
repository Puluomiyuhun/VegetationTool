#version 330 core

in vec3 vFragPos;   // 世界坐标
in vec3 vNormal;

// 用与天空完全相同的渐变公式对地面着色, 使地面与远端天空无缝融合(看不出面片)
uniform vec3  uCamPos;
uniform vec3  uSkyTop;        // 天顶色
uniform vec3  uSkyHorizon;    // 地平线色(暖雾)
uniform vec3  uSkyGround;     // 地平线以下色

uniform vec3  uLightDir;
uniform float uAlpha;         // 地面不透明度: <1 让被遮挡的植被半透明透出

// 阴影(与树共用光源空间矩阵/深度贴图)
uniform mat4      uLightSpace;
uniform sampler2D uShadowMap;
uniform int   uShadowEnabled;
uniform float uGroundShadowStrength;  // 地面接收阴影的强度(独立于树的 shadowStrength)
uniform float uShadowBias;

out vec4 FragColor;

// PCF 3x3 采样阴影贴图，返回可见度[0,1]，0=全阴影
float shadowVisibility(vec3 fragPos, float NdotL) {
    if (uShadowEnabled == 0) return 1.0;
    vec4 lsPos = uLightSpace * vec4(fragPos, 1.0);
    vec3 proj  = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float bias = max(uShadowBias * (1.0 - NdotL), uShadowBias * 0.2);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(uShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    shadow /= 9.0;
    return 1.0 - shadow * uGroundShadowStrength;
}

void main()
{
    // 视线方向: 相机 -> 该地面片元, 与 sky.frag 中的 dir 语义一致
    vec3 dir = normalize(vFragPos - uCamPos);
    float h = dir.y;

    // 与 sky.frag 完全相同的渐变: h>0 天顶插值, h<=0 地平线->地面色
    vec3 skyColor;
    if (h > 0.0) {
        float t = pow(clamp(h, 0.0, 1.0), 0.42);
        skyColor = mix(uSkyHorizon, uSkyTop, t);
    } else {
        float t = pow(clamp(-h, 0.0, 1.0), 0.5);
        skyColor = mix(uSkyHorizon, uSkyGround, t);
    }

    // 接收阴影: 用光线与地面法线(朝上)的夹角作 NdotL, 仅做可见度压暗
    float NdotL = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    float vis   = shadowVisibility(vFragPos, NdotL);

    FragColor = vec4(skyColor * vis, uAlpha);
}
