#include "Common/Lighting.hlsli"
#include "Common/AdvancedPBR.hlsli"
#include "Common/PBRCommon.hlsli"
#include "Lighting/IBLRuntime.hlsli"
#include "Shadow/SoftShadow.hlsli"

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
ConstantBuffer<DirectionalLightData> gDirectionalLight : register(b1);

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

SamplerState gTextureSampler : register(s0);
SamplerState gShadowSampler : register(s1);

static const float kIBLPrefilterMipCount = 9.0f;

float CalculateSpotAttenuation(float3 lightDirection, float3 spotForward)
{
    float spotCos = dot(lightDirection, normalize(spotForward));
    float innerCos = max(gDirectionalLight.spotCosInner, gDirectionalLight.spotCosOuter);
    float outerCos = min(gDirectionalLight.spotCosInner, gDirectionalLight.spotCosOuter);
    return saturate((spotCos - outerCos) / max(innerCos - outerCos, 0.0001f));
}

void BuildLightInfo(float3 worldPosition, out float3 lightDirection, out float lightIntensity)
{
    lightIntensity = max(gDirectionalLight.intensity, 0.0f);
    if (gDirectionalLight.lightType == 0) {
        lightDirection = normalize(-gDirectionalLight.direction);
        return;
    }
    float3 toLight = gDirectionalLight.position - worldPosition;
    float distanceToLight = length(toLight);
    lightDirection = normalize(toLight);
    lightIntensity *= 1.0f / max(distanceToLight * distanceToLight, 0.35f);
    float range = max(gDirectionalLight.range, 0.01f);
    float distanceRate = saturate(distanceToLight / range);
    lightIntensity *= 1.0f - pow(distanceRate, 4.0f);
    lightIntensity *= lightIntensity;
    if (gDirectionalLight.lightType == 2)
        lightIntensity *= CalculateSpotAttenuation(-lightDirection, gDirectionalLight.direction);
}

float SampleShadow(float4 shadowPosition)
{
    if (shadowPosition.w <= 0.000001f) return 1.0f;
    float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f) return 1.0f;
    float receiverDepth = saturate(shadowNdc.z);
    float2 texelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);
    return SampleSoftShadow9Tap(gShadowMap, gShadowSampler, shadowUv, receiverDepth - 0.0025f, texelSize, 1.35f);
}

float GetShadowVisibility(float3 normal, float3 lightDirection, float4 shadowPosition)
{
    if (saturate(dot(normal, lightDirection)) <= 0.0001f) return 1.0f;
    return SampleShadow(shadowPosition);
}

AdvancedPbrMaterial BuildMaterial(float2 texcoord)
{
    float4 uvPosition = mul(float4(texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gMaterial.useTexture != 0
        ? gTexture.Sample(gTextureSampler, uvPosition.xy)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 materialColor = textureColor * gMaterial.color;

    AdvancedPbrMaterial material;
    material.baseColor = max(materialColor.rgb, 0.0f);
    material.alpha = saturate(materialColor.a);
    material.metallic = saturate(gMaterial.metallic);
    material.roughness = clamp(gMaterial.roughness, 0.035f, 1.0f);
    material.reflectance = saturate(gMaterial.reflectance);
    material.ior = max(gMaterial.ior, 1.0f);
    material.emissionStrength = max(gMaterial.emissionStrength, 0.0f);
    return material;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    AdvancedPbrMaterial material = BuildMaterial(input.texcoord);

    if (gMaterial.enableLighting == 0) {
        float3 unlitColor = material.baseColor + material.baseColor * material.emissionStrength;
        return float4(unlitColor, material.alpha);
    }

    //============================================================
    // 法線と視線を安定化する。
    // 視線が 0 ベクトルに近い時も黒潰れしないよう、前方を予備値に使う。
    //============================================================
    float3 N = normalize(input.normal);
    float3 viewVector = gDirectionalLight.cameraPosition - input.worldPosition;
    float3 V = dot(viewVector, viewVector) > 0.000001f
        ? normalize(viewVector)
        : float3(0.0f, 0.0f, 1.0f);
    float NdotV = saturate(dot(N, V));

    float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    float lightIntensity = 0.0f;
    BuildLightInfo(input.worldPosition, lightDirection, lightIntensity);

    float3 L = dot(lightDirection, lightDirection) > 0.000001f
        ? normalize(lightDirection)
        : float3(0.0f, 1.0f, 0.0f);
    float shadow = GetShadowVisibility(N, L, input.shadowPosition);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));

    float3 albedo = material.baseColor;
    float metallic = saturate(gMaterial.metallic);
    float roughness = clamp(gMaterial.roughness, 0.04f, 1.0f);
    //============================================================
    // reflectance は F0 用の反射率であり AO ではない。
    // ここへ入れると反射率 0 の面から環境光まで消えて中央が黒く抜ける。
    //============================================================
    float ao = 1.0f;

    float3 F0 = GetF0(albedo, metallic);

    float3 direct = 0.0f;
    if (NdotL > 0.0f)
    {
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);
        float3 numerator = D * G * F;
        float denominator = max(4.0f * NdotV * NdotL, 0.0001f);
        float3 specular = numerator / denominator;
        float3 radiance = gDirectionalLight.color.rgb * max(lightIntensity, 0.0f);
        direct = (kD * albedo / PI + specular) * radiance * NdotL * shadow;
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
        gDirectionalLight.environmentTextureEnabled >= 0.5f
        ? max(gDirectionalLight.environmentTextureIntensity, 0.0f)
        : 0.0f;
    float3 ibl = EvaluateIBL(N, V, albedo, metallic, roughness, ao, iblIntensity, kIBLPrefilterMipCount);

    //============================================================
    // IBL を切った状態でも最低限の環境光を残す。
    //============================================================
    float3 ambient =
        albedo *
        max(gDirectionalLight.ambientIntensity, 0.0f) *
        (1.0f - metallic);

    float3 emission = albedo * material.emissionStrength;

    float3 finalColor = direct + ibl + ambient + emission;
    return float4(max(finalColor, 0.0f), material.alpha);
}
