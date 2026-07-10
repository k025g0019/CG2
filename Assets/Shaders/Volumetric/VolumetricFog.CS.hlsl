#include "VolumetricCommon.hlsli"

RWTexture2D<float4> gFogVolume : register(u0);

struct VolumetricFogCB
{
    float density;
    float scattering;
    float phase;
    float padding0;
    float3 fogColor;
    float padding1;
};

ConstantBuffer<VolumetricFogCB> gVolumetricFog : register(b0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const float depthRate = dispatchThreadId.y / 256.0f;
    const float fogFactor = ComputeFogFactor(depthRate * 100.0f, gVolumetricFog.density);
    gFogVolume[dispatchThreadId.xy] = float4(gVolumetricFog.fogColor * fogFactor * gVolumetricFog.scattering, fogFactor);
}
