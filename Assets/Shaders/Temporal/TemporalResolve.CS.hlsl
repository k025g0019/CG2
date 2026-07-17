#include "TemporalCommon.hlsli"
#include "HistoryClamp.hlsli"

Texture2D<float4> gCurrentColor : register(t0);
Texture2D<float4> gColorHistory : register(t1);
Texture2D<float2> gDilatedVelocity : register(t2);
Texture2D<float> gReactiveMask : register(t3);
RWTexture2D<float4> gResolvedColor : register(u0);

//================================================================
// 履歴色を現在近傍の範囲へ制限して時間方向のちらつきを抑える
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
    const float4 currentColor = gCurrentColor.Load(int3(pixelPosition, 0));
    const float reactiveMask = gReactiveMask.Load(int3(pixelPosition, 0));

    if (gTemporalParameters.x < 0.5f || reactiveMask >= 0.98f || !IsScreenUvValid(previousUv))
    {
        gResolvedColor[pixelPosition] = currentColor;
        return;
    }

    float3 minimumColor;
    float3 maximumColor;
    GetNeighborhoodBounds(gCurrentColor, int2(pixelPosition), gRenderSize, minimumColor, maximumColor);
    const float3 historyColor = gColorHistory.SampleLevel(gLinearClampSampler, previousUv, 0.0f).rgb;
    const float3 clampedHistoryColor = clamp(historyColor, minimumColor, maximumColor);
    const float motionAmount = saturate(length(velocity) * float(gRenderSize.x));
    const float configuredHistoryWeight = saturate(gTemporalParameters.y);
    const float stableHistoryWeight = lerp(configuredHistoryWeight, configuredHistoryWeight * 0.78f, motionAmount);
    const float historyWeight = stableHistoryWeight * (1.0f - saturate(reactiveMask));
    float3 resolvedColor = lerp(currentColor.rgb, clampedHistoryColor, historyWeight);

    const int2 pixel = int2(pixelPosition);
    const int2 maxPixel = int2(gRenderSize) - int2(1, 1);
    const float3 neighborAverage = (
        gCurrentColor.Load(int3(min(pixel + int2(1, 0), maxPixel), 0)).rgb +
        gCurrentColor.Load(int3(max(pixel - int2(1, 0), int2(0, 0)), 0)).rgb +
        gCurrentColor.Load(int3(min(pixel + int2(0, 1), maxPixel), 0)).rgb +
        gCurrentColor.Load(int3(max(pixel - int2(0, 1), int2(0, 0)), 0)).rgb) * 0.25f;
    resolvedColor += (resolvedColor - neighborAverage) * saturate(gTemporalParameters.z);

    gResolvedColor[pixelPosition] = float4(resolvedColor, currentColor.a);
}
