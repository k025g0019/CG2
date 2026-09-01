struct CullingObject
{
    float3 boundsCenter;
    float3 boundsExtent;
    int gameObjectId;
    uint vertexCount;
};

StructuredBuffer<CullingObject> gCullingObjects : register(t0);
RWStructuredBuffer<uint> gFrustumVisibility : register(u0);

cbuffer CullingConstants : register(b0)
{
    uint gObjectCount;
    uint2 gDepthPyramidSize;
    float gDepthBias;
    row_major float4x4 gViewProjection;
    float4 gUnusedParameters;
};

//================================================================
// AABB の全頂点が同じクリップ平面外にある物体だけを除外する
//================================================================

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;

    if (objectIndex >= gObjectCount)
    {
        return;
    }

    const CullingObject cullingObject = gCullingObjects[objectIndex];
    uint outsideLeftCount = 0u;
    uint outsideRightCount = 0u;
    uint outsideBottomCount = 0u;
    uint outsideTopCount = 0u;
    uint outsideNearCount = 0u;
    uint outsideFarCount = 0u;

    [unroll]
    for (uint cornerIndex = 0u; cornerIndex < 8u; cornerIndex++)
    {
        const float3 cornerSign = float3(
            (cornerIndex & 1u) != 0u ? 1.0f : -1.0f,
            (cornerIndex & 2u) != 0u ? 1.0f : -1.0f,
            (cornerIndex & 4u) != 0u ? 1.0f : -1.0f);
        const float3 cornerPosition =
            cullingObject.boundsCenter + cullingObject.boundsExtent * cornerSign;
        const float4 clipPosition = mul(float4(cornerPosition, 1.0f), gViewProjection);
        outsideLeftCount += clipPosition.x < -clipPosition.w ? 1u : 0u;
        outsideRightCount += clipPosition.x > clipPosition.w ? 1u : 0u;
        outsideBottomCount += clipPosition.y < -clipPosition.w ? 1u : 0u;
        outsideTopCount += clipPosition.y > clipPosition.w ? 1u : 0u;
        outsideNearCount += clipPosition.z < 0.0f ? 1u : 0u;
        outsideFarCount += clipPosition.z > clipPosition.w ? 1u : 0u;
    }

    const bool isOutsideFrustum =
        outsideLeftCount == 8u || outsideRightCount == 8u ||
        outsideBottomCount == 8u || outsideTopCount == 8u ||
        outsideNearCount == 8u || outsideFarCount == 8u;
    gFrustumVisibility[objectIndex] = isOutsideFrustum ? 0u : 1u;
}
