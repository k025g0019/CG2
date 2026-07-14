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
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const int2 pixelPosition = int2(dispatchThreadId.xy);
    const float centerDepth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float3 centerNormal = normalize(
        gWorldNormal.Load(int3(pixelPosition, 0)).xyz * 2.0f - 1.0f);
    const float roughness = saturate(1.0f - gMaterialMask.Load(int3(pixelPosition, 0)).g);
    const int filterRadius = roughness >= 0.5f ? 2 : 1;
    float4 accumulatedReflection = 0.0f;
    float accumulatedWeight = 0.0f;

    [loop]
    for (int offsetY = -filterRadius; offsetY <= filterRadius; offsetY++)
    {
        [loop]
        for (int offsetX = -filterRadius; offsetX <= filterRadius; offsetX++)
        {
            const int2 samplePosition = clamp(
                pixelPosition + int2(offsetX, offsetY),
                int2(0, 0),
                int2(gRenderSize) - 1);
            const float sampleDepth = gSceneDepth.Load(int3(samplePosition, 0));
            const float3 sampleNormal = normalize(
                gWorldNormal.Load(int3(samplePosition, 0)).xyz * 2.0f - 1.0f);
            const float depthWeight = exp2(-abs(sampleDepth - centerDepth) * 640.0f);
            const float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), 24.0f);
            const float spatialWeight = 1.0f / (1.0f + float(offsetX * offsetX + offsetY * offsetY));
            const float sampleWeight = depthWeight * normalWeight * spatialWeight;
            accumulatedReflection +=
                gTemporalReflection.Load(int3(samplePosition, 0)) * sampleWeight;
            accumulatedWeight += sampleWeight;
        }
    }

    gDenoisedReflection[pixelPosition] = accumulatedReflection / max(accumulatedWeight, 0.0001f);
}
