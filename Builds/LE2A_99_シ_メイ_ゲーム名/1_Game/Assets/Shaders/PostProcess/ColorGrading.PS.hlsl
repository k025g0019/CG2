#include "ToneMappingCommon.hlsli"

struct ColorGradingCB
{
    float temperature;
    float tint;
    float saturation;
    float contrast;
    float3 lift;
    float gamma;
    float3 gain;
    float pad;
};

ConstantBuffer<ColorGradingCB> gColorGrading : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gLUT : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 ApplyColorGrading(float3 color, ColorGradingCB cb)
{
    float3 c = color;
    c = pow(abs(c), cb.gamma);
    c *= cb.gain;
    c += cb.lift;
    float3 whiteBalance = float3(1.0f, 1.0f, 1.0f);
    whiteBalance.g *= 1.0f - cb.tint * 0.5f;
    whiteBalance.b *= 1.0f + cb.tint * 0.5f;
    c *= whiteBalance;
    float3 lab = float3(0.0f, 0.0f, 0.0f);
    float luma = Luminance(c);
    c = lerp(float3(luma, luma, luma), c, cb.saturation);
    c = lerp(float3(0.5f, 0.5f, 0.5f), c, cb.contrast);
    return c;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float3 color = gSceneColor.Sample(gSampler, input.texcoord).rgb;
    color = ApplyColorGrading(color, gColorGrading);
    if (gLUT)
    {
        float3 lutUV = color * (63.0f / 64.0f) + (0.5f / 64.0f);
        float2 lutCoord = float2(lutUV.x / 8.0f + floor(lutUV.z * 8.0f) / 8.0f, lutUV.y);
        float3 lutColor = gLUT.Sample(gSampler, lutCoord).rgb;
        color = lerp(color, lutColor, 1.0f);
    }
    return float4(LinearToSRGB(color), 1.0f);
}
