//============================================================
// Ocean GPU Tessellation Vertex Shader
//============================================================

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct OceanControlPoint
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

OceanControlPoint main(VertexShaderInput input)
{
    OceanControlPoint output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    return output;
}
