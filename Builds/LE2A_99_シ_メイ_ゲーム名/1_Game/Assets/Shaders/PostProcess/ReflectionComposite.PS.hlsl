#include "../Reflection/ReflectionCommon.hlsli"

Texture2D gSceneColor : register(t0);
Texture2D gReflectionColor : register(t1);
Texture2D gMaskTexture : register(t2);
SamplerState gSampler : register(s0);

struct ReflectionCompositeCB
{
    float intensity;
    float fresnelPower;
    float padding0;
    float padding1;
};

ConstantBuffer<ReflectionCompositeCB> gReflectionComposite : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 sceneColor = gSceneColor.Sample(gSampler, input.texcoord);
    const float4 reflectionColor = gReflectionColor.Sample(gSampler, input.texcoord);
    const float mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
    const float blendRate = saturate(mask * gReflectionComposite.intensity);
    return float4(lerp(sceneColor.rgb, reflectionColor.rgb, blendRate), sceneColor.a);
}
