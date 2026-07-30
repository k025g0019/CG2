struct HistogramConstants
{
    uint sampleWidth;
    uint sampleHeight;
    float2 viewportUvOrigin;
    float2 viewportUvSize;
    float minimumLogLuminance;
    float maximumLogLuminance;
    float4 padding0;
};

ConstantBuffer<HistogramConstants> gHistogramConstants : register(b0);
Texture2D<float4> gInputTexture : register(t0);
RWStructuredBuffer<uint> gHistogram : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gHistogramConstants.sampleWidth ||
        dispatchThreadId.y >= gHistogramConstants.sampleHeight)
    {
        return;
    }

    uint sourceWidth = 1u;
    uint sourceHeight = 1u;
    gInputTexture.GetDimensions(sourceWidth, sourceHeight);
    const float2 sampleGridSize = max(
        float2(gHistogramConstants.sampleWidth, gHistogramConstants.sampleHeight),
        float2(1.0f, 1.0f));
    const float2 samplePosition =
        (float2(dispatchThreadId.xy) + 0.5f) / sampleGridSize;
    const float2 sampleUv = saturate(
        gHistogramConstants.viewportUvOrigin +
        samplePosition * gHistogramConstants.viewportUvSize);
    const uint2 sourcePixel = min(
        uint2(sampleUv * float2(sourceWidth, sourceHeight)),
        uint2(sourceWidth - 1u, sourceHeight - 1u));
    const float3 color = max(gInputTexture.Load(int3(sourcePixel, 0)).rgb, 0.0f);
    const float luminance = max(dot(color, float3(0.2126f, 0.7152f, 0.0722f)), 0.00001f);
    const float logLuminance = log2(luminance);
    const float normalizedLogLuminance = saturate(
        (logLuminance - gHistogramConstants.minimumLogLuminance) /
        max(
            gHistogramConstants.maximumLogLuminance -
            gHistogramConstants.minimumLogLuminance,
            0.0001f));
    const uint histogramIndex = min(
        (uint)(normalizedLogLuminance * 255.0f),
        255u);
    InterlockedAdd(gHistogram[histogramIndex], 1u);
}
