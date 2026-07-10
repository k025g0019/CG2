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

struct GlassCB
{
    float3 cameraPosition;
    float refractionScale;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<GlassCB> gGlass : register(b1);
Texture2D gSceneColor : register(t0);
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
    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(gGlass.cameraPosition - input.worldPosition);
    const float fresnel = ReflectionFresnel(normal, viewDirection, 5.0f);
    const float2 refractionOffset = normal.xy * (max(gMaterial.ior, 1.0f) - 1.0f) * gGlass.refractionScale;
    const float3 refractedColor = gSceneColor.Sample(gSampler, input.texcoord + refractionOffset).rgb;
    const float3 reflectionColor = gReflectionTexture.Sample(gSampler, input.texcoord).rgb;
    const float3 tintColor = gMaterial.color.rgb;
    const float3 color = lerp(refractedColor * tintColor, reflectionColor, saturate(fresnel * max(gMaterial.reflectance, 0.0f)));
    return float4(color, saturate(gMaterial.color.a));
}
