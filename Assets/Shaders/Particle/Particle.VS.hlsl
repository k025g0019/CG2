#include "ParticleCommon.hlsli"

struct ParticleCamera
{
    row_major float4x4 viewProjection;
};

ConstantBuffer<ParticleCamera> gParticleCamera : register(b0);
StructuredBuffer<ParticleData> gParticles : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    const ParticleData particle = gParticles[instanceId];
    const float2 quadOffsets[6] =
    {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, -1.0f),
        float2(1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f)
    };
    const float2 quadTexcoords[6] =
    {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 1.0f),
        float2(1.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f)
    };
    const float2 quadOffset = quadOffsets[vertexId] * particle.size;
    const float4 worldPosition = float4(particle.position.xy + quadOffset, particle.position.z, 1.0f);
    output.position = mul(worldPosition, gParticleCamera.viewProjection);
    output.texcoord = quadTexcoords[vertexId];
    output.color = particle.color;
    return output;
}
