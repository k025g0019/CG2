Texture2D<float> gSceneDepth : register(t0);
RWTexture2D<float4> gReconstructedNormal : register(u0);

cbuffer ReconstructNormalConstants : register(b0)
{
    uint2 gSourceSize;
    uint2 gDestinationSize;
    row_major float4x4 gInverseViewProjection;
};

float3 ReconstructWorldPosition(uint2 pixelPosition)
{
    const uint2 clampedPosition = min(pixelPosition, gSourceSize - 1u);
    const float depth = gSceneDepth.Load(int3(clampedPosition, 0));
    const float2 uv = (float2(clampedPosition) + 0.5f) / float2(gSourceSize);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, depth, 1.0f), gInverseViewProjection);
    const float safeWorldW = abs(worldPosition.w) >= 0.00001f
        ? worldPosition.w
        : 0.00001f;
    return worldPosition.xyz / safeWorldW;
}

//================================================================
// 隣接深度のワールド位置差から滑らかなワールド法線を再構築
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gDestinationSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const uint2 leftPosition = uint2(max(pixelPosition.x, 1u) - 1u, pixelPosition.y);
    const uint2 rightPosition = uint2(min(pixelPosition.x + 1u, gSourceSize.x - 1u), pixelPosition.y);
    const uint2 upPosition = uint2(pixelPosition.x, max(pixelPosition.y, 1u) - 1u);
    const uint2 downPosition = uint2(pixelPosition.x, min(pixelPosition.y + 1u, gSourceSize.y - 1u));

    const float3 worldLeft = ReconstructWorldPosition(leftPosition);
    const float3 worldRight = ReconstructWorldPosition(rightPosition);
    const float3 worldUp = ReconstructWorldPosition(upPosition);
    const float3 worldDown = ReconstructWorldPosition(downPosition);
    const float3 worldNormal = normalize(cross(worldRight - worldLeft, worldDown - worldUp));

    gReconstructedNormal[pixelPosition] = float4(worldNormal * 0.5f + 0.5f, 1.0f);
}
