#include "PBRCommon.hlsli"
#include "ToneMappingCommon.hlsli"

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
    float padding0;
    float padding1;
    float padding2;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gAlbedo : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float> gDepth : register(t2);
TextureCube gIrradianceMap : register(t3);
TextureCube gPrefilterMap : register(t4);
Texture2D<float2> gBRDFLUT : register(t5);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

static const float3 F0_DIELECTRIC = float3(0.04f, 0.04f, 0.04f);

float3 IBLDiffuse(float3 N, float3 V, float3 albedo, float3 F0, float metallic, float roughness)
{
    float3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 irradiance = gIrradianceMap.Sample(gSampler, N).rgb;
    return kD * irradiance * albedo;
}

float3 IBLSpecular(float3 N, float3 V, float3 F0, float roughness, float NdotV)
{
    float3 R = 2.0f * dot(V, N) * N - V;
    float3 prefilteredColor = gPrefilterMap.SampleLevel(gSampler, R, roughness * 4.0f).rgb;
    float2 envBRDF = gBRDFLUT.Sample(gSampler, float2(NdotV, roughness)).rg;
    return prefilteredColor * (F0 * envBRDF.x + envBRDF.y);
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    float3 albedo = gAlbedo.Sample(gSampler, input.texcoord).rgb;
    float3 N = normalize(gNormal.Sample(gSampler, input.texcoord).xyz * 2.0f - 1.0f);
    float3 V = float3(0.0f, 0.0f, 1.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float3 F0 = lerp(F0_DIELECTRIC, albedo, gMaterial.metallic);
    float3 diffuse = IBLDiffuse(N, V, albedo, F0, gMaterial.metallic, gMaterial.roughness);
    float3 specular = IBLSpecular(N, V, F0, gMaterial.roughness, NdotV);
    float3 ambient = float3(0.03f, 0.03f, 0.03f) * albedo;
    output.color = float4(diffuse + specular + ambient, 1.0f);
    return output;
}
