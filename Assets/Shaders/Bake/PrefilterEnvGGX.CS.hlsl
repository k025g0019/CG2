#include "../Common/Sampling.hlsli"
#include "../Common/PBRCommon.hlsli"

RWTexture2DArray<float4> gOutPrefilter : register(u0);
TextureCube gEnvironmentCube : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PrefilterCB : register(b0)
{
    uint gOutputSize;
    uint gMipIndex;
    uint gMipCount;
    uint gSampleCount;
};

float3 GetCubeDirection(uint face, uint2 pixel, uint size)
{
    float2 uv = (float2(pixel) + 0.5f) / float(size);
    uv = uv * 2.0f - 1.0f;
    uv.y = -uv.y;
    if (face == 0) return normalize(float3(1.0f, uv.y, -uv.x));
    if (face == 1) return normalize(float3(-1.0f, uv.y, uv.x));
    if (face == 2) return normalize(float3(uv.x, 1.0f, -uv.y));
    if (face == 3) return normalize(float3(uv.x, -1.0f, uv.y));
    if (face == 4) return normalize(float3(uv.x, uv.y, 1.0f));
    return normalize(float3(-uv.x, uv.y, -1.0f));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gOutputSize || id.y >= gOutputSize || id.z >= 6) return;

    float roughness = float(gMipIndex) / float(max(gMipCount - 1, 1));
    float3 N = GetCubeDirection(id.z, id.xy, gOutputSize);
    float3 R = N;
    float3 V = R;
    float3 prefilteredColor = 0.0f;
    float totalWeight = 0.0f;
    uint sampleCount = max(gSampleCount, 64u);

    for (uint i = 0u; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, roughness, N);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0f)
        {
            float3 sampleColor = gEnvironmentCube.SampleLevel(gLinearClamp, L, 0.0f).rgb;
            prefilteredColor += sampleColor * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, 1e-5f);
    gOutPrefilter[uint3(id.xy, id.z)] = float4(prefilteredColor, 1.0f);
}
