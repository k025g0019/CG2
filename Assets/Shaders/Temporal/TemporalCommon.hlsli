#ifndef CG2_TEMPORAL_COMMON_HLSLI
#define CG2_TEMPORAL_COMMON_HLSLI

cbuffer TemporalConstants : register(b0)
{
    uint2 gRenderSize;
    float2 gInverseRenderSize;
    row_major float4x4 gMatrixA;
    row_major float4x4 gMatrixB;
    float4 gTemporalParameters;
};

SamplerState gLinearClampSampler : register(s0);
SamplerState gPointClampSampler : register(s1);

float2 GetScreenUv(uint2 pixelPosition)
{
    return (float2(pixelPosition) + 0.5f) * gInverseRenderSize;
}

float3 ReconstructWorldPosition(float2 screenUv, float depth, row_major float4x4 inverseViewProjection)
{
    const float2 ndc = float2(screenUv.x * 2.0f - 1.0f, 1.0f - screenUv.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, depth, 1.0f), inverseViewProjection);
    const float safeW = abs(worldPosition.w) >= 0.00001f ? worldPosition.w : 0.00001f;
    return worldPosition.xyz / safeW;
}

float2 ProjectWorldPosition(float3 worldPosition, row_major float4x4 viewProjection)
{
    const float4 clipPosition = mul(float4(worldPosition, 1.0f), viewProjection);
    const float safeW = abs(clipPosition.w) >= 0.00001f ? clipPosition.w : 0.00001f;
    const float2 ndc = clipPosition.xy / safeW;
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

bool IsScreenUvValid(float2 screenUv)
{
    return all(screenUv >= 0.0f) && all(screenUv <= 1.0f);
}

#endif
