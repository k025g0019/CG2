#include "Common/Lighting.hlsli"
#include "Common/AdvancedPBR.hlsli"
#include "Common/PBRCommon.hlsli"
#include "Common/AnalyticAtmosphere.hlsli"
#include "Lighting/IBLRuntime.hlsli"
#include "Reflection/ParallaxCorrectedCubemap.hlsli"
#include "Shadow/SoftShadow.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    int useTexture;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
    float reflectionMode;
    float reflectionProbeIntensity;
    float reflectionReserved;
    float materialPadding0;
    float materialPadding1;
    float3 reflectionProbeCenter;
    float reflectionProbeBoxProjection;
    float3 reflectionProbeExtent;
    float materialPadding2;
    row_major float4x4 uvTransform;
    float normalScale;
    float ambientOcclusionStrength;
    float heightScale;
    float alphaCutoff;
    float clearCoat;
    float clearCoatRoughness;
    float transmission;
    float subsurface;
    float anisotropy;
    float anisotropyRotation;
    float specularTint;
    float sheen;
    float3 emissionColor;
    float sheenTint;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAmbientOcclusionMap;
    int useEmissionMap;
    int useHeightMap;
    int useOpacityMap;
    int alphaMode;
    int doubleSided;
    float materialExtensionPadding0;
    float materialExtensionPadding1;
    float materialExtensionPadding2;
    float2 uvTiling;
    float2 uvOffset;
    float oceanEnabled;
    float oceanFoamStrength;
    float oceanRoughness;
    float oceanColorBlendScale;
    float3 oceanDeepColor;
    float oceanMaterialPadding;
    float oceanDetailNormalStrength;
    float oceanFoamThreshold;
    float oceanAbsorptionDistance;
    float oceanRefractionDistortion;
    float oceanWaterDepth;
    float oceanCrestSharpness;
    float oceanMaterialPadding1;
    float oceanMaterialPadding2;
    int surfaceMode;
    float surfaceMaterialPadding0;
    float surfaceMaterialPadding1;
    float surfaceMaterialPadding2;
};

struct DirectionalLightData
{
    float4 color;
    float3 direction;
    float intensity;
    float3 position;
    float range;
    float3 skyUpperColor;
    float skyIntensity;
    float3 skyLowerColor;
    float skyEmission;
    float ambientIntensity;
    float horizonSharpness;
    float reflectionIntensity;
    float spotCosInner;
    float spotCosOuter;
    int lightType;
    float areaRadius;
    float3 cameraPosition;
    float padding3;
    float environmentTextureEnabled;
    float environmentTextureIntensity;
    float environmentTextureRotation;
    float environmentTextureMipBias;
    float shadowTileIndex;
    float shadowTileUvScaleX;
    float shadowTileUvScaleY;
    float shadowTileUvBiasX;
    float shadowTileUvBiasY;
    float shadowEnabled;
    float shadowPadding0, shadowPadding1, shadowPadding2;
    row_major float4x4 shadowVP;
    float4 shadowCascadeSplits;
    float shadowCascadeCount;
    float shadowCascadePadding0;
    float shadowCascadePadding1;
    float shadowCascadePadding2;
    row_major float4x4 shadowCascadeVP[4];
    float4 shadowCascadeAtlas[4];
};

struct DirectionalLightArray
{
    DirectionalLightData lights[4];
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
    float4 oceanData : TEXCOORD3;
    bool isFrontFace : SV_IsFrontFace;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);

struct EmissiveLightData
{
    float3 position;
    float intensity;
    float3 color;
    float range;
};

struct EmissiveLightArray
{
    int count;
    float padding0;
    float padding1;
    float padding2;
    EmissiveLightData lights[8];
};

ConstantBuffer<EmissiveLightArray> gEmissiveLights : register(b2);

struct WaterViewConstants
{
    row_major float4x4 inverseViewProjection;
    float2 viewportUvOffset;
    float2 viewportUvScale;
};

ConstantBuffer<WaterViewConstants> gWaterView : register(b3);

Texture2D gTexture : register(t0);
Texture2D gShadowMap : register(t1);
Texture2D gEnvironmentTexture : register(t2);
Texture2D gNormalMap : register(t7);
Texture2D gMetallicMap : register(t8);
Texture2D gRoughnessMap : register(t9);
Texture2D gAmbientOcclusionMap : register(t10);
Texture2D gEmissionMap : register(t11);
Texture2D gHeightMap : register(t12);
Texture2D gOpacityMap : register(t13);
Texture2D<float4> gWaterSceneColor : register(t18);
Texture2D<float> gWaterSceneDepth : register(t19);

SamplerState gTextureSampler : register(s0);
SamplerState gShadowSampler : register(s1);

static const float kIBLPrefilterMipCount = 8.0f;

#define LIGHT_PRIMARY gDirectionalLight.lights[0]

float3 RotateEnvironmentDirection(float3 direction, float rotationRadian)
{
    const float cosValue = cos(rotationRadian);
    const float sinValue = sin(rotationRadian);

    return float3(
        direction.x * cosValue - direction.z * sinValue,
        direction.y,
        direction.x * sinValue + direction.z * cosValue);
}

float2 MakeEnvironmentLatLongUv(float3 direction)
{
    const float3 safeDirection = normalize(direction);

    return float2(
        atan2(safeDirection.z, safeDirection.x) * (0.5f / PI) + 0.5f,
        acos(clamp(safeDirection.y, -1.0f, 1.0f)) / PI);
}

float3 SampleRuntimeEnvironment(float3 direction, float mipLevel)
{
    const float3 safeDirection = normalize(direction);
    const float3 safeSunDirection = normalize(-LIGHT_PRIMARY.direction);
    const float3 atmosphereColor = EvaluateAnalyticAtmosphere(
        safeDirection,
        safeSunDirection,
        LIGHT_PRIMARY.skyUpperColor,
        LIGHT_PRIMARY.skyLowerColor,
        LIGHT_PRIMARY.skyIntensity,
        LIGHT_PRIMARY.skyEmission);
    const float3 rotatedDirection = RotateEnvironmentDirection(
        safeDirection,
        LIGHT_PRIMARY.environmentTextureRotation);
    const float2 environmentUv = MakeEnvironmentLatLongUv(rotatedDirection);
    const float3 textureColor = gEnvironmentTexture.SampleLevel(
        gIblSampler,
        environmentUv,
        max(mipLevel + LIGHT_PRIMARY.environmentTextureMipBias, 0.0f)).rgb *
        max(LIGHT_PRIMARY.environmentTextureIntensity, 0.0f);
    const float environmentTextureMask = saturate(
        LIGHT_PRIMARY.environmentTextureEnabled);
    const float3 generatedEnvironment = gPrefilterCube.SampleLevel(
        gIblSampler,
        safeDirection,
        clamp(mipLevel, 0.0f, kIBLPrefilterMipCount - 1.0f)).rgb;
    const float3 generatedAtmosphere = lerp(
        generatedEnvironment,
        atmosphereColor,
        0.32f);
    float3 environmentColor = lerp(
        generatedAtmosphere,
        textureColor,
        environmentTextureMask);

    // HDRI 使用時も Directional Light の太陽散乱を少量残し、背景と反射の光源方向を一致させる。
    environmentColor += EvaluateAtmosphereSolarScattering(
        safeDirection,
        safeSunDirection,
        LIGHT_PRIMARY.skyEmission * environmentTextureMask * 0.16f);

    return max(environmentColor, 0.0f);
}

