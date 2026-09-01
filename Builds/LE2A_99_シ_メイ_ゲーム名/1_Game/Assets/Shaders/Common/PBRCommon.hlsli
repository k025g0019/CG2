#ifndef PBRCOMMON_HLSLI
#define PBRCOMMON_HLSLI

#include "Math.hlsli"

struct PBRMaterial
{
    float3 albedo;
    float metallic;
    float3 emissive;
    float roughness;
    float3 F0;
    float ao;
    float3 clearCoat;
    float clearCoatRoughness;
};

float3 GetF0(float3 albedo, float metallic)
{
    return lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / (PI * denom * denom + EPSILON);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 BRDF(float3 L, float3 V, float3 N, float3 F0, float roughness, float metallic)
{
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;
    return kD * float3(1.0f, 1.0f, 1.0f) / PI + numerator / denominator;
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
    float3 N = float3(0.0f, 0.0f, 1.0f);
    float A = 0.0f;
    float B = 0.0f;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; i++)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0f);
        float NdotH = max(H.z, 0.0f);
        float VdotH = max(dot(V, H), 0.0f);
        if (NdotL > 0.0f)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = G * VdotH / (NdotH * max(NdotV, 0.0001f));
            float Fc = pow(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / float(SAMPLE_COUNT);
}

#endif
