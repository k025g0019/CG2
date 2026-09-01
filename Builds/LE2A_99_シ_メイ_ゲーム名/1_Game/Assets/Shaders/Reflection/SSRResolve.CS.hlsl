#include "../Temporal/TemporalCommon.hlsli"

#include "ReflectionCommon.hlsli"

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gTraceResult : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
Texture2D<float4> gWorldNormal : register(t3);
RWTexture2D<float4> gResolvedReflection : register(u0);

//================================================================
// Trace が返した命中 UV から反射色を取得する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, pixelPosition))
    {
        return;
    }

    const float4 traceResult = gTraceResult.Load(int3(pixelPosition, 0));

    if (traceResult.a <= 0.0f || !IsScreenUvValid(traceResult.xy))
    {
        gResolvedReflection[pixelPosition] = 0.0f;
        return;
    }

    const float4 materialMask = gMaterialMask.Load(int3(pixelPosition, 0));
    const float roughness = saturate(materialMask.g);
    const float3 sourceNormal = normalize(
        gWorldNormal.Load(int3(pixelPosition, 0)).xyz * 2.0f - 1.0f);
    const float3 hitNormal = normalize(
        gWorldNormal.SampleLevel(gPointClampSampler, traceResult.xy, 0.0f).xyz * 2.0f - 1.0f);
    const float3 worldPosition = ReconstructWorldPosition(
        GetScreenUv(pixelPosition),
        traceResult.z,
        gMatrixA);
    const float3 viewDirection = normalize(gTemporalParameters.xyz - worldPosition);
    const float3 reflectionDirection = normalize(reflect(-viewDirection, sourceNormal));
    const float normalDotView = saturate(dot(sourceNormal, viewDirection));
    const float hitFacing = saturate(dot(hitNormal, -reflectionDirection) * 4.0f);
    const float fresnel = ReflectionFresnelSchlickRoughness(
        normalDotView,
        saturate(materialMask.r),
        roughness);
    const float reflectionIntensity = materialMask.a > 0.0001f
        ? materialMask.a
        : 1.0f;
    const float roughnessVisibility = 1.0f - smoothstep(0.65f, 0.98f, roughness);
    const float3 reflectionColor = gSceneColor.SampleLevel(
        gLinearClampSampler,
        traceResult.xy,
        0.0f).rgb;
    gResolvedReflection[pixelPosition] = float4(
        reflectionColor,
        traceResult.a * hitFacing * fresnel * roughnessVisibility * reflectionIntensity);
}
