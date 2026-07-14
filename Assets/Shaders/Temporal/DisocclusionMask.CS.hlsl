#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float> gPreviousDepth : register(t1);
Texture2D<float2> gDilatedVelocity : register(t2);
RWTexture2D<float> gDisocclusionMask : register(u0);

//================================================================
// 前フレームに存在しなかった領域を検出して履歴の混入を止める
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
    const float currentDepth = gSceneDepth.Load(int3(pixelPosition, 0));

    if (gTemporalParameters.x < 0.5f || !IsScreenUvValid(previousUv))
    {
        gDisocclusionMask[pixelPosition] = 1.0f;
        return;
    }

    const float previousDepth = gPreviousDepth.SampleLevel(gPointClampSampler, previousUv, 0.0f);
    const float depthThreshold = max(gTemporalParameters.y, 0.0001f) * max(currentDepth, 0.1f);
    gDisocclusionMask[pixelPosition] = abs(currentDepth - previousDepth) > depthThreshold ? 1.0f : 0.0f;
}
