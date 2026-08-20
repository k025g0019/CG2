#include "IBLCommon.hlsli"

TextureCube gEnvironmentMap : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float3 normal = normalize(input.direction);
    const float3 color = gEnvironmentMap.Sample(gSampler, normal).rgb;
    return float4(color, 1.0f);
}
