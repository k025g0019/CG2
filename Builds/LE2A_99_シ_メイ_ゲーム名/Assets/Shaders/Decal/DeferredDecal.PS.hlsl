#include "DecalCommon.hlsli"

Texture2D gDepthTexture : register(t0);
Texture2D gNormalTexture : register(t1);
Texture2D gDecalTexture : register(t2);
SamplerState gSampler : register(s0);

struct DecalCB
{
    float4 tintColor;
    float intensity;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<DecalCB> gDecal : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float depth = gDepthTexture.Sample(gSampler, input.texcoord).r;
    const float3 normal = DecodeNormal(gNormalTexture.Sample(gSampler, input.texcoord).xyz);
    const float facing = saturate(normal.z);
    const float4 decal = gDecalTexture.Sample(gSampler, input.texcoord) * gDecal.tintColor;
    return float4(decal.rgb * gDecal.intensity * facing, decal.a * saturate(1.0f - depth));
}
