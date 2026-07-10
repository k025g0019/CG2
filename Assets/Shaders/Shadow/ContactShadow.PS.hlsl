#include "../PostProcess/ToneMappingCommon.hlsli"

Texture2D<float4> gAoTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gLinearSampler : register(s0);

struct PSInput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
	uint depthWidth = 1u;
	uint depthHeight = 1u;
	gDepthTexture.GetDimensions(depthWidth, depthHeight);
	const float2 texelSize = float2(
		1.0f / max((float)depthWidth, 1.0f),
		1.0f / max((float)depthHeight, 1.0f));
	const float centerDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
	const float baseAo = gAoTexture.Sample(gLinearSampler, input.texcoord).r;
	float occlusion = 0.0f;

	[unroll]
	for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex) {
		const float2 offset =
			(sampleIndex == 0) ? float2(texelSize.x, 0.0f) :
			(sampleIndex == 1) ? float2(-texelSize.x, 0.0f) :
			(sampleIndex == 2) ? float2(0.0f, texelSize.y) :
								 float2(0.0f, -texelSize.y);

		const float sampleDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord + offset);
		occlusion += saturate((sampleDepth - centerDepth) * 64.0f);
	}

	occlusion = saturate(occlusion * 0.25f);
	const float combinedAo = saturate(baseAo * (1.0f - occlusion * 0.65f));
	return float4(combinedAo.xxx, 1.0f);
}
