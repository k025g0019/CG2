#include "Common/Lighting.hlsli"
#include "Common/AdvancedPBR.hlsli"
#include "Common/PBRCommon.hlsli"
#include "Common/AnalyticAtmosphere.hlsli"
#include "Lighting/IBLRuntime.hlsli"
#include "Reflection/ParallaxCorrectedCubemap.hlsli"
#include "Shadow/SoftShadow.hlsli"
#include "Shadow/ShadowSampling.hlsli"
#include "Common/SceneLightData.hlsli"
#include "GI/ProbeSampling.hlsli"
#include "Water/OceanSurface.hlsli"

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
    float materialThickness;
    float materialWetness;
    float materialWaterlineHeight;
    float2 uvTiling;
    float2 uvOffset;
    float oceanEnabled;
    float oceanFoamStrength;
    float oceanRoughness;
    float oceanColorBlendScale;
    float3 oceanDeepColor;
    float oceanPerPixelDisplacementStrength;
    float oceanDetailNormalStrength;
    float oceanFoamThreshold;
    float oceanAbsorptionDistance;
    float oceanRefractionDistortion;
    float oceanWaterDepth;
    float oceanCrestSharpness;
    float oceanPerPixelDisplacementSteps;
    float oceanPerPixelDisplacementDistance;
    int surfaceMode;
    float materialWaterlineWidth;
    float surfaceMaterialPadding1;
    float surfaceMaterialPadding2;
};

#include "Common/SceneLightData.hlsli"

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

bool IsOceanSurfacePass()
{
#if defined(CG2_OCEAN_SURFACE_PASS)
    return true;
#else
    return gMaterial.oceanEnabled >= 0.5f;
#endif
}

// EmissiveLightData / SunPortalLightData / EmissiveLightArray は
// Common/SceneLightData.hlsli で共有する(C++の構造体と1対1で保つため)。
ConstantBuffer<EmissiveLightArray> gEmissiveLights : register(b2);

// Light Probe GI。Root Signature の 64 DWORD 上限の都合で、
// この2つは1つのDescriptor Tableへまとめて束縛している。
StructuredBuffer<float4> gProbeShBuffer : register(t20);
Texture2D<float2> gProbeVisibilityAtlas : register(t21);

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

// 不透明材質の直接光は面の向きで決める。直角・背面には直接光を足さない。
// 回り込みは材質が指定したSubsurface（水面・草を含む）だけに限定する。
float EvaluateDiffuseCosine(float normalDotLight, float subsurfaceWrap)
{
    return saturate((normalDotLight + subsurfaceWrap) / (1.0f + subsurfaceWrap));
}

// 影のサンプリング本体は Shadow/ShadowSampling.hlsli で共有する。
// ここは既存の呼び出し名を保つための薄いラッパー。
float SampleShadowAtlas(float3 worldPosition, float normalDotLight, DirectionalLightData light)
{
    return SampleShadowAtlasForLight(
        gShadowMap,
        gShadowSampler,
        worldPosition,
        normalDotLight,
        light);
}

float GetShadowVisibility(float3 normal, float3 lightDirection, float3 worldPosition, DirectionalLightData light)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));
    if (normalDotLight <= 0.0001f) return 1.0f;
    return SampleShadowAtlas(worldPosition, normalDotLight, light);
}

