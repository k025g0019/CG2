#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float2> gVelocity : register(t1);
RWTexture2D<float2> gDilatedVelocity : register(u0);

//================================================================
// 輪郭の欠損を防ぐため、3x3 内で最も手前の画素速度を広げる
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const int2 pixelPosition = int2(dispatchThreadId.xy);
    float nearestDepth = 1.0f;
    float2 selectedVelocity = 0.0f;

    [unroll]
    for (int offsetY = -1; offsetY <= 1; offsetY++)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; offsetX++)
        {
            const int2 samplePosition = clamp(
                pixelPosition + int2(offsetX, offsetY),
                int2(0, 0),
                int2(gRenderSize) - 1);
            const float sampleDepth = gSceneDepth.Load(int3(samplePosition, 0));

            if (sampleDepth <= nearestDepth)
            {
                nearestDepth = sampleDepth;
                selectedVelocity = gVelocity.Load(int3(samplePosition, 0));
            }
        }
    }

    gDilatedVelocity[pixelPosition] = selectedVelocity;
}
