#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
RWTexture2D<float2> gVelocity : register(u0);

//================================================================
// 現在の深度を前フレーム ViewProjection へ投影して画面速度を求める
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const float depth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float2 currentUv = GetScreenUv(pixelPosition);

    if (depth >= 0.999999f || gTemporalParameters.x < 0.5f)
    {
        gVelocity[pixelPosition] = 0.0f;
        return;
    }

    const float3 worldPosition = ReconstructWorldPosition(currentUv, depth, gMatrixA);
    const float2 previousUv = ProjectWorldPosition(worldPosition, gMatrixB);
    gVelocity[pixelPosition] = IsScreenUvValid(previousUv) ? currentUv - previousUv : 0.0f;
}
