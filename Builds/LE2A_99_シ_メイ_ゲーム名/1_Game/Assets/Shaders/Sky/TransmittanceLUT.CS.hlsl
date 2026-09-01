#include "SkyAtmosphereCommon.hlsli"

RWTexture2D<float4> gTransmittanceLut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    gTransmittanceLut.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float2 uv = (dispatchThreadId.xy + 0.5f) / float2(width, height);
    const float heightFactor = uv.y;
    const float3 transmittance = exp(-kRayleighScattering * (1.0f - heightFactor) * 8.0f);
    gTransmittanceLut[dispatchThreadId.xy] = float4(transmittance, 1.0f);
}
