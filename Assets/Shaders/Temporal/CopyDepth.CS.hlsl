#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
RWTexture2D<float> gPreviousDepth : register(u0);

//================================================================
// 次フレームの非連続判定で使う深度を保存する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, pixelPosition))
    {
        return;
    }

    gPreviousDepth[pixelPosition] = gSceneDepth.Load(int3(pixelPosition, 0));
}
