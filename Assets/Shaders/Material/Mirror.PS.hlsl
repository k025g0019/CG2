#include "MaterialCommon.hlsli"
#include "../Reflection/ReflectionCommon.hlsli"

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

struct MirrorLight
{
    float3 cameraPosition;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<MirrorLight> gMirrorLight : register(b1);
Texture2D gBaseTexture : register(t0);
Texture2D gReflectionTexture : register(t1);
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
    const float4 baseColor = (gMaterial.useTexture != 0 ? gBaseTexture.Sample(gSampler, uvPosition.xy) : 1.0f.xxxx) * gMaterial.color;
    const float3 viewDirection = normalize(gMirrorLight.cameraPosition - input.worldPosition);
    const float fresnel = ReflectionFresnel(input.normal, viewDirection, 5.0f);
    const float3 reflectionColor = gReflectionTexture.Sample(gSampler, input.texcoord).rgb;
    const float reflectionRate = saturate(lerp(0.35f, 1.0f, fresnel) * max(gMaterial.reflectance, 0.0f) * gMirrorLight.intensity);
    const float3 color = lerp(baseColor.rgb, reflectionColor, reflectionRate);
    return float4(color, saturate(baseColor.a));
}
