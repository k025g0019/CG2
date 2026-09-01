Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float2> gMotionTexture : register(t1);
Texture2D<float> gDepthTexture : register(t2);
SamplerState gLinearSampler : register(s0);

cbuffer MotionBlurConstants : register(b0)
{
    float blurStrength;
    float maximumSampleCount;
    float maximumVelocityPixels;
    float depthRejectionScale;
    float nearClip;
    float farClip;
    float2 inverseResolution;
    float2 viewportOriginUv;
    float2 viewportSizeUv;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float LinearizeDepth(float deviceDepth)
{
    return (nearClip * farClip) /
        max(farClip - deviceDepth * (farClip - nearClip), 0.0001f);
}

float LoadDeviceDepth(float2 uv)
{
    uint textureWidth;
    uint textureHeight;
    gDepthTexture.GetDimensions(textureWidth, textureHeight);
    const int2 maximumPixel = int2(textureWidth - 1u, textureHeight - 1u);
    const int2 pixelPosition = clamp(
        int2(uv * float2(textureWidth, textureHeight)),
        int2(0, 0),
        maximumPixel);
    return gDepthTexture.Load(int3(pixelPosition, 0));
}

bool IsInsideEffectViewport(float2 uv)
{
    const float2 viewportMaximumUv = viewportOriginUv + viewportSizeUv;
    return all(uv >= viewportOriginUv) && all(uv <= viewportMaximumUv);
}

float4 main(PSInput input) : SV_TARGET
{
    const float3 centerColor = gSceneTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f).rgb;

    if (!IsInsideEffectViewport(input.texcoord))
    {
        return float4(centerColor, 1.0f);
    }

    const float2 rawVelocity =
        gMotionTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f) * max(blurStrength, 0.0f);
    const float2 velocityPixels = rawVelocity / max(inverseResolution, 0.000001f.xx);
    const float velocityLengthPixels = length(velocityPixels);

    if (velocityLengthPixels < 0.5f)
    {
        return float4(centerColor, 1.0f);
    }

    const float clampedVelocityLength = min(velocityLengthPixels, maximumVelocityPixels);
    const float2 velocityUv =
        velocityPixels / max(velocityLengthPixels, 0.0001f) *
        clampedVelocityLength *
        inverseResolution;
    const int configuredTapCount = clamp((int)maximumSampleCount, 2, 16);
    const int tapCount = clamp((int)ceil(clampedVelocityLength * 0.5f), 2, configuredTapCount);
    const float centerDepth = LinearizeDepth(LoadDeviceDepth(input.texcoord));
    const float2 viewportMinimumUv = viewportOriginUv + inverseResolution * 0.5f;
    const float2 viewportMaximumUv =
        viewportOriginUv + viewportSizeUv - inverseResolution * 0.5f;
    float3 accumulatedColor = 0.0f;
    float accumulatedWeight = 0.0f;

    [loop]
    for (int tapIndex = 0; tapIndex < tapCount; tapIndex++)
    {
        const float tapRatio = tapCount <= 1
            ? 0.0f
            : float(tapIndex) / float(tapCount - 1);
        const float centeredRatio = tapRatio - 0.5f;
        const float2 sampleUv = clamp(
            input.texcoord + velocityUv * centeredRatio,
            viewportMinimumUv,
            viewportMaximumUv);
        const float sampleDepth = LinearizeDepth(LoadDeviceDepth(sampleUv));
        const float relativeDepthDifference =
            abs(sampleDepth - centerDepth) / max(centerDepth, 0.1f);
        const float depthWeight = exp2(-relativeDepthDifference * max(depthRejectionScale, 1.0f));
        const float shutterWeight = 1.0f - abs(centeredRatio) * 0.65f;
        const float sampleWeight = depthWeight * shutterWeight;
        accumulatedColor +=
            gSceneTexture.SampleLevel(gLinearSampler, sampleUv, 0.0f).rgb * sampleWeight;
        accumulatedWeight += sampleWeight;
    }

    return float4(accumulatedColor / max(accumulatedWeight, 0.0001f), 1.0f);
}
