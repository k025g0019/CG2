#include "ToneMappingCommon.hlsli"

struct ToneMappingCB
{
	float exposure;
	float whitePoint;
	float mode;
	float bloomIntensity;
	float saturation;
	float contrast;
	float vignetteStrength;
	float vignetteRadius;
	float filmGrain;
	float chromaticAberration;
	float sharpenStrength;
	float pad0;
};

ConstantBuffer<ToneMappingCB> gToneMapping : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gBloomTexture : register(t1);
Texture2D<float> gSSAOTexture : register(t2);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
	float2 centeredUv = input.texcoord - float2(0.5f, 0.5f);
	float aberrationStrength = max(gToneMapping.chromaticAberration, 0.0f);
	float3 hdrColor = gSceneColor.Sample(gSampler, input.texcoord).rgb;

	// 色収差が無効な時は、同じ Scene Color を追加で 2 回読む処理を行わない。
	if (aberrationStrength > 0.0001f) {
		float2 aberrationOffset = centeredUv * aberrationStrength * 0.006f;
		hdrColor.r = gSceneColor.Sample(gSampler, input.texcoord + aberrationOffset).r;
		hdrColor.b = gSceneColor.Sample(gSampler, input.texcoord - aberrationOffset).b;
	}

	float exposure = max(gToneMapping.exposure, 0.0001f);
	float whitePoint = max(gToneMapping.whitePoint, 0.0001f);
	float bloomWeight = max(gToneMapping.bloomIntensity, 0.0f);
	float ssaoStrength = saturate(gToneMapping.sharpenStrength);
	float3 color = hdrColor * exposure;

	// AO が無効なら SSAO Texture のサンプルを省く。
	if (ssaoStrength > 0.0001f) {
		float ssao = gSSAOTexture.Sample(gSampler, input.texcoord).r;
		color *= lerp(1.0f, ssao, ssaoStrength);
	}

	// Bloom が無効なら合成用 Texture のサンプルを省く。
	if (bloomWeight > 0.0001f) {
		float3 bloomColor = gBloomTexture.Sample(gSampler, input.texcoord).rgb;
		float bloomLuminance = Luminance(bloomColor);
		float bloomSoftClamp = bloomLuminance / (bloomLuminance + 1.0f);
		color += bloomColor * bloomWeight * lerp(1.0f, bloomSoftClamp, 0.35f);
	}

	if (gToneMapping.mode >= 0.5f && gToneMapping.mode < 1.5f) {
		color = ReinhardToneMapExt(color, whitePoint);
	}
	else if (gToneMapping.mode >= 1.5f && gToneMapping.mode < 2.5f) {
		color = FilmicToneMap(color / whitePoint);
	}
	else if (gToneMapping.mode >= 2.5f && gToneMapping.mode < 3.5f) {
		color = TimothyToneMap(color / whitePoint);
	}
	else if (gToneMapping.mode >= 3.5f && gToneMapping.mode < 4.5f) {
		color = Uncharted2ToneMap(color / whitePoint);
	}
	else {
		color = ACESFilmToneMap(color / whitePoint);
	}

	float contrast = max(gToneMapping.contrast, 0.0f);
	float saturation = max(gToneMapping.saturation, 0.0f);

	if (abs(contrast - 1.0f) > 0.0001f) {
		color = ApplyContrast(color, contrast);
	}

	if (abs(saturation - 1.0f) > 0.0001f) {
		color = ApplySaturation(color, saturation);
	}

	if (gToneMapping.vignetteStrength > 0.0001f) {
		color *= ApplyVignette(input.texcoord, gToneMapping.vignetteStrength, gToneMapping.vignetteRadius);
	}

	if (gToneMapping.filmGrain > 0.0001f) {
		float grain = Hash12(input.position.xy) - 0.5f;
		color += grain * gToneMapping.filmGrain * 0.015f;
	}

	return float4(LinearToSRGB(color), 1.0f);
}
