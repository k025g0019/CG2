#include "ParticleCommon.hlsli"

RWStructuredBuffer<ParticleData> gParticles : register(u0);
RWStructuredBuffer<uint> gAliveList : register(u1);
RWStructuredBuffer<uint> gDeadList : register(u2);

struct ParticleUpdateCB
{
    float deltaTime;
    float globalDamping;
    uint maxParticleCount;
    float padding;
};

ConstantBuffer<ParticleUpdateCB> gParticleUpdate : register(b0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gParticleUpdate.maxParticleCount)
    {
        return;
    }

    ParticleData particle = gParticles[index];
    if (particle.positionLifetime.w <= 0.0f)
    {
        uint deadWriteIndex = 0u;
        InterlockedAdd(gDeadList[0], 1u, deadWriteIndex);
        gDeadList[deadWriteIndex + 1u] = index;
        return;
    }

    particle.lifeSize.x += gParticleUpdate.deltaTime;
    particle.positionLifetime.w -= gParticleUpdate.deltaTime;
    if (particle.positionLifetime.w <= 0.0f)
    {
        particle.positionLifetime.w = 0.0f;
        uint deadWriteIndex = 0u;
        InterlockedAdd(gDeadList[0], 1u, deadWriteIndex);
        gDeadList[deadWriteIndex + 1u] = index;
        gParticles[index] = particle;
        return;
    }

    const float dragRate = saturate(1.0f - (particle.physics.y + gParticleUpdate.globalDamping) * gParticleUpdate.deltaTime);
    const float noiseTime = particle.lifeSize.x * max(particle.physics.w, 0.0001f);
    const float3 noise = float3(
        sin(noiseTime + index * 0.37f),
        sin(noiseTime * 1.17f + index * 0.53f),
        sin(noiseTime * 0.83f + index * 0.91f)) * particle.physics.z;

    const uint motionType = (uint)round(particle.motion0.x);
    const float3 centerOffset = particle.positionLifetime.xyz - particle.motion1.xyz;
    const float centerDistance = max(length(centerOffset), 0.0001f);
    const float3 radialDirection = centerOffset / centerDistance;
    float3 motionAcceleration = float3(0.0f, 0.0f, 0.0f);

    // 軌道は中心からの半径を保ちながら接線方向へ進める。
    if (motionType == 1u)
    {
        const float3 tangent = normalize(cross(float3(0.0f, 1.0f, 0.0f), radialDirection) + float3(0.0001f, 0.0f, 0.0f));
        motionAcceleration += tangent * particle.motion0.y * centerDistance;
        motionAcceleration += radialDirection * particle.motion0.z;
    }
    // 渦は軌道運動へ上昇流を足し、煙・竜巻・魔法の渦を作る。
    else if (motionType == 2u)
    {
        const float3 tangent = normalize(cross(float3(0.0f, 1.0f, 0.0f), radialDirection) + float3(0.0001f, 0.0f, 0.0f));
        motionAcceleration += tangent * particle.motion0.y * max(centerDistance, 0.25f);
        motionAcceleration += radialDirection * particle.motion0.z;
        motionAcceleration.y += abs(particle.motion0.y) * 0.25f;
    }
    // 波は物理落下とは独立した周期運動として加える。
    else if (motionType == 3u)
    {
        motionAcceleration.y += sin(particle.lifeSize.x * particle.motion1.w * 6.2831853f + index * 0.37f) * particle.motion0.w;
    }
    // 吸引は指定中心へ距離に依存しない加速度を加える。
    else if (motionType == 4u)
    {
        motionAcceleration -= radialDirection * particle.motion2.x;
    }
    // 雲は乱流、上下波、弱い凝集を組み合わせて形を保ちながら漂わせる。
    else if (motionType == 5u)
    {
        motionAcceleration += noise * 1.75f;
        motionAcceleration.y += sin(particle.lifeSize.x * particle.motion1.w * 6.2831853f + index * 0.19f) * particle.motion0.w;
        motionAcceleration -= radialDirection * particle.motion2.x * saturate(centerDistance * 0.1f);
    }
    // 爆発は発生中心から外向きへ加速し、重力やDragとの併用も可能にする。
    else if (motionType == 6u)
    {
        motionAcceleration += radialDirection * max(abs(particle.motion0.z), 1.0f);
    }

    particle.velocitySize.xyz += (noise + motionAcceleration) * gParticleUpdate.deltaTime;
    particle.velocitySize.y -= particle.physics.x * gParticleUpdate.deltaTime;
    particle.velocitySize.xyz *= dragRate;
    particle.positionLifetime.xyz += particle.velocitySize.xyz * gParticleUpdate.deltaTime;
    particle.motion2.y += particle.motion2.z * gParticleUpdate.deltaTime;

    const float lifeRate = saturate(particle.lifeSize.x / max(particle.lifeSize.y, 0.0001f));
    particle.velocitySize.w = lerp(particle.lifeSize.z, particle.lifeSize.w, lifeRate);

    uint aliveWriteIndex = 0u;
    InterlockedAdd(gAliveList[0], 1u, aliveWriteIndex);
    gAliveList[aliveWriteIndex + 1u] = index;

    gParticles[index] = particle;
}
