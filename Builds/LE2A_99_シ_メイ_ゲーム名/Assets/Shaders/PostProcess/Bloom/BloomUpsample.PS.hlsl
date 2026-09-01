#include "BloomCommon.hlsli"

Texture2D<float4> gLowResolutionTexture : register(t0);
Texture2D<float4> gHighResolutionTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 gLowResolutionTexelSize;
    float gScatter;
    float gIntensity;
    float4 gUnusedParameters;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float3 lowResolutionColor = SampleTent9(
        gLowResolutionTexture,
        gLinearSampler,
        input.texcoord,
        gLowResolutionTexelSize);
    const float3 highResolutionColor = gHighResolutionTexture.SampleLevel(
        gLinearSampler,
        input.texcoord,
        0.0f).rgb;
    const float3 bloomColor = highResolutionColor + lowResolutionColor * saturate(gScatter);
    return float4(bloomColor * max(gIntensity, 0.0f), 1.0f);
}