float CalculateSpotAttenuation(float3 lightDirection, float3 spotForward, DirectionalLightData light)
{
    float spotCos = dot(lightDirection, normalize(spotForward));
    float innerCos = max(light.spotCosInner, light.spotCosOuter);
    float outerCos = min(light.spotCosInner, light.spotCosOuter);
    return saturate((spotCos - outerCos) / max(innerCos - outerCos, 0.0001f));
}

void BuildLightInfo(float3 worldPosition, out float3 lightDirection, out float lightIntensity, DirectionalLightData light)
{
    lightIntensity = max(light.intensity, 0.0f);
    if (light.lightType == 0) {
        lightDirection = normalize(-light.direction);
        return;
    }
    float3 toLight = light.position - worldPosition;
    float distanceToLight = length(toLight);
    lightDirection = normalize(toLight);
    lightIntensity *= 1.0f / max(distanceToLight * distanceToLight, 0.35f);
    float range = max(light.range, 0.01f);
    float distanceRate = saturate(distanceToLight / range);
    lightIntensity *= 1.0f - pow(distanceRate, 4.0f);
    lightIntensity *= lightIntensity;
    if (light.lightType == 2)
        lightIntensity *= CalculateSpotAttenuation(-lightDirection, light.direction, light);
}

float SampleShadowProjection(
    float3 worldPosition,
    float normalDotLight,
    row_major float4x4 shadowViewProjection,
    float4 atlasTransform)
{
    float4 shadowPosition = mul(float4(worldPosition, 1.0f), shadowViewProjection);
    if (shadowPosition.w <= 0.000001f) return 1.0f;
    float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f) return 1.0f;
    float2 atlasUv = shadowUv * atlasTransform.xy + atlasTransform.zw;
    float receiverDepth = saturate(shadowNdc.z);
    uint shadowMapWidth = 1u;
    uint shadowMapHeight = 1u;
    gShadowMap.GetDimensions(shadowMapWidth, shadowMapHeight);
    const float2 texelSize = rcp(max(float2(shadowMapWidth, shadowMapHeight), 1.0f));
    const float2 tileMinimumUv = atlasTransform.zw + texelSize * 2.0f;
    const float2 tileMaximumUv =
        atlasTransform.zw +
        atlasTransform.xy -
        texelSize * 2.0f;
    const float receiverBias = lerp(
        0.0020f,
        0.00035f,
        saturate(normalDotLight));
    return SampleSoftShadow9Tap(
        gShadowMap,
        gShadowSampler,
        atlasUv,
        receiverDepth - receiverBias,
        texelSize,
        1.35f,
        tileMinimumUv,
        tileMaximumUv);
}

float SampleShadowAtlas(
    float3 worldPosition,
    float normalDotLight,
    DirectionalLightData light)
{
    if (light.shadowEnabled < 0.5f)
    {
        return 1.0f;
    }

    const bool usesCascadedShadow =
        light.lightType == 0 &&
        light.shadowCascadeCount > 1.5f;

    if (!usesCascadedShadow)
    {
        const float4 atlasTransform = float4(
            light.shadowTileUvScaleX,
            light.shadowTileUvScaleY,
            light.shadowTileUvBiasX,
            light.shadowTileUvBiasY);
        return SampleShadowProjection(
            worldPosition,
            normalDotLight,
            light.shadowVP,
            atlasTransform);
    }

    const float cameraDistance = length(worldPosition - light.cameraPosition);
    uint cascadeIndex = 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
    cascadeIndex = min(cascadeIndex, 3u);

    const float currentShadow = SampleShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex],
        light.shadowCascadeAtlas[cascadeIndex]);

    if (cascadeIndex >= 3u)
    {
        return currentShadow;
    }

    const float cascadeNearDistance = cascadeIndex == 0u
        ? 0.0f
        : light.shadowCascadeSplits[cascadeIndex - 1u];
    const float cascadeFarDistance = light.shadowCascadeSplits[cascadeIndex];
    const float blendStartDistance = lerp(cascadeNearDistance, cascadeFarDistance, 0.88f);
    const float cascadeBlend = saturate(
        (cameraDistance - blendStartDistance) /
        max(cascadeFarDistance - blendStartDistance, 0.0001f));

    if (cascadeBlend <= 0.0f)
    {
        return currentShadow;
    }

    const float nextShadow = SampleShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex + 1u],
        light.shadowCascadeAtlas[cascadeIndex + 1u]);
    return lerp(currentShadow, nextShadow, cascadeBlend);
}

float GetShadowVisibility(float3 normal, float3 lightDirection, float3 worldPosition, DirectionalLightData light)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));
    if (normalDotLight <= 0.0001f) return 1.0f;
    return SampleShadowAtlas(worldPosition, normalDotLight, light);
}

float2 BuildMaterialUv(float2 texcoord)
{
    float2 tiledUv = texcoord * gMaterial.uvTiling + gMaterial.uvOffset;
    float4 uvPosition = mul(float4(tiledUv, 0.0f, 1.0f), gMaterial.uvTransform);
    return uvPosition.xy;
}

