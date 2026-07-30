#ifndef CG2_TEMPORAL_COMMON_HLSLI
#define CG2_TEMPORAL_COMMON_HLSLI

cbuffer TemporalConstants : register(b0)
{
    uint2 gRenderSize;
    float2 gInverseRenderSize;
    row_major float4x4 gMatrixA;
    row_major float4x4 gMatrixB;
    float4 gTemporalParameters;
    float4 gViewportRect;
};

SamplerState gLinearClampSampler : register(s0);
SamplerState gPointClampSampler : register(s1);

float2 GetScreenUv(uint2 pixelPosition)
{
    return (float2(pixelPosition) + 0.5f) * gInverseRenderSize;
}

float2 ScreenUvToViewportUv(float2 screenUv)
{
    const float2 pixelPosition = screenUv * float2(gRenderSize);
    return (pixelPosition - gViewportRect.xy) / max(gViewportRect.zw, 1.0f);
}

float2 ViewportUvToScreenUv(float2 viewportUv)
{
    const float2 pixelPosition = gViewportRect.xy + viewportUv * gViewportRect.zw;
    return pixelPosition * gInverseRenderSize;
}

float3 ReconstructWorldPosition(float2 screenUv, float depth, row_major float4x4 inverseViewProjection)
{
    const float2 viewportUv = ScreenUvToViewportUv(screenUv);
    const float2 ndc = float2(
        viewportUv.x * 2.0f - 1.0f,
        1.0f - viewportUv.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, depth, 1.0f), inverseViewProjection);
    const float safeW = abs(worldPosition.w) >= 0.00001f ? worldPosition.w : 0.00001f;
    return worldPosition.xyz / safeW;
}

float2 ProjectWorldPosition(float3 worldPosition, row_major float4x4 viewProjection)
{
    const float4 clipPosition = mul(float4(worldPosition, 1.0f), viewProjection);
    const float safeW = abs(clipPosition.w) >= 0.00001f ? clipPosition.w : 0.00001f;
    const float2 ndc = clipPosition.xy / safeW;
    const float2 viewportUv = float2(
        ndc.x * 0.5f + 0.5f,
        0.5f - ndc.y * 0.5f);
    return ViewportUvToScreenUv(viewportUv);
}

bool IsScreenUvValid(float2 screenUv)
{
    const float2 viewportUv = ScreenUvToViewportUv(screenUv);
    return all(viewportUv >= 0.0f) && all(viewportUv <= 1.0f);
}

bool ResolveViewportDispatchPixel(uint2 localDispatchPosition, out uint2 pixelPosition)
{
    const uint2 viewportOrigin = uint2(max(floor(gViewportRect.xy), 0.0f));
    pixelPosition = viewportOrigin + localDispatchPosition;
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float2 viewportEnd = gViewportRect.xy + gViewportRect.zw;

    return all(pixelPosition < gRenderSize) &&
        all(pixelCenter >= gViewportRect.xy) &&
        all(pixelCenter < viewportEnd);
}

int2 GetViewportMinimumPixel()
{
    return clamp(
        int2(floor(gViewportRect.xy)),
        int2(0, 0),
        int2(gRenderSize) - int2(1, 1));
}

int2 GetViewportMaximumPixel()
{
    return clamp(
        int2(ceil(gViewportRect.xy + gViewportRect.zw)) - int2(1, 1),
        int2(0, 0),
        int2(gRenderSize) - int2(1, 1));
}

#endif
