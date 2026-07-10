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

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 shadowPosition : TEXCOORD3;
};

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 material : SV_TARGET2;
    float4 emission : SV_TARGET3;
};

GBufferOutput main(PSInput input)
{
    GBufferOutput output;
    const float4 uvPosition = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    const float4 textureColor = gMaterial.useTexture != 0
        ? gTexture.Sample(gSampler, uvPosition.xy)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float4 baseColor = textureColor * gMaterial.color;
    const float3 packedNormal = normalize(input.normal) * 0.5f + 0.5f;

    output.albedo = float4(max(baseColor.rgb, 0.0f), saturate(baseColor.a));
    output.normal = float4(packedNormal, 1.0f);
    output.material = float4(
        saturate(gMaterial.roughness),
        saturate(gMaterial.metallic),
        saturate(gMaterial.reflectance),
        saturate((max(gMaterial.ior, 1.0f) - 1.0f) / 2.0f));
    output.emission = float4(baseColor.rgb * max(gMaterial.emissionStrength, 0.0f), 1.0f);
    return output;
}
