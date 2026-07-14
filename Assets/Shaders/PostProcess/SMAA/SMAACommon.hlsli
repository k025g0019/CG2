#ifndef CG2_SMAA_COMMON_HLSLI
#define CG2_SMAA_COMMON_HLSLI

float Luma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float SearchEdgeLength(
    Texture2D<float4> edgeTexture,
    SamplerState linearSampler,
    float2 startUv,
    float2 direction,
    float2 texelSize)
{
    float edgeLength = 0.0f;
    float2 sampleUv = startUv;

    [loop]
    for (int stepIndex = 0; stepIndex < 16; stepIndex++)
    {
        sampleUv += direction * texelSize;

        if (any(sampleUv <= 0.0f) || any(sampleUv >= 1.0f))
        {
            break;
        }

        const float2 edge = edgeTexture.SampleLevel(linearSampler, sampleUv, 0.0f).rg;

        if (dot(edge, edge) <= 0.0001f)
        {
            break;
        }

        edgeLength += 1.0f;
    }

    return edgeLength;
}

#endif