//============================================================
// Sun Portal
//============================================================
// 窓や開口部にSun Portalを置くと、その面を簡易的なArea Lightとして扱い、
// 室内側だけへSunの光を足す。フルGIではなく、以下だけの軽量な近似:
//   ・光の向きはPortalの外向き法線の逆で固定(放射状の計算はしない)
//   ・遮蔽は「Sun→Portal自身」だけ既存Cascaded Shadowを再利用して見る。
//     「Sun→対象ピクセル」の遮蔽(=壁や天井の影)は無関係なので使わない。
//     窓の先の壁で光が止まる「Portal→対象ピクセル」の遮蔽は、この
//     軽量版では判定しない(隣室まで多少漏れる可能性は割り切り事項)。
//   ・室内へ入るほど照らす範囲が広がる、という奥行き方向の減衰だけを持つ
float3 EvaluateSunPortalLight(
    float3 worldPosition,
    float3 normal,
    DirectionalLightData sunLight)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);

    // Sun強度が0(夜・無効化)なら、Portalも一緒に無効。
    if (gEmissiveLights.sunPortalCount <= 0 || sunLight.intensity <= 0.0001f)
    {
        return result;
    }

    const float3 sunTravelDirection = normalize(sunLight.direction);
    const float3 towardSun = -sunTravelDirection;

    for (int portalIndex = 0; portalIndex < gEmissiveLights.sunPortalCount; portalIndex++)
    {
        SunPortalLightData portal = gEmissiveLights.sunPortals[portalIndex];

        // SunがPortalの裏側にある(窓の外側から光が来ていない)場合は無効。
        const float sunFacing = dot(towardSun, portal.outwardNormal);
        if (sunFacing <= 0.0f)
        {
            continue;
        }

        const float3 toPixel = worldPosition - portal.position;
        // 外向き法線の逆＝室内方向への深さ。0以下や範囲外は無効。
        const float depth = dot(toPixel, -portal.outwardNormal);
        if (depth <= 0.0f || depth >= portal.range)
        {
            continue;
        }

        const float rightOffset = dot(toPixel, portal.right);
        const float upOffset = dot(toPixel, portal.up);
        // 窓の外形をそのまま延長すると室内が真っ暗な角柱になるため、
        // 奥へ進むほど広がる簡易的な円錐台で近似する。
        const float spread = 1.0f + depth * max(portal.spreadRate, 0.0f);
        const float extentX = max(portal.halfWidth * spread, 0.001f);
        const float extentY = max(portal.halfHeight * spread, 0.001f);
        const float edgeX = 1.0f - smoothstep(extentX * 0.6f, extentX, abs(rightOffset));
        const float edgeY = 1.0f - smoothstep(extentY * 0.6f, extentY, abs(upOffset));

        if (edgeX <= 0.0f || edgeY <= 0.0f)
        {
            continue;
        }

        const float rangeFalloff = 1.0f - smoothstep(portal.range * 0.6f, portal.range, depth);
        // 光は「窓から室内向き」の一定方向で流れると単純化し、
        // 本物のArea Lightのような放射状の方向計算を省く。
        const float3 portalLightDirection = -portal.outwardNormal;
        const float portalNdotL = saturate(dot(normal, portalLightDirection));

        const float coverage = edgeX * edgeY * rangeFalloff * sunFacing * portalNdotL;

        if (coverage <= 0.0f)
        {
            continue;
        }

        // ここで見るのは「Sun→対象ピクセル」ではなく「Sun→Portal自身」の遮蔽。
        // 対象ピクセルが壁や天井の影に入っていても、窓自体に日が当たっていれば
        // Portal光は通す。既存のCascaded Shadowをそのまま再利用するだけで、
        // 追加のShadow Map/追加パスは持たない。
        const float sunToPortalVisibility = GetShadowVisibility(
            portal.outwardNormal,
            towardSun,
            portal.position,
            sunLight);

        if (sunToPortalVisibility <= 0.0f)
        {
            continue;
        }

        result +=
            sunLight.color.rgb *
            sunLight.intensity *
            max(portal.tint, 0.0f) *
            max(portal.intensityScale, 0.0f) *
            coverage *
            sunToPortalVisibility;
    }

    return result;
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

float DistributionGGXAnisotropic(
    float3 normal,
    float3 halfVector,
    float3 tangent,
    float3 bitangent,
    float roughness,
    float anisotropy)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float aspect = sqrt(max(1.0f - 0.9f * abs(anisotropy), 0.1f));
    const float alphaTangent = anisotropy >= 0.0f ? alpha / aspect : alpha * aspect;
    const float alphaBitangent = anisotropy >= 0.0f ? alpha * aspect : alpha / aspect;
    const float tangentDotHalf = dot(tangent, halfVector) / alphaTangent;
    const float bitangentDotHalf = dot(bitangent, halfVector) / alphaBitangent;
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float denominatorBase =
        tangentDotHalf * tangentDotHalf +
        bitangentDotHalf * bitangentDotHalf +
        normalDotHalf * normalDotHalf;
    return rcp(max(PI * alphaTangent * alphaBitangent * denominatorBase * denominatorBase, 0.00001f));
}

