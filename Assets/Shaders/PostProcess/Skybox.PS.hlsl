#include "ToneMappingCommon.hlsli"

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
    const float horizonRate = pow(
        saturate(viewDirection.y * 0.5f + 0.5f),
        max(gSkybox.horizonSharpness, 0.0001f));
    const float3 gradientColor = lerp(
        max(gSkybox.bottomColor, 0.0f),
        max(gSkybox.topColor, 0.0f),
        horizonRate) * max(gSkybox.intensity, 0.0f);

    const float3 rotatedViewDirection = RotateDirectionAroundYAxis(
        viewDirection,
        gSkybox.environmentTextureRotation);
    const float2 environmentUv = MakeLatLongUv(rotatedViewDirection);
    const float3 environmentColor = gEnvironmentTexture.SampleLevel(
        gEnvironmentSampler,
        environmentUv,
        max(gSkybox.environmentTextureMipBias, 0.0f)).rgb *
        max(gSkybox.environmentTextureIntensity, 0.0f);
    float3 skyColor = lerp(
        gradientColor,
        environmentColor,
        saturate(gSkybox.environmentTextureEnabled));

    const float3 safeSunDirection = normalize(-gSkybox.sunDirection);
    const float sunRate = pow(saturate(dot(viewDirection, safeSunDirection)), 256.0f);
    const float sunGlow = pow(saturate(dot(viewDirection, safeSunDirection)), 12.0f);
    const float3 sunColor = float3(1.0f, 0.86f, 0.62f) *
        (sunRate * 12.0f + sunGlow * 0.35f) *
        max(gSkybox.sunIntensity, 0.0f);

    skyColor += sunColor;

    return float4(max(skyColor, 0.0f), 1.0f);
}
