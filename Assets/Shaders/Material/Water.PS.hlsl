#include "../Reflection/ReflectionCommon.hlsli"

struct WaterMaterial
{
    float4 color;
    float3 cameraPosition;
    float reflectionStrength;
    float refractionStrength;
    float foamStrength;
    float time;
};

ConstantBuffer<WaterMaterial> gWaterMaterial : register(b0);
Texture2D gReflectionTexture : register(t0);
Texture2D gSceneColor : register(t1);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(gWaterMaterial.cameraPosition - input.worldPosition);
    const float fresnel = ReflectionFresnel(normal, viewDirection, 4.0f);
    const float2 waveOffset = float2(
        sin(input.texcoord.y * 16.0f + gWaterMaterial.time) * 0.01f,
        cos(input.texcoord.x * 18.0f + gWaterMaterial.time * 0.75f) * 0.01f);
    const float3 reflectionColor = gReflectionTexture.Sample(gSampler, input.texcoord + waveOffset).rgb;
    const float3 refractionColor = gSceneColor.Sample(gSampler, input.texcoord - waveOffset).rgb;
    const float foam = saturate((1.0f - abs(normal.y)) * gWaterMaterial.foamStrength);
    const float3 color = lerp(refractionColor, reflectionColor, saturate(fresnel * gWaterMaterial.reflectionStrength)) +
        gWaterMaterial.color.rgb * foam * 0.25f;
    return float4(color, gWaterMaterial.color.a);
}
