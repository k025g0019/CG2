Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float4> gBlendWeightTexture : register(t1);
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
    const float4 centerColor = gSceneTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f);
    const float4 blendWeights = gBlendWeightTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f);
    const float horizontalWeight = saturate(max(blendWeights.r, blendWeights.b));
    const float verticalWeight = saturate(max(blendWeights.g, blendWeights.a));
    const float4 horizontalColor = 0.5f * (
        gSceneTexture.SampleLevel(gLinearSampler, input.texcoord - float2(gTexelSize.x, 0.0f), 0.0f) +
        gSceneTexture.SampleLevel(gLinearSampler, input.texcoord + float2(gTexelSize.x, 0.0f), 0.0f));
    const float4 verticalColor = 0.5f * (
        gSceneTexture.SampleLevel(gLinearSampler, input.texcoord - float2(0.0f, gTexelSize.y), 0.0f) +
        gSceneTexture.SampleLevel(gLinearSampler, input.texcoord + float2(0.0f, gTexelSize.y), 0.0f));
    const float totalWeight = horizontalWeight + verticalWeight;
    const float4 filteredColor = totalWeight > 0.0001f
        ? (horizontalColor * horizontalWeight + verticalColor * verticalWeight) / totalWeight
        : centerColor;
    return lerp(centerColor, filteredColor, saturate(totalWeight));
}
