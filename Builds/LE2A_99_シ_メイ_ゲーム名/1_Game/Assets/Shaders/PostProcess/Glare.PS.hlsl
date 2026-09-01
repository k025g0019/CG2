Texture2D<float4> gHighlightTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer GlareConstants : register(b0)
{
    float2 gInverseResolution;
    float gGlareMode;
    float gIntensity;
    float gSize;
    float gAngle;
    float gStreakCount;
    float gFade;
    float gColorModulation;
    float2 gCenter;
    float gPreserveSource;
    float3 gTintColor;
    float gReserved0;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float kPi = 3.1415926535f;

float3 SampleHighlight(float2 uv)
{
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        return 0.0f;
    }

    return max(gHighlightTexture.SampleLevel(gLinearSampler, uv, 0.0f).rgb, 0.0f);
}

float3 SampleChromatic(float2 uv, float2 direction, float amount)
{
    const float2 colorOffset = direction * amount * gInverseResolution;
    return float3(
        SampleHighlight(uv + colorOffset).r,
        SampleHighlight(uv).g,
        SampleHighlight(uv - colorOffset).b);
}

float3 EvaluateGhosts(float2 uv)
{
    const float2 fromCenter = uv - gCenter;
    float3 glare = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int ghostIndex = 0; ghostIndex < 6; ghostIndex++)
    {
        const float ghostRate = (float(ghostIndex) + 1.0f) / 6.0f;
        const float ghostScale = lerp(-0.35f, -2.25f, ghostRate);
        const float2 ghostUv = gCenter + fromCenter * ghostScale;
        const float radialMask = pow(saturate(1.0f - length(ghostUv - gCenter) * 1.25f), 2.0f);
        const float weight = pow(saturate(gFade), float(ghostIndex));
        glare += SampleChromatic(
            ghostUv,
            normalize(fromCenter + float2(0.0001f, 0.0f)),
            gColorModulation * (1.0f + ghostRate * 4.0f)) * radialMask * weight;
        totalWeight += weight;
    }

    return glare / max(totalWeight, 0.0001f);
}

float3 EvaluateStreaks(float2 uv, bool isSimpleStar)
{
    const int directionCount = isSimpleStar ? clamp(int(gStreakCount), 4, 8) : clamp(int(gStreakCount), 2, 4);
    const int stepCount = isSimpleStar ? 8 : 22;
    const float stepWidth = isSimpleStar ? 1.15f : 3.25f;
    float3 glare = SampleHighlight(uv);
    float totalWeight = 1.0f;

    [loop]
    for (int directionIndex = 0; directionIndex < 8; directionIndex++)
    {
        if (directionIndex >= directionCount)
        {
            break;
        }

        const float directionAngle = isSimpleStar
            ? gAngle + kPi * 2.0f * float(directionIndex) / float(directionCount)
            : gAngle + kPi * float(directionIndex) / float(directionCount);
        const float2 direction = float2(cos(directionAngle), sin(directionAngle));

        [loop]
        for (int stepIndex = 1; stepIndex <= 16; stepIndex++)
        {
            if (stepIndex > stepCount)
            {
                break;
            }

            const float stepRate = float(stepIndex) / float(stepCount);
            const float weight = pow(saturate(gFade), float(stepIndex)) * (1.0f - stepRate * (isSimpleStar ? 0.65f : 0.25f));
            const float2 offset = direction * gInverseResolution * gSize * float(stepIndex) * stepWidth;
            const float colorShift = gColorModulation * float(stepIndex);
            glare += SampleChromatic(uv + offset, direction, colorShift) * weight;
            glare += SampleChromatic(uv - offset, -direction, colorShift) * weight;
            totalWeight += weight * 2.0f;
        }
    }

    return glare / max(totalWeight, 0.0001f);
}

float3 EvaluateFogGlow(float2 uv)
{
    float3 glare = SampleHighlight(uv) * 2.0f;
    float totalWeight = 2.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < 32; sampleIndex++)
    {
        const float ring = 1.0f + float(sampleIndex / 8);
        const float angle = kPi * 2.0f * float(sampleIndex % 8) / 8.0f + ring * 0.37f;
        const float2 direction = float2(cos(angle), sin(angle));
        const float weight = exp2(-ring * 0.85f);
        const float2 offset = direction * gInverseResolution * gSize * ring * 3.0f;
        glare += SampleHighlight(uv + offset) * weight;
        totalWeight += weight;
    }

    return glare / max(totalWeight, 0.0001f);
}

float3 EvaluateSunBeams(float2 uv)
{
    const float2 ray = gCenter - uv;
    const float distanceToCenter = length(ray);
    const float centerMask = pow(saturate(1.0f - distanceToCenter * 1.8f), 2.0f);
    float3 glare = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < 24; sampleIndex++)
    {
        const float sampleRate = float(sampleIndex) / 23.0f;
        const float weight = pow(saturate(gFade), float(sampleIndex)) * (1.0f - sampleRate);
        const float beamLength = saturate(gSize * 0.18f);
        const float2 sampleUv = uv + ray * sampleRate * beamLength;
        glare += SampleHighlight(sampleUv) * weight;
        totalWeight += weight;
    }

    return glare / max(totalWeight, 0.0001f) * centerMask;
}

float3 EvaluateKernel(float2 uv)
{
    const float2 texel = gInverseResolution * gSize;
    const float3 center = SampleHighlight(uv) * 4.0f;
    const float3 axis =
        SampleHighlight(uv + float2(texel.x, 0.0f)) +
        SampleHighlight(uv - float2(texel.x, 0.0f)) +
        SampleHighlight(uv + float2(0.0f, texel.y)) +
        SampleHighlight(uv - float2(0.0f, texel.y));
    const float3 diagonal =
        SampleHighlight(uv + texel) +
        SampleHighlight(uv - texel) +
        SampleHighlight(uv + float2(texel.x, -texel.y)) +
        SampleHighlight(uv + float2(-texel.x, texel.y));
    return (center + axis * 2.0f + diagonal) / 16.0f;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const int glareMode = int(gGlareMode + 0.5f);
    float3 glare = 0.0f;

    if (glareMode == 2)
    {
        glare = EvaluateGhosts(input.texcoord);
    }
    else if (glareMode == 3)
    {
        glare = EvaluateStreaks(input.texcoord, false);
    }
    else if (glareMode == 4)
    {
        glare = EvaluateFogGlow(input.texcoord);
    }
    else if (glareMode == 5)
    {
        glare = EvaluateStreaks(input.texcoord, true);
    }
    else if (glareMode == 6)
    {
        glare = EvaluateSunBeams(input.texcoord);
    }
    else if (glareMode == 7)
    {
        glare = EvaluateKernel(input.texcoord);
    }

    const float3 sourceColor = SampleHighlight(input.texcoord) * saturate(gPreserveSource);
    return float4(max(sourceColor + glare * gIntensity * max(gTintColor, 0.0f), 0.0f), 1.0f);
}
