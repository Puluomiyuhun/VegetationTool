#version 330 core

// FXAA (Timothy Lottes, 经典版): 单帧边缘检测抗锯齿。基于亮度梯度找边缘方向,
// 沿边缘做定向模糊。相比 TAA 无历史/无拖影, 单帧即生效, 但对亚像素闪烁抑制弱于 TAA。
in  vec2 vUv;
out vec4 FragColor;

uniform sampler2D uTex;
uniform vec4 uTexel;   // xy = 1/分辨率

#define FXAA_SPAN_MAX   8.0
#define FXAA_REDUCE_MUL (1.0/8.0)
#define FXAA_REDUCE_MIN (1.0/128.0)

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 inv = uTexel.xy;
    vec3 rgbNW = texture(uTex, vUv + vec2(-1.0,-1.0) * inv).rgb;
    vec3 rgbNE = texture(uTex, vUv + vec2( 1.0,-1.0) * inv).rgb;
    vec3 rgbSW = texture(uTex, vUv + vec2(-1.0, 1.0) * inv).rgb;
    vec3 rgbSE = texture(uTex, vUv + vec2( 1.0, 1.0) * inv).rgb;
    vec3 rgbM  = texture(uTex, vUv).rgb;

    float lNW = luma(rgbNW), lNE = luma(rgbNE);
    float lSW = luma(rgbSW), lSE = luma(rgbSE);
    float lM  = luma(rgbM);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // 边缘方向 = 亮度梯度的垂向
    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));

    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * inv;

    vec3 rgbA = 0.5 * (texture(uTex, vUv + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(uTex, vUv + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uTex, vUv + dir * -0.5).rgb +
                                     texture(uTex, vUv + dir *  0.5).rgb);
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
