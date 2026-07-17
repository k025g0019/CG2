#include "Common/Lighting.hlsli"
#include "Common/AdvancedPBR.hlsli"
#include "Common/PBRCommon.hlsli"
#include "Lighting/IBLRuntime.hlsli"
#include "DirectXTK/EnvironmentMapEffectBridge.hlsli"
#include "Reflection/ParallaxCorrectedCubemap.hlsli"
#include "Shadow/SoftShadow.hlsli"
#include "lygia/color/blend/softLight.hlsl"
#include "lygia/generative/snoise.hlsl"
#include "FidelityFX/CAS/ShaderLibDX/ffx_a.h"

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

Texture2D gTexture : register(t0);
Texture2D gShadowMap : register(t1);
Texture2D gNormalMap : register(t7);
Texture2D gMetallicMap : register(t8);
Texture2D gRoughnessMap : register(t9);
Texture2D gAmbientOcclusionMap : register(t10);
Texture2D gEmissionMap : register(t11);
Texture2D gHeightMap : register(t12);
Texture2D gOpacityMap : register(t13);

SamplerState gTextureSampler : register(s0);
SamplerState gShadowSampler : register(s1);

static const float kIBLPrefilterMipCount = 9.0f;

#define LIGHT_PRIMARY gDirectionalLight.lights[0]

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

float SampleShadowAtlas(float3 worldPosition, DirectionalLightData light)
{
    float4 shadowPosition = mul(float4(worldPosition, 1.0f), light.shadowVP);
    if (shadowPosition.w <= 0.000001f) return 1.0f;
    if (light.shadowEnabled < 0.5f) return 1.0f;
    float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f) return 1.0f;
    float2 atlasUv = shadowUv * float2(light.shadowTileUvScaleX, light.shadowTileUvScaleY)
        + float2(light.shadowTileUvBiasX, light.shadowTileUvBiasY);
    float receiverDepth = saturate(shadowNdc.z);
    float2 texelSize = float2(1.0f / 4096.0f, 1.0f / 4096.0f);
    return SampleSoftShadow9Tap(gShadowMap, gShadowSampler, atlasUv, receiverDepth - 0.0025f, texelSize, 1.35f);
}

