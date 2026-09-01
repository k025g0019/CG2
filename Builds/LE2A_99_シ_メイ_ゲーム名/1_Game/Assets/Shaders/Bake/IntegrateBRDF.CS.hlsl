#include "../Common/Sampling.hlsli"
#include "../Common/PBRCommon.hlsli"

RWTexture2D<float2> gOutBRDFLUT : register(u0);

cbuffer BRDFLUTCB : register(b0)
{
    uint gOutputWidth;
    uint gOutputHeight;
    uint gSampleCount;
    uint gPadding;
};

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(max(1.0f - NdotV * NdotV, 0.0f));
    V.y = 0.0f;
    V.z = NdotV;
    float A = 0.0f;
    float B = 0.0f;
    float3 N = float3(0.0f, 0.0f, 1.0f);
    uint sampleCount = max(gSampleCount, 64u);

    for (uint i = 0u; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, roughness, N);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));
        if (NdotL > 0.0f)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-5f);
            float Fc = pow(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / float(sampleCount);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gOutputWidth || id.y >= gOutputHeight) return;
    float2 uv = (float2(id.xy) + 0.5f) / float2(gOutputWidth, gOutputHeight);
    float NdotV = saturate(uv.x);
    float roughness = saturate(uv.y);
    gOutBRDFLUT[id.xy] = IntegrateBRDF(NdotV, roughness);
}
