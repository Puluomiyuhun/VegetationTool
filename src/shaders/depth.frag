#version 330 core

// 阴影深度 pass 片元着色器：仅写深度。
// 对叶片按 opacity 遮罩做 alpha 剔除，避免叶片方片投出实心方块阴影。
in vec2 vUV;

uniform sampler2D uTexOpacity;
uniform sampler2D uTexBaseColor;
uniform int   uHasOpacity;
uniform int   uHasBaseColor;
uniform int   uIsLeaf;
uniform float uAlphaCutoff;

void main()
{
    if (uIsLeaf != 0) {
        float opacity = 1.0;
        if (uHasOpacity != 0)
            opacity = texture(uTexOpacity, vUV).r;
        else if (uHasBaseColor != 0)
            opacity = texture(uTexBaseColor, vUV).a;
        if (opacity < uAlphaCutoff) discard;
    }
    // 深度自动写入，无需输出颜色
}
