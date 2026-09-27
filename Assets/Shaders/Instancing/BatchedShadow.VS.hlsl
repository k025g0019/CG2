#include "BatchedInstanceCommon.hlsli"

cbuffer ShadowView : register(b3)
{
    row_major float4x4 shadowViewProjection;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    const float4 worldPosition = mul(input.position, gBatchedInstances[instanceId].World);
    output.position = mul(worldPosition, shadowViewProjection);
    output.texcoord = input.texcoord;
    return output;
}
