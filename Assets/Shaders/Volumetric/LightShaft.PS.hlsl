Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s0);

struct LightShaftCB
{
    float2 lightScreenPosition;
    float intensity;
    float decay;
};

ConstantBuffer<LightShaftCB> gLightShaft : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 toLight = gLightShaft.lightScreenPosition - input.texcoord;
    const float distanceToLight = length(toLight);
    const float shaft = exp(-distanceToLight * max(gLightShaft.decay, 0.0001f)) * gLightShaft.intensity;
    const float3 sceneColor = gSceneColor.Sample(gSampler, input.texcoord).rgb;
    return float4(sceneColor + shaft.xxx, 1.0f);
}
