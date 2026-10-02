

#include "Particle.hlsli"
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

StructuredBuffer<TransformationMatrix> gTransformationMatrix : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input,uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    TransformationMatrix gTransformationMatrix[10];
    output.position = mul(input.position, gTransformationMatrix[instanceId].WVP);
    output.texcoord = input.texcoord;
    float4 transformedNormal =normalize(mul(input.normal,(float32_t3x3)gTransformationMatrix[instanceId].World));

    
    output.normal = transformedNormal.xyz;

    return output;
}