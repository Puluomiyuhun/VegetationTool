#version 330 core

in vec2 vNdc;
out vec4 FragColor;

uniform mat4  uInvViewProj;   // 逆 view-projection，用于重建世界视线方向
uniform vec3  uCamPos;
uniform vec3  uSkyTop;        // 天顶色
uniform vec3  uSkyHorizon;    // 地平线色(暖雾)
uniform vec3  uGround;        // 地平线以下色

void main()
{
    // 由 NDC 反投影出远平面世界点，减相机位得到视线方向
    vec4 worldFar = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(worldFar.xyz / worldFar.w - uCamPos);

    float h = dir.y;  // 视线仰角分量 [-1,1]

    vec3 color;
    if (h > 0.0) {
        // 地平线->天顶：平滑过渡，靠近地平线保留暖雾
        float t = pow(clamp(h, 0.0, 1.0), 0.42);
        color = mix(uSkyHorizon, uSkyTop, t);
    } else {
        // 地平线以下：雾色向地面色渐变
        float t = pow(clamp(-h, 0.0, 1.0), 0.5);
        color = mix(uSkyHorizon, uGround, t);
    }

    FragColor = vec4(color, 1.0);
}
