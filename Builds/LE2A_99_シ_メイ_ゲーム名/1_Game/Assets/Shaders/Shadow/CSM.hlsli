#ifndef CSM_HLSLI
#define CSM_HLSLI

static const int MAX_CASCADES = 4;

struct CSMCB
{
    float4x4 viewProj[MAX_CASCADES];
    float4 cascadeSplits;
    float4 cascadeOffsets[MAX_CASCADES];
    float4 cascadeScales[MAX_CASCADES];
    float2 shadowTexelSize;
    float cascadeBlend;
    float pad;
};

float3 GetShadowUV(float3 worldPos, float4x4 lightViewProj)
{
    float4 clipPos = mul(float4(worldPos, 1.0f), lightViewProj);
    clipPos.xyz /= clipPos.w;
    return float3(clipPos.x * 0.5f + 0.5f, -clipPos.y * 0.5f + 0.5f, clipPos.z);
}

int GetCascadeIndex(float3 worldPos, float4 splits, float4x4 viewProj0)
{
    float4 clipPos = mul(float4(worldPos, 1.0f), viewProj0);
    float depth = clipPos.z / clipPos.w;
    [unroll]
    for (int i = 0; i < MAX_CASCADES - 1; i++)
    {
        if (depth < splits[i])
            return i;
    }
    return MAX_CASCADES - 1;
}

float SampleShadowMapCascade(sampler comparisonSampler, Texture2DArray shadowMap, float3 shadowUV, uint cascadeIndex)
{
    return shadowMap.SampleCmpLevelZero(comparisonSampler, float3(shadowUV.xy, cascadeIndex), shadowUV.z);
}

float SampleShadowMapBlend(sampler comparisonSampler, Texture2DArray shadowMap, float3 worldPos, CSMCB csm)
{
    float4 clipPos = mul(float4(worldPos, 1.0f), csm.viewProj[0]);
    float depth = clipPos.z / clipPos.w;
    int c0 = 0;
    int c1 = 0;
    float blend = 0.0f;
    [unroll]
    for (int i = 0; i < MAX_CASCADES - 1; i++)
    {
        if (depth < csm.cascadeSplits[i])
        {
            c0 = i;
            c1 = max(i - 1, 0);
            break;
        }
        c0 = MAX_CASCADES - 1;
        c1 = MAX_CASCADES - 2;
    }
    float border = csm.cascadeBlend;
    float splitDist = csm.cascadeSplits[c0];
    if (c0 > 0 && depth > splitDist - border)
    {
        blend = (depth - (splitDist - border)) / border;
        c1 = c0 - 1;
    }
    float3 uv0 = GetShadowUV(worldPos, csm.viewProj[c0]);
    float s0 = SampleShadowMapCascade(comparisonSampler, shadowMap, uv0, c0);
    if (blend > 0.0f)
    {
        float3 uv1 = GetShadowUV(worldPos, csm.viewProj[c1]);
        float s1 = SampleShadowMapCascade(comparisonSampler, shadowMap, uv1, c1);
        return lerp(s1, s0, blend);
    }
    return s0;
}

#endif
