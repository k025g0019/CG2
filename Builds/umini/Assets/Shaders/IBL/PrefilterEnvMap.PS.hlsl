#include "IBLCommon.hlsli"

TextureCube gEnvironmentMap : register(t0);
SamplerState gSampler : register(s0);

struct PrefilterCB
{
    float roughness;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<PrefilterCB> gPrefilter : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float mipLevel = saturate(gPrefilter.roughness) * 5.0f;
    const float3 color = gEnvironmentMap.SampleLevel(gSampler, normalize(input.direction), mipLevel).rgb;
    return float4(color, 1.0f);
}
