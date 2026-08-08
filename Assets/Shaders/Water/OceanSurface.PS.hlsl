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
    float4 atlasTransform)
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
        1.35f,
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
            atlasTransform);
    }

    const float cameraDistance = length(worldPosition - light.cameraPosition);
    uint cascadeIndex = 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
    cascadeIndex = min(cascadeIndex, 3u);

    const float currentShadow = SampleOceanShadowProjection(
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
        light.shadowCascadeAtlas[cascadeIndex + 1u]);
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
        viewPosition.z * gWaterView.projectionWScale;

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
    const float refractionDistanceFade = 1.0f - smoothstep(
        oceanDomainLength * 0.06f,
        oceanDomainLength * 0.34f,
        cameraDistance);
    const float2 refractionOffset = clamp(
        (projectedNormalUv - projectedSurfaceUv) *
            max(gMaterial.oceanRefractionDistortion, 0.0f) *
            refractionDistanceFade,
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

float3 EvaluateOceanSunGlitter(
    float3 macroNormal,
    float3 opticalNormal,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance,
    float roughness,
    float fresnelBase,
    float slope,
    float shadowVisibility)
{
    const float3 reflectedSunDirection = normalize(reflect(
        -lightDirection,
        opticalNormal));
    const float reflectionAlignment = saturate(dot(
        reflectedSunDirection,
        viewDirection));
    const float roughnessWeight = saturate(
        (roughness - 0.055f) / 0.32f);
    const float coreExponent = lerp(
        720.0f,
        72.0f,
        roughnessWeight);
    const float glitterCore = pow(
        max(reflectionAlignment, 0.0001f),
        coreExponent);
    const float glitterHalo = pow(
        max(reflectionAlignment, 0.0001f),
        coreExponent * 0.16f);
    const float normalDotLight = saturate(dot(macroNormal, lightDirection));
    const float normalDotView = saturate(dot(opticalNormal, viewDirection));
    const float surfaceVisibility =
        smoothstep(0.015f, 0.18f, normalDotLight) *
        smoothstep(0.01f, 0.22f, normalDotView);
    const float glitterCoverage = lerp(
        0.72f,
        1.26f,
        saturate(slope));
    const float3 fresnel = OceanFresnelSchlick(
        normalDotView,
        fresnelBase);
    const float3 glitterRadiance =
        radiance *
        fresnel *
        (glitterCore * 2.2f + glitterHalo * 0.34f) *
        surfaceVisibility *
        glitterCoverage *
        shadowVisibility;

    // HDR値はBloomへ渡すが、自動露出を破壊する単一画素ピークにはしない。
    return CompressOceanHighlight(glitterRadiance, 7.5f);
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

        const float blurRadius =
            roughness * roughness * 4.0f;
        const float2 blurOffset = texelSize * blurRadius;
        float3 reflectedColor =
            gWaterSceneColor.SampleLevel(gSceneSampler, rayScreenUv, 0.0f).rgb * 0.40f;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(blurOffset.x, 0.0f)),
            0.0f).rgb * 0.15f;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(blurOffset.x, 0.0f)),
            0.0f).rgb * 0.15f;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(0.0f, blurOffset.y)),
            0.0f).rgb * 0.15f;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(0.0f, blurOffset.y)),
            0.0f).rgb * 0.15f;

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
    const float3 interpolatedMacroNormal =
        gMaterial.doubleSided != 0 && !input.isFrontFace
            ? -input.normal
            : input.normal;
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
        interpolatedMacroNormal,
        input.worldPosition,
        oceanData,
        0.0f,
        gMaterial.oceanCrestSharpness);
    const float3 macroNormal = macroSurfaceFrame.macroNormal;
    const float crestResponse =
        macroSurfaceFrame.crest *
        saturate(macroSurfaceFrame.slope * 0.72f + macroSurfaceFrame.curvature * 0.38f);
    const float3 opticalNormal = ResolveOceanLayeredOpticalNormal(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        resolvedFftNormal,
        gMaterial.oceanDetailNormalStrength,
        crestResponse);
    const float3 rippleRefractionOffset =
        opticalNormal - normalize(resolvedFftNormal);
    const float3 viewDirection = normalize(
        OCEAN_PRIMARY_LIGHT.cameraPosition - input.worldPosition);
    const float normalDotView = saturate(dot(opticalNormal, viewDirection));
    const OceanSceneSample sceneSample = SampleOceanOpaqueScene(
        input,
        rippleRefractionOffset);
    const float foam = max(
        EvaluateOceanFoam(
            input.worldPosition,
            oceanData,
            macroSurfaceFrame,
            gMaterial.oceanFoamStrength,
            gMaterial.oceanFoamThreshold,
            gMaterial.oceanCrestSharpness),
        sceneSample.shoreFoam);

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
        macroSurfaceFrame.curvature * 0.32f +
        crestResponse * 0.20f);
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

    float3 directBodyLight = float3(0.0f, 0.0f, 0.0f);
    float3 reliefBodyLight = float3(0.0f, 0.0f, 0.0f);
    float3 sunVolumeLight = float3(0.0f, 0.0f, 0.0f);
    float3 directSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 sunGlitter = float3(0.0f, 0.0f, 0.0f);
    const float baseRoughness = clamp(
        gMaterial.oceanRoughness * lerp(1.0f, 0.91f, crestResponse),
        0.055f,
        1.0f);
    const float normalVariance = max(
        dot(ddx(opticalNormal), ddx(opticalNormal)),
        dot(ddy(opticalNormal), ddy(opticalNormal)));
    const float filteredRoughness = clamp(
        sqrt(baseRoughness * baseRoughness + min(normalVariance * 0.22f, 0.30f)),
        0.075f,
        1.0f);
    const float waterIor = max(gMaterial.ior, 1.0001f);
    const float fresnelRoot = (waterIor - 1.0f) / (waterIor + 1.0f);
    const float fresnelBase = fresnelRoot * fresnelRoot;

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
        const float wrappedLight = saturate(normalDotLight * 0.72f + 0.28f);
        const float forwardScatter =
            pow(saturate(dot(-viewDirection, lightDirection)), 4.0f) *
            (0.18f + macroSurfaceFrame.slope * 0.22f);
        directBodyLight += radiance *
            (wrappedLight + forwardScatter) *
            softShadowVisibility;

        // 海の高低差はライト強度ではなく法線方向の変化で読ませる。
        // SUNを強くして船を白飛びさせないため、Ocean専用に強度圧縮した陰影だけを足す。
        const float radianceLuminance = max(
            dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)),
            0.0001f);
        const float3 normalizedRadianceColor = radiance / radianceLuminance;
        const float reliefIntensity = 0.42f + 0.36f * saturate(radianceLuminance);
        const float reliefWrap = saturate(normalDotLight * 0.92f + 0.08f);
        const float troughOcclusion = smoothstep(
            0.08f,
            0.55f,
            1.0f - macroNormal.y);
        reliefBodyLight += normalizedRadianceColor *
            reliefIntensity *
            (reliefWrap * 0.86f + troughOcclusion * 0.32f) *
            softShadowVisibility;

        if (light.lightType == 0)
        {
            // SUNを船や通常モデルと同じ露出で受けると白飛びしやすい。
            // ただし完全に分離すると海だけ暗く残るため、水中へ入る方向光だけを圧縮して戻す。
            const float sunLift = saturate(log2(1.0f + radianceLuminance) * 0.34f);
            const float directionalVolume = saturate(normalDotLight * 0.55f + 0.45f);
            sunVolumeLight += normalizedRadianceColor *
                sunLift *
                directionalVolume *
                (0.72f + macroSurfaceFrame.slope * 0.24f) *
                softShadowVisibility;
        }

        const float3 halfDirection = normalize(viewDirection + lightDirection);
        const float normalDotLightOptical = saturate(dot(
            opticalNormal,
            lightDirection));
        const float normalDotHalf = saturate(dot(opticalNormal, halfDirection));
        const float viewDotHalf = saturate(dot(viewDirection, halfDirection));

        if (normalDotLightOptical > 0.0f && normalDotView > 0.0f)
        {
            const float distribution = OceanDistributionGgx(
                normalDotHalf,
                filteredRoughness);
            const float geometry = OceanGeometrySmithCorrelated(
                normalDotView,
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
            directSpecular += radiance *
                finiteSpecularResponse * directFresnel *
                softShadowVisibility;
        }

        if (light.lightType == 0)
        {
            sunGlitter += EvaluateOceanSunGlitter(
                macroNormal,
                opticalNormal,
                viewDirection,
                lightDirection,
                radiance,
                filteredRoughness,
                fresnelBase,
                macroSurfaceFrame.slope,
                softShadowVisibility);
        }
    }

    const float skyBlend = saturate(macroNormal.y * 0.5f + 0.5f);
    const float3 skyIrradiance = lerp(
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        skyBlend) *
        max(OCEAN_PRIMARY_LIGHT.ambientIntensity, 0.0f);
    const float3 compressedDirectBodyLight =
        directBodyLight / (1.0f + directBodyLight * 0.42f);
    const float3 compressedReliefBodyLight =
        reliefBodyLight / (1.0f + reliefBodyLight * 0.18f);
    const float3 compressedSunVolumeLight =
        sunVolumeLight / (1.0f + sunVolumeLight * 0.26f);
    const float3 bodyLighting =
        float3(0.30f, 0.30f, 0.30f) +
        skyIrradiance * 0.34f +
        compressedDirectBodyLight * 0.20f +
        compressedReliefBodyLight * 0.34f +
        compressedSunVolumeLight * 0.26f;
    const float3 litVolume = refractedColor * bodyLighting;

    const float fresnel = fresnelBase +
        (1.0f - fresnelBase) * pow(1.0f - normalDotView, 5.0f);
    const float3 reflectionDirection = reflect(-viewDirection, opticalNormal);
    const float3 environmentReflection = SampleOceanEnvironment(
        reflectionDirection,
        filteredRoughness) *
        (0.30f + max(OCEAN_PRIMARY_LIGHT.reflectionIntensity, 0.0f) * 0.70f);
    const float reflectionWeight = saturate(
        fresnel * max(gMaterial.reflectance, 0.0f));
    const OceanScreenReflection screenReflection = TraceOceanScreenReflection(
        input.worldPosition,
        opticalNormal,
        viewDirection,
        filteredRoughness,
        reflectionWeight);
    const float3 resolvedReflection = lerp(
        environmentReflection,
        screenReflection.color,
        screenReflection.confidence);
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
    const float3 surfaceTint = lerp(
        shallowColor,
        midWaterColor,
        saturate(fogWaterLayer + deepWaterLayer * 0.65f));
    const float surfaceTintWeight =
        lerp(0.18f, 0.055f, viewAngleTransparency) *
        (1.0f - saturate(foam) * 0.45f);
    float3 finalColor =
        litVolume * transmissionWeight +
        surfaceTint * surfaceTintWeight +
        resolvedReflection * gameReflectionWeight;
    const float clearWaterWeight = 1.0f - foam;
    finalColor += CompressOceanHighlight(
        directSpecular,
        9.0f) * clearWaterWeight;
    finalColor += sunGlitter * clearWaterWeight;

    const float3 foamColor =
        float3(0.82f, 0.93f, 0.97f) *
        (0.55f + skyIrradiance * 0.25f + compressedReliefBodyLight * 0.16f);
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
