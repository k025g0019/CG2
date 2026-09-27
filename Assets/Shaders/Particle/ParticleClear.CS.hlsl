#include "ParticleCommon.hlsli"

RWStructuredBuffer<ParticleData> gParticles : register(u0);
RWStructuredBuffer<uint> gAliveList : register(u1);
RWStructuredBuffer<uint> gDeadList : register(u2);

struct ParticleComputeCB
{
    float deltaTime;
    float globalDamping;
    uint maxParticleCount;
    uint commandValue; // 0: 件数だけ消去、1: 全ParticleをDeadへ戻す。
};

ConstantBuffer<ParticleComputeCB> gParticleCompute : register(b0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;

    if (particleIndex == 0u)
    {
        gAliveList[0] = 0u;
        gDeadList[0] = gParticleCompute.commandValue != 0u
            ? gParticleCompute.maxParticleCount
            : 0u;
    }

    if (gParticleCompute.commandValue == 0u || particleIndex >= gParticleCompute.maxParticleCount)
    {
        return;
    }

    ParticleData emptyParticle = (ParticleData)0;
    gParticles[particleIndex] = emptyParticle;
    gDeadList[particleIndex + 1u] = particleIndex;
}
