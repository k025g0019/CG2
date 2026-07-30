#include "../Temporal/TemporalCommon.hlsli"

#include "../Temporal/HistoryClamp.hlsli"

Texture2D<float4> gCurrentReflection : register(t0);
Texture2D<float4> gReflectionHistory : register(t1);
Texture2D<float2> gDilatedVelocity : register(t2);
Texture2D<float> gDisocclusionMask : register(t3);
RWTexture2D<float4> gResolvedReflection : register(u0);

//================================================================
// SSR の欠損を履歴で補い、非連続部分では現在結果だけを採用する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const float2 currentUv = GetScreenUv(pixelPosition);
    const float2 velocity = gDilatedVelocity.Load(int3(pixelPosition, 0));
    const float2 previousUv = currentUv - velocity;
    const float4 currentReflection = gCurrentReflection.Load(int3(pixelPosition, 0));
    const float disocclusion = gDisocclusionMask.Load(int3(pixelPosition, 0));

    if (gTemporalParameters.x < 0.5f || disocclusion >= 0.5f || !IsScreenUvValid(previousUv))
    {
        gResolvedReflection[pixelPosition] = currentReflection;
        return;
    }

    float3 minimumColor;
    float3 maximumColor;
    GetNeighborhoodBounds(
        gCurrentReflection,
        int2(pixelPosition),
        gRenderSize,
        minimumColor,
        maximumColor);

    const float4 historyReflection = gReflectionHistory.SampleLevel(
        gLinearClampSampler,
        previousUv,
        0.0f);
    const float3 clampedHistoryColor = clamp(
        historyReflection.rgb,
        minimumColor,
        maximumColor);
    const float currentConfidence = saturate(currentReflection.a);
    const float historyConfidence = saturate(historyReflection.a);
    const float motionAmount = saturate(length(velocity) * float(gRenderSize.x));
    const float stableHistoryWeight = lerp(0.88f, 0.68f, motionAmount);
    const float historyWeight = stableHistoryWeight * historyConfidence;
    const float3 resolvedColor = lerp(
        currentReflection.rgb,
        clampedHistoryColor,
        historyWeight);
    const float historyDecay = currentConfidence > 0.01f ? 0.98f : 0.86f;
    const float resolvedConfidence = lerp(
        currentConfidence,
        historyConfidence * historyDecay,
        historyWeight);
    gResolvedReflection[pixelPosition] = float4(resolvedColor, resolvedConfidence);
}