void BuildCotangentFrame(
    float3 worldPosition,
    float2 texcoord,
    float3 surfaceNormal,
    out float3 tangent,
    out float3 bitangent)
{
    float3 positionDx = ddx(worldPosition);
    float3 positionDy = ddy(worldPosition);
    float2 uvDx = ddx(texcoord);
    float2 uvDy = ddy(texcoord);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;

    if (abs(determinant) > 0.000001f)
    {
        float inverseDeterminant = rcp(determinant);
        tangent = normalize((positionDx * uvDy.y - positionDy * uvDx.y) * inverseDeterminant);
        bitangent = normalize((positionDy * uvDx.x - positionDx * uvDy.x) * inverseDeterminant);
    }
    else
    {
        float3 fallbackAxis = abs(surfaceNormal.y) < 0.999f
            ? float3(0.0f, 1.0f, 0.0f)
            : float3(1.0f, 0.0f, 0.0f);
        tangent = normalize(cross(fallbackAxis, surfaceNormal));
        bitangent = normalize(cross(surfaceNormal, tangent));
    }

    float rotationSin = sin(gMaterial.anisotropyRotation * 6.28318530718f);
    float rotationCos = cos(gMaterial.anisotropyRotation * 6.28318530718f);
    float3 rotatedTangent = tangent * rotationCos + bitangent * rotationSin;
    float3 rotatedBitangent = bitangent * rotationCos - tangent * rotationSin;
    tangent = normalize(rotatedTangent);
    bitangent = normalize(rotatedBitangent);
}

float2 ApplyHeightParallax(
    float2 texcoord,
    float3 viewDirection,
    float3 tangent,
    float3 bitangent,
    float3 normal)
{
    if (gMaterial.useHeightMap == 0 || abs(gMaterial.heightScale) <= 0.000001f)
    {
        return texcoord;
    }

    float3 tangentViewDirection = float3(
        dot(viewDirection, tangent),
        dot(viewDirection, bitangent),
        dot(viewDirection, normal));
    float height = gHeightMap.Sample(gTextureSampler, texcoord).r - 0.5f;
    return texcoord -
        tangentViewDirection.xy /
        max(abs(tangentViewDirection.z), 0.15f) *
        height *
        gMaterial.heightScale;
}

float3 ApplyNormalMap(float2 texcoord, float3 normal, float3 tangent, float3 bitangent)
{
    if (gMaterial.useNormalMap == 0)
    {
        return normal;
    }

    float3 tangentNormal = gNormalMap.Sample(gTextureSampler, texcoord).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= gMaterial.normalScale;
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        normal * max(tangentNormal.z, 0.001f));
}

AdvancedPbrMaterial BuildMaterial(float2 materialUv)
{
    float4 textureColor = gMaterial.useTexture != 0
        ? gTexture.Sample(gTextureSampler, materialUv)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 materialColor = textureColor * gMaterial.color;

    if (gMaterial.useOpacityMap != 0)
    {
        materialColor.a *= gOpacityMap.Sample(gTextureSampler, materialUv).r;
    }

    float metallicMap = gMaterial.useMetallicMap != 0
        ? gMetallicMap.Sample(gTextureSampler, materialUv).r
        : 1.0f;
    float roughnessMap = gMaterial.useRoughnessMap != 0
        ? gRoughnessMap.Sample(gTextureSampler, materialUv).r
        : 1.0f;

    AdvancedPbrMaterial material;
    material.baseColor = max(materialColor.rgb, 0.0f);
    material.alpha = saturate(materialColor.a);
    material.metallic = saturate(gMaterial.metallic * metallicMap);
    material.roughness = clamp(gMaterial.roughness * roughnessMap, 0.035f, 1.0f);
    material.reflectance = saturate(gMaterial.reflectance);
    material.ior = max(gMaterial.ior, 1.0f);
    material.emissionStrength = max(gMaterial.emissionStrength, 0.0f);
    return material;
}

