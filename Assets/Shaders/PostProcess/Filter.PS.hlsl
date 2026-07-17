Texture2D<float4> gSourceTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer FilterConstants : register(b0)
{
    float2 gInverseResolution;
    float gFilterMode;
    float gStrength;
    float3 gTintColor;
    float gReserved0;
    float4 gReserved1;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 texel = gInverseResolution;
    const float3 c00 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(-1.0f, -1.0f), 0.0f).rgb;
    const float3 c10 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(0.0f, -1.0f), 0.0f).rgb;
    const float3 c20 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(1.0f, -1.0f), 0.0f).rgb;
    const float3 c01 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(-1.0f, 0.0f), 0.0f).rgb;
    const float3 c11 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f).rgb;
    const float3 c21 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(1.0f, 0.0f), 0.0f).rgb;
    const float3 c02 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(-1.0f, 1.0f), 0.0f).rgb;
    const float3 c12 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(0.0f, 1.0f), 0.0f).rgb;
    const float3 c22 = gSourceTexture.SampleLevel(gLinearSampler, input.texcoord + texel * float2(1.0f, 1.0f), 0.0f).rgb;
    const int filterMode = int(gFilterMode + 0.5f);
    float3 filteredColor = c11;

    if (filterMode == 1)
    {
        filteredColor = (c00 + c20 + c02 + c22 + (c10 + c01 + c21 + c12) * 2.0f + c11 * 4.0f) / 16.0f;
    }
    else if (filterMode == 2)
    {
        filteredColor = c11 * 9.0f - (c00 + c10 + c20 + c01 + c21 + c02 + c12 + c22);
    }
    else if (filterMode == 3)
    {
        filteredColor = c11 * 5.0f - (c10 + c01 + c21 + c12);
    }
    else if (filterMode == 4)
    {
        filteredColor = abs(c11 * 8.0f - (c00 + c10 + c20 + c01 + c21 + c02 + c12 + c22));
    }
    else if (filterMode == 5 || filterMode == 6)
    {
        const float kernelScale = filterMode == 5 ? 2.0f : 1.0f;
        const float gx =
            -Luminance(c00) - kernelScale * Luminance(c01) - Luminance(c02) +
            Luminance(c20) + kernelScale * Luminance(c21) + Luminance(c22);
        const float gy =
            -Luminance(c00) - kernelScale * Luminance(c10) - Luminance(c20) +
            Luminance(c02) + kernelScale * Luminance(c12) + Luminance(c22);
        filteredColor = sqrt(gx * gx + gy * gy).xxx;
    }
    else if (filterMode == 7)
    {
        const float l00 = Luminance(c00);
        const float l10 = Luminance(c10);
        const float l20 = Luminance(c20);
        const float l01 = Luminance(c01);
        const float l21 = Luminance(c21);
        const float l02 = Luminance(c02);
        const float l12 = Luminance(c12);
        const float l22 = Luminance(c22);
        const float k0 = 5.0f * (l00 + l10 + l20) - 3.0f * (l01 + l21 + l02 + l12 + l22);
        const float k1 = 5.0f * (l10 + l20 + l21) - 3.0f * (l00 + l01 + l02 + l12 + l22);
        const float k2 = 5.0f * (l20 + l21 + l22) - 3.0f * (l00 + l10 + l01 + l02 + l12);
        const float k3 = 5.0f * (l21 + l12 + l22) - 3.0f * (l00 + l10 + l20 + l01 + l02);
        const float k4 = 5.0f * (l02 + l12 + l22) - 3.0f * (l00 + l10 + l20 + l01 + l21);
        const float k5 = 5.0f * (l01 + l02 + l12) - 3.0f * (l00 + l10 + l20 + l21 + l22);
        const float k6 = 5.0f * (l00 + l01 + l02) - 3.0f * (l10 + l20 + l21 + l12 + l22);
        const float k7 = 5.0f * (l00 + l10 + l01) - 3.0f * (l20 + l21 + l02 + l12 + l22);
        const float edge = max(max(max(k0, k1), max(k2, k3)), max(max(k4, k5), max(k6, k7)));
        filteredColor = max(edge, 0.0f).xxx;
    }
    else if (filterMode == 8)
    {
        const float shadowEdge =
            Luminance(c00) + 2.0f * Luminance(c10) + Luminance(c20) -
            Luminance(c02) - 2.0f * Luminance(c12) - Luminance(c22);
        filteredColor = c11 * saturate(1.0f - max(shadowEdge, 0.0f));
    }

    const float3 outputColor = lerp(c11, max(filteredColor, 0.0f) * max(gTintColor, 0.0f), saturate(gStrength));
    return float4(outputColor, 1.0f);
}
