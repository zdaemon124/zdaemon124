#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 farPoint = frame.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - frame.cameraPosition.xyz);

    vec3 zenith = vec3(0.16, 0.36, 0.75);
    vec3 horizon = vec3(0.62, 0.72, 0.85);
    vec3 ground = vec3(0.24, 0.22, 0.2);

    vec3 color = dir.y >= 0.0 ? mix(horizon, zenith, pow(dir.y, 0.45))
                              : mix(horizon, ground, pow(-dir.y, 0.35));

    vec3 toSun = normalize(-frame.lightDirection.xyz);
    float sun = max(dot(dir, toSun), 0.0);
    color += frame.lightColor.rgb * (pow(sun, 1500.0) * 8.0 + pow(sun, 12.0) * 0.15);

    outColor = vec4(color, 1.0);
}
