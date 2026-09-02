//============================================================
// Light Probe Bake: Probe位置から見たシーンの放射輝度を書き出す
//------------------------------------------------------------
// RT0 = その面から出ていく放射輝度、RT1 = Probeからの距離。
// 拡散のみを扱う(Irradiance Probeに鏡面反射を焼くと破綻するため)。
// 前回Bakeしたプローブを間接光として読み戻すことで、Bakeを繰り返す
// たびにバウンス数が増える(DDGIと同じ漸進的マルチバウンス)。
//============================================================
#include "../Common/SceneLightData.hlsli"
#include "../Shadow/ShadowSampling.hlsli"
#include "ProbeSampling.hlsli"

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
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);
ConstantBuffer<EmissiveLightArray> gEmissiveLights : register(b2);

cbuffer ProbeCaptureView : register(b3)
{
    row_major float4x4 gProbeViewProjection;
    float3 gProbeWorldPosition;
    float gProbeCaptureFarDistance;
};

Texture2D gTexture : register(t0);
Texture2D gShadowMap : register(t1);
StructuredBuffer<float4> gProbeShBuffer : register(t20);
Texture2D<float2> gProbeVisibilityAtlas : register(t21);

SamplerState gTextureSampler : register(s0);
SamplerState gShadowSampler : register(s1);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
};

struct ProbeCaptureOutput
{
    float4 radiance : SV_TARGET0;
    float distanceFromProbe : SV_TARGET1;
};

ProbeCaptureOutput main(PixelShaderInput input)
{
    float2 tiledUv = input.texcoord * gMaterial.uvTiling + gMaterial.uvOffset;
    float2 materialUv = mul(float4(tiledUv, 0.0f, 1.0f), gMaterial.uvTransform).xy;

    float4 baseColor = gMaterial.color;

    if (gMaterial.useTexture != 0)
    {
        baseColor *= gTexture.Sample(gTextureSampler, materialUv);
    }

    if (gMaterial.alphaMode == 1)
    {
        clip(baseColor.a - saturate(gMaterial.alphaCutoff));
    }

    const float3 albedo = max(baseColor.rgb, 0.0f);
    // Probeへ入る光の向きに対して面がどちらを向いているかで法線を決める。
    const float3 geometricNormal = normalize(input.normal);
    const float3 toProbe = gProbeWorldPosition - input.worldPosition;
    const float3 normal = dot(geometricNormal, toProbe) >= 0.0f
        ? geometricNormal
        : -geometricNormal;

    float3 outgoingRadiance = float3(0.0f, 0.0f, 0.0f);

    // 直接光(拡散のみ)。影は通常描画と同じAtlasから読む。
    for (int lightIndex = 0; lightIndex < 4; lightIndex++)
    {
        DirectionalLightData light = gDirectionalLight.lights[lightIndex];

        if (light.shadowEnabled < -0.5f) break;
        if (light.shadowEnabled < 0.5f && light.intensity < 0.0001f) continue;

        float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
        float lightIntensity = 0.0f;
        BuildSceneLightInfo(input.worldPosition, lightDirection, lightIntensity, light);

        const float normalDotLight = saturate(dot(normal, lightDirection));

        if (normalDotLight <= 0.0f || lightIntensity <= 0.0001f)
        {
            continue;
        }

        const float shadowVisibility = SampleShadowAtlasForLight(
            gShadowMap,
            gShadowSampler,
            input.worldPosition,
            normalDotLight,
            light);

        outgoingRadiance +=
            (albedo / kProbePi) *
            normalDotLight *
            lightIntensity *
            max(light.color.rgb, 0.0f) *
            shadowVisibility;
    }

    // Emissive Light(点光源近似)の寄与も通常描画と揃える。
    for (int emissiveIndex = 0; emissiveIndex < gEmissiveLights.count; emissiveIndex++)
    {
        const float3 toEmissive =
            gEmissiveLights.lights[emissiveIndex].position - input.worldPosition;
        const float emissiveDistance = length(toEmissive);
        const float emissiveRange = max(gEmissiveLights.lights[emissiveIndex].range, 0.01f);

        if (emissiveDistance >= emissiveRange)
        {
            continue;
        }

        const float3 emissiveDirection = toEmissive / max(emissiveDistance, 0.0001f);
        const float emissiveNormalDotLight = saturate(dot(normal, emissiveDirection));

        if (emissiveNormalDotLight <= 0.0f)
        {
            continue;
        }

        const float distanceRate = saturate(emissiveDistance / emissiveRange);
        const float attenuation =
            (1.0f - distanceRate * distanceRate * distanceRate * distanceRate) /
            max(emissiveDistance * emissiveDistance, 0.35f);
        outgoingRadiance +=
            (albedo / kProbePi) *
            emissiveNormalDotLight *
            max(gEmissiveLights.lights[emissiveIndex].intensity, 0.0f) *
            attenuation *
            max(gEmissiveLights.lights[emissiveIndex].color, 0.0f);
    }

    // 前回Bakeしたプローブを読み戻し、バウンスを1段ずつ積み上げる。
    bool hasProbeGi = false;
    const float3 previousBounce = SampleLightProbeGi(
        gProbeShBuffer,
        gProbeVisibilityAtlas,
        gEmissiveLights.probeGrid,
        input.worldPosition,
        normal,
        normal,
        hasProbeGi);

    if (hasProbeGi)
    {
        outgoingRadiance += albedo * previousBounce;
    }

    // 自己発光は面の向きに関係なくそのまま放射する。
    outgoingRadiance +=
        max(gMaterial.emissionColor, 0.0f) *
        max(gMaterial.emissionStrength, 0.0f);

    ProbeCaptureOutput output;
    output.radiance = float4(max(outgoingRadiance, 0.0f), 1.0f);
    output.distanceFromProbe = min(length(toProbe), gProbeCaptureFarDistance);
    return output;
}
