Texture2D<float> gDepthTexture : register(t0);
Texture2D<float4> gNormalTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer GTAOConstants : register(b0)
{
    float2 inverseResolution;
    float radius;
    float intensity;
    float bias;
    float power;
    float nearClip;
    float farClip;
}

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float LinearizeDepth(float deviceDepth)
{
    const float safeNearClip = max(nearClip, 0.0001f);
    const float safeFarClip = max(farClip, safeNearClip + 0.0001f);
    return safeNearClip * safeFarClip /
        max(safeFarClip - deviceDepth * (safeFarClip - safeNearClip), 0.0001f);
}

float2 RotateDirection(float2 direction, float2 rotation)
{
    return float2(
        direction.x * rotation.x - direction.y * rotation.y,
        direction.x * rotation.y + direction.y * rotation.x);
}

float4 main(PixelShaderInput input) : SV_TARGET
{
    const float centerDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
    const float4 centerNormalSample = gNormalTexture.Sample(gLinearSampler, input.texcoord);

    if (centerDepth >= 0.9999f || centerNormalSample.a <= 0.0f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float3 centerNormal = normalize(centerNormalSample.xyz * 2.0f - 1.0f);
    const float centerLinearDepth = LinearizeDepth(centerDepth);
    const float randomAngle = frac(
        sin(dot(input.position.xy, float2(12.9898f, 78.233f))) * 43758.5453f) *
        6.28318530718f;
    const float2 sampleRotation = float2(cos(randomAngle), sin(randomAngle));
    float occlusion = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int directionIndex = 0; directionIndex < 8; directionIndex++)
    {
        const float angle = 6.28318530718f * float(directionIndex) / 8.0f;
        const float2 direction = RotateDirection(
            float2(cos(angle), sin(angle)),
            sampleRotation);

        [unroll]
        for (int stepIndex = 1; stepIndex <= 2; stepIndex++)
        {
            const float stepRate = float(stepIndex) * 0.5f;
            const float2 sampleUv = input.texcoord +
                direction * inverseResolution * max(radius, 1.0f) * stepRate;
            const float sampleDepth = gDepthTexture.Sample(gLinearSampler, sampleUv);
            const float4 sampleNormalValue = gNormalTexture.Sample(gLinearSampler, sampleUv);

            if (sampleDepth >= 0.9999f || sampleNormalValue.a <= 0.0f)
            {
                continue;
            }

            const float sampleLinearDepth = LinearizeDepth(sampleDepth);
            const float3 sampleNormal = normalize(sampleNormalValue.xyz * 2.0f - 1.0f);
            const float depthDelta = centerLinearDepth - sampleLinearDepth;
            const float depthRange = max(centerLinearDepth * 0.075f, 0.08f);
            const float rangeWeight = saturate(1.0f - abs(depthDelta) / depthRange);
            const float normalWeight = smoothstep(
                0.15f,
                0.92f,
                dot(centerNormal, sampleNormal));
            const float horizonWeight = smoothstep(
                max(bias, 0.0001f),
                max(bias, 0.0001f) + depthRange * 0.12f,
                depthDelta);
            const float distanceWeight = 1.0f / float(stepIndex);
            const float sampleWeight = rangeWeight * normalWeight * distanceWeight;
            occlusion += horizonWeight * sampleWeight;
            totalWeight += sampleWeight;
        }
    }

    const float ao = saturate(
        1.0f - occlusion / max(totalWeight, 0.0001f) * max(intensity, 0.0f));
    const float poweredAo = pow(ao, max(power, 0.01f));
    return float4(poweredAo.xxx, 1.0f);
}
