#include "DepthCommon.hlsli"

Texture2D gDepthTexture : register(t0);
SamplerState gSampler : register(s0);

struct LinearDepthCB
{
    float nearClip;
    float farClip;
    float padding0;
    float padding1;
};

ConstantBuffer<LinearDepthCB> gLinearDepth : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float depth = gDepthTexture.Sample(gSampler, input.texcoord).r;
    const float linearDepth = LinearizeDepth(depth, gLinearDepth.nearClip, gLinearDepth.farClip) / max(gLinearDepth.farClip, 0.0001f);
    return float4(linearDepth.xxx, 1.0f);
}
