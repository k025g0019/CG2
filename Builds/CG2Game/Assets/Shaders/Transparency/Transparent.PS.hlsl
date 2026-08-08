#include "TransparencyCommon.hlsli"

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct MaterialCB
{
    float4 color;
    float transparency;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<MaterialCB> gMaterial : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    float4 color = gTexture.Sample(gSampler, input.texcoord) * gMaterial.color;
    color.a *= saturate(1.0f - gMaterial.transparency);
    return color;
}
