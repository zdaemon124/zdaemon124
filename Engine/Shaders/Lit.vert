#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;

void main()
{
    vec4 worldPos = draw.model * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(draw.model))) * inNormal;
    vUV = inUV;
    gl_Position = frame.viewProjection * worldPos;
}