float GeometryAnisotropicDirection(
    float3 normal,
    float3 direction,
    float3 tangent,
    float3 bitangent,
    float roughness,
    float anisotropy)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float aspect = sqrt(max(1.0f - 0.9f * abs(anisotropy), 0.1f));
    const float alphaTangent = anisotropy >= 0.0f ? alpha / aspect : alpha * aspect;
    const float alphaBitangent = anisotropy >= 0.0f ? alpha * aspect : alpha / aspect;
    const float normalDotDirection = saturate(dot(normal, direction));
    const float tangentTerm = dot(tangent, direction) * alphaTangent;
    const float bitangentTerm = dot(bitangent, direction) * alphaBitangent;
    const float projectedLength = sqrt(
        tangentTerm * tangentTerm +
        bitangentTerm * bitangentTerm +
        normalDotDirection * normalDotDirection);
    return 2.0f * normalDotDirection / max(normalDotDirection + projectedLength, 0.00001f);
}

float GeometrySmithAnisotropic(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 tangent,
    float3 bitangent,
    float roughness,
    float anisotropy)
{
    return GeometryAnisotropicDirection(
        normal,
        viewDirection,
        tangent,
        bitangent,
        roughness,
        anisotropy) *
        GeometryAnisotropicDirection(
            normal,
            lightDirection,
            tangent,
            bitangent,
            roughness,
            anisotropy);
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
    const float dielectricF0 = IsOceanSurfacePass()
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
            (IsOceanSurfacePass() ? 1.0f : dielectricSpecularScale)),
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
    if (IsOceanSurfacePass())
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

