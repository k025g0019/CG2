#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (std140, binding = 0) uniform vertexBuffer {
    mat4 ProjectionMatrix;
} myVertexBuffer;
layout (location = 0) in vec4 pos;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec4 inColor;
layout (location = 0) out vec2 outTexCoord;
layout (location = 1) out vec4 outColor;
void main() {
   outColor = inColor;
   outTexCoord = inTexCoord;
   gl_Position = myVertexBuffer.ProjectionMatrix * pos;
}
