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
    uint2 resolvedPixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, resolvedPixelPosition))
    {
        return;
    }

    const int2 pixelPosition = int2(resolvedPixelPosition);
    const float2 currentUv = GetScreenUv(resolvedPixelPosition);

    if (!IsScreenUvValid(currentUv))
    {
        gDilatedVelocity[pixelPosition] = 0.0f;
        return;
    }

    const int2 viewportMinimumPixel = GetViewportMinimumPixel();
    const int2 viewportMaximumPixel = GetViewportMaximumPixel();
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
                viewportMinimumPixel,
                viewportMaximumPixel);
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
