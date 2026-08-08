#include "MaterialCommon.hlsli"
#include "../Common/PBRCommon.hlsli"

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
    row_major float4x4 uvTransform;
};

struct LightData
{
    float4 color;
    float3 direction;
    float intensity;
    float3 cameraPosition;
    float ambientIntensity;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<LightData> gLight : register(b1);
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 uvPosition = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    const float4 texel = gMaterial.useTexture != 0 ? gTexture.Sample(gSampler, uvPosition.xy) : 1.0f.xxxx;
    const float4 baseColor = texel * gMaterial.color;
    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(gLight.cameraPosition - input.worldPosition);
    const float3 lightDirection = normalize(-gLight.direction);
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float3 f0 = GetF0(baseColor.rgb, gMaterial.metallic);
    const float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    const float ndf = DistributionGGX(normal, halfVector, max(gMaterial.roughness, 0.04f));
    const float geometry = GeometrySmith(normal, viewDirection, lightDirection, max(gMaterial.roughness, 0.04f));
    const float3 specular = (ndf * geometry * fresnel) /
        max(4.0f * saturate(dot(normal, viewDirection)) * saturate(dot(normal, lightDirection)), 0.0001f);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - gMaterial.metallic) * baseColor.rgb / PI;
    const float3 color = (diffuse + specular) * gLight.color.rgb * gLight.intensity * saturate(dot(normal, lightDirection)) +
        baseColor.rgb * gLight.ambientIntensity +
        baseColor.rgb * max(gMaterial.emissionStrength, 0.0f);
    return float4(max(color, 0.0f), saturate(baseColor.a));
}
