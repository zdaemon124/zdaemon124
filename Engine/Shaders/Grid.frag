#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

float GridLines(vec2 coord)
{
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

// Infinite-looking editor grid on the XZ plane: 1 m cells, 10 m major lines, colored axes.
void main()
{
    vec2 coord = vWorldPos.xz;
    float minor = GridLines(coord);
    float major = GridLines(coord / 10.0);

    vec3 color = vec3(0.55);
    float alpha = max(minor * 0.22, major * 0.45);

    vec2 derivative = fwidth(coord);
    if (abs(coord.y) < derivative.y * 1.5) { color = vec3(0.86, 0.3, 0.3); alpha = 0.8; } // X axis
    if (abs(coord.x) < derivative.x * 1.5) { color = vec3(0.3, 0.5, 0.95); alpha = 0.8; } // Z axis

    float dist = length(frame.cameraPosition.xyz - vWorldPos);
    float fade = 1.0 - smoothstep(20.0, 120.0 + abs(frame.cameraPosition.y) * 4.0, dist);
    outColor = vec4(color, alpha * fade);
    if (outColor.a < 0.01)
        discard;
}
