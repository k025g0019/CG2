#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (location = 0) in vec2 inTexCoord;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout(set=0, binding=1) uniform texture2D sTexture;
layout(set=0, binding=2) uniform sampler sSampler;

void main() {
#if 1
   outColor = inColor * texture(sampler2D(sTexture, sSampler), inTexCoord.st);
#else
   outColor = inColor;
#endif
}
