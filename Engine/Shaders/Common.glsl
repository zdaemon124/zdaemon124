// Shared declarations. Must match FrameUniforms / DrawPushConstants in Renderer.cpp.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 lightDirection; // xyz = direction the light travels, w = intensity
    vec4 lightColor;
    vec4 skyAmbient;
    vec4 groundAmbient;
    vec4 time;
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 color;
    vec4 params; // x = checker scale
} draw;
