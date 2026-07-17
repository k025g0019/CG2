#ifndef IBL_RUNTIME_HLSLI
#define IBL_RUNTIME_HLSLI

#include "../Common/PBRCommon.hlsli"

TextureCube gIrradianceCube : register(t3);
TextureCube gPrefilterCube : register(t4);
TextureCube gEnvironmentCube : register(t5);
Texture2D<float2> gBRDFLUT : register(t6);

SamplerState gIblSampler : register(s2);

float3 EvaluateIBLWithF0(
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    float iblIntensity,
    float prefilterMipCount,
    float3 F0)
{
    N = normalize(N);
    V = normalize(V);

    float NdotV = saturate(dot(N, V));

    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);

    float3 kS = F;
    float3 kD = 1.0f - kS;
    kD *= 1.0f - metallic;

    float3 irradiance = gIrradianceCube.Sample(gIblSampler, N).rgb;
    float3 diffuseIBL = irradiance * albedo;

    float3 R = normalize(reflect(-V, N));

    float mip = roughness * (prefilterMipCount - 1.0f);
    float3 prefilteredColor = gPrefilterCube.SampleLevel(gIblSampler, R, mip).rgb;

    float2 brdf = gBRDFLUT.Sample(gIblSampler, float2(NdotV, roughness)).rg;

    float3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);

    float3 ibl = (kD * diffuseIBL + specularIBL) * ao;

    return ibl * iblIntensity;
}

float3 EvaluateIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float ao, float iblIntensity, float prefilterMipCount)
{
    return EvaluateIBLWithF0(
        N,
        V,
        albedo,
        metallic,
        roughness,
        ao,
        iblIntensity,
        prefilterMipCount,
        GetF0(albedo, metallic));
}

float3 SampleEnvironment(float3 direction)
{
    return gEnvironmentCube.SampleLevel(gIblSampler, normalize(direction), 0.0f).rgb;
}

#endif
