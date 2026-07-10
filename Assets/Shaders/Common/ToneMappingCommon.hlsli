#ifndef TONEMAPPINGCOMMON_HLSLI
#define TONEMAPPINGCOMMON_HLSLI

static const float3 LUMA_WEIGHTS = float3(0.2126f, 0.7152f, 0.0722f);

float Luminance(float3 color)
{
    return dot(color, LUMA_WEIGHTS);
}

float3 ReinhardToneMap(float3 color)
{
    return color / (color + 1.0f);
}

float3 ReinhardToneMapExt(float3 color, float whitePoint)
{
    float3 normalized = color * (1.0f / whitePoint);
    return normalized / (normalized + 1.0f);
}

float3 ACESFilmToneMap(float3 color)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 UnrealToneMap(float3 color)
{
    return color / (color + 1.0f);
}

float3 UchimuraToneMap(float3 color)
{
    const float P = 1.0f;
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    const float W = 11.2f;
    float3 tone = color * (color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F) - E / F;
    float white = W * (W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F) - E / F;
    return tone / white;
}

float3 GammaCorrect(float3 color, float gamma)
{
    return pow(abs(color), 1.0f / gamma);
}

float3 LinearToSRGB(float3 color)
{
    return max(1.055f * pow(abs(color), 1.0f / 2.4f) - 0.055f, 0.0f);
}

float3 SRGBToLinear(float3 color)
{
    return color * (color * (color * 0.305306011f + 0.682171111f) + 0.012522878f);
}

float3 LinearToSRGBFast(float3 color)
{
    return pow(abs(color), 1.0f / 2.2f);
}

float3 SRGBToLinearFast(float3 color)
{
    return pow(abs(color), 2.2f);
}

float3 FilmicToneMap(float3 color)
{
    color = max(float3(0.0f, 0.0f, 0.0f), color - 0.004f);
    color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f) + 0.06f);
    return color;
}

#endif
