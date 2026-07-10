#include "ToneMappingCommon.hlsli"

struct SkyboxCB
{
	float3 topColor;
	float intensity;
	float3 bottomColor;
	float horizonSharpness;
	float3 sunDirection;
	float sunIntensity;
	float environmentTextureEnabled;
	float environmentTextureIntensity;
	float environmentTextureRotation;
	float environmentTextureMipBias;
};

ConstantBuffer<SkyboxCB> gSkybox : register(b0);
Texture2D gEnvironmentTexture : register(t0);
SamplerState gEnvironmentSampler : register(s0);

struct PixelShaderInput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float3 RotateDirectionAroundYAxis(float3 direction, float rotationRadian)
{
	float cosValue = cos(rotationRadian);
	float sinValue = sin(rotationRadian);
	return float3(
		direction.x * cosValue - direction.z * sinValue,
		direction.y,
		direction.x * sinValue + direction.z * cosValue);
}

float2 MakeLatLongUv(float3 direction)
{
	float3 safeDirection = normalize(direction);
	float2 uv;
	uv.x = atan2(safeDirection.z, safeDirection.x) * (0.5f / 3.14159265f) + 0.5f;
	uv.y = acos(clamp(safeDirection.y, -1.0f, 1.0f)) / 3.14159265f;
	return uv;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
	float2 centeredUv = input.texcoord * 2.0f - float2(1.0f, 1.0f);
	float3 viewDirection = normalize(float3(centeredUv.x, -centeredUv.y * 0.55f + 0.12f, 1.0f));

	float horizonRate = pow(saturate(viewDirection.y * 0.5f + 0.5f), max(gSkybox.horizonSharpness, 0.0001f));
	float3 skyColor = lerp(max(gSkybox.bottomColor, 0.0f), max(gSkybox.topColor, 0.0f), horizonRate);

	float sunRate = pow(saturate(dot(viewDirection, normalize(-gSkybox.sunDirection))), 256.0f);
	float sunGlow = pow(saturate(dot(viewDirection, normalize(-gSkybox.sunDirection))), 12.0f);
	float3 sunColor = float3(1.0f, 0.86f, 0.62f) * (sunRate * 12.0f + sunGlow * 0.35f) * max(gSkybox.sunIntensity, 0.0f);
	float vignette = 1.0f - saturate(dot(centeredUv, centeredUv) * 0.10f);

	return float4((skyColor * max(gSkybox.intensity, 0.0f) + sunColor) * vignette, 1.0f);
}
