#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 farPoint = frame.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - frame.cameraPosition.xyz);

    vec3 zenith = vec3(0.03, 0.12, 0.45);
    vec3 ground = vec3(0.05, 0.045, 0.04);

    vec3 color = dir.y >= 0.0 ? mix(kHorizonColor, zenith, pow(dir.y, 0.45))
                              : mix(kHorizonColor, ground, pow(-dir.y, 0.35));

    if (frame.lightDirection.w > 0.0) {
        vec3 toSun = normalize(-frame.lightDirection.xyz);
        float sun = max(dot(dir, toSun), 0.0);
        vec3 sunColor = SRGBToLinear(frame.lightColor.rgb) * frame.lightDirection.w;
        color += sunColor * (pow(sun, 1500.0) * 8.0 + pow(sun, 12.0) * 0.12);
    }

    outColor = vec4(LinearToSRGB(color), 1.0);
}
