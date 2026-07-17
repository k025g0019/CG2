Texture2D<float> gDepthTexture : register(t0);
Texture2D<float4> gNormalTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer GTAOConstants : register(b0)
{
    float2 inverseResolution;
    float radius;
    float intensity;
}

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    const float centerDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
    const float4 centerNormalSample = gNormalTexture.Sample(gLinearSampler, input.texcoord);

    if (centerDepth >= 0.9999f || centerNormalSample.a <= 0.0f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float3 centerNormal = normalize(centerNormalSample.xyz * 2.0f - 1.0f);
    float visibility = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)
    {
        const float angle = (6.2831853f / 8.0f) * float(sampleIndex);
        const float2 direction = float2(cos(angle), sin(angle));
        const float2 sampleUv = input.texcoord + direction * inverseResolution * radius;
        const float sampleDepth = gDepthTexture.Sample(gLinearSampler, sampleUv);
        const float4 sampleNormalValue = gNormalTexture.Sample(gLinearSampler, sampleUv);
        const float3 sampleNormal = normalize(sampleNormalValue.xyz * 2.0f - 1.0f);
        const float depthDelta = sampleDepth - centerDepth;
        const float horizonWeight = saturate(depthDelta * 32.0f);
        const float distanceWeight = 1.0f - float(sampleIndex) / 8.0f;
        const float normalWeight = sampleNormalValue.a > 0.0f
            ? saturate(dot(centerNormal, sampleNormal) * 0.5f + 0.5f)
            : 0.0f;
        const float sampleWeight = distanceWeight * normalWeight;
        visibility += horizonWeight * sampleWeight;
        totalWeight += sampleWeight;
    }

    const float ao = saturate(
        1.0f - visibility / max(totalWeight, 0.0001f) * max(intensity, 0.0f));
    return float4(ao.xxx, 1.0f);
}
