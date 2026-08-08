#include "ToneMappingCommon.hlsli"

#include "../Common/AnalyticAtmosphere.hlsli"

struct SkyboxCB
{
    row_major float4x4 inverseViewProjection;
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
    float3 cameraPosition;
    float padding0;
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
    const float cosValue = cos(rotationRadian);
    const float sinValue = sin(rotationRadian);

    return float3(
        direction.x * cosValue - direction.z * sinValue,
        direction.y,
        direction.x * sinValue + direction.z * cosValue);
}

float2 MakeLatLongUv(float3 direction)
{
    const float3 safeDirection = normalize(direction);
    float2 uv;
    uv.x = atan2(safeDirection.z, safeDirection.x) * (0.5f / 3.14159265f) + 0.5f;
    uv.y = acos(clamp(safeDirection.y, -1.0f, 1.0f)) / 3.14159265f;

    return uv;
}

float3 ReconstructViewDirection(float2 uv)
{
    const float4 clipPosition = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        1.0f,
        1.0f);
    const float4 worldPosition = mul(clipPosition, gSkybox.inverseViewProjection);
    const float3 farWorldPosition = worldPosition.xyz / max(abs(worldPosition.w), 0.0001f);

    return normalize(farWorldPosition - gSkybox.cameraPosition);
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float3 viewDirection = ReconstructViewDirection(input.texcoord);
    const float3 safeSunDirection = normalize(-gSkybox.sunDirection);
    const float3 atmosphereColor = EvaluateAnalyticAtmosphere(
        viewDirection,
        safeSunDirection,
        gSkybox.topColor,
        gSkybox.bottomColor,
        gSkybox.intensity,
        gSkybox.sunIntensity);

    const float3 rotatedViewDirection = RotateDirectionAroundYAxis(
        viewDirection,
        gSkybox.environmentTextureRotation);
    const float2 environmentUv = MakeLatLongUv(rotatedViewDirection);
    const float3 environmentColor = gEnvironmentTexture.SampleLevel(
        gEnvironmentSampler,
        environmentUv,
        max(gSkybox.environmentTextureMipBias, 0.0f)).rgb *
        max(gSkybox.environmentTextureIntensity, 0.0f);
    const float environmentMask = saturate(gSkybox.environmentTextureEnabled);
    const float3 environmentSolarScattering = EvaluateAtmosphereSolarScattering(
        viewDirection,
        safeSunDirection,
        gSkybox.sunIntensity * 0.18f);
    const float3 skyColor = lerp(
        atmosphereColor,
        environmentColor + environmentSolarScattering,
        environmentMask);

    return float4(max(skyColor, 0.0f), 1.0f);
}
