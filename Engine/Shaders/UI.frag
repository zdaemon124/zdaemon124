#version 450

layout(set = 0, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

// Sprites and text glyphs (text atlas is white with coverage in alpha).
void main()
{
    outColor = texture(spriteTexture, vUV) * vColor;
}
