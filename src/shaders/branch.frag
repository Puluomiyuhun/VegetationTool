#version 330 core

in vec3 vNormal;
in vec3 vFragPos;
in vec3 vViewPos;
in vec2 vUV;

// 材质 uniform（贴图不可用时的回退值）
uniform vec3  uAlbedo;
uniform float uRoughness;
uniform float uMetallic;
uniform float uAoStrength;

// 贴图（可选）
uniform sampler2D uTexBaseColor;
uniform sampler2D uTexRoughness;   // R=roughness, G=metallic
uniform sampler2D uTexNormal;
uniform int uHasBaseColor;
uniform int uHasRoughness;
uniform int uHasNormal;

// 光照
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform vec3  uAmbientTop;
uniform vec3  uAmbientBot;

out vec4 FragColor;

const float PI = 3.14159265;

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-5);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    float r  = roughness + 1.0;
    float k  = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // 法线（可选法线贴图）
    vec3 N = normalize(vNormal);
    if (uHasNormal != 0) {
        vec3 nMap = texture(uTexNormal, vUV).rgb * 2.0 - 1.0;
        // 简单TBN：用vNormal为Z，建立正交基
        vec3 up    = abs(N.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0);
        vec3 T     = normalize(cross(up, N));
        vec3 B     = cross(N, T);
        N = normalize(T * nMap.x + B * nMap.y + N * nMap.z);
    }

    // 材质参数（贴图优先）
    vec3  albedo    = (uHasBaseColor != 0) ? texture(uTexBaseColor, vUV).rgb : uAlbedo;
    float roughness = uRoughness;
    float metallic  = uMetallic;
    if (uHasRoughness != 0) {
        vec2 rm     = texture(uTexRoughness, vUV).rg;
        roughness   = rm.r;
        metallic    = rm.g;
    }

    vec3  V   = normalize(vViewPos - vFragPos);
    vec3  L   = normalize(uLightDir);
    vec3  H   = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // 半球环境光
    float hemi   = dot(N, vec3(0,1,0)) * 0.5 + 0.5;
    vec3  ambient = mix(uAmbientBot, uAmbientTop, hemi) * albedo * uAoStrength;

    // PBR
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = F_Schlick(HdotV, F0);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-5);
    vec3 kD       = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse  = kD * albedo / PI;

    vec3 Lo    = (diffuse + specular) * uLightColor * NdotL;
    vec3 color = ambient + Lo;

    // Reinhard tonemapping + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}
