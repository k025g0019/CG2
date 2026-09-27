#include "../Temporal/TemporalCommon.hlsli"

Texture2D<float4> gTemporalReflection : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gWorldNormal : register(t2);
Texture2D<float4> gMaterialMask : register(t3);
RWTexture2D<float4> gDenoisedReflection : register(u0);

//================================================================
// 深度・法線・粗さを保つバイラテラルフィルターで SSR を平滑化する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 resolvedPixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, resolvedPixelPosition))
    {
        return;
    }

    const int2 pixelPosition = int2(resolvedPixelPosition);
    const float centerDepth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float3 centerNormal = normalize(
        gWorldNormal.Load(int3(pixelPosition, 0)).xyz * 2.0f - 1.0f);
    const float roughness = saturate(gMaterialMask.Load(int3(pixelPosition, 0)).g);
    const int filterRadius = roughness >= 0.5f ? 2 : 1;
    const int2 viewportMinimumPixel = GetViewportMinimumPixel();
    const int2 viewportMaximumPixel = GetViewportMaximumPixel();
    float3 accumulatedColor = 0.0f;
    float accumulatedColorWeight = 0.0f;
    float accumulatedConfidence = 0.0f;
    float accumulatedWeight = 0.0f;

    [loop]
    for (int offsetY = -filterRadius; offsetY <= filterRadius; offsetY++)
    {
        [loop]
        for (int offsetX = -filterRadius; offsetX <= filterRadius; offsetX++)
        {
            const int2 samplePosition = clamp(
                pixelPosition + int2(offsetX, offsetY),
                viewportMinimumPixel,
                viewportMaximumPixel);
            const float sampleDepth = gSceneDepth.Load(int3(samplePosition, 0));
            const float3 sampleNormal = normalize(
                gWorldNormal.Load(int3(samplePosition, 0)).xyz * 2.0f - 1.0f);
            const float sampleRoughness = saturate(
                gMaterialMask.Load(int3(samplePosition, 0)).g);
            const float4 sampleReflection = gTemporalReflection.Load(int3(samplePosition, 0));
            const float depthWeight = exp2(-abs(sampleDepth - centerDepth) * 640.0f);
            const float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), 24.0f);
            const float roughnessWeight = exp2(-abs(sampleRoughness - roughness) * 12.0f);
            const float spatialWeight = 1.0f / (1.0f + float(offsetX * offsetX + offsetY * offsetY));
            const float sampleWeight =
                depthWeight * normalWeight * roughnessWeight * spatialWeight;
            const float colorWeight = sampleWeight * max(sampleReflection.a, 0.001f);
            accumulatedColor += sampleReflection.rgb * colorWeight;
            accumulatedColorWeight += colorWeight;
            accumulatedConfidence += sampleReflection.a * sampleWeight;
            accumulatedWeight += sampleWeight;
        }
    }

    gDenoisedReflection[pixelPosition] = float4(
        accumulatedColor / max(accumulatedColorWeight, 0.0001f),
        accumulatedConfidence / max(accumulatedWeight, 0.0001f));
}
