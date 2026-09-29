#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 albedo = draw.color.rgb;
    if (draw.params.x > 0.0) {
        vec2 cell = floor(vWorldPos.xz * draw.params.x);
        float checker = mod(cell.x + cell.y, 2.0);
        albedo *= mix(0.72, 1.0, checker);
    }

    vec3 N = normalize(vNormal);
    vec3 L = normalize(-frame.lightDirection.xyz);
    vec3 V = normalize(frame.cameraPosition.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float intensity = frame.lightDirection.w;
    float NdotL = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 64.0) * 0.3 * step(0.0, NdotL);

    vec3 ambient = mix(frame.groundAmbient.rgb, frame.skyAmbient.rgb, N.y * 0.5 + 0.5) * albedo;
    vec3 direct = (albedo * NdotL + specular) * frame.lightColor.rgb * intensity;

    // Simple exponential distance fog towards the horizon color.
    float dist = length(frame.cameraPosition.xyz - vWorldPos);
    float fog = 1.0 - exp(-dist * 0.004);
    vec3 fogColor = vec3(0.62, 0.72, 0.85);

    outColor = vec4(mix(ambient + direct, fogColor, fog), draw.color.a);
}
