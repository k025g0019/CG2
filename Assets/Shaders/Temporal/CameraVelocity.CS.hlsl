#include "TemporalCommon.hlsli"

Texture2D<float> gSceneDepth : register(t0);
Texture2D<float2> gObjectMotionVector : register(t1);
RWTexture2D<float2> gVelocity : register(u0);

//================================================================
// 現在の深度を前フレーム ViewProjection へ投影して画面速度を求める
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, pixelPosition))
    {
        return;
    }

    const float depth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float2 currentUv = GetScreenUv(pixelPosition);

    if (!IsScreenUvValid(currentUv) ||
        depth >= 0.999999f ||
        gTemporalParameters.x < 0.5f)
    {
        gVelocity[pixelPosition] = 0.0f;
        return;
    }

    const float3 worldPosition = ReconstructWorldPosition(currentUv, depth, gMatrixA);
    const float2 previousUv = ProjectWorldPosition(worldPosition, gMatrixB);
    const float2 cameraVelocity =
        IsScreenUvValid(previousUv) ? currentUv - previousUv : 0.0f;
    const float2 objectVelocity = gObjectMotionVector.Load(int3(pixelPosition, 0));
    gVelocity[pixelPosition] =
        dot(objectVelocity, objectVelocity) > 0.00000001f
            ? objectVelocity
            : cameraVelocity;
}
