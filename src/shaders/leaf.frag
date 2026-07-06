#version 330 core

in vec3 vNormal;
in vec3 vFragPos;
in vec3 vViewPos;
in vec2 vUV;
in vec3 vAlbedo;

uniform float uRoughness;
uniform float uAoStrength;
uniform float uSssStrength;
uniform float uAlphaCutoff;

uniform sampler2D uTexBaseColor;
uniform sampler2D uTexRoughness;
uniform sampler2D uTexNormal;
uniform sampler2D uTexOpacity;
uniform int uHasBaseColor;
uniform int uHasRoughness;
uniform int uHasNormal;
uniform int uHasOpacity;

uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform vec3  uAmbientTop;
uniform vec3  uAmbientBot;
uniform float uLightIntensity;
uniform float uAmbientStrength;
uniform float uExposure;

// 阴影
uniform mat4      uLightSpace;
uniform sampler2D uShadowMap;
uniform int   uShadowEnabled;
uniform float uShadowStrength;
uniform float uShadowBias;

out vec4 FragColor;

const float PI = 3.14159265;

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
    return 1.0 - shadow * uShadowStrength;
}

void main()
{
    // 不透明度遮罩：优先用独立 opacity 贴图(R通道)，否则用 baseColor 的 alpha 通道
    float opacity = 1.0;
    if (uHasOpacity != 0)
        opacity = texture(uTexOpacity, vUV).r;
    else if (uHasBaseColor != 0)
        opacity = texture(uTexBaseColor, vUV).a;
    if (opacity < uAlphaCutoff) discard;

    vec3  N   = normalize(vNormal);
    vec3  V   = normalize(vViewPos - vFragPos);
    // 叶片双面
    if (dot(N, V) < 0.0) N = -N;

    // 可选法线贴图
    if (uHasNormal != 0) {
        vec3 nMap = texture(uTexNormal, vUV).rgb * 2.0 - 1.0;
        vec3 up   = abs(N.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0);
        vec3 T    = normalize(cross(up, N));
        vec3 B    = cross(N, T);
        N = normalize(T * nMap.x + B * nMap.y + N * nMap.z);
    }

    // albedo：贴图 > per-vertex
    vec3  albedo    = (uHasBaseColor != 0) ? texture(uTexBaseColor, vUV).rgb : vAlbedo;
    float roughness = uRoughness;
    if (uHasRoughness != 0) roughness = texture(uTexRoughness, vUV).r;

    vec3  L   = normalize(uLightDir);
    float NdotL = max(dot(N, L), 0.0);

    float hemi   = dot(N, vec3(0,1,0)) * 0.5 + 0.5;
    vec3 ambient = mix(uAmbientBot, uAmbientTop, hemi) * albedo * uAoStrength * uAmbientStrength;

    vec3 diffuse = albedo * uLightColor * uLightIntensity * NdotL;

    // SSS
    float backLight = max(dot(-N, L), 0.0);
    vec3  sss       = albedo * uLightColor * uLightIntensity * backLight * uSssStrength * 0.6;

    // 高光
    vec3  H    = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), max(8.0 * (1.0 - roughness), 1.0));
    vec3  specular = vec3(spec * 0.04 * (1.0 - roughness)) * uLightIntensity;

    // 阴影作用于直接漫反射+高光(不含 SSS 透光与环境光)
    float vis = shadowVisibility(vFragPos, NdotL);
    vec3 color = ambient + sss + (diffuse + specular) * vis;
    color *= uExposure;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}