float ComputeOceanShallowWeight(
    float normalDotView,
    float normalizedWaveHeight,
    float waterThickness)
{
    return EvaluateOceanShallowWeight(
        waterThickness,
        normalizedWaveHeight,
        gMaterial.oceanAbsorptionDistance,
        gMaterial.oceanColorBlendScale);
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
    OceanSurfaceFrame oceanSurfaceFrame;
    float4 oceanSurfaceData = input.oceanData;

    if (IsOceanSurfacePass())
    {
        float3 oceanFftNormal =
            gMaterial.doubleSided != 0 && !input.isFrontFace ? -input.normal : input.normal;
        ResolveOceanPixelSurface(
            input.oceanSamplingData,
            input.oceanWorldAxisX,
            input.oceanWorldAxisY,
            input.oceanWorldAxisZ,
            oceanFftNormal,
            oceanSurfaceData);
        oceanSurfaceFrame = EvaluateOceanSurfaceFrame(
            oceanFftNormal,
            input.worldPosition,
            oceanSurfaceData,
            gMaterial.oceanDetailNormalStrength,
            gMaterial.oceanCrestSharpness);
        N = oceanSurfaceFrame.shadingNormal;
    }

    float NdotV = saturate(dot(N, V));

    float3 albedo = material.baseColor;
    float metallic = material.metallic;
    float roughness = material.roughness;
    float surfaceTransmission = saturate(gMaterial.transmission);
    float oceanFoam = 0.0f;
    float oceanWaterThickness = max(gMaterial.oceanWaterDepth, 0.1f);
    float3 oceanRefractedColor = float3(0.0f, 0.0f, 0.0f);

    if (!IsOceanSurfacePass() && gMaterial.materialWetness > 0.0001f)
    {
        const float waterlineWidth = max(gMaterial.materialWaterlineWidth, 0.001f);
        const float waterlineMask = 1.0f - smoothstep(
            gMaterial.materialWaterlineHeight,
            gMaterial.materialWaterlineHeight + waterlineWidth,
            input.worldPosition.y);
        const float wetness = saturate(gMaterial.materialWetness * waterlineMask);
        albedo *= lerp(1.0f, 0.62f, wetness);
        roughness = lerp(roughness, min(roughness, 0.12f), wetness);
    }

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

    if (IsOceanSurfacePass())
    {
        const float3 waterOpticalNormal = EvaluateOceanOpticalNormal(oceanSurfaceFrame);
        const float waterNormalDotView = saturate(dot(waterOpticalNormal, V));
        const OceanSceneSample oceanSceneSample = SampleOceanScene(input, waterOpticalNormal);
        oceanWaterThickness = oceanSceneSample.waterThickness;
        oceanRefractedColor = oceanSceneSample.refractedColor;
        const float shallowWeight = ComputeOceanShallowWeight(
            waterNormalDotView,
            oceanSurfaceData.w,
            oceanWaterThickness);
        oceanFoam = max(
            EvaluateOceanFoam(
                input.worldPosition,
                oceanSurfaceData,
                oceanSurfaceFrame,
                gMaterial.oceanFoamStrength,
                gMaterial.oceanFoamThreshold,
                gMaterial.oceanCrestSharpness),
            oceanSceneSample.shoreFoam);
        const float oceanCrest = smoothstep(0.38f, 0.92f, saturate(oceanSurfaceData.w));
        albedo = EvaluateOceanWaterColor(
            gMaterial.oceanDeepColor,
            material.baseColor,
            shallowWeight,
            oceanFoam,
            oceanSurfaceFrame,
            oceanSurfaceData.w,
            waterNormalDotView);
        metallic = 0.0f;
        roughness = EvaluateOceanRoughness(
            clamp(gMaterial.oceanRoughness, 0.055f, 1.0f),
            N,
            oceanSurfaceFrame.interpolationVariance,
            oceanCrest,
            oceanFoam);
        surfaceTransmission *=
            (1.0f - oceanFoam) *
            lerp(0.55f, 1.0f, shallowWeight);
    }

    const float3 opticalNormal = IsOceanSurfacePass()
        ? EvaluateOceanOpticalNormal(oceanSurfaceFrame)
        : N;
    const float opticalNdotV = saturate(dot(opticalNormal, V));

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
            float diffuseFactor = EvaluateDiffuseCosine(rawNdotL, 0.0f);

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

    if (IsOceanSurfacePass())
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
        float rawNdotL = dot(opticalNormal, L);
        // 直接光はLambertを基準にする(NdotL<0で確実に0)。側面・裏の柔らかさは
        // ambient/IBL側の仕事なので、直接光にwrapで底上げするのはSubsurfaceを
        // 明示指定した材質(肌・葉など)だけに限定する。海面だけは既存の見た目を
        // 保つため最低限のwrapを残す。
        const float effectiveSubsurface = IsOceanSurfacePass()
            ? max(saturate(gMaterial.subsurface), 0.18f)
            : saturate(gMaterial.subsurface);
        const float NdotL = EvaluateDiffuseCosine(rawNdotL, effectiveSubsurface);

        if (NdotL > 0.0f)
        {
            // 拡散光と鏡面反射の両方に、同じ遮蔽を適用する。
            const float localShadow = GetShadowVisibility(opticalNormal, L, input.worldPosition, light);
            float3 H = normalize(V + L);
            const float anisotropy = clamp(gMaterial.anisotropy, -0.98f, 0.98f);
            float D = abs(anisotropy) > 0.001f
                ? DistributionGGXAnisotropic(
                    opticalNormal,
                    H,
                    tangent,
                    bitangent,
                    roughness,
                    anisotropy)
                : DistributionGGX(opticalNormal, H, roughness);
            float G = abs(anisotropy) > 0.001f
                ? GeometrySmithAnisotropic(
                    opticalNormal,
                    V,
                    L,
                    tangent,
                    bitangent,
                    roughness,
                    anisotropy)
                : GeometrySmith(opticalNormal, V, L, roughness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
            float3 kS = F;
            float3 kD = (1.0f - kS) * (1.0f - metallic);
            float3 numerator = D * G * F;
            float denominator = max(4.0f * opticalNdotV * NdotL, 0.0001f);
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
            direct += EvaluateClearCoat(opticalNormal, V, L, radiance) * localShadow;

            if (!IsOceanSurfacePass() && effectiveSubsurface > 0.0001f)
            {
                const float backLight = pow(saturate(dot(-opticalNormal, L)), 1.5f);
                const float thickness = max(gMaterial.materialThickness, 0.001f);
                const float3 absorption = max(1.0f - albedo, 0.04f);
                const float3 transmittedLight = exp(-absorption * thickness * 3.0f);
                direct +=
                    transmittedLight *
                    albedo *
                    radiance *
                    backLight *
                    effectiveSubsurface *
                    localShadow;
            }

            if (IsOceanSurfacePass())
            {
                const float macroNormalDotHalf = saturate(dot(oceanSurfaceFrame.macroNormal, H));
                const float opticalNormalDotHalf = saturate(dot(opticalNormal, H));
                const float macroNormalDotLight = dot(oceanSurfaceFrame.macroNormal, L);
                const float wrappedOceanLight = saturate(macroNormalDotLight * 0.58f + 0.42f);
                const float horizon = 1.0f - opticalNdotV;
                const float macroSlope = oceanSurfaceFrame.slope;
                const float crest = oceanSurfaceFrame.crest;
                const float wideHighlight =
                    pow(macroNormalDotHalf, 22.0f) *
                    (0.08f + horizon * 0.38f + macroSlope * 0.16f + crest * 0.12f);
                const float sharpHighlight =
                    pow(opticalNormalDotHalf, 88.0f) *
                    (0.08f + horizon * 0.48f + macroSlope * 0.20f);
                direct +=
                    radiance *
                    (wideHighlight * 0.075f + sharpHighlight * 0.14f) *
                    (1.0f - oceanFoam) *
                    localShadow;

                // 水の体積色へ直接光を通す。環境反射とは別に評価するため、
                // Sun / Point / Spot の方向と減衰が波面の高低へ明確に現れる。
                const float bodyLight =
                    wrappedOceanLight *
                    (0.24f + crest * 0.12f) *
                    (1.0f - oceanFoam * 0.45f);
                direct += albedo * radiance * bodyLight * localShadow;
            }
        }

        if (!IsOceanSurfacePass() && light.lightType == 0)
        {
            // Sun Portalの光は、Sunそのものへ向くNdotL(rawNdotL)ではなく
            // Portal自身の向きに対するNdotLで別途評価するため、NdotL>0の
            // 分岐の外(=ここ)で加算する。壁の陰で直接Sunが見えない床でも、
            // 窓に日が当たっていれば正しく明るくなる。
            direct += albedo * EvaluateSunPortalLight(
                input.worldPosition,
                opticalNormal,
                light);
        }

        // 光の筋(God Ray)は PostProcess/VolumetricLightShaft.PS.hlsl の専用パスが
        // 深度バッファ全体をレイマーチして描く。ここで同じ値を面ごとに加算すると
        // 二重に明るくなるため、表面シェーダー側では加算しない。
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

    // ambient/IBLの拡散成分だけに、Sunの遮蔽をゆるく反映する。囲まれた室内は
    // ちゃんと暗くなり、Sun Beamsのような「暗い所から明るい所へ」の差が出る。
    // 完全な0にはしない: Sunそのものは遮られていても、別方向からの空光や
    // バウンス光は物理的に残るはずなので、下限を残した緩やかな減衰にする。
    // 法線に依存せず「その位置がSunから見えるか」だけを見るため、
    // normalDotLightには常に1.0を渡してバイアス計算をスキップする。
    const float sunOcclusionForAmbient = LIGHT_PRIMARY.shadowEnabled > 0.5f
        ? SampleShadowAtlas(input.worldPosition, 1.0f, LIGHT_PRIMARY)
        : 1.0f;
    const float ambientShadowFactor = lerp(0.15f, 1.0f, sunOcclusionForAmbient);

    const float cubemapModeMask =
        step(0.5f, gMaterial.reflectionMode) *
        (1.0f - step(1.5f, gMaterial.reflectionMode));
    const float environmentReflectionIntensity = max(
        LIGHT_PRIMARY.reflectionIntensity,
        0.0f);
    const float3 environmentFresnel = FresnelSchlickRoughness(
        opticalNdotV,
        F0,
        roughness);
    const float3 environmentDiffuseWeight =
        (1.0f - environmentFresnel) * (1.0f - metallic);
    const float3 generatedIrradiance = gIrradianceCube.Sample(
        gIblSampler,
        opticalNormal).rgb;
    const float3 skyIrradiance = lerp(
        generatedIrradiance,
        SampleRuntimeEnvironment(opticalNormal, 5.0f),
        saturate(LIGHT_PRIMARY.environmentTextureEnabled));

    // Light Probe が焼けている場所では、空由来の一様な拡散環境光を
    // Probeの間接光で置き換える。Probeは空の見え方も遮蔽も含んでいるので、
    // 室内が暗くなることも、壁のバウンスで色が付くことも同時に成立する。
    // Probeが無い(または範囲外の)場所は従来どおり空＋Sun遮蔽近似へ戻す。
    bool hasProbeIrradiance = false;
    const float3 probeIrradiance = SampleLightProbeGi(
        gProbeShBuffer,
        gProbeVisibilityAtlas,
        gEmissiveLights.probeGrid,
        input.worldPosition,
        opticalNormal,
        -V,
        hasProbeIrradiance);
    const float3 indirectIrradiance = hasProbeIrradiance
        ? probeIrradiance
        : skyIrradiance * ambientShadowFactor;
    const float3 diffuseEnvironment = indirectIrradiance * albedo;
    const float3 reflectionDirection = normalize(reflect(-V, opticalNormal));
    const float reflectionMip =
        roughness * (kIBLPrefilterMipCount - 1.0f);
    const float3 reflectedEnvironment = SampleRuntimeEnvironment(
        reflectionDirection,
        reflectionMip);
    const float2 environmentBrdf = SampleEnvironmentBrdf(opticalNdotV, roughness);
    const float3 environmentSpecular =
        reflectedEnvironment *
        (environmentFresnel * environmentBrdf.x + environmentBrdf.y) *
        environmentReflectionIntensity *
        (IsOceanSurfacePass()
            ? max(gMaterial.reflectance, 0.0f)
            : 1.0f);
    float3 ibl =
        (environmentDiffuseWeight * diffuseEnvironment + environmentSpecular) *
        ao;

    const float effectiveClearCoat = IsOceanSurfacePass()
        ? 0.0f
        : saturate(gMaterial.clearCoat);
    float clearCoatMip =
        clamp(gMaterial.clearCoatRoughness, 0.035f, 1.0f) *
        (kIBLPrefilterMipCount - 1.0f);
    float3 clearCoatReflection = SampleRuntimeEnvironment(
        reflect(-V, opticalNormal),
        clearCoatMip) *
        effectiveClearCoat *
        environmentReflectionIntensity;
    ibl += clearCoatReflection * FresnelSchlick(opticalNdotV, float3(0.04f, 0.04f, 0.04f));

    float3 refractionDirection = refract(-V, N, 1.0f / max(gMaterial.ior, 1.0001f));
    float3 transmissionColor = SampleRuntimeEnvironment(
        refractionDirection,
        roughness * (kIBLPrefilterMipCount - 1.0f));
    float transmissionAmount = surfaceTransmission;

    if (IsOceanSurfacePass())
    {
        transmissionColor = oceanRefractedColor;
        transmissionAmount *= 1.0f - FresnelSchlick(opticalNdotV, F0).r;
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

    const float3 materialTransmissionAbsorption = exp(
        -max(1.0f - albedo, 0.04f) *
        max(gMaterial.materialThickness, 0.001f) *
        2.0f);
    const float3 transmittedRadiance = IsOceanSurfacePass()
        ? transmissionColor
        : transmissionColor * albedo * materialTransmissionAbsorption;
    ibl = lerp(
        ibl,
        transmittedRadiance,
        saturate(transmissionAmount));

    float3 ambient =
        albedo *
        max(LIGHT_PRIMARY.ambientIntensity, 0.0f) *
        (1.0f - metallic) *
        ambientShadowFactor;

    float3 emissionMap = gMaterial.useEmissionMap != 0
        ? gEmissionMap.Sample(gTextureSampler, materialUv).rgb
        : float3(1.0f, 1.0f, 1.0f);
    float3 emission =
        emissionMap *
        max(gMaterial.emissionColor, 0.0f) *
        material.emissionStrength;

    float3 finalColor = direct + ibl + ambient + emission;

    if (IsOceanSurfacePass())
    {
        // 最終反射合成後も、FFTの連続波高に対応した光路長差を残す。
        // テクスチャやノイズではなく低周波の波高だけなので、汚れを増やさず谷を深く見せる。
        const float normalizedHeight =
            clamp(oceanSurfaceData.w, -1.0f, 1.0f) * 0.5f + 0.5f;
        const float opticalHeight = smoothstep(0.02f, 0.98f, normalizedHeight);
        const float heightTransmission = lerp(0.70f, 1.16f, opticalHeight);
        finalColor *= heightTransmission;
    }

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
            const float3 environmentCoordinate = normalize(reflect(-V, opticalNormal));
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
                opticalNdotV,
                roughness);
            const float2 correctedReflectionBrdf = SampleEnvironmentBrdf(
                opticalNdotV,
                probeRoughness);
            const float3 originalReflectionResponse =
                FresnelSchlickRoughness(opticalNdotV, F0, roughness) * originalReflectionBrdf.x +
                originalReflectionBrdf.y;
            const float3 correctedReflectionResponse =
                FresnelSchlickRoughness(opticalNdotV, F0, probeRoughness) * correctedReflectionBrdf.x +
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

    const float aerialPerspectiveDensity = IsOceanSurfacePass()
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

    const float outputAlpha = IsOceanSurfacePass() || isDedicatedRefraction
        ? 1.0f
        : material.alpha;
    return BuildObjectPixelOutput(float4(max(finalColor, 0.0f), outputAlpha), input.position.z);
}
