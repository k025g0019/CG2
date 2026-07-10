#include "../Common/PBRCommon.hlsli"

struct DeferredLight
{
    float4 color;
    float3 direction;
    float intensity;
    float3 cameraPosition;
    float ambientIntensity;
};

ConstantBuffer<DeferredLight> gLight : register(b0);
Texture2D gAlbedoTexture : register(t0);
Texture2D gNormalTexture : register(t1);
Texture2D gMaterialTexture : register(t2);
Texture2D gDepthTexture : register(t3);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 albedo = gAlbedoTexture.Sample(gSampler, input.texcoord);
    const float3 normal = normalize(gNormalTexture.Sample(gSampler, input.texcoord).xyz * 2.0f - 1.0f);
    const float4 material = gMaterialTexture.Sample(gSampler, input.texcoord);
    const float metallic = material.y;
    const float roughness = max(material.x, 0.04f);
    const float3 viewDirection = normalize(gLight.cameraPosition);
    const float3 lightDirection = normalize(-gLight.direction);
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float3 f0 = GetF0(albedo.rgb, metallic);
    const float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    const float ndf = DistributionGGX(normal, halfVector, roughness);
    const float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    const float3 specular = (ndf * geometry * fresnel) /
        max(4.0f * saturate(dot(normal, viewDirection)) * saturate(dot(normal, lightDirection)), 0.0001f);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * albedo.rgb / PI;
    const float ndotl = saturate(dot(normal, lightDirection));
    const float occlusion = (gDepthTexture.Sample(gSampler, input.texcoord).r >= 1.0f) ? 0.0f : 1.0f;
    const float3 color = (diffuse + specular) * gLight.color.rgb * gLight.intensity * ndotl * occlusion +
        albedo.rgb * gLight.ambientIntensity;
    return float4(max(color, 0.0f), albedo.a);
}
