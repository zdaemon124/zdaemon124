#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

void main()
{
    vec3 albedo = SRGBToLinear(draw.color.rgb * texture(albedoTexture, vUV).rgb);
    if (draw.params.x > 0.0) {
        vec2 cell = floor(vWorldPos.xz * draw.params.x);
        float checker = mod(cell.x + cell.y, 2.0);
        albedo *= mix(0.6, 1.0, checker);
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing)
        N = -N;
    vec3 L = normalize(-frame.lightDirection.xyz);
    vec3 V = normalize(frame.cameraPosition.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    vec3 lightColor = SRGBToLinear(frame.lightColor.rgb) * frame.lightDirection.w;
    float NdotL = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 64.0) * 0.25 * step(0.0, NdotL);

    vec3 sky = SRGBToLinear(frame.skyAmbient.rgb);
    vec3 ground = SRGBToLinear(frame.groundAmbient.rgb);
    vec3 ambient = mix(ground, sky, N.y * 0.5 + 0.5) * frame.skyAmbient.w * albedo;
    vec3 direct = (albedo * NdotL + specular) * lightColor;

    float dist = length(frame.cameraPosition.xyz - vWorldPos);
    float fog = 1.0 - exp(-dist * 0.004);

    outColor = vec4(LinearToSRGB(mix(ambient + direct, kHorizonColor, fog)), draw.color.a);
}
