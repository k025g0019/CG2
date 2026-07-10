#include "ParticleCommon.hlsli"

RWStructuredBuffer<ParticleData> gParticles : register(u0);

struct ParticleUpdateCB
{
    float deltaTime;
    float damping;
    float padding0;
    float padding1;
};

ConstantBuffer<ParticleUpdateCB> gParticleUpdate : register(b0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ParticleData particle = gParticles[dispatchThreadId.x];
    particle.position += particle.velocity * gParticleUpdate.deltaTime;
    particle.velocity *= max(1.0f - gParticleUpdate.damping * gParticleUpdate.deltaTime, 0.0f);
    particle.lifetime -= gParticleUpdate.deltaTime;
    gParticles[dispatchThreadId.x] = particle;
}
