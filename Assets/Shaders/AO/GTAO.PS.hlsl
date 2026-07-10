Texture2D<float> gDepthTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer GTAOConstants : register(b0) {
	float2 inverseResolution;
	float radius;
	float intensity;
}

struct PSInput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
	const float centerDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
	float visibility = 0.0f;

	[unroll]
	for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
		const float angle = (6.2831853f / 8.0f) * sampleIndex;
		const float2 direction = float2(cos(angle), sin(angle));
		const float2 sampleUv = input.texcoord + direction * inverseResolution * radius;
		const float sampleDepth = gDepthTexture.Sample(gLinearSampler, sampleUv);
		const float depthDelta = sampleDepth - centerDepth;
		const float horizonWeight = saturate(depthDelta * 32.0f);
		const float distanceWeight = 1.0f - (float(sampleIndex) / 8.0f);
		visibility += horizonWeight * distanceWeight;
	}

	const float ao = saturate(1.0f - (visibility / 8.0f) * intensity);
	return float4(ao.xxx, 1.0f);
}