float GetShadowVisibility(float3 normal, float3 lightDirection, float3 worldPosition, DirectionalLightData light)
{
    if (saturate(dot(normal, lightDirection)) <= 0.0001f) return 1.0f;
    return SampleShadowAtlas(worldPosition, light);
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
    float dielectricF0 = AdvancedPbrIorToF0(gMaterial.ior);
    float luminance = max(dot(baseColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.0001f);
    float3 baseTint = baseColor / luminance;
    float3 dielectricTint = lerp(
        float3(dielectricF0, dielectricF0, dielectricF0),
        baseTint * dielectricF0,
        saturate(gMaterial.specularTint));
    float3 f0 = lerp(dielectricTint, baseColor, metallic);
    return lerp(f0, float3(1.0f, 1.0f, 1.0f), saturate(gMaterial.reflectance));
}

float3 EvaluateClearCoat(float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance)
{
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

float ComputeProceduralMicroVariation(float3 worldPosition, float3 normal)
{
    const float noiseValue = snoise(worldPosition * 0.18f + normal * 0.35f);
    return saturate(noiseValue * 0.5f + 0.5f);
}

float4 main(PixelShaderInput input) : SV_TARGET0
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

    if (gMaterial.enableLighting == 0) {
        float3 unlitColor = material.baseColor + material.baseColor * material.emissionStrength;
        return float4(unlitColor, material.alpha);
    }

    float3 N = ApplyNormalMap(materialUv, geometricNormal, tangent, bitangent);
    float NdotV = saturate(dot(N, V));

    float3 albedo = material.baseColor;
    float metallic = material.metallic;
    float roughness = material.roughness;
    const float proceduralVariation = ComputeProceduralMicroVariation(input.worldPosition, N);
    const float fidelityPhase = proceduralVariation * A_2PI;
    const float hashVariation = frac(sin(dot(input.worldPosition, float3(12.9898f, 78.233f, 45.164f)) + fidelityPhase) * 43758.5453f);
    const float blendOpacity = saturate((1.0f - roughness) * 0.12f + material.emissionStrength * 0.03f);
    const float3 proceduralTint = lerp(
        float3(1.0f, 1.0f, 1.0f),
        float3(proceduralVariation, proceduralVariation, proceduralVariation),
        0.18f);
    albedo = blendSoftLight(albedo, proceduralTint, blendOpacity);
    roughness = clamp(roughness + (hashVariation - 0.5f) * 0.02f, 0.04f, 1.0f);
    float sampledAo = gMaterial.useAmbientOcclusionMap != 0
        ? gAmbientOcclusionMap.Sample(gTextureSampler, materialUv).r
        : 1.0f;
    float ao = lerp(1.0f, sampledAo, saturate(gMaterial.ambientOcclusionStrength));

    float3 F0 = BuildExtendedF0(albedo, metallic);

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
        float NdotL = saturate(
            (rawNdotL + saturate(gMaterial.subsurface)) /
            (1.0f + saturate(gMaterial.subsurface)));
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

    const float iblIntensity =
        LIGHT_PRIMARY.environmentTextureEnabled >= 0.5f
        ? max(LIGHT_PRIMARY.environmentTextureIntensity, 0.0f)
        : 0.0f;
    float3 ibl = EvaluateIBLWithF0(
        N,
        V,
        albedo,
        metallic,
        roughness,
        ao,
        iblIntensity,
        kIBLPrefilterMipCount,
        F0);

    float clearCoatMip =
        clamp(gMaterial.clearCoatRoughness, 0.035f, 1.0f) *
        (kIBLPrefilterMipCount - 1.0f);
    float3 clearCoatReflection = gPrefilterCube.SampleLevel(
        gIblSampler,
        reflect(-V, N),
        clearCoatMip).rgb * saturate(gMaterial.clearCoat) * iblIntensity;
    ibl += clearCoatReflection * FresnelSchlick(NdotV, float3(0.04f, 0.04f, 0.04f));

    float3 refractionDirection = refract(-V, N, 1.0f / max(gMaterial.ior, 1.0001f));
    float3 transmissionColor = gEnvironmentCube.SampleLevel(
        gIblSampler,
        refractionDirection,
        roughness * (kIBLPrefilterMipCount - 1.0f)).rgb;
    ibl = lerp(
        ibl,
        transmissionColor * albedo,
        saturate(gMaterial.transmission) * iblIntensity);

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
    // DirectXTK EnvironmentMapEffect 方式のキューブ環境反射
    //============================================================
    {
        const float smoothness = saturate(1.0f - roughness);
        const float materialReflection = max(max(gMaterial.reflectance, gMaterial.reflectionProbeIntensity), metallic);
        const float cubemapModeMask =
            step(0.5f, gMaterial.reflectionMode) *
            (1.0f - step(1.5f, gMaterial.reflectionMode));
        const float environmentMapAmount = saturate(
            cubemapModeMask *
            max(materialReflection, 1.0f) *
            smoothness *
            max(LIGHT_PRIMARY.reflectionIntensity, 1.0f));

        if (environmentMapAmount > 0.0001f)
        {
            DirectXTKEnvironmentMapInput environmentMapInput;
            environmentMapInput.litColor = finalColor;
            environmentMapInput.alpha = material.alpha;
            environmentMapInput.eyeVector = V;
            environmentMapInput.worldNormal = N;
            environmentMapInput.environmentMapAmount = environmentMapAmount;
            environmentMapInput.fresnelFactor = 5.0f;
            environmentMapInput.environmentMapSpecular = F0 * max(LIGHT_PRIMARY.environmentTextureIntensity, 1.0f) * smoothness;
            environmentMapInput.useFresnel = false;
            const float3 environmentCoordinate = DirectXTKComputeEnvironmentCoordinate(V, N);
            environmentMapInput.environmentCoordinate = CorrectParallaxCubemapDirection(
                input.worldPosition,
                environmentCoordinate,
                gMaterial.reflectionProbeCenter,
                gMaterial.reflectionProbeExtent);
            environmentMapInput.useEnvironmentCoordinate =
                gMaterial.reflectionProbeBoxProjection >= 0.5f;

            finalColor = DirectXTKApplyEnvironmentMap(
                environmentMapInput,
                gEnvironmentCube,
                gIblSampler,
                LIGHT_PRIMARY.environmentTextureEnabled);
        }
    }

    finalColor += (hashVariation - 0.5f) / 255.0f;
    return float4(max(finalColor, 0.0f), material.alpha);
}
