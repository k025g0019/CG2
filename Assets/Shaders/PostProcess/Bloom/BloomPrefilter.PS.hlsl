#include "BloomCommon.hlsli"

Texture2D<float4> gSourceTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 gSourceTexelSize;
    float gThreshold;
    float gSoftKnee;
    float4 gUnusedParameters;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float3 color = 0.0f;

    [unroll]
    for (int offsetY = 0; offsetY < 2; offsetY++)
    {
        [unroll]
        for (int offsetX = 0; offsetX < 2; offsetX++)
        {
            const float2 offset = (float2(offsetX, offsetY) - 0.5f) * gSourceTexelSize;
            color += KarisAverage(gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + offset, 0.0f).rgb);
        }
    }

    color *= 0.25f;
    const float brightness = Luminance(color);
    const float knee = max(gThreshold * saturate(gSoftKnee), 0.0001f);
    float softContribution = saturate((brightness - gThreshold + knee) / (2.0f * knee));
    softContribution = softContribution * softContribution * knee;
    const float contribution = max(brightness - gThreshold, softContribution) / max(brightness, 0.0001f);
    return float4(color * contribution, 1.0f);
}
