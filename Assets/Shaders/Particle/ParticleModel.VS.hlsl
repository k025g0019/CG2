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

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    output.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    const uint aliveCount = gAliveList[0];
    if (instanceId >= aliveCount)
    {
        return output;
    }

    const ParticleData particle = gParticles[gAliveList[instanceId + 1u]];
    if ((uint)round(particle.rendering.x) != gParticleCamera.renderGroup)
    {
        return output;
    }

    const float lifeRate = saturate(particle.lifeSize.x / max(particle.lifeSize.y, 0.0001f));
    float4 particleColor = lerp(particle.startColor, particle.endColor, lifeRate);
    particleColor.rgb *= 1.0f + max(particle.rendering.y, 0.0f);
    const float sineRotation = sin(particle.motion2.y);
    const float cosineRotation = cos(particle.motion2.y);
    float3 localPosition = input.position.xyz * particle.velocitySize.w;
    localPosition.xz = float2(
        localPosition.x * cosineRotation - localPosition.z * sineRotation,
        localPosition.x * sineRotation + localPosition.z * cosineRotation);

    const float4 worldPosition = float4(localPosition + particle.positionLifetime.xyz, 1.0f);
    output.position = mul(worldPosition, gParticleCamera.viewProjection);
    output.texcoord = input.texcoord;
    output.color = particleColor;
    return output;
}
