#include "../Common/AnalyticAtmosphere.hlsli"
#include "../Lighting/IBLRuntime.hlsli"
#include "../Shadow/SoftShadow.hlsli"

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
    // Ocean Sun Lighting / Glitter 調整項目（C++ 側 EditorCommonTypes.h の Material 拡張と対応）
    float oceanSunDiffuseInfluence;
    float oceanSunSpecularInfluence;
    float oceanSunGlitterInfluence;
    float oceanSkyReflectionInfluence;
    float oceanAmbientInfluence;
    float oceanDiffuseFloor;
    float oceanGlitterIntensity;
    float oceanGlitterSharpness;
    float oceanGlitterDensity;
    float oceanGlitterThreshold;
    float oceanGlitterMaxClamp;
    float oceanLightingExtensionPadding0;
    float oceanMacroReflectionInfluence;
    float oceanCurvatureInfluence;
    float oceanTroughOcclusionStrength;
    float oceanCrestHazeStrength;
    float oceanCrestDetailBoost;
    float oceanSlopeRefractionInfluence;
    float oceanMediumWaveStrength;
    float oceanWaveColorSeparation;
    float oceanShapeRoughnessVariation;
    float oceanDetailFilterSharpness;
    float oceanGrazingShapeVisibility;
    float oceanShapeLightingPadding0;
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
    float shadowPadding0;
    float shadowPadding1;
    float shadowPadding2;
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

struct WaterViewConstants
{
    row_major float4x4 inverseViewProjection;
    float2 viewportUvOffset;
    float2 viewportUvScale;
    float3 viewRight;
    float projectionScaleX;
    float3 viewUp;
    float projectionScaleY;
    float3 viewForward;
    float projectionWScale;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
    float4 oceanData : TEXCOORD3;
    float4 oceanSamplingData : TEXCOORD4;
    nointerpolation float3 oceanWorldAxisX : TEXCOORD5;
    nointerpolation float3 oceanWorldAxisY : TEXCOORD6;
    nointerpolation float3 oceanWorldAxisZ : TEXCOORD7;
    bool isFrontFace : SV_IsFrontFace;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);
ConstantBuffer<WaterViewConstants> gWaterView : register(b3);

Texture2D gShadowMap : register(t1);
Texture2D gEnvironmentTexture : register(t2);
Texture2D<float4> gWaterSceneColor : register(t18);
Texture2D<float> gWaterSceneDepth : register(t19);

SamplerState gTextureSampler : register(s0);
SamplerState gSceneSampler : register(s1);

#include "OceanSurface.hlsli"

static const float kOceanPi = 3.14159265359f;
static const float kOceanEnvironmentMipCount = 8.0f;

// Ocean Debug View: 0=Final(通常) / 1=Sun Diffuse / 2=Normal Specular /
// 3=Glitter Reflection Alignment / 4=Glitter Continuous Mask / 5=Final Glitter Mask
// 調査・確認用。用が済んだら必ず 0 に戻してからコミットすること。
#define OCEAN_GLITTER_DEBUG_VIEW 0

#define OCEAN_PRIMARY_LIGHT gDirectionalLight.lights[0]

float2 MakeOceanEnvironmentUv(float3 direction)
{
    const float3 safeDirection = normalize(direction);
    return float2(
        atan2(safeDirection.z, safeDirection.x) * (0.5f / kOceanPi) + 0.5f,
        acos(clamp(safeDirection.y, -1.0f, 1.0f)) / kOceanPi);
}

float3 RotateOceanEnvironment(float3 direction, float rotation)
{
    const float rotationSin = sin(rotation);
    const float rotationCos = cos(rotation);
    return float3(
        direction.x * rotationCos - direction.z * rotationSin,
        direction.y,
        direction.x * rotationSin + direction.z * rotationCos);
}

float3 SampleOceanEnvironment(float3 direction, float roughness)
{
    const float3 safeDirection = normalize(direction);
    const float3 sunDirection = normalize(-OCEAN_PRIMARY_LIGHT.direction);
    const float3 atmosphere = EvaluateAnalyticAtmosphere(
        safeDirection,
        sunDirection,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyIntensity,
        OCEAN_PRIMARY_LIGHT.skyEmission);
    const float3 generatedEnvironment = gPrefilterCube.SampleLevel(
        gIblSampler,
        safeDirection,
        saturate(roughness) * (kOceanEnvironmentMipCount - 1.0f)).rgb;
    const float3 rotatedDirection = RotateOceanEnvironment(
        safeDirection,
        OCEAN_PRIMARY_LIGHT.environmentTextureRotation);
    const float3 textureEnvironment = gEnvironmentTexture.SampleLevel(
        gIblSampler,
        MakeOceanEnvironmentUv(rotatedDirection),
        max(
            saturate(roughness) * (kOceanEnvironmentMipCount - 1.0f) +
            OCEAN_PRIMARY_LIGHT.environmentTextureMipBias,
            0.0f)).rgb *
        max(OCEAN_PRIMARY_LIGHT.environmentTextureIntensity, 0.0f);
    const float3 generatedSky = lerp(generatedEnvironment, atmosphere, 0.42f);
    return max(lerp(
        generatedSky,
        textureEnvironment,
        saturate(OCEAN_PRIMARY_LIGHT.environmentTextureEnabled)), 0.0f);
}

float CalculateOceanSpotAttenuation(
    float3 lightDirection,
    float3 spotForward,
    DirectionalLightData light)
{
    const float spotCosine = dot(lightDirection, normalize(spotForward));
    const float innerCosine = max(light.spotCosInner, light.spotCosOuter);
    const float outerCosine = min(light.spotCosInner, light.spotCosOuter);
    return saturate(
        (spotCosine - outerCosine) /
        max(innerCosine - outerCosine, 0.0001f));
}

void BuildOceanLight(
    float3 worldPosition,
    DirectionalLightData light,
    out float3 lightDirection,
    out float3 radiance)
{
    if (light.lightType == 0)
    {
        lightDirection = normalize(-light.direction);
        radiance = max(light.color.rgb, 0.0f) * max(light.intensity, 0.0f);
        return;
    }

    const float3 toLight = light.position - worldPosition;
    const float distanceToLight = max(length(toLight), 0.001f);
    lightDirection = toLight / distanceToLight;
    const float range = max(light.range, 0.01f);
    const float normalizedDistance = saturate(distanceToLight / range);
    const float rangeAttenuation =
        saturate(1.0f - normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance);
    float attenuation =
        rangeAttenuation * rangeAttenuation /
        max(distanceToLight * distanceToLight, 0.35f);

    if (light.lightType == 2)
    {
        attenuation *= CalculateOceanSpotAttenuation(
            -lightDirection,
            light.direction,
            light);
    }

    radiance =
        max(light.color.rgb, 0.0f) *
        max(light.intensity, 0.0f) *
        attenuation;
}

//============================================================
// Ocean Shadow
//============================================================

