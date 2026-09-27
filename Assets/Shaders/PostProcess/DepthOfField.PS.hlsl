Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer DepthOfFieldConstants : register(b0)
{
    float focusDistance;
    float apertureScale;
    float nearClip;
    float farClip;
    float focalLengthMillimeters;
    float maximumBlurRadiusPixels;
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

float GetSignedCircleOfConfusionPixels(float linearDepth)
{
    const float focalScale = max(focalLengthMillimeters, 1.0f) / 50.0f;
    const float focusDelta =
        (linearDepth - max(focusDistance, 0.001f)) / max(linearDepth, 0.001f);
    return clamp(
        focusDelta * max(apertureScale, 0.0f) * focalScale * maximumBlurRadiusPixels,
        -maximumBlurRadiusPixels,
        maximumBlurRadiusPixels);
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

    const float centerDepth = LinearizeDepth(LoadDeviceDepth(input.texcoord));
    const float signedCenterCoc = GetSignedCircleOfConfusionPixels(centerDepth);
    const float centerRadiusPixels = abs(signedCenterCoc);

    if (centerRadiusPixels < 0.5f)
    {
        return float4(centerColor, 1.0f);
    }

    const float2 viewportMinimumUv = viewportOriginUv + inverseResolution * 0.5f;
    const float2 viewportMaximumUv =
        viewportOriginUv + viewportSizeUv - inverseResolution * 0.5f;
    const int tapCount = clamp((int)ceil(centerRadiusPixels * 0.75f), 4, 16);
    float3 accumulatedColor = centerColor;
    float accumulatedWeight = 1.0f;

    [loop]
    for (int tapIndex = 0; tapIndex < tapCount; tapIndex++)
    {
        const float tapRatio = (float(tapIndex) + 0.5f) / float(tapCount);
        const float angle = float(tapIndex) * 2.39996323f;
        float sineAngle;
        float cosineAngle;
        sincos(angle, sineAngle, cosineAngle);
        const float2 sampleOffset =
            float2(cosineAngle, sineAngle) *
            sqrt(tapRatio) *
            centerRadiusPixels *
            inverseResolution;
        const float2 sampleUv = clamp(
            input.texcoord + sampleOffset,
            viewportMinimumUv,
            viewportMaximumUv);
        const float sampleDepth = LinearizeDepth(LoadDeviceDepth(sampleUv));
        const float sampleCoc = abs(GetSignedCircleOfConfusionPixels(sampleDepth));
        const float relativeDepthDifference =
            abs(sampleDepth - centerDepth) / max(centerDepth, 0.1f);
        const float sameSurfaceWeight = exp2(-relativeDepthDifference * 48.0f);
        const float foregroundOccluder =
            sampleDepth + max(centerDepth * 0.01f, 0.02f) < centerDepth ? 1.0f : 0.0f;
        const float occlusionWeight =
            signedCenterCoc > 0.0f ? lerp(1.0f, 0.05f, foregroundOccluder) : 1.0f;
        const float cocWeight =
            0.25f + 0.75f * saturate(sampleCoc / max(centerRadiusPixels, 1.0f));
        const float sampleWeight = sameSurfaceWeight * occlusionWeight * cocWeight;
        accumulatedColor +=
            gSceneTexture.SampleLevel(gLinearSampler, sampleUv, 0.0f).rgb * sampleWeight;
        accumulatedWeight += sampleWeight;
    }

    const float3 blurredColor = accumulatedColor / max(accumulatedWeight, 0.0001f);
    const float blurBlend = saturate(centerRadiusPixels / max(maximumBlurRadiusPixels, 1.0f));
    return float4(lerp(centerColor, blurredColor, blurBlend), 1.0f);
}
