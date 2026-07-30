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

    const float dielectricF0 = max(0.04f, ReflectionIorToF0(gMaterial.ior));
    const float metalF0 = ReflectionLuminance(gMaterial.color.rgb * textureColor.rgb);
    const float materialF0 = lerp(dielectricF0, metalF0, saturate(gMaterial.metallic));
    float reflectanceF0 = lerp(materialF0, 1.0f, saturate(gMaterial.reflectance));
    float reflectionRoughness = clamp(gMaterial.roughness, 0.035f, 1.0f);
    const float reflectionMode = clamp(gMaterial.reflectionMode, 0.0f, 2.0f);
    const float reflectionProbeIntensity = max(gMaterial.reflectionProbeIntensity, 0.0f);

    // Probe は材質の基礎反射率を上書きせず、反射像の粗さと寄与率だけを制御する。
    if (reflectionMode >= 0.5f)
    {
        reflectionRoughness = clamp(gMaterial.reflectionReserved, 0.035f, 1.0f);
    }

    // SSR は画面全体へレイを飛ばすため、通常材質は反射指定か金属成分がある場合だけ対象にする。
    if (reflectionMode < 0.5f &&
        max(
            max(gMaterial.reflectance, gMaterial.metallic),
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