float SampleOceanShadowProjection(
    float3 worldPosition,
    float normalDotLight,
    row_major float4x4 shadowViewProjection,
    float4 atlasTransform,
    float filterRadius)
{
    const float4 shadowPosition = mul(
        float4(worldPosition, 1.0f),
        shadowViewProjection);

    if (shadowPosition.w <= 0.000001f)
    {
        return 1.0f;
    }

    const float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    const float2 shadowUv = float2(
        shadowNdc.x * 0.5f + 0.5f,
        -shadowNdc.y * 0.5f + 0.5f);

    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
        shadowUv.y < 0.0f || shadowUv.y > 1.0f)
    {
        return 1.0f;
    }

    uint shadowMapWidth = 1u;
    uint shadowMapHeight = 1u;
    gShadowMap.GetDimensions(shadowMapWidth, shadowMapHeight);
    const float2 texelSize = rcp(max(
        float2(shadowMapWidth, shadowMapHeight),
        1.0f));
    const float2 tileMinimumUv = atlasTransform.zw + texelSize * 2.0f;
    const float2 tileMaximumUv =
        atlasTransform.zw +
        atlasTransform.xy -
        texelSize * 2.0f;
    const float2 atlasUv = shadowUv * atlasTransform.xy + atlasTransform.zw;
    const float receiverDepth = saturate(shadowNdc.z);
    const float receiverBias = lerp(
        0.0020f,
        0.00035f,
        saturate(normalDotLight));
    return SampleSoftShadow9Tap(
        gShadowMap,
        gSceneSampler,
        atlasUv,
        receiverDepth - receiverBias,
        texelSize,
        filterRadius,
        tileMinimumUv,
        tileMaximumUv);
}

float SampleOceanShadowAtlas(
    float3 worldPosition,
    float normalDotLight,
    DirectionalLightData light)
{
    if (light.shadowEnabled < 0.5f)
    {
        return 1.0f;
    }

    // 近距離まで固定半径のPCFを掛けると波面に投影された影が常にぼける。
    // Tap数は変えず、遠距離だけ従来の半径へ戻す。
    const float cameraDistance = length(worldPosition - light.cameraPosition);
    const float shadowFilterRadius = lerp(
        0.82f,
        1.35f,
        smoothstep(20.0f, 140.0f, cameraDistance));

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
        return SampleOceanShadowProjection(
            worldPosition,
            normalDotLight,
            light.shadowVP,
            atlasTransform,
            shadowFilterRadius);
    }

    uint cascadeIndex = 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
    cascadeIndex = min(cascadeIndex, 3u);

    const float currentShadow = SampleOceanShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex],
        light.shadowCascadeAtlas[cascadeIndex],
        shadowFilterRadius);

    if (cascadeIndex >= 3u)
    {
        return currentShadow;
    }

    const float cascadeNearDistance = cascadeIndex == 0u
        ? 0.0f
        : light.shadowCascadeSplits[cascadeIndex - 1u];
    const float cascadeFarDistance = light.shadowCascadeSplits[cascadeIndex];
    const float blendStartDistance = lerp(
        cascadeNearDistance,
        cascadeFarDistance,
        0.88f);
    const float cascadeBlend = saturate(
        (cameraDistance - blendStartDistance) /
        max(cascadeFarDistance - blendStartDistance, 0.0001f));

    if (cascadeBlend <= 0.0f)
    {
        return currentShadow;
    }

    const float nextShadow = SampleOceanShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex + 1u],
        light.shadowCascadeAtlas[cascadeIndex + 1u],
        shadowFilterRadius);
    return lerp(currentShadow, nextShadow, cascadeBlend);
}

float GetOceanShadowVisibility(
    float3 normal,
    float3 lightDirection,
    float3 worldPosition,
    DirectionalLightData light)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));

    if (normalDotLight <= 0.0001f)
    {
        return 1.0f;
    }

    return SampleOceanShadowAtlas(
        worldPosition,
        normalDotLight,
        light);
}

float2 GetOceanViewportUv(float2 screenUv)
{
    const float2 safeViewportScale = max(
        gWaterView.viewportUvScale,
        float2(0.00001f, 0.00001f));
    return saturate(
        (screenUv - gWaterView.viewportUvOffset) /
        safeViewportScale);
}

float2 GetOceanScreenUv(float2 viewportUv)
{
    return gWaterView.viewportUvOffset +
        saturate(viewportUv) * gWaterView.viewportUvScale;
}

float2 ClampOceanScreenUv(float2 screenUv)
{
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f)));
    const float2 minimumUv = gWaterView.viewportUvOffset + texelSize * 1.5f;
    const float2 maximumUv =
        gWaterView.viewportUvOffset +
        gWaterView.viewportUvScale -
        texelSize * 1.5f;
    return clamp(screenUv, minimumUv, maximumUv);
}

bool ProjectOceanWorldPosition(
    float3 worldPosition,
    out float2 screenUv)
{
    const float3 cameraRelativePosition =
        worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition;
    const float3 viewPosition = float3(
        dot(cameraRelativePosition, gWaterView.viewRight),
        dot(cameraRelativePosition, gWaterView.viewUp),
        dot(cameraRelativePosition, gWaterView.viewForward));
    const float clipW =
        viewPosition.z * abs(gWaterView.projectionWScale);

    if (clipW <= 0.00001f)
    {
        screenUv = 0.0f;
        return false;
    }

    const float2 viewportUv = float2(
        viewPosition.x * gWaterView.projectionScaleX / clipW * 0.5f + 0.5f,
        -viewPosition.y * gWaterView.projectionScaleY / clipW * 0.5f + 0.5f);
    screenUv = GetOceanScreenUv(viewportUv);
    return all(viewportUv >= 0.0f) &&
        all(viewportUv <= 1.0f);
}

