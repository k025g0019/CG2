#include "SMAACommon.hlsli"

Texture2D<float4> gEdgeTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer SmaaConstants : register(b0)
{
    float2 gTexelSize;
    float gEdgeThreshold;
    float gCornerRounding;
    float4 gUnusedParameters;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 edge = gEdgeTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f).rg;
    const float leftDistance = SearchEdgeLength(gEdgeTexture, gLinearSampler, input.texcoord, float2(-1.0f, 0.0f), gTexelSize);
    const float rightDistance = SearchEdgeLength(gEdgeTexture, gLinearSampler, input.texcoord, float2(1.0f, 0.0f), gTexelSize);
    const float topDistance = SearchEdgeLength(gEdgeTexture, gLinearSampler, input.texcoord, float2(0.0f, -1.0f), gTexelSize);
    const float bottomDistance = SearchEdgeLength(gEdgeTexture, gLinearSampler, input.texcoord, float2(0.0f, 1.0f), gTexelSize);
    const float horizontalWeight = edge.g * saturate((leftDistance + rightDistance) / 16.0f);
    const float verticalWeight = edge.r * saturate((topDistance + bottomDistance) / 16.0f);
    const float cornerReduction = 1.0f - saturate(gCornerRounding / 100.0f) * min(horizontalWeight, verticalWeight) * 0.5f;
    return float4(horizontalWeight, verticalWeight, horizontalWeight, verticalWeight) * cornerReduction;
}
