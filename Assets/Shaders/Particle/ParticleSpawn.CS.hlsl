#include "ParticleCommon.hlsli"

RWStructuredBuffer<ParticleData> gParticles : register(u0);
RWStructuredBuffer<uint> gAliveList : register(u1);
RWStructuredBuffer<uint> gDeadList : register(u2);
StructuredBuffer<ParticleData> gSpawnParticles : register(t0);

struct ParticleComputeCB
{
    float deltaTime;
    float globalDamping;
    uint maxParticleCount;
    uint commandValue; // 発生要求数。
};

ConstantBuffer<ParticleComputeCB> gParticleCompute : register(b0);

bool TryAcquireDeadParticle(out uint particleIndex)
{
    particleIndex = 0u;

    // 複数Threadが同じDeadスロットを取らないよう、件数をCompareExchangeで減らす。
    while (true)
    {
        const uint deadCount = gDeadList[0];

        if (deadCount == 0u)
        {
            return false;
        }

        uint originalCount = 0u;
        InterlockedCompareExchange(gDeadList[0], deadCount, deadCount - 1u, originalCount);

        if (originalCount == deadCount)
        {
            particleIndex = gDeadList[deadCount];
            return particleIndex < gParticleCompute.maxParticleCount;
        }
    }
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint spawnIndex = dispatchThreadId.x;

    if (spawnIndex >= gParticleCompute.commandValue)
    {
        return;
    }

    uint particleIndex = 0u;

    if (!TryAcquireDeadParticle(particleIndex))
    {
        return;
    }

    gParticles[particleIndex] = gSpawnParticles[spawnIndex];

    uint aliveWriteIndex = 0u;
    InterlockedAdd(gAliveList[0], 1u, aliveWriteIndex);
    gAliveList[aliveWriteIndex + 1u] = particleIndex;
}
