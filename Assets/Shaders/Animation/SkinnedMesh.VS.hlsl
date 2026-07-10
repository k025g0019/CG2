#include "SkinningCommon.hlsli"

struct SkinnedTransform
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
};

ConstantBuffer<SkinnedTransform> gSkinnedTransform : register(b0);
StructuredBuffer<float4x4> gBoneMatrices : register(t0);

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 skinnedPosition = ApplySkinningPosition(input.position, input.boneIndices, input.boneWeights, gBoneMatrices);
    const float3 skinnedNormal = normalize(ApplySkinningNormal(input.normal, input.boneIndices, input.boneWeights, gBoneMatrices));
    const float4 worldPosition = mul(skinnedPosition, gSkinnedTransform.World);
    output.position = mul(skinnedPosition, gSkinnedTransform.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(skinnedNormal, 0.0f), gSkinnedTransform.World).xyz);
    output.worldPosition = worldPosition.xyz;
    return output;
}
