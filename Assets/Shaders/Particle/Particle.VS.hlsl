#include "ParticleCommon.hlsli"

struct ParticleCamera
{
    row_major float4x4 viewProjection;
    uint renderGroup;
    float3 padding;
};

ConstantBuffer<ParticleCamera> gParticleCamera : register(b0);
StructuredBuffer<ParticleData> gParticles : register(t0);
StructuredBuffer<uint> gAliveList : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float particleStyle : TEXCOORD1;
    float4 color : COLOR0;
};

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    output.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
    output.texcoord = float2(0.0f, 0.0f);
    output.particleStyle = 0.0f;
    output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    const uint aliveCount = gAliveList[0];
    if (instanceId >= aliveCount)
    {
        return output;
    }

    const ParticleData particle = gParticles[gAliveList[instanceId + 1u]];
    if (particle.rendering.x >= 0.5f)
    {
        return output;
    }
    const float lifeRate = saturate(particle.lifeSize.x / max(particle.lifeSize.y, 0.0001f));
    float4 particleColor = lerp(particle.startColor, particle.endColor, lifeRate);
    particleColor.rgb *= 1.0f + max(particle.rendering.y, 0.0f);
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
    const float2 quadOffset = quadOffsets[vertexId] * particle.velocitySize.w;
    const float4 worldPosition = float4(
        particle.positionLifetime.x + quadOffset.x,
        particle.positionLifetime.y + quadOffset.y,
        particle.positionLifetime.z,
        1.0f);
    output.position = mul(worldPosition, gParticleCamera.viewProjection);
    output.texcoord = quadTexcoords[vertexId];
    output.particleStyle = particle.motion0.x;
    output.color = particleColor;
    return output;
}
