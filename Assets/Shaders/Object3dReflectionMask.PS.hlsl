#include "Common/Lighting.hlsli"

#include "Reflection/ReflectionCommon.hlsli"

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
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D gTexture : register(t0);
Texture2D gMetallicMap : register(t8);
Texture2D gRoughnessMap : register(t9);
Texture2D gOpacityMap : register(t13);
SamplerState gTextureSampler : register(s0);

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 tiledUv = input.texcoord * gMaterial.uvTiling + gMaterial.uvOffset;
    const float2 materialUv = mul(
        float4(tiledUv, 0.0f, 1.0f),
        gMaterial.uvTransform).xy;
    const float4 textureColor = gMaterial.useTexture != 0
        ? gTexture.Sample(gTextureSampler, materialUv)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float materialAlpha = textureColor.a * gMaterial.color.a;

    if (gMaterial.useOpacityMap != 0)
    {
        materialAlpha *= gOpacityMap.Sample(gTextureSampler, materialUv).r;
    }

    if (gMaterial.alphaMode == 1)
    {
        clip(materialAlpha - saturate(gMaterial.alphaCutoff));
    }

    const bool isOceanSurface = gMaterial.oceanEnabled >= 0.5f;
    const float metallic = gMaterial.useMetallicMap != 0
        ? saturate(gMetallicMap.Sample(gTextureSampler, materialUv).r)
        : saturate(gMaterial.metallic);
    const float roughness = gMaterial.useRoughnessMap != 0
        ? saturate(gRoughnessMap.Sample(gTextureSampler, materialUv).r)
        : saturate(gMaterial.roughness);
    const float iorF0 = ReflectionIorToF0(gMaterial.ior);
    const float dielectricF0 = isOceanSurface
        ? max(iorF0, 0.001f)
        : max(0.04f, iorF0);
    const float dielectricSpecularScale = lerp(
        1.0f,
        2.0f,
        saturate(gMaterial.reflectance));
    const float metalF0 = ReflectionLuminance(gMaterial.color.rgb * textureColor.rgb);
    float reflectanceF0 = lerp(
        saturate(
            dielectricF0 *
            (isOceanSurface ? 1.0f : dielectricSpecularScale)),
        metalF0,
        metallic);
    float reflectionRoughness = clamp(roughness, 0.035f, 1.0f);
    const float reflectionMode = isOceanSurface
        ? -1.0f
        : clamp(gMaterial.reflectionMode, 0.0f, 2.0f);
    const float reflectionProbeIntensity = isOceanSurface
        ? max(gMaterial.reflectance, 0.0f)
        : max(gMaterial.reflectionProbeIntensity, 0.0f);

    if (isOceanSurface)
    {
        reflectionRoughness = clamp(gMaterial.oceanRoughness, 0.035f, 1.0f);
    }

    // Probe は材質の基礎反射率を上書きせず、反射像の粗さと寄与率だけを制御する。
    if (!isOceanSurface && reflectionMode >= 0.5f)
    {
        reflectionRoughness = clamp(gMaterial.reflectionReserved, 0.035f, 1.0f);
    }

    // SSR は画面全体へレイを飛ばすため、通常材質は反射指定か金属成分がある場合だけ対象にする。
    if (reflectionMode < 0.5f &&
        max(
            max(gMaterial.reflectance, metallic),
            reflectionProbeIntensity) <= 0.0001f)
    {
        reflectanceF0 = 0.0f;
    }

    if (gMaterial.alphaMode == 2)
    {
        reflectanceF0 *= saturate(materialAlpha);
    }

    if (gMaterial.enableLighting == 0 && reflectionMode < 0.5f)
    {
        reflectanceF0 = 0.0f;
    }

    // R: 基礎反射率、G: 粗さ、B: 反射方式、A: Probe 寄与率。
    return float4(
        saturate(reflectanceF0),
        reflectionRoughness,
        reflectionMode,
        reflectionProbeIntensity);
}
