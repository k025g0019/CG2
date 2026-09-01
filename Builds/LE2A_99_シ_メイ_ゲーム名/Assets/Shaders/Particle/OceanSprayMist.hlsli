#ifndef CG2_OCEAN_SPRAY_MIST_HLSLI
#define CG2_OCEAN_SPRAY_MIST_HLSLI

float HashSprayCoordinate(float2 coordinate)
{
    return frac(sin(dot(coordinate, float2(127.1f, 311.7f))) * 43758.5453f);
}

float SprayValueNoise(float2 coordinate)
{
    const float2 cell = floor(coordinate);
    const float2 fraction = frac(coordinate);
    const float2 smoothFraction = fraction * fraction * (3.0f - 2.0f * fraction);
    const float lower = lerp(HashSprayCoordinate(cell), HashSprayCoordinate(cell + float2(1.0f, 0.0f)), smoothFraction.x);
    const float upper = lerp(HashSprayCoordinate(cell + float2(0.0f, 1.0f)), HashSprayCoordinate(cell + float2(1.0f, 1.0f)), smoothFraction.x);
    return lerp(lower, upper, smoothFraction.y);
}

void EvaluateOceanSprayMist(
    float2 texcoord,
    float3 sourceColor,
    out float3 sprayColor,
    out float sprayAlpha)
{
    const float2 centerOffset = texcoord * 2.0f - 1.0f;
    const float radialDistance = length(centerOffset);
    const float broadMist = saturate(1.0f - radialDistance);
    const float verticalSpray = saturate(1.0f - abs(centerOffset.x) * 2.2f) * saturate(1.0f - (centerOffset.y + 0.3f) * 0.72f);
    const float lowFrequencyNoise = SprayValueNoise(texcoord * 5.0f + float2(3.7f, 9.1f));
    const float highFrequencyNoise = SprayValueNoise(texcoord * 17.0f + float2(21.0f, 4.0f));
    const float breakup = smoothstep(0.24f, 0.88f, lowFrequencyNoise * 0.72f + highFrequencyNoise * 0.28f);
    sprayAlpha = saturate(max(broadMist * 0.62f, verticalSpray) * lerp(0.48f, 1.0f, breakup));
    const float forwardScatter = pow(saturate(1.0f - radialDistance * 0.7f), 3.0f);
    sprayColor = lerp(sourceColor, float3(0.78f, 0.93f, 1.0f), 0.72f);
    sprayColor += float3(0.35f, 0.42f, 0.48f) * forwardScatter;
}

#endif
