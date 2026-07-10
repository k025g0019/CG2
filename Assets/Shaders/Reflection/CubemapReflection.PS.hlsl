#include "PBRCommon.hlsli"

struct ReflectionCB
{
    float4x4 invViewProj;
    float3 cameraPos;
    float roughness;
    float3 scale;
    float intensity;
    float2 screenSize;
    float2 pad;
};

ConstantBuffer<ReflectionCB> gReflection : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gDepth : register(t1);
TextureCube gCubeTexture : register(t2);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float depth = gDepth.Sample(gSampler, input.texcoord).r;
    float2 uv = input.texcoord * float2(1.0f, -1.0f);
    float4 clipPos = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    float4 worldPos = mul(clipPos, gReflection.invViewProj);
    worldPos.xyz /= worldPos.w;
    float3 V = normalize(gReflection.cameraPos - worldPos.xyz);
    float3 N = float3(0.0f, 1.0f, 0.0f);
    float3 R = reflect(-V, N);
    float3 cubeColor = gCubeTexture.SampleLevel(gSampler, R, gReflection.roughness * 4.0f).rgb;
    float fresnel = pow(1.0f - max(dot(N, V), 0.0f), 5.0f);
    float3 reflection = cubeColor * lerp(gReflection.scale, float3(1.0f, 1.0f, 1.0f), fresnel);
    return float4(reflection * gReflection.intensity, 1.0f);
}
