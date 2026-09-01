#include "SkyAtmosphereCommon.hlsli"

RWTexture2D<float4> gMultiScatteringLut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    gMultiScatteringLut.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float2 uv = (dispatchThreadId.xy + 0.5f) / float2(width, height);
    const float horizonFactor = saturate(1.0f - uv.y);
    const float3 scattering = (kRayleighScattering + kMieScattering) * (0.5f + horizonFactor);
    gMultiScatteringLut[dispatchThreadId.xy] = float4(scattering, 1.0f);
}
