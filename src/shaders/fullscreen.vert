#version 330 core

// 全屏三角形: 用 gl_VertexID 生成, 无需顶点缓冲。输出 [0,1] 的 uv 供后处理采样。
out vec2 vUv;

void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;                       // (0,0),(2,0),(0,2) 覆盖屏幕
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
