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

out vec4 FragColor;

const float PI = 3.14159265;

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
    vec3 ambient = mix(uAmbientBot, uAmbientTop, hemi) * albedo * uAoStrength;

    vec3 diffuse = albedo * uLightColor * NdotL;

    // SSS
    float backLight = max(dot(-N, L), 0.0);
    vec3  sss       = albedo * uLightColor * backLight * uSssStrength * 0.6;

    // 高光
    vec3  H    = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), max(8.0 * (1.0 - roughness), 1.0));
    vec3  specular = vec3(spec * 0.04 * (1.0 - roughness));

    vec3 color = ambient + diffuse + sss + specular;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}
