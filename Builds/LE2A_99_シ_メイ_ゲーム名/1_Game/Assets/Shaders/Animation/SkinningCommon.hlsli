#ifndef SKINNINGCOMMON_HLSLI
#define SKINNINGCOMMON_HLSLI

struct SkinningVertex
{
    float4 position;
    float3 normal;
    float2 texcoord;
    uint4 boneIndices;
    float4 boneWeights;
};

float4 ApplySkinningPosition(float4 position, uint4 boneIndices, float4 boneWeights, StructuredBuffer<float4x4> boneMatrices)
{
    return
        mul(position, boneMatrices[boneIndices.x]) * boneWeights.x +
        mul(position, boneMatrices[boneIndices.y]) * boneWeights.y +
        mul(position, boneMatrices[boneIndices.z]) * boneWeights.z +
        mul(position, boneMatrices[boneIndices.w]) * boneWeights.w;
}

float3 ApplySkinningNormal(float3 normal, uint4 boneIndices, float4 boneWeights, StructuredBuffer<float4x4> boneMatrices)
{
    return
        mul(float4(normal, 0.0f), boneMatrices[boneIndices.x]).xyz * boneWeights.x +
        mul(float4(normal, 0.0f), boneMatrices[boneIndices.y]).xyz * boneWeights.y +
        mul(float4(normal, 0.0f), boneMatrices[boneIndices.z]).xyz * boneWeights.z +
        mul(float4(normal, 0.0f), boneMatrices[boneIndices.w]).xyz * boneWeights.w;
}

#endif
