#include "BloomCommon.hlsli"

Texture2D<float4> gSourceTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 gSourceTexelSize;
    float2 gUnused0;
    float4 gUnused1;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float3 downsampledColor = SampleTent9(
        gSourceTexture,
        gLinearSampler,
        input.texcoord,
        gSourceTexelSize);
    return float4(downsampledColor, 1.0f);
}
