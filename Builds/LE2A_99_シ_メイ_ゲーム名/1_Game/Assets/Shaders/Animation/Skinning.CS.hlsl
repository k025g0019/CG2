#include "SkinningCommon.hlsli"

StructuredBuffer<SkinningVertex> gInputVertices : register(t0);
StructuredBuffer<float4x4> gBoneMatrices : register(t1);
RWStructuredBuffer<float4> gOutputPositions : register(u0);
RWStructuredBuffer<float4> gOutputNormals : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const SkinningVertex vertex = gInputVertices[dispatchThreadId.x];
    const float4 skinnedPosition = ApplySkinningPosition(vertex.position, vertex.boneIndices, vertex.boneWeights, gBoneMatrices);
    const float3 skinnedNormal = normalize(ApplySkinningNormal(vertex.normal, vertex.boneIndices, vertex.boneWeights, gBoneMatrices));
    gOutputPositions[dispatchThreadId.x] = skinnedPosition;
    gOutputNormals[dispatchThreadId.x] = float4(skinnedNormal, 0.0f);
}