float3 BuildExtendedF0(float3 baseColor, float metallic)
{
    const float iorF0 = AdvancedPbrIorToF0(gMaterial.ior);
    const float dielectricF0 = gMaterial.oceanEnabled >= 0.5f
        ? max(iorF0, 0.001f)
        : max(0.04f, iorF0);
    float luminance = max(dot(baseColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.0001f);
    float3 baseTint = baseColor / luminance;
    float3 dielectricTint = lerp(
        float3(dielectricF0, dielectricF0, dielectricF0),
        baseTint * dielectricF0,
        saturate(gMaterial.specularTint));
    const float dielectricSpecularScale = lerp(
        1.0f,
        2.0f,
        saturate(gMaterial.reflectance));
    return lerp(
        saturate(
            dielectricTint *
            (gMaterial.oceanEnabled >= 0.5f ? 1.0f : dielectricSpecularScale)),
        baseColor,
        metallic);
}

float2 ApproximateEnvironmentBrdf(float normalDotView, float roughness)
{
    const float4 coefficient0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 coefficient1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 response = roughness * coefficient0 + coefficient1;
    const float approximation =
        min(response.x * response.x, exp2(-9.28f * normalDotView)) *
        response.x + response.y;
    return float2(-1.04f, 1.04f) * approximation + response.zw;
}

float2 SampleEnvironmentBrdf(float normalDotView, float roughness)
{
    const float2 lutResponse = gBRDFLUT.Sample(
        gIblSampler,
        float2(normalDotView, roughness)).rg;
    const float2 analyticResponse = ApproximateEnvironmentBrdf(
        normalDotView,
        roughness);
    const float hasValidLut = step(0.0001f, lutResponse.x + lutResponse.y);
    return lerp(analyticResponse, lutResponse, hasValidLut);
}

float3 EvaluateClearCoat(float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance)
{
    // 水面自体が誘電体の鏡面層なので、ClearCoat を重ねて反射を二重加算しない。
    if (gMaterial.oceanEnabled >= 0.5f)
    {
        return 0.0f;
    }

    float clearCoat = saturate(gMaterial.clearCoat);
    if (clearCoat <= 0.0001f)
    {
        return 0.0f;
    }

    float3 halfVector = normalize(viewDirection + lightDirection);
    float roughness = clamp(gMaterial.clearCoatRoughness, 0.035f, 1.0f);
    float normalDotLight = saturate(dot(normal, lightDirection));
    float normalDotView = saturate(dot(normal, viewDirection));
    float distribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float3 fresnel = FresnelSchlick(
        saturate(dot(halfVector, viewDirection)),
        float3(0.04f, 0.04f, 0.04f));
    float denominator = max(4.0f * normalDotView * normalDotLight, 0.0001f);
    return distribution * geometry * fresnel / denominator * radiance * normalDotLight * clearCoat;
}

float FastOceanSin(float phase)
{
    return sin(phase);
}

float FastOceanCos(float phase)
{
    return cos(phase);
}

float3 ApplyOceanDetailNormal(
    float3 surfaceNormal,
    float3 worldPosition,
    float oceanTime,
    float detailWeight)
{
    const float detailStrength = max(gMaterial.oceanDetailNormalStrength, 0.0f);

    if (detailStrength <= 0.0001f)
    {
        return surfaceNormal;
    }

    // 低周波の法線は遠景にも残し、高周波だけを距離と画面上の大きさで落とす。
    const float worldFootprint = max(
        length(ddx(worldPosition.xz)),
        length(ddy(worldPosition.xz)));
    const float2 horizonBandWeights = saturate(
        1.0f - worldFootprint * float2(0.035f, 0.08f));
    const float2 horizonDirection0 = float2(0.91f, 0.41f);
    const float2 horizonDirection1 = float2(-0.36f, 0.93f);
    const float horizonPhase0 =
        dot(worldPosition.xz, horizonDirection0) * 0.18f + oceanTime * 0.32f;
    const float horizonPhase1 =
        dot(worldPosition.xz, horizonDirection1) * 0.42f - oceanTime * 0.46f;
    float2 detailGradient =
        horizonDirection0 * FastOceanCos(horizonPhase0) * 0.055f * horizonBandWeights.x +
        horizonDirection1 * FastOceanCos(horizonPhase1) * 0.028f * horizonBandWeights.y;

    const float nearDetailWeight = saturate(detailWeight);
    const float3 bandWeights = saturate(
        1.0f - worldFootprint * float3(2.4f, 4.4f, 8.0f)) * nearDetailWeight;

    if (bandWeights.x > 0.0001f)
    {
        const float phase0 =
            dot(worldPosition.xz, float2(0.82f, 0.57f)) * 7.4f + oceanTime * 2.1f;
        detailGradient +=
            float2(0.82f, 0.57f) * FastOceanCos(phase0) * 0.12f * bandWeights.x;

        const float phase1 =
            dot(worldPosition.xz, float2(-0.46f, 0.89f)) * 13.7f + oceanTime * 3.4f;
        detailGradient +=
            float2(-0.46f, 0.89f) * FastOceanCos(phase1) * 0.075f * bandWeights.y;
    }

    if (bandWeights.z > 0.0001f)
    {
        const float phase2 =
            dot(worldPosition.xz, float2(0.96f, -0.28f)) * 25.0f - oceanTime * 5.2f;
        detailGradient +=
            float2(0.96f, -0.28f) * FastOceanCos(phase2) * 0.035f * bandWeights.z;
    }

    const float distortionScale = 1.0f + max(gMaterial.oceanRefractionDistortion, 0.0f) * 3.0f;
    const float3 detailOffset = float3(-detailGradient.x, 0.0f, -detailGradient.y);
    return normalize(surfaceNormal + detailOffset * detailStrength * distortionScale);
}

float ComputeOceanFoam(float3 worldPosition, float3 normal, float4 oceanData)
{
    const float foamStrength = max(gMaterial.oceanFoamStrength, 0.0f);

    if (foamStrength <= 0.0001f)
    {
        return 0.0f;
    }

    const float foamThreshold = min(saturate(gMaterial.oceanFoamThreshold), 0.999f);
    const float compressionWidth = max(fwidth(oceanData.x), 0.015f);
    const float compressionFoam = smoothstep(
        max(foamThreshold - compressionWidth, 0.0f),
        min(foamThreshold + compressionWidth * 2.0f, 1.0f),
        oceanData.x);
    const float slopeFoam = smoothstep(0.16f, 0.52f, 1.0f - saturate(normal.y));
    const float crestFoam = smoothstep(0.68f, 0.96f, oceanData.w);
    const float foamCoverage =
        compressionFoam * lerp(0.72f, 0.92f, saturate(gMaterial.oceanCrestSharpness)) +
        slopeFoam * 0.14f +
        crestFoam * 0.10f;

    if (foamCoverage <= 0.0001f)
    {
        return 0.0f;
    }

    const float breakupWeight = saturate(oceanData.z);
    float foamVariation = 0.82f;

    if (breakupWeight > 0.0001f)
    {
        const float foamWarp = FastOceanSin(
            dot(worldPosition.xz, float2(-0.23f, 0.97f)) * 0.31f - oceanData.y * 0.18f);
        const float foamPattern =
            FastOceanSin(
                dot(worldPosition.xz, float2(0.73f, -0.68f)) * 1.15f +
                oceanData.y * 0.55f + foamWarp * 0.85f) *
            0.5f + 0.5f;
        const float secondaryPattern =
            FastOceanSin(
                dot(worldPosition.xz, float2(-0.61f, -0.79f)) * 0.47f -
                oceanData.y * 0.27f) *
            0.5f + 0.5f;
        const float continuousVariation = lerp(
            0.72f,
            1.0f,
            saturate(foamPattern * 0.65f + secondaryPattern * 0.35f));
        foamVariation = lerp(foamVariation, continuousVariation, breakupWeight);
    }

    return saturate(
        foamCoverage *
        0.78f *
        foamVariation *
        foamStrength);
}

float ComputeOceanShallowWeight(
    float normalDotView,
    float normalizedCrestHeight,
    float waterThickness)
{
    const float absorptionDistance = max(gMaterial.oceanAbsorptionDistance, 0.1f);
    const float waterDepth = max(waterThickness, 0.0f);

    // 水深による吸収へ、薄く光を通す波頭だけを足して浅瀬色を見える状態にする。
    const float depthTransmission = rcp(1.0f + waterDepth / absorptionDistance);
    const float surfaceTransmission = 0.08f + depthTransmission * 0.92f;
    const float viewTransmission = lerp(0.48f, 1.0f, saturate(normalDotView));
    const float crestTransmission =
        smoothstep(0.08f, 0.82f, saturate(normalizedCrestHeight)) *
        lerp(0.28f, 0.62f, saturate(gMaterial.oceanCrestSharpness));
    return saturate(
        (surfaceTransmission * viewTransmission + crestTransmission) *
        max(gMaterial.oceanColorBlendScale, 0.01f));
}

struct OceanSceneSample
{
    float3 refractedColor;
    float waterThickness;
    float shoreFoam;
};

float2 GetWaterViewportUv(float2 screenUv)
{
    const float2 safeViewportScale = max(gWaterView.viewportUvScale, float2(0.00001f, 0.00001f));
    return saturate((screenUv - gWaterView.viewportUvOffset) / safeViewportScale);
}

float3 ReconstructWaterWorldPosition(float2 screenUv, float deviceDepth)
{
    const float2 viewportUv = GetWaterViewportUv(screenUv);
    const float2 ndc = float2(
        viewportUv.x * 2.0f - 1.0f,
        1.0f - viewportUv.y * 2.0f);
    const float4 worldPosition = mul(
        float4(ndc, deviceDepth, 1.0f),
        gWaterView.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

OceanSceneSample SampleOceanScene(
    PixelShaderInput input,
    float3 surfaceNormal)
{
    OceanSceneSample sceneSample;
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);

    const float2 sceneSize = max(float2(sceneWidth, sceneHeight), float2(1.0f, 1.0f));
    const float2 screenUv = saturate(input.position.xy / sceneSize);
    const float opaqueDepth = gWaterSceneDepth.SampleLevel(gShadowSampler, screenUv, 0.0f);

    // 水面より手前にある不透明物を、水面専用パスで上書きしない。
    clip(opaqueDepth + 0.00002f - input.position.z);

    const float maximumWaterDepth = max(gMaterial.oceanWaterDepth, 0.1f);
    const bool hasOpaqueSurface = opaqueDepth < 0.99999f;
    const float3 opaqueWorldPosition = ReconstructWaterWorldPosition(screenUv, opaqueDepth);
    const float viewPathLength = hasOpaqueSurface
        ? length(opaqueWorldPosition - input.worldPosition)
        : maximumWaterDepth;
    const float verticalWaterDepth = hasOpaqueSurface
        ? max(input.worldPosition.y - opaqueWorldPosition.y, 0.0f)
        : maximumWaterDepth;
    const float normalizedVerticalDepth = saturate(verticalWaterDepth / maximumWaterDepth);
    sceneSample.waterThickness = clamp(viewPathLength, 0.02f, maximumWaterDepth);
    sceneSample.shoreFoam =
        (1.0f - smoothstep(
            maximumWaterDepth * 0.0125f,
            maximumWaterDepth * 0.12f,
            verticalWaterDepth)) *
        saturate(gMaterial.oceanFoamStrength);

    const float2 refractionOffset =
        surfaceNormal.xz *
        max(gMaterial.oceanRefractionDistortion, 0.0f) *
        lerp(0.003f, 0.016f, normalizedVerticalDepth);
    const float2 refractedUv = saturate(screenUv + refractionOffset);
    const float3 opaqueSceneColor = gWaterSceneColor.SampleLevel(
        gShadowSampler,
        refractedUv,
        0.0f).rgb;
    const float3 deepColor = max(gMaterial.oceanDeepColor, 0.0f);
    const float3 absorptionCoefficient =
        max(1.0f - saturate(deepColor), 0.025f) /
        max(gMaterial.oceanAbsorptionDistance, 0.1f);
    const float3 waterTransmittance = exp(
        -absorptionCoefficient * sceneSample.waterThickness);
    sceneSample.refractedColor =
        opaqueSceneColor * waterTransmittance +
        deepColor * (1.0f - waterTransmittance);
    return sceneSample;
}

#if defined(ENABLE_REFRACTIVE_SURFACE)
float3 SampleRefractiveScene(
    PixelShaderInput input,
    float3 surfaceNormal,
    float3 absorptionTint,
    float roughness,
    float transmission)
{
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);

    const float2 sceneSize = max(float2(sceneWidth, sceneHeight), float2(1.0f, 1.0f));
    const float2 screenUv = saturate(input.position.xy / sceneSize);
    const float opaqueDepth = gWaterSceneDepth.SampleLevel(gShadowSampler, screenUv, 0.0f);

    // 専用屈折パスではDSVを同時に使えないため、不透明面との前後判定をScene Depthで行う。
    clip(opaqueDepth + 0.00002f - input.position.z);

    const float3 opaqueWorldPosition = ReconstructWaterWorldPosition(screenUv, opaqueDepth);
    const float opticalThickness = opaqueDepth >= 0.99999f
        ? 0.35f
        : clamp(length(opaqueWorldPosition - input.worldPosition), 0.01f, 8.0f);
    const float refractionStrength =
        lerp(0.002f, 0.024f, saturate(transmission)) *
        (1.0f - saturate(roughness) * 0.75f);
    const float2 refractedUv = saturate(screenUv + surfaceNormal.xy * refractionStrength);
    const float3 sceneColor = gWaterSceneColor.SampleLevel(
        gShadowSampler,
        refractedUv,
        0.0f).rgb;
    const float3 absorption = exp(
        -max(1.0f - saturate(absorptionTint), 0.015f) *
        opticalThickness * 1.5f);
    return sceneColor * absorption + absorptionTint * (1.0f - absorption) * 0.08f;
}
#endif

#if defined(ENABLE_WEIGHTED_OIT)
struct ObjectPixelOutput
{
    float4 accumulation : SV_TARGET0;
    float revealage : SV_TARGET1;
};

ObjectPixelOutput BuildObjectPixelOutput(float4 color, float deviceDepth)
{
    ObjectPixelOutput output;
    const float alpha = saturate(color.a);
    const float depthWeight = saturate(1.0f - deviceDepth * 0.9f);
    // R16G16B16A16_FLOAT の飽和を避けつつ、手前の透明面を適度に優先する。
    const float weight =
        clamp(alpha * 0.8f + 0.2f, 0.05f, 1.0f) *
        lerp(1.0f, 24.0f, depthWeight * depthWeight * depthWeight);
    output.accumulation = float4(max(color.rgb, 0.0f) * alpha, alpha) * weight;
    output.revealage = alpha;
    return output;
}
#else
typedef float4 ObjectPixelOutput;

ObjectPixelOutput BuildObjectPixelOutput(float4 color, float deviceDepth)
{
    return color;
}
#endif

#if defined(ENABLE_WEIGHTED_OIT)
ObjectPixelOutput main(PixelShaderInput input)
#else
ObjectPixelOutput main(PixelShaderInput input) : SV_TARGET0
#endif
{
    float3 geometricNormal = normalize(input.normal);
    if (gMaterial.doubleSided != 0 && !input.isFrontFace)
    {
        geometricNormal = -geometricNormal;
    }

    float3 V = normalize(LIGHT_PRIMARY.cameraPosition - input.worldPosition);
    float2 materialUv = BuildMaterialUv(input.texcoord);
    float3 tangent;
    float3 bitangent;
    BuildCotangentFrame(input.worldPosition, materialUv, geometricNormal, tangent, bitangent);
    materialUv = ApplyHeightParallax(materialUv, V, tangent, bitangent, geometricNormal);

    AdvancedPbrMaterial material = BuildMaterial(materialUv);
    if (gMaterial.alphaMode == 1)
    {
        clip(material.alpha - saturate(gMaterial.alphaCutoff));
    }

#if defined(ENABLE_REFRACTIVE_SURFACE)
    const bool isDedicatedRefraction =
        gMaterial.alphaMode == 2 &&
        gMaterial.transmission > 0.0001f;
#else
    const bool isDedicatedRefraction = false;
#endif

    if (gMaterial.enableLighting == 0 && !isDedicatedRefraction) {
        float3 unlitColor = material.baseColor + material.baseColor * material.emissionStrength;
        return BuildObjectPixelOutput(float4(unlitColor, material.alpha), input.position.z);
    }

    float3 N = ApplyNormalMap(materialUv, geometricNormal, tangent, bitangent);

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        N = ApplyOceanDetailNormal(
            N,
            input.worldPosition,
            input.oceanData.y,
            input.oceanData.z);

        // 画面上で 1 pixel 未満になる法線帯域は幾何法線へ戻し、遠景の水玉状ハイライトを防ぐ。
        const float oceanPixelFootprint = max(
            length(ddx(input.worldPosition.xz)),
            length(ddy(input.worldPosition.xz)));
        const float resolvedNormalWeight = saturate(
            1.0f - max(oceanPixelFootprint - 0.20f, 0.0f) * 0.82f);
        N = normalize(lerp(geometricNormal, N, resolvedNormalWeight));
    }

    float NdotV = saturate(dot(N, V));

    float3 albedo = material.baseColor;
    float metallic = material.metallic;
    float roughness = material.roughness;
    float surfaceTransmission = saturate(gMaterial.transmission);
    float oceanFoam = 0.0f;
    float oceanWaterThickness = max(gMaterial.oceanWaterDepth, 0.1f);
    float3 oceanRefractedColor = float3(0.0f, 0.0f, 0.0f);

    if (gMaterial.surfaceMode == 1)
    {
        const float slopeWeight = smoothstep(0.18f, 0.72f, 1.0f - saturate(N.y));
        const float heightVariation = 0.92f + 0.08f * sin(input.worldPosition.y * 0.17f);
        const float3 grassTint = float3(0.58f, 0.86f, 0.52f) * heightVariation;
        const float3 rockTint = float3(0.72f, 0.69f, 0.64f);
        albedo *= lerp(grassTint, rockTint, slopeWeight);
        metallic = 0.0f;
        roughness = max(roughness, lerp(0.58f, 0.82f, slopeWeight));
    }
    else if (gMaterial.surfaceMode == 2)
    {
        const float foliageVariation =
            0.88f + 0.12f * sin(dot(input.worldPosition.xz, float2(0.73f, 1.17f)));
        albedo *= float3(0.72f, 1.0f, 0.68f) * foliageVariation;
        metallic = 0.0f;
        roughness = max(roughness, 0.48f);
    }

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        const OceanSceneSample oceanSceneSample = SampleOceanScene(input, N);
        oceanWaterThickness = oceanSceneSample.waterThickness;
        oceanRefractedColor = oceanSceneSample.refractedColor;
        const float shallowWeight = ComputeOceanShallowWeight(
            NdotV,
            input.oceanData.w,
            oceanWaterThickness);
        oceanFoam = max(
            ComputeOceanFoam(input.worldPosition, N, input.oceanData),
            oceanSceneSample.shoreFoam);
        albedo = lerp(max(gMaterial.oceanDeepColor, 0.0f), max(material.baseColor, 0.0f), shallowWeight);
        const float horizonBlend =
            (1.0f - NdotV) *
            (1.0f - NdotV) *
            0.32f;
        const float3 horizonWaterColor = lerp(
            max(gMaterial.oceanDeepColor, 0.0f),
            max(material.baseColor, 0.0f),
            0.42f);
        albedo = lerp(albedo, horizonWaterColor, horizonBlend);
        albedo = lerp(albedo, float3(0.82f, 0.94f, 0.98f), oceanFoam);
        metallic = 0.0f;
        roughness = lerp(
            clamp(gMaterial.oceanRoughness, 0.12f, 1.0f),
            0.72f,
            oceanFoam);

        // 1 pixel 内で法線が大きく変わる遠景は粗さへ畳み込み、点状の鏡面エイリアシングを防ぐ。
        const float normalVariance = max(
            dot(ddx(N), ddx(N)),
            dot(ddy(N), ddy(N)));
        roughness = clamp(
            sqrt(roughness * roughness + min(normalVariance, 1.0f)),
            0.12f,
            1.0f);
        surfaceTransmission *=
            (1.0f - oceanFoam) *
            lerp(0.55f, 1.0f, shallowWeight);
    }

    //============================================================
    // 課題用の基本ライティング
    //============================================================

    if (gMaterial.enableLighting == 1 || gMaterial.enableLighting == 2)
    {
        float3 classicDirect = 0.0f;

        for (int lightIdx = 0; lightIdx < 4; lightIdx++)
        {
            DirectionalLightData light = gDirectionalLight.lights[lightIdx];
            if (light.shadowEnabled < -0.5f) break;
            if (light.shadowEnabled < 0.5f && light.intensity < 0.0001f) continue;

            float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
            float lightIntensity = 0.0f;
            BuildLightInfo(input.worldPosition, lightDirection, lightIntensity, light);

            float3 L = dot(lightDirection, lightDirection) > 0.000001f
                ? normalize(lightDirection)
                : float3(0.0f, 1.0f, 0.0f);
            float rawNdotL = dot(N, L);
            float diffuseFactor = saturate(rawNdotL);

            if (gMaterial.enableLighting == 2)
            {
                diffuseFactor = saturate(rawNdotL * 0.5f + 0.5f);
                diffuseFactor *= diffuseFactor;
            }

            float shadowVisibility = GetShadowVisibility(N, L, input.worldPosition, light);
            float3 radiance = light.color.rgb * max(lightIntensity, 0.0f);
            classicDirect += albedo * diffuseFactor * radiance * shadowVisibility;
        }

        float skyBlend = saturate(N.y * 0.5f + 0.5f);
        float3 ambientColor = lerp(
            LIGHT_PRIMARY.skyLowerColor,
            LIGHT_PRIMARY.skyUpperColor,
            skyBlend);
        float3 classicAmbient =
            albedo *
            ambientColor *
            max(LIGHT_PRIMARY.ambientIntensity, 0.0f);
        float3 emissionMap = gMaterial.useEmissionMap != 0
            ? gEmissionMap.Sample(gTextureSampler, materialUv).rgb
            : float3(1.0f, 1.0f, 1.0f);
        float3 emission =
            emissionMap *
            max(gMaterial.emissionColor, 0.0f) *
            material.emissionStrength;

        return BuildObjectPixelOutput(
            float4(classicDirect + classicAmbient + emission, material.alpha),
            input.position.z);
    }

    float sampledAo = gMaterial.useAmbientOcclusionMap != 0
        ? gAmbientOcclusionMap.Sample(gTextureSampler, materialUv).r
        : 1.0f;
    float ao = lerp(1.0f, sampledAo, saturate(gMaterial.ambientOcclusionStrength));

    float3 F0 = BuildExtendedF0(albedo, metallic);

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        const float waterIor = max(gMaterial.ior, 1.0001f);
        const float waterF0Root = (waterIor - 1.0f) / (waterIor + 1.0f);
        F0 = float3(1.0f, 1.0f, 1.0f) * waterF0Root * waterF0Root;
    }

    float3 direct = 0.0f;
    for (int lightIdx = 0; lightIdx < 4; lightIdx++)
    {
        DirectionalLightData light = gDirectionalLight.lights[lightIdx];
        if (light.shadowEnabled < -0.5f) break;
        if (light.shadowEnabled < 0.5f && light.intensity < 0.0001f) continue;
        float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
        float lightIntensity = 0.0f;
        BuildLightInfo(input.worldPosition, lightDirection, lightIntensity, light);

        float3 L = dot(lightDirection, lightDirection) > 0.000001f
            ? normalize(lightDirection)
            : float3(0.0f, 1.0f, 0.0f);
        float rawNdotL = dot(N, L);
        const float effectiveSubsurface = gMaterial.surfaceMode == 2
            ? max(saturate(gMaterial.subsurface), 0.38f)
            : saturate(gMaterial.subsurface);
        float NdotL = saturate(
            (rawNdotL + effectiveSubsurface) /
            (1.0f + effectiveSubsurface));
        if (NdotL > 0.0f)
        {
            // 各ライトの shadowVP でワールド座標を投影し、atlas 内の対応タイルを読む。
            float localShadow = GetShadowVisibility(N, L, input.worldPosition, light);
            float3 H = normalize(V + L);
            float tangentAlignment = abs(dot(H, tangent));
            float anisotropicRoughness = clamp(
                roughness * (1.0f - saturate(abs(gMaterial.anisotropy)) * 0.45f * tangentAlignment),
                0.035f,
                1.0f);
            float D = DistributionGGX(N, H, anisotropicRoughness);
            float G = GeometrySmith(N, V, L, anisotropicRoughness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
            float3 kS = F;
            float3 kD = (1.0f - kS) * (1.0f - metallic);
            float3 numerator = D * G * F;
            float denominator = max(4.0f * NdotV * NdotL, 0.0001f);
            float3 specular = numerator / denominator;
            float3 radiance = light.color.rgb * max(lightIntensity, 0.0f);
            float3 sheenColor = lerp(
                float3(1.0f, 1.0f, 1.0f),
                albedo,
                saturate(gMaterial.sheenTint));
            float3 sheen =
                sheenColor *
                saturate(gMaterial.sheen) *
                pow(1.0f - saturate(dot(H, V)), 5.0f);
            direct += ((kD * albedo / PI + specular) * NdotL + sheen) * radiance * localShadow;
            direct += EvaluateClearCoat(N, V, L, radiance) * localShadow;
        }
    }

    for (int emissiveIndex = 0; emissiveIndex < gEmissiveLights.count; emissiveIndex++)
    {
        float3 toEmissive = gEmissiveLights.lights[emissiveIndex].position - input.worldPosition;
        float distanceToEmissive = length(toEmissive);
        float3 emissiveDir = toEmissive / max(distanceToEmissive, 0.001f);
        float emissiveAttenuation = 1.0f / max(distanceToEmissive * distanceToEmissive, 0.35f);
        float emissiveRange = max(gEmissiveLights.lights[emissiveIndex].range, 0.01f);
        float emissiveRate = saturate(distanceToEmissive / emissiveRange);
        emissiveAttenuation *= 1.0f - pow(emissiveRate, 4.0f);
        emissiveAttenuation *= emissiveAttenuation;
        float emissiveIntensity = gEmissiveLights.lights[emissiveIndex].intensity * emissiveAttenuation;
        if (emissiveIntensity > 0.001f)
        {
            float3 EL = normalize(emissiveDir);
            float3 EH = normalize(V + EL);
            float ENdotL = saturate(dot(N, EL));
            if (ENdotL > 0.0f)
            {
                float ED = DistributionGGX(N, EH, roughness);
                float EG = GeometrySmith(N, V, EL, roughness);
                float3 EF = FresnelSchlick(max(dot(EH, V), 0.0f), F0);
                float3 EkS = EF;
                float3 EkD = (1.0f - EkS) * (1.0f - metallic);
                float3 enumerator = ED * EG * EF;
                float edenominator = max(4.0f * NdotV * ENdotL, 0.0001f);
                float3 especular = enumerator / edenominator;
                float3 eradiance = max(gEmissiveLights.lights[emissiveIndex].color, 0.0f) * emissiveIntensity;
                direct += (EkD * albedo / PI + especular) * eradiance * ENdotL;
            }
        }
    }

    const float cubemapModeMask =
        step(0.5f, gMaterial.reflectionMode) *
        (1.0f - step(1.5f, gMaterial.reflectionMode));
    const float environmentReflectionIntensity = max(
        LIGHT_PRIMARY.reflectionIntensity,
        0.0f);
    const float3 environmentFresnel = FresnelSchlickRoughness(
        NdotV,
        F0,
        roughness);
    const float3 environmentDiffuseWeight =
        (1.0f - environmentFresnel) * (1.0f - metallic);
    const float3 generatedIrradiance = gIrradianceCube.Sample(
        gIblSampler,
        N).rgb;
    const float3 diffuseEnvironment = lerp(
        generatedIrradiance,
        SampleRuntimeEnvironment(N, 5.0f),
        saturate(LIGHT_PRIMARY.environmentTextureEnabled)) * albedo;
    const float3 reflectionDirection = normalize(reflect(-V, N));
    const float reflectionMip =
        roughness * (kIBLPrefilterMipCount - 1.0f);
    const float3 reflectedEnvironment = SampleRuntimeEnvironment(
        reflectionDirection,
        reflectionMip);
    const float2 environmentBrdf = SampleEnvironmentBrdf(NdotV, roughness);
    const float3 environmentSpecular =
        reflectedEnvironment *
        (environmentFresnel * environmentBrdf.x + environmentBrdf.y) *
        environmentReflectionIntensity *
        (gMaterial.oceanEnabled >= 0.5f
            ? max(gMaterial.reflectance, 0.0f)
            : 1.0f);
    float3 ibl =
        (environmentDiffuseWeight * diffuseEnvironment + environmentSpecular) *
        ao;

    const float effectiveClearCoat = gMaterial.oceanEnabled >= 0.5f
        ? 0.0f
        : saturate(gMaterial.clearCoat);
    float clearCoatMip =
        clamp(gMaterial.clearCoatRoughness, 0.035f, 1.0f) *
        (kIBLPrefilterMipCount - 1.0f);
    float3 clearCoatReflection = SampleRuntimeEnvironment(
        reflect(-V, N),
        clearCoatMip) *
        effectiveClearCoat *
        environmentReflectionIntensity;
    ibl += clearCoatReflection * FresnelSchlick(NdotV, float3(0.04f, 0.04f, 0.04f));

    float3 refractionDirection = refract(-V, N, 1.0f / max(gMaterial.ior, 1.0001f));
    float3 transmissionColor = SampleRuntimeEnvironment(
        refractionDirection,
        roughness * (kIBLPrefilterMipCount - 1.0f));
    float transmissionAmount = surfaceTransmission;

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        transmissionColor = oceanRefractedColor;
        transmissionAmount *= 1.0f - FresnelSchlick(NdotV, F0).r;
    }
