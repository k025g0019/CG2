#include "../Temporal/TemporalCommon.hlsli"

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gTraceResult : register(t1);
Texture2D<float> gSceneDepth : register(t2);
Texture2D<float4> gWorldNormal : register(t3);
RWTexture2D<float4> gResolvedReflection : register(u0);

//================================================================
// Trace が返した命中 UV から反射色を取得する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const float4 traceResult = gTraceResult.Load(int3(pixelPosition, 0));

    if (traceResult.a <= 0.0f || !IsScreenUvValid(traceResult.xy))
    {
        gResolvedReflection[pixelPosition] = 0.0f;
        return;
    }

    const float sourceDepth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float hitDepth = gSceneDepth.SampleLevel(gPointClampSampler, traceResult.xy, 0.0f);
    const float3 worldNormal = normalize(
        gWorldNormal.Load(int3(pixelPosition, 0)).xyz * 2.0f - 1.0f);
    const float depthContinuity = saturate(1.0f - abs(hitDepth - traceResult.z) * 160.0f);
    const float surfaceValidity = sourceDepth < 0.999999f && dot(worldNormal, worldNormal) > 0.25f
        ? 1.0f
        : 0.0f;
    const float3 reflectionColor = gSceneColor.SampleLevel(
        gLinearClampSampler,
        traceResult.xy,
        0.0f).rgb;
    gResolvedReflection[pixelPosition] = float4(
        reflectionColor,
        traceResult.a * depthContinuity * surfaceValidity);
}
