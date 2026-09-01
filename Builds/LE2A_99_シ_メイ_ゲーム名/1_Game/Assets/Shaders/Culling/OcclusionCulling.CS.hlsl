struct CullingObject
{
    float3 boundsCenter;
    float3 boundsExtent;
    int gameObjectId;
    uint vertexCount;
};

StructuredBuffer<CullingObject> gCullingObjects : register(t0);
Texture2D<float2> gDepthPyramid : register(t1);
StructuredBuffer<uint> gFrustumVisibility : register(t2);
RWStructuredBuffer<uint> gVisibility : register(u0);
SamplerState gPointClampSampler : register(s0);

cbuffer CullingConstants : register(b0)
{
    uint gObjectCount;
    uint2 gDepthPyramidSize;
    float gDepthBias;
    row_major float4x4 gViewProjection;
    float4 gViewportUvTransform; // xy: 全体RT内の左上UV、zw: 全体RTに対するViewportのUVサイズ
};

//================================================================
// ワールド AABB を画面へ投影し、Hi-Z より奥に隠れた物体を除外する
//================================================================

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;

    if (objectIndex >= gObjectCount)
    {
        return;
    }

    if (gFrustumVisibility[objectIndex] == 0u)
    {
        gVisibility[objectIndex] = 0u;
        return;
    }

    const CullingObject cullingObject = gCullingObjects[objectIndex];
    float2 minimumUv = 1.0f;
    float2 maximumUv = 0.0f;
    float nearestObjectDepth = 1.0f;
    bool hasUnsafeCorner = false;

    [unroll]
    for (uint cornerIndex = 0u; cornerIndex < 8u; cornerIndex++)
    {
        const float3 cornerSign = float3(
            (cornerIndex & 1u) != 0u ? 1.0f : -1.0f,
            (cornerIndex & 2u) != 0u ? 1.0f : -1.0f,
            (cornerIndex & 4u) != 0u ? 1.0f : -1.0f);
        const float3 cornerPosition = cullingObject.boundsCenter + cullingObject.boundsExtent * cornerSign;
        const float4 clipPosition = mul(float4(cornerPosition, 1.0f), gViewProjection);

        if (clipPosition.w <= 0.00001f)
        {
            hasUnsafeCorner = true;
            continue;
        }

        const float3 ndc = clipPosition.xyz / clipPosition.w;
        const float2 viewportUv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        minimumUv = min(minimumUv, viewportUv);
        maximumUv = max(maximumUv, viewportUv);
        nearestObjectDepth = min(nearestObjectDepth, ndc.z);
    }

    if (hasUnsafeCorner)
    {
        gVisibility[objectIndex] = 1u;
        return;
    }

    if (maximumUv.x < 0.0f || maximumUv.y < 0.0f ||
        minimumUv.x > 1.0f || minimumUv.y > 1.0f ||
        nearestObjectDepth > 1.0f)
    {
        gVisibility[objectIndex] = 0u;
        return;
    }

    // 画面端にまたがる物体は、Hi-Z の少数サンプルだけでは安全に遮蔽判定できない。
    // ここで無理に saturate して判定すると、モデルの一部がカメラ外へ出た瞬間に全体が消える。
    if (minimumUv.x < 0.0f || minimumUv.y < 0.0f ||
        maximumUv.x > 1.0f || maximumUv.y > 1.0f)
    {
        gVisibility[objectIndex] = 1u;
        return;
    }

    minimumUv = saturate(minimumUv);
    maximumUv = saturate(maximumUv);
    const float2 centerUv = (minimumUv + maximumUv) * 0.5f;
    const float2 sampleUvs[5] = {
        centerUv,
        minimumUv,
        maximumUv,
        float2(minimumUv.x, maximumUv.y),
        float2(maximumUv.x, minimumUv.y)
    };
    float conservativeOccluderDepth = 0.0f;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 5u; sampleIndex++)
    {
        const float2 depthPyramidUv =
            gViewportUvTransform.xy + sampleUvs[sampleIndex] * gViewportUvTransform.zw;
        conservativeOccluderDepth = max(
            conservativeOccluderDepth,
            gDepthPyramid.SampleLevel(gPointClampSampler, depthPyramidUv, 0.0f).x);
    }

    const bool isOccluded = nearestObjectDepth > conservativeOccluderDepth + gDepthBias;
    gVisibility[objectIndex] = isOccluded ? 0u : 1u;
}
