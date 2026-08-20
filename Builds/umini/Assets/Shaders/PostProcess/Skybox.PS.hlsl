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
    float timeSeconds;
    float cloudEnabled;
    float cloudCoverage;
    float cloudDensity;
    float cloudScale;
    float cloudSpeed;
    float cloudHeight;
    float cloudThickness;
    float cloudLightAbsorption;
    float cloudSilverLining;
    float3 cloudColor;
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

float HashCloudCell(float2 coordinate)
{
    const float3 hashInput = frac(float3(coordinate.xyx) * 0.1031f);
    const float hashMix = dot(hashInput, hashInput.yzx + 33.33f);
    return frac((hashInput.x + hashInput.y) * (hashInput.z + hashMix));
}

float CloudValueNoise(float2 coordinate)
{
    const float2 cell = floor(coordinate);
    const float2 fraction = frac(coordinate);
    const float2 smoothFraction = fraction * fraction * (3.0f - 2.0f * fraction);
    const float lowerNoise = lerp(
        HashCloudCell(cell),
        HashCloudCell(cell + float2(1.0f, 0.0f)),
        smoothFraction.x);
    const float upperNoise = lerp(
        HashCloudCell(cell + float2(0.0f, 1.0f)),
        HashCloudCell(cell + float2(1.0f, 1.0f)),
        smoothFraction.x);
    return lerp(lowerNoise, upperNoise, smoothFraction.y);
}

float EvaluateCloudNoise(float2 coordinate)
{
    float noiseValue = 0.0f;
    float amplitude = 0.56f;
    float2 octaveCoordinate = coordinate;

    [unroll]
    for (uint octaveIndex = 0u; octaveIndex < 4u; octaveIndex++)
    {
        noiseValue += CloudValueNoise(octaveCoordinate) * amplitude;
        octaveCoordinate = octaveCoordinate * 2.03f + float2(17.7f, 9.2f);
        amplitude *= 0.48f;
    }

    return noiseValue;
}

float EvaluateCloudShape(float3 worldPosition, float layerRate, float2 windOffset)
{
    const float2 baseCoordinate =
        worldPosition.xz * max(gSkybox.cloudScale, 0.0001f) +
        windOffset;
    const float largeShape = EvaluateCloudNoise(baseCoordinate * 0.54f);
    const float mediumShape = EvaluateCloudNoise(baseCoordinate * 1.18f + largeShape * 2.35f);
    const float erosionShape = EvaluateCloudNoise(baseCoordinate * 4.70f + 31.4f);
    const float anvilProfile = smoothstep(0.02f, 0.22f, layerRate) *
        (1.0f - smoothstep(0.70f, 1.0f, layerRate));
    const float softTopProfile = smoothstep(0.0f, 0.18f, layerRate) *
        (1.0f - smoothstep(0.72f, 1.0f, layerRate));
    const float coverageThreshold = lerp(
        0.78f,
        0.30f,
        saturate(gSkybox.cloudCoverage));
    const float shapedDensity = largeShape * 0.55f +
        mediumShape * 0.55f -
        erosionShape * 0.24f;

    return saturate(
        (shapedDensity - coverageThreshold) /
        max(1.0f - coverageThreshold, 0.001f)) *
        lerp(softTopProfile, anvilProfile, 0.35f);
}

float4 EvaluateVolumetricCloud(
    float3 viewDirection,
    float3 sunDirection)
{
    if (gSkybox.cloudEnabled < 0.5f || viewDirection.y <= 0.015f)
    {
        return 0.0f;
    }

    const float cloudBottom = gSkybox.cloudHeight;
    const float cloudTop = cloudBottom + max(gSkybox.cloudThickness, 1.0f);
    const float firstPlaneDistance = (cloudBottom - gSkybox.cameraPosition.y) / viewDirection.y;
    const float secondPlaneDistance = (cloudTop - gSkybox.cameraPosition.y) / viewDirection.y;
    const float rayStart = max(min(firstPlaneDistance, secondPlaneDistance), 0.0f);
    const float rayEnd = max(firstPlaneDistance, secondPlaneDistance);

    if (rayEnd <= rayStart)
    {
        return 0.0f;
    }

    const float maximumCloudDistance = max(gSkybox.cloudThickness * 24.0f, 8000.0f);
    const float resolvedRayEnd = min(rayEnd, rayStart + maximumCloudDistance);
    const float rayLength = resolvedRayEnd - rayStart;
    const float stepLength = rayLength / 12.0f;
    const float2 windOffset = float2(1.0f, 0.37f) *
        gSkybox.timeSeconds * gSkybox.cloudSpeed * max(gSkybox.cloudScale, 0.0001f);
    const float horizonFade = smoothstep(0.015f, 0.13f, viewDirection.y);
    const float sunForward = pow(saturate(dot(viewDirection, sunDirection)), 10.0f);
    const float sunSide = saturate(dot(normalize(float3(viewDirection.x, 0.24f, viewDirection.z)), sunDirection));
    float transmittance = 1.0f;
    float3 scatteredLight = 0.0f;

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 12u; sampleIndex++)
    {
        const float rayDistance = rayStart + ((float)sampleIndex + 0.5f) * stepLength;
        const float3 samplePosition = gSkybox.cameraPosition + viewDirection * rayDistance;
        const float layerRate = saturate((samplePosition.y - cloudBottom) / max(gSkybox.cloudThickness, 1.0f));
        const float sampleDensity =
            EvaluateCloudShape(samplePosition, layerRate, windOffset) *
            max(gSkybox.cloudDensity, 0.0f) *
            horizonFade;

        if (sampleDensity <= 0.0001f)
        {
            continue;
        }

        const float opticalDepth = sampleDensity * stepLength / max(gSkybox.cloudThickness, 1.0f);
        const float sampleTransmittance = exp(-opticalDepth * max(gSkybox.cloudLightAbsorption, 0.0f));
        const float silverLining = sunForward * max(gSkybox.cloudSilverLining, 0.0f);
        const float3 sampleLight = max(gSkybox.cloudColor, 0.0f) *
            (0.28f + saturate(sunDirection.y) * 0.48f + sunSide * 0.22f + silverLining);
        scatteredLight += transmittance * (1.0f - sampleTransmittance) * sampleLight;
        transmittance *= sampleTransmittance;
    }

    const float cloudOpacity = saturate((1.0f - transmittance) * horizonFade);
    return float4(scatteredLight, cloudOpacity);
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
    float3 skyColor = lerp(
        atmosphereColor,
        environmentColor + environmentSolarScattering,
        environmentMask);

    const float4 cloud = EvaluateVolumetricCloud(viewDirection, safeSunDirection);
    skyColor = skyColor * (1.0f - cloud.a) + cloud.rgb;

    return float4(max(skyColor, 0.0f), 1.0f);
}
