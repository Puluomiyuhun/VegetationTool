#version 330 core

// 全屏三角形：用 gl_VertexID 生成，无需顶点缓冲
out vec2 vNdc;

void main()
{
    // (0,0)->(-1,-1), (1,0)->(3,-1), (0,1)->(-1,3) 覆盖整个屏幕
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
