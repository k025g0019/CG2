#include "ToneMappingCommon.hlsli"

struct SkyboxCB
{
    float4x4 invViewProj;
    float3 cameraPos;
    float intensity;
    float3 skyTopColor;
    float horizonSharpness;
    float3 skyBottomColor;
    float exposure;
};

ConstantBuffer<SkyboxCB> gSkybox : register(b0);
TextureCube gCubeTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float2 uv = input.texcoord * float2(1.0f, -1.0f);
    float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    float4 worldPos = mul(clipPos, gSkybox.invViewProj);
    float3 viewDir = normalize(worldPos.xyz / worldPos.w - gSkybox.cameraPos);
    float4 skyColor = gCubeTexture.Sample(gSampler, viewDir) * gSkybox.intensity;
    float t = pow(max(viewDir.y, 0.0f), gSkybox.horizonSharpness);
    float3 gradient = lerp(gSkybox.skyBottomColor, gSkybox.skyTopColor, t);
    float3 color = skyColor.rgb + gradient * (1.0f - skyColor.a);
    return float4(LinearToSRGB(color * gSkybox.exposure), 1.0f);
}
