#include "TemporalCommon.hlsli"

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gMaterialMask : register(t1);
Texture2D<float> gDisocclusionMask : register(t2);
Texture2D<float2> gDilatedVelocity : register(t3);
RWTexture2D<float> gReactiveMask : register(u0);

//================================================================
// 発光・反射境界・高速移動を検出し、履歴を弱める領域を作る
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const float3 sceneColor = gSceneColor.Load(int3(pixelPosition, 0)).rgb;
    const float4 materialMask = gMaterialMask.Load(int3(pixelPosition, 0));
    const float disocclusion = gDisocclusionMask.Load(int3(pixelPosition, 0));
    const float2 velocity = gDilatedVelocity.Load(int3(pixelPosition, 0));
    const float luminance = dot(sceneColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float emissiveReaction = saturate((luminance - 2.0f) * 0.25f);
    const float reflectionReaction = saturate(materialMask.r * materialMask.g);
    const float motionReaction = saturate(length(velocity * float2(gRenderSize)) * 0.075f);
    gReactiveMask[pixelPosition] = max(
        disocclusion,
        max(emissiveReaction, max(reflectionReaction * 0.35f, motionReaction)));
}
