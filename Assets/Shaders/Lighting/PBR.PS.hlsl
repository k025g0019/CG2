#include "PBRCommon.hlsli"
#include "Lighting.hlsli"
#include "ShadowCommon.hlsli"

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

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    float3 skyUpperColor;
    float skyIntensity;
    float3 skyLowerColor;
    float skyEmission;
    float ambientIntensity;
    float horizonSharpness;
    float reflectionIntensity;
    float padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
Texture2D<float4> gTexture : register(t0);
Texture2D<float> gShadowTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    float4 transformedTexcoord = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedTexcoord.xy);
    float4 baseColor = gMaterial.useTexture != 0 ? textureColor : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float3 albedo = (gMaterial.color * baseColor).rgb;

    float3 N = normalize(input.normal);
    float3 V = normalize(float3(0.0f, 2.5f, -8.0f) - input.worldPosition);
    float3 L = normalize(-gDirectionalLight.direction);
    float3 F0 = GetF0(albedo, gMaterial.metallic);

    float NdotL = max(dot(N, L), 0.0f);
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    if (gMaterial.enableLighting != 0)
    {
        float3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, gMaterial.roughness);
        float G = GeometrySmith(N, V, L, gMaterial.roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - gMaterial.metallic);
        float3 numerator = NDF * G * F;
        float denominator = 4.0f * max(dot(N, V), 0.0f) * NdotL + 0.0001f;
        float3 specular = numerator / denominator;

        float3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
        Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    }

    float3 shadow = 1.0f;
    float3 shadowNdc = input.shadowPosition.xyz / input.shadowPosition.w;
    float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
    bool isInsideShadowMap = shadowUv.x >= 0.0f && shadowUv.x <= 1.0f
        && shadowUv.y >= 0.0f && shadowUv.y <= 1.0f && shadowNdc.z >= 0.0f && shadowNdc.z <= 1.0f;
    if (isInsideShadowMap)
    {
        float shadowTexel = 1.0f / 2048.0f;
        float shadowBias = max(0.0008f * (1.0f - NdotL), 0.00025f);
        float shadowSum = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                float closestDepth = gShadowTexture.Sample(gSampler, shadowUv + float2(x, y) * shadowTexel);
                shadowSum += (shadowNdc.z - shadowBias <= closestDepth) ? 1.0f : 0.0f;
            }
        }
        shadow = lerp(0.35f, 1.0f, shadowSum / 9.0f);
    }

    float skyBlend = pow(saturate(N.y * 0.5f + 0.5f), gDirectionalLight.horizonSharpness);
    float3 ambientSky = lerp(gDirectionalLight.skyLowerColor, gDirectionalLight.skyUpperColor, skyBlend);
    float3 ambient = albedo * ambientSky * gDirectionalLight.ambientIntensity * gDirectionalLight.skyIntensity;
    float3 emission = albedo * gMaterial.emissionStrength;

    output.color.rgb = Lo * shadow + ambient + emission;
    output.color.a = saturate(gMaterial.color.a * baseColor.a);
    output.color.rgb = LinearToSRGBFast(output.color.rgb);
    return output;
}
