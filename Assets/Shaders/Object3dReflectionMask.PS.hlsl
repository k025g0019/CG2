#include "Common/Lighting.hlsli"

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
    row_major float4x4 uvTransform;
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

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float reflectMask = saturate(max(gMaterial.reflectance, gMaterial.metallic));
    float smoothness = saturate(1.0f - gMaterial.roughness);
    float reflectionMode = clamp(gMaterial.reflectionMode, 0.0f, 2.0f);
    float reflectionProbeIntensity = max(gMaterial.reflectionProbeIntensity, 0.0f);

    if (gMaterial.enableLighting == 0)
    {
        reflectMask = 0.0f;
    }

    return float4(reflectMask, smoothness, reflectionMode, reflectionProbeIntensity);
}
