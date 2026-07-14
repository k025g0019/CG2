#ifndef CG2_HISTORY_CLAMP_HLSLI
#define CG2_HISTORY_CLAMP_HLSLI

void GetNeighborhoodBounds(
    Texture2D<float4> currentTexture,
    int2 pixelPosition,
    uint2 renderSize,
    out float3 minimumColor,
    out float3 maximumColor)
{
    minimumColor = float3(65504.0f, 65504.0f, 65504.0f);
    maximumColor = 0.0f;

    [unroll]
    for (int offsetY = -1; offsetY <= 1; offsetY++)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; offsetX++)
        {
            const int2 samplePosition = clamp(
                pixelPosition + int2(offsetX, offsetY),
                int2(0, 0),
                int2(renderSize) - 1);
            const float3 sampleColor = currentTexture.Load(int3(samplePosition, 0)).rgb;
            minimumColor = min(minimumColor, sampleColor);
            maximumColor = max(maximumColor, sampleColor);
        }
    }
}

#endif
