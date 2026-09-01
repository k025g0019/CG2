Texture2D gCurrentColor : register(t0);
Texture2D gHistoryColor : register(t1);
SamplerState gSampler : register(s0);

struct ResolveCB
{
    float blendRate;
    float padding0;
    float padding1;
    float padding2;
};

ConstantBuffer<ResolveCB> gResolve : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float3 currentColor = gCurrentColor.Sample(gSampler, input.texcoord).rgb;
    const float3 historyColor = gHistoryColor.Sample(gSampler, input.texcoord).rgb;
    return float4(lerp(currentColor, historyColor, saturate(gResolve.blendRate)), 1.0f);
}
