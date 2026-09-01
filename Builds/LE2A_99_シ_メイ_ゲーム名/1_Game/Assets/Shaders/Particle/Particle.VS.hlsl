#include "ParticleCommon.hlsli"

struct ParticleCamera
{
    row_major float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
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
    float3 billboardRight = normalize(gParticleCamera.cameraRight.xyz);
    float3 billboardUp = normalize(gParticleCamera.cameraUp.xyz);
    float stretch = 1.0f;
    const uint billboardMode = (uint)round(particle.orientation.x);

    if (billboardMode == 1u)
    {
        const float3 flattenedRight = float3(billboardRight.x, 0.0f, billboardRight.z);
        billboardRight = dot(flattenedRight, flattenedRight) > 0.000001f
            ? normalize(flattenedRight)
            : float3(1.0f, 0.0f, 0.0f);
        billboardUp = float3(0.0f, 1.0f, 0.0f);
    }
    else if (billboardMode == 2u)
    {
        const float2 screenVelocity = float2(
            dot(particle.velocitySize.xyz, billboardRight),
            dot(particle.velocitySize.xyz, billboardUp));

        if (dot(screenVelocity, screenVelocity) > 0.000001f)
        {
            const float2 velocityAxis = normalize(screenVelocity);
            const float3 cameraRight = billboardRight;
            const float3 cameraUp = billboardUp;
            billboardRight = normalize(cameraRight * velocityAxis.y - cameraUp * velocityAxis.x);
            billboardUp = normalize(cameraRight * velocityAxis.x + cameraUp * velocityAxis.y);
        }

        stretch = max(particle.orientation.y, 0.01f);
    }
    else if (billboardMode == 3u)
    {
        billboardRight = float3(1.0f, 0.0f, 0.0f);
        billboardUp = float3(0.0f, 1.0f, 0.0f);
    }

    float2 quadOffset = quadOffsets[vertexId] * particle.velocitySize.w;
    quadOffset.y *= stretch;
    const float sineRotation = sin(particle.motion2.y);
    const float cosineRotation = cos(particle.motion2.y);
    quadOffset = float2(
        quadOffset.x * cosineRotation - quadOffset.y * sineRotation,
        quadOffset.x * sineRotation + quadOffset.y * cosineRotation);
    const float3 worldPositionValue = particle.positionLifetime.xyz
        + billboardRight * quadOffset.x
        + billboardUp * quadOffset.y;
    const float4 worldPosition = float4(worldPositionValue, 1.0f);
    output.position = mul(worldPosition, gParticleCamera.viewProjection);
    output.texcoord = quadTexcoords[vertexId];
    output.particleStyle = particle.motion0.x;
    output.color = particleColor;
    return output;
}
