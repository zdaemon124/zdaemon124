// Shared declarations. Must match FrameUniforms / DrawPushConstants in Renderer.cpp.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 lightDirection; // xyz = direction the light travels, w = intensity (0 = no light)
    vec4 lightColor;     // sRGB
    vec4 skyAmbient;     // sRGB, w = ambient intensity
    vec4 groundAmbient;  // sRGB
    vec4 time;           // x = seconds
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 color;  // sRGB
    vec4 params; // x = checker scale
} draw;

vec3 SRGBToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 LinearToSRGB(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

const vec3 kHorizonColor = vec3(0.36, 0.48, 0.66); // linear
