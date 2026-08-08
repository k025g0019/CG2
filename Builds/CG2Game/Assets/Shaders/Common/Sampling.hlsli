#ifndef SAMPLING_HLSLI
#define SAMPLING_HLSLI

#include "Math.hlsli"

static const float GOLDEN_ANGLE = 2.399963229728653f;

float3 SampleHemisphere(float2 uv)
{
    float theta = TWO_PI * uv.x;
    float phi = acos(1.0f - uv.y);
    return float3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

float3 SampleHemisphereCosine(float2 uv)
{
    float theta = TWO_PI * uv.x;
    float r = sqrt(uv.y);
    return float3(r * cos(theta), r * sin(theta), sqrt(1.0f - uv.y));
}

float3 SampleSphere(float2 uv)
{
    float theta = TWO_PI * uv.x;
    float phi = acos(2.0f * uv.y - 1.0f);
    return float3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

float2 SampleDisk(float2 uv)
{
    float theta = TWO_PI * uv.x;
    float r = sqrt(uv.y);
    return float2(r * cos(theta), r * sin(theta));
}

float2 SampleDiskConcentric(float2 uv)
{
    float2 offset = 2.0f * uv - 1.0f;
    if (offset.x == 0.0f && offset.y == 0.0f) return float2(0.0f, 0.0f);
    float theta, r;
    if (abs(offset.x) > abs(offset.y))
    {
        r = offset.x;
        theta = HALF_PI * (offset.y / offset.x);
    }
    else
    {
        r = offset.y;
        theta = HALF_PI * (1.0f - offset.x / offset.y);
    }
    return float2(r * cos(theta), r * sin(theta));
}

float2 InterleavedGradientNoise(float2 pixelPos)
{
    return frac(pixelPos / float2(7.0f, 3.0f) + float2(0.231f, 0.417f));
}

float3 TangentToWorld(float3 sample, float3 N)
{
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * sample.x + bitangent * sample.y + N * sample.z);
}

#endif