float3 ReconstructOceanWorldPosition(float2 screenUv, float deviceDepth)
{
    const float2 viewportUv = GetOceanViewportUv(screenUv);
    const float2 ndc = float2(
        viewportUv.x * 2.0f - 1.0f,
        1.0f - viewportUv.y * 2.0f);
    const float4 worldPosition = mul(
        float4(ndc, deviceDepth, 1.0f),
        gWaterView.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

struct OceanSceneSample
{
    float3 sceneColor;
    float waterThickness;
    float shoreFoam;
    float shallowWater;
};

struct OceanScreenReflection
{
    float3 color;
    float confidence;
};

OceanSceneSample SampleOceanOpaqueScene(
    PixelShaderInput input,
    float3 rippleRefractionOffset)
{
    OceanSceneSample sceneSample;
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 sceneSize = max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f));
    const float2 screenUv = saturate(input.position.xy / sceneSize);
    const float opaqueDepth = gWaterSceneDepth.SampleLevel(
        gSceneSampler,
        screenUv,
        0.0f);
    clip(opaqueDepth + 0.00002f - input.position.z);

    const float maximumWaterDepth = max(gMaterial.oceanWaterDepth, 0.1f);
    const bool hasOpaqueSurface = opaqueDepth < 0.99999f;
    const float3 opaqueWorldPosition = ReconstructOceanWorldPosition(
        screenUv,
        opaqueDepth);
    const float viewThickness = hasOpaqueSurface
        ? length(opaqueWorldPosition - input.worldPosition)
        : maximumWaterDepth;
    const float verticalDepth = hasOpaqueSurface
        ? max(input.worldPosition.y - opaqueWorldPosition.y, 0.0f)
        : maximumWaterDepth;
    sceneSample.waterThickness = clamp(
        viewThickness,
        0.02f,
        maximumWaterDepth);
    sceneSample.shoreFoam =
        (1.0f - smoothstep(
            maximumWaterDepth * 0.0125f,
            maximumWaterDepth * 0.12f,
        verticalDepth)) *
        saturate(gMaterial.oceanFoamStrength);
    sceneSample.shallowWater = 1.0f - smoothstep(
        maximumWaterDepth * 0.025f,
        maximumWaterDepth * 0.22f,
        verticalDepth);

    const float normalizedDepth = saturate(verticalDepth / maximumWaterDepth);
    float2 projectedSurfaceUv = screenUv;
    float2 projectedNormalUv = screenUv;
    ProjectOceanWorldPosition(
        input.worldPosition,
        projectedSurfaceUv);
    ProjectOceanWorldPosition(
        input.worldPosition +
            rippleRefractionOffset * lerp(0.28f, 1.10f, normalizedDepth),
        projectedNormalUv);
    const float cameraDistance = length(
        input.worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
    const float oceanDomainLength = rcp(max(input.oceanSamplingData.w, 0.000001f));
    const float depthAwareRefraction = lerp(
        0.42f,
        1.0f,
        smoothstep(0.015f, 0.62f, normalizedDepth));
    const float nearRefractionStability = lerp(
        0.72f,
        1.0f,
        smoothstep(24.0f, 110.0f, cameraDistance));
    const float refractionDistanceFade = 1.0f - smoothstep(
        oceanDomainLength * 0.06f,
        oceanDomainLength * 0.34f,
        cameraDistance);
    const float2 refractionOffset = clamp(
        (projectedNormalUv - projectedSurfaceUv) *
            max(gMaterial.oceanRefractionDistortion, 0.0f) *
            refractionDistanceFade *
            depthAwareRefraction *
            nearRefractionStability,
        -gWaterView.viewportUvScale * 0.018f,
        gWaterView.viewportUvScale * 0.018f);
    const float2 refractedUv = ClampOceanScreenUv(screenUv + refractionOffset);
    const float refractedDepth = gWaterSceneDepth.SampleLevel(
        gSceneSampler,
        refractedUv,
        0.0f);
    const float refractionValidity = step(
        input.position.z - 0.00002f,
        refractedDepth);
    const float2 resolvedRefractionUv = lerp(
        screenUv,
        refractedUv,
        refractionValidity);
    sceneSample.sceneColor = gWaterSceneColor.SampleLevel(
        gSceneSampler,
        resolvedRefractionUv,
        0.0f).rgb;
    return sceneSample;
}

float OceanDistributionGgx(float normalDotHalf, float roughness)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alphaSquared = alpha * alpha;
    const float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared /
        max(kOceanPi * denominator * denominator, 0.00001f);
}

float OceanGeometrySmithCorrelated(
    float normalDotView,
    float normalDotLight,
    float roughness)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alphaSquared = alpha * alpha;
    const float viewLambda = normalDotLight * sqrt(max(
        (-normalDotView * alphaSquared + normalDotView) * normalDotView +
        alphaSquared,
        0.00001f));
    const float lightLambda = normalDotView * sqrt(max(
        (-normalDotLight * alphaSquared + normalDotLight) * normalDotLight +
        alphaSquared,
        0.00001f));
    return 0.5f / max(viewLambda + lightLambda, 0.00001f);
}

float3 OceanFresnelSchlick(float cosineTheta, float fresnelBase)
{
    return fresnelBase +
        (1.0f - fresnelBase) * pow(1.0f - saturate(cosineTheta), 5.0f);
}