#if defined(ENABLE_REFRACTIVE_SURFACE)
    else if (isDedicatedRefraction)
    {
        transmissionColor = SampleRefractiveScene(
            input,
            N,
            albedo,
            roughness,
            surfaceTransmission);
        transmissionAmount *= 1.0f - FresnelSchlick(NdotV, F0).r;
    }
#endif

    const float3 transmittedRadiance = gMaterial.oceanEnabled >= 0.5f
        ? transmissionColor
        : transmissionColor * albedo;
    ibl = lerp(
        ibl,
        transmittedRadiance,
        saturate(transmissionAmount));

    float3 ambient =
        albedo *
        max(LIGHT_PRIMARY.ambientIntensity, 0.0f) *
        (1.0f - metallic);

    float3 emissionMap = gMaterial.useEmissionMap != 0
        ? gEmissionMap.Sample(gTextureSampler, materialUv).rgb
        : float3(1.0f, 1.0f, 1.0f);
    float3 emission =
        emissionMap *
        max(gMaterial.emissionColor, 0.0f) *
        material.emissionStrength;

    float3 finalColor = direct + ibl + ambient + emission;

    //============================================================
    // Reflection Probe の Box Projection 補正
    //============================================================
    {
        const float probeIntensity = max(
            gMaterial.reflectionProbeIntensity,
            0.0f);

        if (cubemapModeMask > 0.5f &&
            probeIntensity > 0.0001f &&
            LIGHT_PRIMARY.environmentTextureEnabled >= 0.5f)
        {
            const float3 environmentCoordinate = normalize(reflect(-V, N));
            const float3 probeCoordinate = CorrectParallaxCubemapDirection(
                input.worldPosition,
                environmentCoordinate,
                gMaterial.reflectionProbeCenter,
                gMaterial.reflectionProbeExtent);
            const float3 correctedCoordinate = normalize(lerp(
                environmentCoordinate,
                probeCoordinate,
                step(0.5f, gMaterial.reflectionProbeBoxProjection)));
            const float originalReflectionMip =
                roughness * (kIBLPrefilterMipCount - 1.0f);
            const float3 originalReflection = SampleRuntimeEnvironment(
                environmentCoordinate,
                originalReflectionMip);
            const float probeRoughness = clamp(
                gMaterial.reflectionReserved,
                0.035f,
                1.0f);
            const float probeReflectionMip =
                probeRoughness * (kIBLPrefilterMipCount - 1.0f);
            const float3 correctedReflection = SampleRuntimeEnvironment(
                correctedCoordinate,
                probeReflectionMip);
            const float2 originalReflectionBrdf = SampleEnvironmentBrdf(
                NdotV,
                roughness);
            const float2 correctedReflectionBrdf = SampleEnvironmentBrdf(
                NdotV,
                probeRoughness);
            const float3 originalReflectionResponse =
                FresnelSchlickRoughness(NdotV, F0, roughness) * originalReflectionBrdf.x +
                originalReflectionBrdf.y;
            const float3 correctedReflectionResponse =
                FresnelSchlickRoughness(NdotV, F0, probeRoughness) * correctedReflectionBrdf.x +
                correctedReflectionBrdf.y;

            // 通常 IBL の鏡面項を Probe 補正後へ置換し、二重反射と強度の二乗を防ぐ。
            const float3 originalSpecular = originalReflection * originalReflectionResponse;
            const float3 correctedSpecular = correctedReflection * correctedReflectionResponse;
            const float3 probeSpecular =
                originalSpecular * (1.0f - saturate(probeIntensity)) +
                correctedSpecular * probeIntensity;
            finalColor +=
                (probeSpecular - originalSpecular) *
                environmentReflectionIntensity *
                ao;
        }
    }

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        finalColor = lerp(
            finalColor,
            float3(0.88f, 0.96f, 1.0f),
            oceanFoam * 0.42f);
    }

    const float aerialPerspectiveDensity = gMaterial.oceanEnabled >= 0.5f
        ? 0.00115f
        : 0.00032f;
    finalColor = ApplyAerialPerspective(
        finalColor,
        input.worldPosition,
        LIGHT_PRIMARY.cameraPosition,
        normalize(-LIGHT_PRIMARY.direction),
        LIGHT_PRIMARY.skyUpperColor,
        LIGHT_PRIMARY.skyLowerColor,
        LIGHT_PRIMARY.skyIntensity,
        LIGHT_PRIMARY.skyEmission,
        aerialPerspectiveDensity);

    const float outputAlpha = gMaterial.oceanEnabled >= 0.5f || isDedicatedRefraction
        ? 1.0f
        : material.alpha;
    return BuildObjectPixelOutput(float4(max(finalColor, 0.0f), outputAlpha), input.position.z);
}
