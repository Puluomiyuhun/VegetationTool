#version 330 core

// TAA resolve: 当前帧 + 历史帧时域混合, 消除叶片 alpha-test 硬边的锯齿/闪烁。
// 无 motion vector, 用深度重投影处理相机运动(静态几何); 风力摆动的叶子靠邻域 clamp 抑制拖影。
in  vec2 vUv;
out vec4 FragColor;

uniform sampler2D uCurrent;   // 本帧场景颜色(带 jitter)
uniform sampler2D uHistory;   // 上一帧 TAA 结果
uniform sampler2D uDepth;     // 本帧深度(NDC z, [0,1])
uniform mat4  uInvViewProj;   // 本帧 inverse(proj*view)(不含 jitter, 用于重建世界坐标)
uniform mat4  uPrevViewProj;  // 上一帧 proj*view(不含 jitter)
uniform vec4  uTexel;         // xy = 1/分辨率
uniform float uBlend;         // 历史权重(≈0.9); 0 表示禁用历史(首帧/相机重置)

// RGB↔YCoCg: 在 YCoCg 空间做邻域包围盒 clamp, 比 RGB 更贴合人眼、拖影更少。
vec3 rgb2ycocg(vec3 c) {
    return vec3(0.25*c.r + 0.5*c.g + 0.25*c.b,
                0.5*c.r - 0.5*c.b,
               -0.25*c.r + 0.5*c.g - 0.25*c.b);
}
vec3 ycocg2rgb(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

void main()
{
    vec3 cur = texture(uCurrent, vUv).rgb;

    if (uBlend <= 0.0) { FragColor = vec4(cur, 1.0); return; }

    // 1) 用本帧深度重建世界坐标, 投到上一帧裁剪空间, 得到历史采样 uv(相机运动的重投影)。
    float z = texture(uDepth, vUv).r;
    vec4 ndc = vec4(vUv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    vec4 wp  = uInvViewProj * ndc;
    wp /= wp.w;
    vec4 prevClip = uPrevViewProj * wp;
    vec2 histUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // 历史 uv 越界(相机转动露出的新区域) → 无历史可用, 直接用当前帧。
    if (histUv.x < 0.0 || histUv.x > 1.0 || histUv.y < 0.0 || histUv.y > 1.0) {
        FragColor = vec4(cur, 1.0); return;
    }

    vec3 hist = texture(uHistory, histUv).rgb;

    // 2) 邻域 3x3 包围盒 clamp: 把历史约束到本帧邻域颜色范围内, 抑制鬼影/拖尾。
    vec3 cmin = rgb2ycocg(cur), cmax = cmin, ycur = cmin;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        if (x == 0 && y == 0) continue;
        vec3 s = rgb2ycocg(texture(uCurrent, vUv + vec2(x, y) * uTexel.xy).rgb);
        cmin = min(cmin, s);
        cmax = max(cmax, s);
    }
    vec3 yhist = clamp(rgb2ycocg(hist), cmin, cmax);

    // 3) 时域混合。
    vec3 outY = mix(ycur, yhist, uBlend);
    FragColor = vec4(max(ycocg2rgb(outY), vec3(0.0)), 1.0);
}
