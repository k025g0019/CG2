struct SSAOCB
{
	float2 texelSize;
	float radiusPixels;
	float intensity;
	float bias;
	float power;
	float2 pad;
};

ConstantBuffer<SSAOCB> gSSAO : register(b0);
Texture2D<float> gDepthTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float SampleDepth(float2 uv)
{
	return gDepthTexture.Sample(gSampler, saturate(uv)).r;
}

float main(PixelShaderInput input) : SV_TARGET0
{
	float centerDepth = SampleDepth(input.texcoord);

	if (centerDepth >= 0.9999f) {
		return 1.0f;
	}

	float radius = max(gSSAO.radiusPixels, 1.0f);
	float occlusion = 0.0f;
	float totalWeight = 0.0f;

	static const float2 sampleOffsets[12] = {
		float2(1.0f, 0.0f),
		float2(-1.0f, 0.0f),
		float2(0.0f, 1.0f),
		float2(0.0f, -1.0f),
		float2(0.707f, 0.707f),
		float2(-0.707f, 0.707f),
		float2(0.707f, -0.707f),
		float2(-0.707f, -0.707f),
		float2(1.65f, 0.35f),
		float2(-1.45f, -0.45f),
		float2(0.35f, -1.65f),
		float2(-0.35f, 1.45f)
	};

	[unroll]
	for (int sampleIndex = 0; sampleIndex < 12; sampleIndex++) {
		float2 sampleUv = input.texcoord + sampleOffsets[sampleIndex] * gSSAO.texelSize * radius;
		float sampleDepth = SampleDepth(sampleUv);
		float depthDelta = centerDepth - sampleDepth;
		float weight = 1.0f / (1.0f + length(sampleOffsets[sampleIndex]));
		occlusion += (depthDelta > gSSAO.bias ? 1.0f : 0.0f) * weight;
		totalWeight += weight;
	}

	float ao = 1.0f - saturate((occlusion / max(totalWeight, 0.0001f)) * max(gSSAO.intensity, 0.0f));
	return pow(saturate(ao), max(gSSAO.power, 0.0001f));
}
