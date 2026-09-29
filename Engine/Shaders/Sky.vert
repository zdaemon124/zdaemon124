#version 450

layout(location = 0) out vec2 vNdc;

void main()
{
    // Full-screen triangle, no vertex buffer needed.
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vNdc = uv * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
