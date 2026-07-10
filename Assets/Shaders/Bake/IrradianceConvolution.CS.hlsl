RWTexture2DArray<float4> gOutIrradiance : register(u0);
TextureCube gEnvironmentCube : register(t0);
SamplerState gLinearClamp : register(s0);

static const float PI = 3.14159265359f;

float3 TangentToWorld(float3 tangentSample, float3 N)
{
    float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return normalize(T * tangentSample.x + B * tangentSample.y + N * tangentSample.z);
}

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
    uint size = 64;
    if (id.x >= size || id.y >= size || id.z >= 6) return;

    float3 N = GetCubeDirection(id.z, id.xy, size);
    float3 irradiance = 0.0f;
    const uint SAMPLE_COUNT_PHI = 64;
    const uint SAMPLE_COUNT_THETA = 32;
    float sampleCount = 0.0f;

    for (uint phiIndex = 0; phiIndex < SAMPLE_COUNT_PHI; ++phiIndex)
    {
        float phi = 2.0f * PI * (float(phiIndex) + 0.5f) / float(SAMPLE_COUNT_PHI);
        for (uint thetaIndex = 0; thetaIndex < SAMPLE_COUNT_THETA; ++thetaIndex)
        {
            float theta = 0.5f * PI * (float(thetaIndex) + 0.5f) / float(SAMPLE_COUNT_THETA);
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            float3 tangentSample = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
            float3 sampleDir = TangentToWorld(tangentSample, N);
            float3 sampleColor = gEnvironmentCube.SampleLevel(gLinearClamp, sampleDir, 0.0f).rgb;
            irradiance += sampleColor * cosTheta * sinTheta;
            sampleCount += 1.0f;
        }
    }

    irradiance = PI * irradiance / max(sampleCount, 1.0f);
    gOutIrradiance[uint3(id.xy, id.z)] = float4(irradiance, 1.0f);
}
