//============================================================
// 大: SoftShadow 共通
// 影のサンプリングを少し広げて、硬すぎる影を和らげる。
//============================================================
#ifndef CG2_SOFT_SHADOW_HLSLI
#define CG2_SOFT_SHADOW_HLSLI

static const float2 kSoftShadowKernel[9] = {
	float2(-1.0f, -1.0f),
	float2(0.0f, -1.0f),
	float2(1.0f, -1.0f),
	float2(-1.0f, 0.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(-1.0f, 1.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 1.0f),
};

float SampleSoftShadow9Tap(
	Texture2D shadowTexture,
	SamplerState shadowSampler,
	float2 uv,
	float compareDepth,
	float2 texelSize,
	float radius)
{
	float shadowValue = 0.0f;

	[unroll]
	for (int kernelIndex = 0; kernelIndex < 9; ++kernelIndex) {
		const float2 offsetUv = uv + kSoftShadowKernel[kernelIndex] * texelSize * radius;
		const float sampledDepth = shadowTexture.SampleLevel(shadowSampler, offsetUv, 0.0f).r;
		shadowValue += (compareDepth <= sampledDepth) ? 1.0f : 0.22f;
	}

	return shadowValue / 9.0f;
}

#endif
