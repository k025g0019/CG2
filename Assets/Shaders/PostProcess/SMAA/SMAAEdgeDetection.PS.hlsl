#include "SMAACommon.hlsli"

Texture2D<float4> gSceneTexture : register(t0);
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
    const float centerLuma = Luma(gSceneTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f).rgb);
    const float leftLuma = Luma(gSceneTexture.SampleLevel(gLinearSampler, input.texcoord - float2(gTexelSize.x, 0.0f), 0.0f).rgb);
    const float topLuma = Luma(gSceneTexture.SampleLevel(gLinearSampler, input.texcoord - float2(0.0f, gTexelSize.y), 0.0f).rgb);
    const float rightLuma = Luma(gSceneTexture.SampleLevel(gLinearSampler, input.texcoord + float2(gTexelSize.x, 0.0f), 0.0f).rgb);
    const float bottomLuma = Luma(gSceneTexture.SampleLevel(gLinearSampler, input.texcoord + float2(0.0f, gTexelSize.y), 0.0f).rgb);
    const float horizontalDelta = max(abs(centerLuma - leftLuma), abs(centerLuma - rightLuma));
    const float verticalDelta = max(abs(centerLuma - topLuma), abs(centerLuma - bottomLuma));
    const float localContrast = max(horizontalDelta, verticalDelta);
    const float threshold = max(gEdgeThreshold, 0.001f);
    const float2 edges = localContrast >= threshold
        ? step(threshold.xx, float2(verticalDelta, horizontalDelta))
        : 0.0f;
    return float4(edges, 0.0f, 1.0f);
}
