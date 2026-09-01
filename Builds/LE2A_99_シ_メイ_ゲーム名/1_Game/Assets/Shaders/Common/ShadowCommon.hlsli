#ifndef SHADOWCOMMON_HLSLI
#define SHADOWCOMMON_HLSLI

static const float SHADOW_BIAS = 0.001f;

float2 ShadowPCF2x2(sampler comparisonSampler, Texture2D shadowMap, float3 shadowUV, float2 texelSize)
{
    float2 dx = float2(texelSize.x, 0.0f);
    float2 dy = float2(0.0f, texelSize.y);
    float depth = shadowUV.z - SHADOW_BIAS;
    float s = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y += 2)
    {
        [unroll]
        for (int x = -1; x <= 1; x += 2)
        {
            float2 uv = shadowUV.xy + float2(x, y) * texelSize;
            s += shadowMap.SampleCmpLevelZero(comparisonSampler, uv, depth);
        }
    }
    return s * 0.25f;
}

float ShadowPCF3x3(sampler comparisonSampler, Texture2D shadowMap, float3 shadowUV, float2 texelSize)
{
    float depth = shadowUV.z - SHADOW_BIAS;
    float s = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 uv = shadowUV.xy + float2(x, y) * texelSize;
            s += shadowMap.SampleCmpLevelZero(comparisonSampler, uv, depth);
        }
    }
    return s / 9.0f;
}

float ShadowPCF5x5(sampler comparisonSampler, Texture2D shadowMap, float3 shadowUV, float2 texelSize)
{
    float depth = shadowUV.z - SHADOW_BIAS;
    float s = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            float2 uv = shadowUV.xy + float2(x, y) * texelSize;
            s += shadowMap.SampleCmpLevelZero(comparisonSampler, uv, depth);
        }
    }
    return s / 25.0f;
}

float ShadowPCSS(sampler comparisonSampler, Texture2D shadowMap, float3 shadowUV, float2 texelSize, float receiverRadius)
{
    float depth = shadowUV.z - SHADOW_BIAS;
    float blockerSum = 0.0f;
    float blockerCount = 0.0f;
    const int BLOCKER_SEARCH = 8;
    for (int y = -BLOCKER_SEARCH; y <= BLOCKER_SEARCH; y++)
    {
        for (int x = -BLOCKER_SEARCH; x <= BLOCKER_SEARCH; x++)
        {
            float2 uv = shadowUV.xy + float2(x, y) * texelSize;
            float shadowDepth = shadowMap.SampleLevel(linearSampler, uv, 0).r;
            if (shadowDepth < depth)
            {
                blockerSum += shadowDepth;
                blockerCount++;
            }
        }
    }
    float blockerAverage = blockerCount > 0.0f ? blockerSum / blockerCount : depth;
    float penumbraSize = (depth - blockerAverage) / blockerAverage * receiverRadius;
    float filterSize = min(penumbraSize, 16.0f);
    float s = 0.0f;
    int sampleCount = clamp((int)(filterSize * 2.0f), 4, 16);
    for (int i = 0; i < sampleCount; i++)
    {
        float2 offset = SampleDiskConcentric(InterleavedGradientNoise(shadowUV.xy * 1024.0f + float(i))) * filterSize * texelSize;
        s += shadowMap.SampleCmpLevelZero(comparisonSampler, shadowUV.xy + offset, depth);
    }
    return s / sampleCount;
}

float ShadowCascadeBlend(sampler comparisonSampler, Texture2DArray shadowMapArray, float3 shadowUV, uint cascadeIndex, float blend)
{
    float depth = shadowUV.z - SHADOW_BIAS;
    float s0 = shadowMapArray.SampleCmpLevelZero(comparisonSampler, float3(shadowUV.xy, cascadeIndex), depth);
    if (blend > 0.0f && cascadeIndex > 0)
    {
        float s1 = shadowMapArray.SampleCmpLevelZero(comparisonSampler, float3(shadowUV.xy, cascadeIndex - 1), depth);
        return lerp(s1, s0, blend);
    }
    return s0;
}

#endif
