#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
RWTexture2D<float> gPreviousDepth : register(u0);

//================================================================
// 次フレームの非連続判定で使う深度を保存する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    gPreviousDepth[dispatchThreadId.xy] = gSceneDepth.Load(int3(dispatchThreadId.xy, 0));
}
