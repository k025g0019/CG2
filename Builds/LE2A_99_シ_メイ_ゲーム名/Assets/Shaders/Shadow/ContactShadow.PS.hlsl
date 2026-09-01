#include "../PostProcess/ToneMappingCommon.hlsli"

Texture2D<float4> gAoTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer ContactShadowConstants : register(b0)
{
	float2 inverseResolution;
	float spatialSigma;
	float depthSharpness;
}

struct PSInput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
	const float centerDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);

	if (centerDepth >= 0.9999f) {
		return float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const float safeSpatialSigma = max(spatialSigma, 0.1f);
	const float safeDepthSharpness = max(depthSharpness, 1.0f);
	float filteredAo = 0.0f;
	float totalWeight = 0.0f;

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			const float2 sampleUv = input.texcoord + float2(x, y) * inverseResolution;
			const float sampleDepth = gDepthTexture.Sample(gLinearSampler, sampleUv);
			const float spatialWeight = exp(
				-float(x * x + y * y) /
				(2.0f * safeSpatialSigma * safeSpatialSigma));
			const float depthWeight = exp(
				-abs(sampleDepth - centerDepth) * safeDepthSharpness);
			const float sampleWeight = spatialWeight * depthWeight;
			filteredAo += gAoTexture.Sample(gLinearSampler, sampleUv).r * sampleWeight;
			totalWeight += sampleWeight;
		}
	}

	filteredAo /= max(totalWeight, 0.0001f);
	return float4(filteredAo.xxx, 1.0f);
}