float OceanLuminance(float3 color)
{
    return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

float3 CompressOceanHighlight(float3 color, float maximumLuminance)
{
    const float safeMaximumLuminance = max(maximumLuminance, 0.01f);
    const float luminance = OceanLuminance(color);
    const float compression = safeMaximumLuminance /
        (safeMaximumLuminance + luminance);
    return max(color, 0.0f) * compression;
}

// ------------------------------------------------------------
// Sun Irradiance Response
// ------------------------------------------------------------
// SUN強度をそのまま鏡面反射へ渡すと、強度を上げた時に反射だけが膜状に広がる。
// 輝度1.0を基準に保った対数応答へ変換し、色相を維持したまま広い強度範囲を扱う。
float3 EvaluateOceanSunIrradiance(float3 radiance)
{
    const float radianceLuminance = max(
        dot(max(radiance, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)),
        0.0f);

    if (radianceLuminance <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float responsiveLuminance = log2(1.0f + radianceLuminance);
    return max(radiance, 0.0f) * (responsiveLuminance / radianceLuminance);
}

// ------------------------------------------------------------
// Sun Glitter
// ------------------------------------------------------------
// 水面に斑点模様が出ないよう、座標Hashやfloor()による点マスクは使わない。
// 太陽方向と水面法線の関係だけで連続した反射帯を作る。

float3 EvaluateOceanSunGlitter(
    float3 macroNormal,
    float3 mediumNormal,
    float3 opticalNormal,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance,
    float roughness,
    float fresnelBase,
    float slope,
    float shadowVisibility,
    float glitterIntensity,
    float glitterSharpness,
    float glitterDensity,
    float glitterThreshold,
    float glitterMaxClamp,
    out float debugReflectionAlignment,
    out float debugContinuousMask,
    out float debugFinalMask)
{
    // (A) 反射整列条件: H = normalize(V + L), reflectionAlignment = saturate(dot(N, H))。
    // ここでは座標Hashによる間引きは使わない。Hash点滅は水面に斑点模様として残るため、
    // SUN反射は連続したSpecular帯として扱う。
    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float largeReflectionAlignment = saturate(dot(macroNormal, halfDirection));
    const float mediumReflectionAlignment = saturate(dot(mediumNormal, halfDirection));
    const float reflectionAlignment = saturate(dot(opticalNormal, halfDirection));

    // Fine NormalがHalf Vectorへ向く微小面の割合をGGX分布で評価する。
    // 座標Noiseではなく実際の法線方向で決まるため、Camera/SUN移動へ連続的に追従する。
    const float glitterMicrofacetRoughness = clamp(
        lerp(0.16f, 0.035f, saturate(glitterSharpness)) + roughness * 0.12f,
        0.035f,
        0.24f);
    const float glitterDistribution = OceanDistributionGgx(
        reflectionAlignment,
        glitterMicrofacetRoughness);
    const float glintCandidate = glitterDistribution /
        (glitterDistribution + 8.0f);

    // 閾値は滑らかに使う。離散的なstepやfloor座標を使うと斑点が発生する。
    const float effectiveThreshold = lerp(0.02f, 0.72f, saturate(glitterThreshold));
    const float glintMask = smoothstep(effectiveThreshold, 1.0f, glintCandidate);

    // Large Normalで太陽反射の通り道を決め、Medium Normalで帯の内部を分割する。
    // Fine Normalは最後のGlitterだけへ使い、大波の反射帯を細波で白い板へ変えない。
    const float largePath = pow(largeReflectionAlignment, lerp(5.0f, 18.0f, saturate(roughness)));
    const float mediumPartition = pow(mediumReflectionAlignment, lerp(10.0f, 34.0f, saturate(roughness)));
    const float broadReflection = largePath * lerp(0.34f, 1.0f, mediumPartition);

    const float normalDotLight = saturate(dot(macroNormal, lightDirection));
    const float normalDotView = saturate(dot(opticalNormal, viewDirection));
    const float surfaceVisibility =
        smoothstep(0.015f, 0.18f, normalDotLight) *
        smoothstep(0.01f, 0.22f, normalDotView);
    // Glitter Pathの帯を波の傾斜でわずかに強調する。
    const float glitterCoverage = lerp(
        0.85f,
        1.15f,
        saturate(slope));

    // 広い反射帯は通常のGGX Sun Specularが担当する。
    // broadReflection単体をGlitterへ混ぜると、Fine Normalが太陽へ向いていない面まで
    // 半透明の銀色領域になり、カメラ距離で水たまり状に拡縮して見える。
    const float finalGlitterMask = saturate(
        glintMask * broadReflection *
        surfaceVisibility *
        lerp(0.65f, 1.0f, saturate(glitterDensity)));

    debugReflectionAlignment = largeReflectionAlignment;
    debugContinuousMask = saturate(glintMask * broadReflection);
    debugFinalMask = saturate(finalGlitterMask);

    const float3 fresnel = OceanFresnelSchlick(
        normalDotView,
        fresnelBase);
    const float3 glitterRadiance =
        radiance *
        fresnel *
        finalGlitterMask *
        glitterCoverage *
        shadowVisibility *
        max(glitterIntensity, 0.0f);

    // HDR値はBloomへ渡すが、自動露出を破壊する単一画素ピークにはしない。
    // ここは「明るさの安全弁」。空間的な点マスクは使わない。
    return CompressOceanHighlight(glitterRadiance, max(glitterMaxClamp, 0.1f));
}

OceanScreenReflection TraceOceanScreenReflection(
    float3 worldPosition,
    float3 surfaceNormal,
    float3 viewDirection,
    float roughness,
    float reflectionVisibility)
{
    OceanScreenReflection reflection;
    reflection.color = 0.0f;
    reflection.confidence = 0.0f;

    const float3 reflectionDirection = normalize(reflect(
        -viewDirection,
        surfaceNormal));

    if (reflectionDirection.y <= -0.015f ||
        roughness >= 0.72f ||
        reflectionVisibility <= 0.035f)
    {
        return reflection;
    }

    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f)));
    const float maximumTraceDistance = min(
        max(gMaterial.oceanWaterDepth * 1.5f, 48.0f),
        180.0f);
    const float3 rayOrigin = worldPosition + surfaceNormal * 0.08f;
    float rayDistance = 0.30f;

    [loop]
    for (int traceIndex = 0; traceIndex < 14; traceIndex++)
    {
        const float traceRatio =
            float(traceIndex) / 13.0f;
        rayDistance += lerp(0.35f, 7.0f, traceRatio * traceRatio);

        if (rayDistance >= maximumTraceDistance)
        {
            break;
        }

        const float3 rayPosition =
            rayOrigin + reflectionDirection * rayDistance;
        float2 rayScreenUv = 0.0f;

        if (!ProjectOceanWorldPosition(
            rayPosition,
            rayScreenUv))
        {
            break;
        }

        const float sceneDepth = gWaterSceneDepth.SampleLevel(
            gSceneSampler,
            rayScreenUv,
            0.0f);

        if (sceneDepth >= 0.99999f)
        {
            continue;
        }

        const float3 sceneWorldPosition = ReconstructOceanWorldPosition(
            rayScreenUv,
            sceneDepth);
        const float rayCameraDistance = length(
            rayPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float sceneCameraDistance = length(
            sceneWorldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float depthDelta = rayCameraDistance - sceneCameraDistance;
        const float hitThickness = max(
            0.20f,
            rayDistance * lerp(0.018f, 0.045f, traceRatio));

        if (depthDelta <= 0.0f || depthDelta >= hitThickness)
        {
            continue;
        }

        // 近距離の低粗さ反射は中心Sampleを優先し、固定5Tapによる面全体の軟化を避ける。
        // 遠距離と高粗さでは従来相当の平滑化へ連続的に戻す。
        const float surfaceCameraDistance = length(
            worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float distanceBlur = smoothstep(
            24.0f,
            140.0f,
            surfaceCameraDistance);
        const float blurRadius =
            lerp(0.18f, 1.0f, distanceBlur) +
            roughness * roughness * 3.0f;
        const float sideSampleWeight = lerp(
            0.055f,
            0.15f,
            saturate(roughness * 1.4f + distanceBlur * 0.45f));
        const float centerSampleWeight = 1.0f - sideSampleWeight * 4.0f;
        const float2 blurOffset = texelSize * blurRadius;
        float3 reflectedColor =
            gWaterSceneColor.SampleLevel(gSceneSampler, rayScreenUv, 0.0f).rgb * centerSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(blurOffset.x, 0.0f)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(blurOffset.x, 0.0f)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(0.0f, blurOffset.y)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(0.0f, blurOffset.y)),
            0.0f).rgb * sideSampleWeight;

        const float2 viewportUv = GetOceanViewportUv(rayScreenUv);
        const float edgeDistance = min(
            min(viewportUv.x, 1.0f - viewportUv.x),
            min(viewportUv.y, 1.0f - viewportUv.y));
        reflection.color = max(reflectedColor, 0.0f);
        reflection.confidence =
            smoothstep(0.01f, 0.08f, edgeDistance) *
            (1.0f - rayDistance / maximumTraceDistance) *
            (1.0f - smoothstep(0.18f, 0.72f, roughness)) *
            smoothstep(-0.015f, 0.08f, reflectionDirection.y);
        return reflection;
    }

    return reflection;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    // OceanはDepthを書かない両面描画なので、急斜面では裏面三角形が表面へ
    // 後描きされる。実際のFFT海面に対するカメラ側だけを残し、櫛状の暗部を防ぐ。
    // projectionWScaleの絶対値は従来の投影係数、符号はCameraの水面側を表す。
    // Root Signatureの64 DWORD上限を増やさず、Scene/Gameごとの判定を渡す。
    const bool cameraIsAboveSurface = gWaterView.projectionWScale >= 0.0f;
    const bool isVisibleCameraSide = cameraIsAboveSurface
        ? input.isFrontFace
        : !input.isFrontFace;
    clip(isVisibleCameraSide ? 1.0f : -1.0f);

    const bool isUnderwaterSurface = !cameraIsAboveSurface;
    const float3 viewDirection = normalize(
        OCEAN_PRIMARY_LIGHT.cameraPosition - input.worldPosition);

    // 曲率と波頭判定は常にFFTの物理的な表向き法線から求める。
    // 裏面だけ先に反転すると、同じ波が表裏で別の波頭として評価されて二重像になる。
    const float3 interpolatedMacroNormal = input.normal;
    float3 resolvedFftNormal = interpolatedMacroNormal;
    float4 oceanData = input.oceanData;
    ResolveOceanPixelSurface(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        resolvedFftNormal,
        oceanData);

    // 大波の陰影は補間済みFFT法線、反射と屈折は画素FFT法線を使う。
    // 同じFFT場の帯域だけを分け、疑似ノイズによる水玉模様を発生させない。
    const OceanSurfaceFrame macroSurfaceFrame = EvaluateOceanSurfaceFrame(
        resolvedFftNormal,
        input.worldPosition,
        oceanData,
        0.0f,
        gMaterial.oceanCrestSharpness);
    float3 macroNormal = macroSurfaceFrame.macroNormal;
    // 画面微分Curvatureを色・粗さ・反射の主Maskへ使うと、急斜面でQuad境界が
    // 三角形状の暗部として露出する。連続なFFT波高と斜面から形状Maskを作る。
    const float curvatureInfluence = saturate(gMaterial.oceanCurvatureInfluence);
    const float stableSlopeMask = smoothstep(0.08f, 0.72f, macroSurfaceFrame.slope);
    const float worldCrestCurvature = smoothstep(
        0.012f,
        0.11f,
        macroSurfaceFrame.signedCurvature);
    const float worldTroughCurvature = smoothstep(
        0.012f,
        0.11f,
        -macroSurfaceFrame.signedCurvature);
    const float curvatureRefinement = curvatureInfluence * 0.20f;
    const float crestCurvatureMask =
        macroSurfaceFrame.crest *
        lerp(0.42f, 1.0f, stableSlopeMask) *
        lerp(0.92f, lerp(0.96f, 1.08f, worldCrestCurvature), curvatureRefinement);
    const float troughCurvatureMask =
        macroSurfaceFrame.trough *
        lerp(0.46f, 1.0f, stableSlopeMask) *
        lerp(0.92f, lerp(0.96f, 1.08f, worldTroughCurvature), curvatureRefinement);
    const float crestResponse = saturate(
        macroSurfaceFrame.crest * lerp(0.46f, 1.0f, stableSlopeMask));
    const float crestPreStage =
        smoothstep(0.08f, 0.42f, crestResponse) *
        (1.0f - smoothstep(0.54f, 0.76f, crestResponse));
    const float crestThinStage =
        smoothstep(0.34f, 0.70f, crestResponse) *
        (1.0f - smoothstep(0.78f, 0.96f, crestResponse));
    const float crestBreakingStage = smoothstep(
        0.68f,
        0.94f,
        crestResponse);
    const OceanNormalLayers normalLayers = ResolveOceanLayeredOpticalNormals(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        resolvedFftNormal,
        gMaterial.oceanDetailNormalStrength,
        gMaterial.oceanMediumWaveStrength,
        gMaterial.oceanDetailFilterSharpness,
        crestResponse,
        gMaterial.oceanCrestDetailBoost);
    float3 mediumNormal = normalLayers.mediumNormal;
    float3 specularNormal = normalLayers.fineNormal;
    const float cameraDistance = length(
        OCEAN_PRIMARY_LIGHT.cameraPosition - input.worldPosition);
    const float nearSurfaceDetail = 1.0f - smoothstep(
        28.0f,
        150.0f,
        cameraDistance);

    // Oceanは両面描画するため、光学法線だけを現在見ている面へ揃える。
    // FFT法線を再Sampleした後に行わないと、裏面でも上向き法線の表面反射が残る。
    if (dot(macroNormal, viewDirection) < 0.0f)
    {
        macroNormal = -macroNormal;
    }

    if (dot(mediumNormal, viewDirection) < 0.0f)
    {
        mediumNormal = -mediumNormal;
    }

    if (dot(specularNormal, viewDirection) < 0.0f)
    {
        specularNormal = -specularNormal;
    }

    const float reflectionNormalBlend = saturate(
        max(gMaterial.oceanMacroReflectionInfluence, 0.0f) *
        lerp(0.82f, 0.96f, nearSurfaceDetail));
    const float3 reflectionNormal = normalize(lerp(
        macroNormal,
        mediumNormal,
        reflectionNormalBlend));
    const float troughOcclusionStrength = clamp(
        gMaterial.oceanTroughOcclusionStrength,
        0.0f,
        0.25f);
    const float skyEnclosure = saturate(
        troughCurvatureMask *
        lerp(0.42f, 1.0f, stableSlopeMask));
    const float skyVisibility = 1.0f - skyEnclosure * troughOcclusionStrength;
    const float3 oceanWorldUp = normalize(input.oceanWorldAxisY);
    const float bentNormalWeight =
        skyEnclosure *
        saturate(troughOcclusionStrength / 0.25f) *
        0.22f;
    const float3 environmentReflectionNormal = normalize(lerp(
        reflectionNormal,
        normalize(reflectionNormal + oceanWorldUp * 0.34f),
        bentNormalWeight));
    // 屈折はFineとMediumの差だけを使う。Large Waveの傾斜は歪み量を増減するが、
    // 形状そのものを二重に屈折へ加えない。
    const float3 rippleRefractionOffset =
        (specularNormal - mediumNormal) *
        (1.0f + macroSurfaceFrame.slope *
            max(gMaterial.oceanSlopeRefractionInfluence, 0.0f));
    const float normalDotView = saturate(dot(mediumNormal, viewDirection));
    const float specularNormalDotView = saturate(dot(specularNormal, viewDirection));
    const float mediumStructure = smoothstep(
        0.002f,
        0.095f,
        1.0f - saturate(dot(macroNormal, mediumNormal)));
    const OceanSceneSample sceneSample = SampleOceanOpaqueScene(
        input,
        rippleRefractionOffset);
    const float surfaceFoam = max(
        EvaluateOceanFoam(
            input.worldPosition,
            oceanData,
            macroSurfaceFrame,
            gMaterial.oceanFoamStrength,
            gMaterial.oceanFoamThreshold,
            gMaterial.oceanCrestSharpness),
        sceneSample.shoreFoam);
    // 水中側から表面用Foamを描くと、波頭の裏に同じ輪郭がもう一枚現れる。
    const float foam = isUnderwaterSurface ? 0.0f : surfaceFoam;

    const float normalizedHeight =
        clamp(oceanData.w, -1.0f, 1.0f) * 0.5f + 0.5f;
    const float smoothHeight = smoothstep(0.02f, 0.98f, normalizedHeight);
    const float heightPathScale = lerp(1.28f, 0.72f, smoothHeight);
    const float opticalThickness =
        sceneSample.waterThickness * heightPathScale;
    const float absorptionDistance = max(
        gMaterial.oceanAbsorptionDistance,
        0.1f);
    const float3 deepColor = max(gMaterial.oceanDeepColor, 0.0f);
    const float3 shallowColor = max(gMaterial.color.rgb, 0.0f);
    const float3 environmentWaterTint = max(
        lerp(
            OCEAN_PRIMARY_LIGHT.skyLowerColor,
            OCEAN_PRIMARY_LIGHT.skyUpperColor,
            0.22f),
        0.0f);
    const float3 midWaterColor = max(
        lerp(shallowColor, deepColor, 0.46f) * float3(0.86f, 1.08f, 1.04f) +
        environmentWaterTint * 0.08f,
        0.0f);
    const float3 absorptionCoefficient =
        max(1.0f - saturate(deepColor), 0.025f) /
        absorptionDistance;
    const float3 transmittance = exp(
        -absorptionCoefficient * opticalThickness);
    const float shallowWeight = exp(
        -opticalThickness / absorptionDistance);
    const float maximumWaterDepth = max(gMaterial.oceanWaterDepth, 0.1f);
    const float clearDepthEnd = min(
        3.0f,
        max(maximumWaterDepth * 0.08f, 0.5f));
    const float fogDepthEnd = min(
        15.0f,
        max(maximumWaterDepth * 0.30f, clearDepthEnd + 2.0f));
    const float deepDepthEnd = min(
        70.0f,
        max(maximumWaterDepth * 0.88f, fogDepthEnd + 5.0f));
    const float clearWaterLayer = 1.0f - smoothstep(
        clearDepthEnd * 0.55f,
        clearDepthEnd * 1.45f,
        sceneSample.waterThickness);
    const float fogWaterLayer =
        smoothstep(
            clearDepthEnd * 0.65f,
            fogDepthEnd,
            sceneSample.waterThickness) *
        (1.0f - smoothstep(
            fogDepthEnd * 0.82f,
            deepDepthEnd,
            sceneSample.waterThickness));
    const float deepWaterLayer = smoothstep(
        fogDepthEnd * 0.82f,
        deepDepthEnd,
        sceneSample.waterThickness);
    const float waterLayerWeight = max(
        clearWaterLayer + fogWaterLayer + deepWaterLayer,
        0.0001f);
    const float3 volumeColor = (
        shallowColor * clearWaterLayer +
        midWaterColor * fogWaterLayer +
        deepColor * deepWaterLayer) /
        waterLayerWeight;
    const float viewThroughWater = saturate(
        normalDotView * 0.78f + shallowWeight * 0.42f);
    const float shallowCausticFocus = saturate(
        oceanData.x * 0.48f +
        crestResponse * 0.24f +
        mediumStructure * 0.12f);
    const float shallowCaustic =
        sceneSample.shallowWater *
        shallowCausticFocus *
        (1.0f - saturate(foam)) *
        0.18f;
    const float3 transmittedSceneColor =
        sceneSample.sceneColor * (1.0f + shallowCaustic);
    const float3 physicallyRefractedColor =
        transmittedSceneColor * transmittance +
        volumeColor * (1.0f - transmittance) *
            lerp(0.72f, 1.08f, viewThroughWater);
    const float3 clearWaterColor = lerp(
        transmittedSceneColor,
        physicallyRefractedColor,
        0.32f);
    const float3 fogWaterColor = lerp(
        midWaterColor,
        environmentWaterTint,
        0.10f);
    float3 refractedColor = lerp(
        clearWaterColor,
        fogWaterColor,
        saturate(fogWaterLayer * 0.82f));
    refractedColor = lerp(
        refractedColor,
        deepColor,
        saturate(deepWaterLayer * 0.94f));

    // 輝度だけでなく水色の深さでも波形を残す。高さ単独ではなく、
    // 符号付き曲率と斜面を条件にするため等高線状の帯を作らない。
    const float waveColorSeparation =
        clamp(gMaterial.oceanWaveColorSeparation, 0.0f, 1.0f);
    const float valleyColorMask = saturate(
        troughCurvatureMask *
        lerp(0.62f, 1.0f, macroSurfaceFrame.slope));
    const float crestColorMask = saturate(
        crestCurvatureMask *
        lerp(0.48f, 0.86f, macroSurfaceFrame.slope)) *
        (1.0f - saturate(foam));
    const float3 valleyWaterColor = lerp(
        deepColor,
        environmentWaterTint,
        0.10f) * float3(0.86f, 0.96f, 1.06f);
    const float3 crestWaterColor = lerp(
        shallowColor,
        environmentWaterTint,
        0.16f) * float3(0.92f, 1.03f, 1.04f);
    refractedColor = lerp(
        refractedColor,
        valleyWaterColor,
        valleyColorMask * waveColorSeparation * 0.16f);
    refractedColor = lerp(
        refractedColor,
        crestWaterColor,
        crestColorMask * waveColorSeparation * 0.075f);

    float3 directBodyLight = float3(0.0f, 0.0f, 0.0f);
    float3 directSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 sunGlitter = float3(0.0f, 0.0f, 0.0f);
    float3 crestTransmissionLight = float3(0.0f, 0.0f, 0.0f);
    // 大波Normalと太陽方向だけから作るDirectional Diffuse。
    // 波の高さそのものではなく、法線とライト方向の関係(NdotL)で明暗差を出す。
    float3 sunDirectionalDiffuse = float3(0.0f, 0.0f, 0.0f);
    const float shapeRoughnessVariation =
        clamp(gMaterial.oceanShapeRoughnessVariation, 0.0f, 0.5f);
    const float shapeRoughnessFactor =
        1.0f +
        troughCurvatureMask * shapeRoughnessVariation * 0.82f -
        crestPreStage * shapeRoughnessVariation * 0.16f -
        crestThinStage * shapeRoughnessVariation * 0.46f +
        crestBreakingStage * shapeRoughnessVariation * 0.12f +
        mediumStructure * shapeRoughnessVariation *
            lerp(0.06f, -0.14f, nearSurfaceDetail) +
        saturate(macroSurfaceFrame.slope) * shapeRoughnessVariation * 0.10f;
    const float baseRoughness = clamp(
        gMaterial.oceanRoughness * shapeRoughnessFactor *
            lerp(1.0f, 0.92f, nearSurfaceDetail),
        0.055f,
        1.0f);
    // Roughness AAも画面微分ではなく、MediumとFineの連続なNormal差を使う。
    // これによりポリゴン境界の微分値が黒い三角形として反射へ出ない。
    const float fineNormalVariance = saturate(
        1.0f - dot(mediumNormal, specularNormal));
    const float stableSpecularVariance = saturate(
        fineNormalVariance * 1.35f +
        macroSurfaceFrame.interpolationVariance * 0.55f +
        mediumStructure * 0.035f);
    const float filteredRoughness = clamp(
        sqrt(baseRoughness * baseRoughness + stableSpecularVariance *
            lerp(0.18f, 0.12f, nearSurfaceDetail)),
        0.075f,
        1.0f);
    const float waterIor = max(gMaterial.ior, 1.0001f);
    const float fresnelRoot = (waterIor - 1.0f) / (waterIor + 1.0f);
    const float fresnelBase = fresnelRoot * fresnelRoot;
    float debugGlitterAlignment = 0.0f;
    float debugGlitterContinuousMask = 0.0f;
    float debugGlitterFinalMask = 0.0f;

    [unroll]
    for (int lightIndex = 0; lightIndex < 4; lightIndex++)
    {
        const DirectionalLightData light = gDirectionalLight.lights[lightIndex];

        if (light.shadowEnabled < -0.5f)
        {
            break;
        }

        if (light.shadowEnabled < 0.5f && light.intensity <= 0.0001f)
        {
            continue;
        }

        float3 lightDirection;
        float3 radiance;
        BuildOceanLight(
            input.worldPosition,
            light,
            lightDirection,
            radiance);
        const float normalDotLight = dot(macroNormal, lightDirection);
        const float shadowVisibility = GetOceanShadowVisibility(
            macroNormal,
            lightDirection,
            input.worldPosition,
            light);
        const float softShadowVisibility = lerp(
            1.0f,
            shadowVisibility,
            0.72f);
        float directionalSurfaceVisibility = softShadowVisibility;

        const float wrappedLight = saturate(normalDotLight * 0.72f + 0.28f);
        const float forwardScatter =
            pow(saturate(dot(-viewDirection, lightDirection)), 4.0f) *
            (0.18f + macroSurfaceFrame.slope * 0.22f);
        if (light.lightType == 0)
        {
            // 波面DiffuseはSUNだけを対象にする。Point/Spotを混ぜると、SUN強度を変更しても
            // 既存の局所ライト成分に埋もれて見た目がほとんど変化しなくなる。
            const float sunNdotL = saturate(dot(macroNormal, lightDirection));
            const float directionalDiffuseFactor = lerp(
                saturate(gMaterial.oceanDiffuseFloor),
                1.0f,
                sunNdotL);
            sunDirectionalDiffuse += radiance *
                directionalDiffuseFactor *
                directionalSurfaceVisibility;

            // 波頭の薄い水だけに弱い透過散乱を出す。
            // SUNが法線の裏側へ回った時だけ発生し、常時発光する波頭にはしない。
            const float crestBackLighting = pow(
                saturate(-normalDotLight),
                0.72f);
            crestTransmissionLight += EvaluateOceanSunIrradiance(radiance) *
                crestBackLighting *
                crestThinStage *
                (0.10f + 0.08f * stableSlopeMask) *
                directionalSurfaceVisibility;
        }
        else
        {
            // Point / Spotは距離減衰を含む局所的な水中散乱として扱う。
            directBodyLight += radiance *
                (wrappedLight + forwardScatter) *
                softShadowVisibility;
        }

        const float3 halfDirection = normalize(viewDirection + lightDirection);
        // SUNの広い鏡面はLarge+Mediumで評価する。FineはGlitterへ限定する。
        const float3 directSpecularNormal = light.lightType == 0
            ? mediumNormal
            : specularNormal;
        const float normalDotLightOptical = saturate(dot(
            directSpecularNormal,
            lightDirection));
        const float normalDotHalf = saturate(dot(directSpecularNormal, halfDirection));
        const float directNormalDotView = saturate(dot(directSpecularNormal, viewDirection));
        const float viewDotHalf = saturate(dot(viewDirection, halfDirection));
        const float largeSunPath = pow(
            saturate(dot(macroNormal, halfDirection)),
            8.0f);
        const float mediumSunPartition = pow(
            saturate(dot(mediumNormal, halfDirection)),
            26.0f);
        const float sunPathPartition = light.lightType == 0
            ? largeSunPath * lerp(0.06f, 1.0f, mediumSunPartition)
            : 1.0f;

        if (normalDotLightOptical > 0.0f && directNormalDotView > 0.0f)
        {
            const float distribution = OceanDistributionGgx(
                normalDotHalf,
                filteredRoughness);
            const float geometry = OceanGeometrySmithCorrelated(
                directNormalDotView,
                normalDotLightOptical,
                filteredRoughness);
            const float3 directFresnel = OceanFresnelSchlick(
                viewDotHalf,
                fresnelBase);
            const float rawSpecularResponse =
                distribution * geometry * normalDotLightOptical;
            const float finiteSpecularResponse =
                rawSpecularResponse /
                (1.0f + rawSpecularResponse / 9.0f);
            const float3 specularRadiance = light.lightType == 0
                ? EvaluateOceanSunIrradiance(radiance)
                : radiance;
            directSpecular += specularRadiance *
                finiteSpecularResponse * directFresnel * sunPathPartition *
                (light.lightType == 0
                    ? directionalSurfaceVisibility
                    : softShadowVisibility);
        }

        if (light.lightType == 0)
        {
            float lightDebugAlignment = 0.0f;
            float lightDebugContinuousMask = 0.0f;
            float lightDebugFinalMask = 0.0f;
            sunGlitter += EvaluateOceanSunGlitter(
                macroNormal,
                mediumNormal,
                specularNormal,
                viewDirection,
                lightDirection,
                EvaluateOceanSunIrradiance(radiance),
                filteredRoughness,
                fresnelBase,
                macroSurfaceFrame.slope,
                directionalSurfaceVisibility,
                gMaterial.oceanGlitterIntensity,
                gMaterial.oceanGlitterSharpness,
                gMaterial.oceanGlitterDensity,
                gMaterial.oceanGlitterThreshold,
                gMaterial.oceanGlitterMaxClamp,
                lightDebugAlignment,
                lightDebugContinuousMask,
                lightDebugFinalMask);
            debugGlitterAlignment = lightDebugAlignment;
            debugGlitterContinuousMask = lightDebugContinuousMask;
            debugGlitterFinalMask = lightDebugFinalMask;
        }
    }

#if OCEAN_GLITTER_DEBUG_VIEW == 1
    // Sun Diffuse単体(圧縮前)を白黒/色付きでそのまま表示する。
    return float4(max(sunDirectionalDiffuse, 0.0f), 1.0f);
#elif OCEAN_GLITTER_DEBUG_VIEW == 2
    // Normal(GGX) Specular単体を表示する。Glitterの寄与は含まない。
    return float4(CompressOceanHighlight(directSpecular, 1.0f), 1.0f);
#elif OCEAN_GLITTER_DEBUG_VIEW == 3
    // Glitter Reflection Alignment (H=normalize(V+L), saturate(dot(N,H)))。
    // ここが太陽方向へ伸びる「帯」になっていれば正常。画面全体が白い場合は異常。
    return float4(debugGlitterAlignment.xxx, 1.0f);
#elif OCEAN_GLITTER_DEBUG_VIEW == 4
    // 座標Hashを使わない連続Glitter Mask。ここに格子状・斑点状の点群が出たら異常。
    return float4(debugGlitterContinuousMask.xxx, 1.0f);
#elif OCEAN_GLITTER_DEBUG_VIEW == 5
    // 最終Glitter Mask = reflectionAlignmentゲート × surfaceVisibility。
    // 白黒表示した時に、連続した反射帯になっていることを確認する。
    return float4(debugGlitterFinalMask.xxx, 1.0f);
#endif

    const float skyBlend = saturate(macroNormal.y * 0.5f + 0.5f);
    const float3 skyIrradiance = lerp(
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        skyBlend) *
        max(OCEAN_PRIMARY_LIGHT.ambientIntensity, 0.0f);
    const float3 compressedDirectBodyLight =
        directBodyLight / (1.0f + directBodyLight * 0.42f);
    // SUNの本体光は鏡面反射とは別に評価する。対数応答は強度1.0を維持し、
    // 強度を上げた時も早期飽和せず波面全体の明るさと斜面差へ反映する。
    const float3 responsiveSunDirectionalDiffuse =
        EvaluateOceanSunIrradiance(sunDirectionalDiffuse);
    const float sunDiffuseInfluence = max(gMaterial.oceanSunDiffuseInfluence, 0.0f);
    const float ambientInfluence = max(gMaterial.oceanAmbientInfluence, 0.0f);
    // SUN由来の水面照明を体積色だけへ入れると、濃い水色に吸われて海だけ暗く見える。
    // 同じSUN Diffuseを浅い水面色側にも少量だけ乗せ、光に反応しつつ白飛びは圧縮で抑える。
    const float3 sunSurfaceLighting =
        responsiveSunDirectionalDiffuse *
        lerp(midWaterColor, shallowColor, 0.58f) *
        (0.34f + 0.20f * saturate(macroSurfaceFrame.slope)) *
        sunDiffuseInfluence;
    const float3 bodyLighting =
        (float3(0.30f, 0.30f, 0.30f) + skyIrradiance * 0.34f) *
            ambientInfluence * lerp(1.0f, skyVisibility, 0.42f) +
        compressedDirectBodyLight * 0.34f +
        responsiveSunDirectionalDiffuse * 0.42f * sunDiffuseInfluence;
    const float3 litVolume = refractedColor * bodyLighting;

    const float fresnel = fresnelBase +
        (1.0f - fresnelBase) * pow(1.0f - normalDotView, 5.0f);
    // Sky/EnvironmentはLarge+Medium Normalから反射方向を作る。
    // Fine Normalを除外し、天頂・中間高度・水平線の空色変化で大波の向きを読ませる。
    const float3 reflectionDirection = reflect(
        -viewDirection,
        environmentReflectionNormal);
    const float3 environmentReflection = SampleOceanEnvironment(
        reflectionDirection,
        baseRoughness) *
        (0.30f + max(OCEAN_PRIMARY_LIGHT.reflectionIntensity, 0.0f) * 0.70f) *
        lerp(0.95f, lerp(1.04f, 1.08f, nearSurfaceDetail), mediumStructure);
    const float reflectionWeight = saturate(
        fresnel * max(gMaterial.reflectance, 0.0f));
    OceanScreenReflection screenReflection;
    screenReflection.color = environmentReflection;
    screenReflection.confidence = 0.0f;

    // SSRは水面上側の反射専用。裏面で走らせると表側のScene像を再取得してしまう。
    if (!isUnderwaterSurface)
    {
        screenReflection = TraceOceanScreenReflection(
            input.worldPosition,
            reflectionNormal,
            viewDirection,
            baseRoughness,
            reflectionWeight);
    }

    const float3 resolvedReflection = lerp(
        environmentReflection,
        screenReflection.color,
        screenReflection.confidence) *
        lerp(1.0f, skyVisibility, 0.88f);
    const float viewAngleTransparency = smoothstep(
        0.08f,
        0.72f,
        normalDotView);
    const float depthTransparency = saturate(
        clearWaterLayer +
        fogWaterLayer * 0.58f +
        deepWaterLayer * 0.12f);
    const float transmissionWeight =
        lerp(0.16f, 1.0f, viewAngleTransparency) *
        lerp(0.34f, 1.0f, depthTransparency);
    const float gameReflectionWeight = saturate(
        reflectionWeight *
        lerp(1.20f, 0.88f, viewAngleTransparency));
    const float transmissionEnergy = 1.0f - saturate(
        gameReflectionWeight * 0.92f);
    const float regularScatteringAmount =
        0.012f +
        mediumStructure * 0.010f +
        saturate(macroSurfaceFrame.slope) * 0.006f;
    const float preCrestScatteringAmount =
        crestPreStage * (1.0f - crestThinStage) * 0.012f;
    const float scatteringVisibility =
        transmissionEnergy *
        lerp(0.35f, 1.0f, viewAngleTransparency) *
        lerp(0.82f, 1.0f, skyVisibility) *
        (1.0f - saturate(foam));
    const float3 regularScatteringColor =
        lerp(deepColor, midWaterColor, 0.58f) * float3(0.72f, 0.96f, 1.02f) +
        skyIrradiance * 0.025f;
    float3 surfaceTint = lerp(
        shallowColor,
        midWaterColor,
        saturate(fogWaterLayer + deepWaterLayer * 0.65f));
    // 浅い視線角では反射比率が上がり、透過色だけへ入れた曲率差が消えていた。
    // Fresnelは変更せず、波頭/谷の水色差だけを弱く表面Tintにも引き継ぐ。
    const float grazingShapeVisibility =
        clamp(gMaterial.oceanGrazingShapeVisibility, 0.0f, 1.0f);
    const float grazingShapeWeight = 1.0f - viewAngleTransparency;
    surfaceTint = lerp(
        surfaceTint,
        valleyWaterColor,
        valleyColorMask * waveColorSeparation * grazingShapeVisibility *
            lerp(0.18f, 0.42f, grazingShapeWeight));
    surfaceTint = lerp(
        surfaceTint,
        crestWaterColor,
        crestColorMask * waveColorSeparation * grazingShapeVisibility *
            lerp(0.10f, 0.24f, grazingShapeWeight));
    const float surfaceTintWeight =
        lerp(0.18f, 0.055f, viewAngleTransparency) *
        (1.0f - saturate(foam) * 0.45f);
    const float skyReflectionInfluence = max(gMaterial.oceanSkyReflectionInfluence, 0.0f);
    const float upperSurfaceVisibility = isUnderwaterSurface ? 0.0f : 1.0f;
    const float underwaterBoundaryReflection = isUnderwaterSurface ? 0.10f : 1.0f;
    float3 finalColor =
        litVolume * transmissionWeight * transmissionEnergy +
        surfaceTint * surfaceTintWeight * transmissionEnergy +
        resolvedReflection * gameReflectionWeight * skyReflectionInfluence *
            underwaterBoundaryReflection +
        sunSurfaceLighting * lerp(0.48f, 1.0f, clearWaterLayer) *
            lerp(0.22f, 1.0f, upperSurfaceVisibility) * transmissionEnergy +
        crestTransmissionLight *
            lerp(shallowColor, float3(0.40f, 0.82f, 0.86f), 0.46f) *
            transmissionEnergy * upperSurfaceVisibility +
        regularScatteringColor *
            (regularScatteringAmount + preCrestScatteringAmount) *
            scatteringVisibility * upperSurfaceVisibility;

    // Foam直前の中間状態。曲率を伴う波頭と圧縮Foam直前だけを青白くし、
    // 完全な白や高さだけの帯にはしない。
    const float preFoamThreshold = max(gMaterial.oceanFoamThreshold - 0.16f, 0.0f);
    const float preFoamCompression =
        smoothstep(
            preFoamThreshold,
            min(gMaterial.oceanFoamThreshold + 0.02f, 1.0f),
            saturate(oceanData.x)) *
        (1.0f - smoothstep(
            min(gMaterial.oceanFoamThreshold + 0.06f, 1.0f),
            min(gMaterial.oceanFoamThreshold + 0.18f, 1.0f),
            saturate(oceanData.x)));
    const float crestHazeMask = saturate(max(
        crestBreakingStage * crestCurvatureMask * 0.82f,
        preFoamCompression * crestBreakingStage * 0.64f)) *
        lerp(0.90f, lerp(1.10f, 1.18f, nearSurfaceDetail), mediumStructure) *
        (1.0f - saturate(foam));
    const float3 crestHazeColor = lerp(
        shallowColor,
        float3(0.62f, 0.82f, 0.88f),
        0.34f);
    finalColor += crestHazeColor *
        crestHazeMask *
        max(gMaterial.oceanCrestHazeStrength, 0.0f) *
        (0.55f + skyIrradiance * 0.18f) *
        upperSurfaceVisibility;
    const float clearWaterWeight = 1.0f - foam;
    // Water Base/Transmission(litVolume) と Sky Reflection(resolvedReflection) はここまでで合成済み。
    // Sun Specular と Sun Glitter は反射由来の加算項として別枠で足す。水深Tintそのものは白く塗らない。
    finalColor += CompressOceanHighlight(
        directSpecular,
        9.0f) * clearWaterWeight * max(gMaterial.oceanSunSpecularInfluence, 0.0f) *
        upperSurfaceVisibility;
    finalColor += sunGlitter * clearWaterWeight *
        max(gMaterial.oceanSunGlitterInfluence, 0.0f) *
        upperSurfaceVisibility;

    const float3 foamColor =
        float3(0.82f, 0.93f, 0.97f) *
        (0.55f + skyIrradiance * 0.25f + compressedDirectBodyLight * 0.14f);
    finalColor = lerp(
        finalColor,
        foamColor,
        saturate(foam) * 0.72f);
    finalColor = ApplyAerialPerspective(
        finalColor,
        input.worldPosition,
        OCEAN_PRIMARY_LIGHT.cameraPosition,
        normalize(-OCEAN_PRIMARY_LIGHT.direction),
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyIntensity,
        OCEAN_PRIMARY_LIGHT.skyEmission,
        0.00018f);
    return float4(max(finalColor, 0.0f), 1.0f);
}
