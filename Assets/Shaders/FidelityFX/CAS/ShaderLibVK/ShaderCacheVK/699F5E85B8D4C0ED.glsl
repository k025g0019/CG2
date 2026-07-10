#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (std140, binding = 0) uniform _cbPerObject
{
    mat4        u_mWorldViewProj;
    vec4        u_Center;
    vec4        u_Radius;
    vec4        u_Color;
} cbPerObject;
layout(location = 0) in vec3 position; 
layout (location = 0) out vec4 outColor;
void main() {
   outColor = cbPerObject.u_Color;
   gl_Position = cbPerObject.u_mWorldViewProj * (vec4(cbPerObject.u_Center.xyz + position * cbPerObject.u_Radius.xyz, 1.0f));
}
