#ifndef CG2_BLOOM_COMMON_HLSLI
#define CG2_BLOOM_COMMON_HLSLI

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 KarisAverage(float3 color)
{
    return color / (1.0f + Luminance(color));
}

float3 SampleTent9(Texture2D<float4> sourceTexture, SamplerState linearSampler, float2 uv, float2 texelSize)
{
    float3 result = sourceTexture.SampleLevel(linearSampler, uv, 0.0f).rgb * 4.0f;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(-1.0f, 0.0f), 0.0f).rgb * 2.0f;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(1.0f, 0.0f), 0.0f).rgb * 2.0f;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(0.0f, -1.0f), 0.0f).rgb * 2.0f;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(0.0f, 1.0f), 0.0f).rgb * 2.0f;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(-1.0f, -1.0f), 0.0f).rgb;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(1.0f, -1.0f), 0.0f).rgb;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(-1.0f, 1.0f), 0.0f).rgb;
    result += sourceTexture.SampleLevel(linearSampler, uv + texelSize * float2(1.0f, 1.0f), 0.0f).rgb;
    return result * (1.0f / 16.0f);
}

#endif
