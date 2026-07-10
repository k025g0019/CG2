#include "SkyAtmosphereCommon.hlsli"

RWTexture2D<float4> gSkyViewLut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    gSkyViewLut.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float2 uv = (dispatchThreadId.xy + 0.5f) / float2(width, height);
    const float zenithFactor = saturate(uv.y);
    const float3 skyColor = lerp(float3(0.6f, 0.75f, 1.0f), float3(0.02f, 0.08f, 0.2f), zenithFactor);
    gSkyViewLut[dispatchThreadId.xy] = float4(skyColor, 1.0f);
}
