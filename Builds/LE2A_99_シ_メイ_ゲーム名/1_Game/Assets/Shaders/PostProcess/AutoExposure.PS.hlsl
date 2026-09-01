struct AutoExposureConstants
{
    float minimumExposure;
    float maximumExposure;
    float adaptationSpeed;
    float deltaTime;
    float targetLuminance;
    float historyValid;
    float2 viewportUvOrigin;
    float2 viewportUvSize;
    float minimumLogLuminance;
    float maximumLogLuminance;
    float lowPercentile;
    float highPercentile;
    float2 padding0;
};

ConstantBuffer<AutoExposureConstants> gAutoExposure : register(b0);
Texture2D<float4> gSourceTexture : register(t0);
Texture2D<float4> gPreviousExposureTexture : register(t1);
StructuredBuffer<uint> gHistogram : register(t2);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    uint totalSampleCount = 0u;

    [unroll]
    for (uint histogramIndex = 0u; histogramIndex < 256u; histogramIndex++)
    {
        totalSampleCount += gHistogram[histogramIndex];
    }

    const float lowSampleLimit =
        float(totalSampleCount) * saturate(gAutoExposure.lowPercentile);
    const float highSampleLimit =
        float(totalSampleCount) * saturate(gAutoExposure.highPercentile);
    float cumulativeSampleCount = 0.0f;
    float acceptedSampleCount = 0.0f;
    float weightedLogLuminance = 0.0f;

    [unroll]
    for (uint histogramIndex = 0u; histogramIndex < 256u; histogramIndex++)
    {
        const float binSampleCount = float(gHistogram[histogramIndex]);
        const float binBegin = cumulativeSampleCount;
        const float binEnd = cumulativeSampleCount + binSampleCount;
        const float acceptedBinSamples = max(
            min(binEnd, highSampleLimit) - max(binBegin, lowSampleLimit),
            0.0f);
        const float normalizedBinCenter =
            (float(histogramIndex) + 0.5f) / 256.0f;
        const float binLogLuminance = lerp(
            gAutoExposure.minimumLogLuminance,
            gAutoExposure.maximumLogLuminance,
            normalizedBinCenter);
        weightedLogLuminance += binLogLuminance * acceptedBinSamples;
        acceptedSampleCount += acceptedBinSamples;
        cumulativeSampleCount = binEnd;
    }

    const float averageLogLuminance = weightedLogLuminance /
        max(acceptedSampleCount, 1.0f);
    const float averageLuminance = exp2(averageLogLuminance);
    const float targetExposure = clamp(
        gAutoExposure.targetLuminance / max(averageLuminance, 0.0001f),
        gAutoExposure.minimumExposure,
        gAutoExposure.maximumExposure);
    const float previousExposure = gAutoExposure.historyValid >= 0.5f
        ? gPreviousExposureTexture.SampleLevel(gSampler, float2(0.5f, 0.5f), 0.0f).r
        : targetExposure;
    const float directionalAdaptationSpeed = targetExposure > previousExposure
        ? gAutoExposure.adaptationSpeed * 0.65f
        : gAutoExposure.adaptationSpeed * 1.35f;
    const float adaptation = 1.0f - exp(
        -max(directionalAdaptationSpeed, 0.0f) *
        max(gAutoExposure.deltaTime, 0.0f));
    const float exposure = lerp(previousExposure, targetExposure, saturate(adaptation));
    return float4(exposure, averageLuminance, targetExposure, 1.0f);
}
