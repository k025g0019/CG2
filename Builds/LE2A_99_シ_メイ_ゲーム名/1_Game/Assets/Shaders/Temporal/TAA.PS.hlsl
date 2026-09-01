Texture2D gCurrentColor : register(t0);
Texture2D gHistoryColor : register(t1);
Texture2D gMotionVector : register(t2);
SamplerState gSampler : register(s0);

struct TaaCB
{
    float historyWeight;
    float clampStrength;
    float padding0;
    float padding1;
};

ConstantBuffer<TaaCB> gTaa : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 motion = gMotionVector.Sample(gSampler, input.texcoord).xy * 2.0f - 1.0f;
    const float2 historyUv = saturate(input.texcoord - motion);
    const float3 currentColor = gCurrentColor.Sample(gSampler, input.texcoord).rgb;
    const float3 historyColor = gHistoryColor.Sample(gSampler, historyUv).rgb;
    const float3 minColor = min(currentColor, historyColor);
    const float3 maxColor = max(currentColor, historyColor);
    const float3 clampedHistory = clamp(historyColor, minColor - gTaa.clampStrength, maxColor + gTaa.clampStrength);
    const float3 color = lerp(currentColor, clampedHistory, saturate(gTaa.historyWeight));
    return float4(color, 1.0f);
}
