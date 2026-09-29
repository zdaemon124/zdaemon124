#version 450

layout(push_constant) uniform UIData {
    vec4 screen; // xy = target size in pixels
} ui;

layout(location = 0) in vec2 inPosition; // pixels, top-left origin
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    vUV = inUV;
    vColor = inColor;
    gl_Position = vec4(inPosition / ui.screen.xy * 2.0 - 1.0, 0.0, 1.0);
}
