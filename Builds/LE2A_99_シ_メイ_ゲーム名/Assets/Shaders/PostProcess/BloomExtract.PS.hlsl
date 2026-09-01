#include "ToneMappingCommon.hlsli"

struct BloomCB
{
    float threshold;
    float softThreshold;
    float intensity;
    float pad;
};

ConstantBuffer<BloomCB> gBloom : register(b0);
Texture2D<float4> gSceneColor : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
	float3 color = gSceneColor.Sample(gSampler, input.texcoord).rgb;
	float luminance = Luminance(color);
	float threshold = max(gBloom.threshold, 0.0001f);
	float softKnee = threshold * saturate(gBloom.softThreshold) + 0.0001f;
	float kneeStart = threshold - softKnee;
	float kneeRange = softKnee * 2.0f;
	float kneeFactor = saturate((luminance - kneeStart) / kneeRange);
	float softContribution = kneeFactor * kneeFactor * (3.0f - 2.0f * kneeFactor) * softKnee;
	float hardContribution = max(luminance - threshold, 0.0f);
	float contribution = max(hardContribution, softContribution);
	float bloomScale = contribution / max(luminance, 0.0001f);
	float3 bloomColor = min(color * bloomScale * max(gBloom.intensity, 0.0f), float3(64.0f, 64.0f, 64.0f));
	return float4(bloomColor, 1.0f);
}
