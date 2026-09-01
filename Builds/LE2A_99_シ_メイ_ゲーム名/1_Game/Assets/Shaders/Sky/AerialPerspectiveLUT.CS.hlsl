#include "SkyAtmosphereCommon.hlsli"

RWTexture2D<float4> gAerialPerspectiveLut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    gAerialPerspectiveLut.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float2 uv = (dispatchThreadId.xy + 0.5f) / float2(width, height);
    const float distanceFactor = uv.x;
    const float3 fogColor = lerp(float3(0.0f, 0.0f, 0.0f), float3(0.45f, 0.6f, 0.8f), distanceFactor);
    gAerialPerspectiveLut[dispatchThreadId.xy] = float4(fogColor, distanceFactor);
}
