#version 330 core

// 呈现 pass: 把 TAA 结果直接拷到目标 FBO(视口显示纹理)。
in  vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;

void main()
{
    FragColor = vec4(texture(uTex, vUv).rgb, 1.0);
}
