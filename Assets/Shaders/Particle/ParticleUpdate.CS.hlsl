#include "ParticleCommon.hlsli"

RWStructuredBuffer<ParticleData> gParticles : register(u0);
RWStructuredBuffer<uint> gAliveList : register(u1);
RWStructuredBuffer<uint> gDeadList : register(u2);
Texture2D<float> gSceneDepth : register(t1);

struct ParticleCollisionProxy
{
    float4 centerType;
    float4 extent;
};

StructuredBuffer<ParticleCollisionProxy> gCollisionProxies : register(t2);

struct ParticleUpdateCB
{
    float deltaTime;
    float globalDamping;
    uint maxParticleCount;
    uint commandValue;
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
    float2 renderSize;
    float collisionThickness;
    float collisionPadding;
    float2 viewportOffset;
    float2 viewportSize;
    uint colliderCount;
    float3 collisionProxyPadding;
};

ConstantBuffer<ParticleUpdateCB> gParticleUpdate : register(b0);

float3 ReconstructWorldPosition(float2 localUv, float deviceDepth)
{
    const float2 ndc = float2(localUv.x * 2.0f - 1.0f, 1.0f - localUv.y * 2.0f);
    const float4 worldPosition = mul(
        float4(ndc, deviceDepth, 1.0f),
        gParticleUpdate.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

bool ResolveDepthCollision(
    float3 previousPosition,
    inout float3 currentPosition,
    inout float3 velocity,
    float particleSize,
    float bounce,
    float friction)
{
    const float4 previousClip = mul(float4(previousPosition, 1.0f), gParticleUpdate.viewProjection);
    const float4 currentClip = mul(float4(currentPosition, 1.0f), gParticleUpdate.viewProjection);

    if (previousClip.w <= 0.0f || currentClip.w <= 0.0f)
    {
        return false;
    }

    const float3 previousNdc = previousClip.xyz / previousClip.w;
    const float3 currentNdc = currentClip.xyz / currentClip.w;
    const float2 localUv = float2(currentNdc.x * 0.5f + 0.5f, 0.5f - currentNdc.y * 0.5f);

    if (any(localUv <= 0.0f) || any(localUv >= 1.0f))
    {
        return false;
    }

    const float2 screenUv =
        (gParticleUpdate.viewportOffset + localUv * gParticleUpdate.viewportSize) /
        max(gParticleUpdate.renderSize, 1.0f);
    const int2 pixelCoordinate = int2(screenUv * gParticleUpdate.renderSize);
    const float sceneDepth = gSceneDepth.Load(int3(pixelCoordinate, 0));

    if (sceneDepth >= 0.99999f ||
        currentNdc.z < sceneDepth - gParticleUpdate.collisionThickness ||
        previousNdc.z > sceneDepth + gParticleUpdate.collisionThickness)
    {
        return false;
    }

    const float3 scenePosition = ReconstructWorldPosition(localUv, sceneDepth);
    const float travelDistance = length(currentPosition - previousPosition);
    const float contactDistance = max(particleSize * 0.75f, travelDistance * 1.5f + 0.02f);

    if (distance(currentPosition, scenePosition) > contactDistance)
    {
        return false;
    }

    const float2 localTexelSize = rcp(max(gParticleUpdate.viewportSize, 1.0f));
    const float2 rightLocalUv = saturate(localUv + float2(localTexelSize.x, 0.0f));
    const float2 downLocalUv = saturate(localUv + float2(0.0f, localTexelSize.y));
    const float2 rightScreenUv =
        (gParticleUpdate.viewportOffset + rightLocalUv * gParticleUpdate.viewportSize) /
        max(gParticleUpdate.renderSize, 1.0f);
    const float2 downScreenUv =
        (gParticleUpdate.viewportOffset + downLocalUv * gParticleUpdate.viewportSize) /
        max(gParticleUpdate.renderSize, 1.0f);
    const float rightDepth = gSceneDepth.Load(int3(int2(rightScreenUv * gParticleUpdate.renderSize), 0));
    const float downDepth = gSceneDepth.Load(int3(int2(downScreenUv * gParticleUpdate.renderSize), 0));
    const float3 rightPosition = ReconstructWorldPosition(rightLocalUv, rightDepth);
    const float3 downPosition = ReconstructWorldPosition(downLocalUv, downDepth);
    float3 surfaceNormal = normalize(cross(downPosition - scenePosition, rightPosition - scenePosition));

    if (dot(surfaceNormal, velocity) > 0.0f)
    {
        surfaceNormal = -surfaceNormal;
    }

    const float normalVelocity = dot(velocity, surfaceNormal);
    const float3 tangentVelocity = velocity - surfaceNormal * normalVelocity;
    velocity =
        tangentVelocity * (1.0f - saturate(friction)) -
        surfaceNormal * normalVelocity * saturate(bounce);
    currentPosition = scenePosition + surfaceNormal * max(particleSize * 0.5f, 0.01f);
    return true;
}

float EvaluateCollisionProxyDistance(
    float3 worldPosition,
    ParticleCollisionProxy collisionProxy)
{
    const uint collisionType = (uint)round(collisionProxy.centerType.w);
    const float3 centerOffset = worldPosition - collisionProxy.centerType.xyz;

    if (collisionType == 1u)
    {
        return length(centerOffset) - max(collisionProxy.extent.x, 0.001f);
    }

    if (collisionType == 2u)
    {
        const float radius = max(collisionProxy.extent.x, 0.001f);
        const float halfSegment = max(collisionProxy.extent.y - radius, 0.0f);
        float3 capsuleOffset = centerOffset;
        capsuleOffset.y -= clamp(capsuleOffset.y, -halfSegment, halfSegment);
        return length(capsuleOffset) - radius;
    }

    const float3 boxExtent = max(collisionProxy.extent.xyz, 0.001f);
    const float3 boxDistance = abs(centerOffset) - boxExtent;
    return length(max(boxDistance, 0.0f)) +
        min(max(boxDistance.x, max(boxDistance.y, boxDistance.z)), 0.0f);
}

float3 EstimateCollisionProxyNormal(
    float3 worldPosition,
    ParticleCollisionProxy collisionProxy,
    float epsilon)
{
    const float3 offsetX = float3(epsilon, 0.0f, 0.0f);
    const float3 offsetY = float3(0.0f, epsilon, 0.0f);
    const float3 offsetZ = float3(0.0f, 0.0f, epsilon);
    const float3 gradient = float3(
        EvaluateCollisionProxyDistance(worldPosition + offsetX, collisionProxy) -
            EvaluateCollisionProxyDistance(worldPosition - offsetX, collisionProxy),
        EvaluateCollisionProxyDistance(worldPosition + offsetY, collisionProxy) -
            EvaluateCollisionProxyDistance(worldPosition - offsetY, collisionProxy),
        EvaluateCollisionProxyDistance(worldPosition + offsetZ, collisionProxy) -
            EvaluateCollisionProxyDistance(worldPosition - offsetZ, collisionProxy));
    return normalize(gradient + float3(0.0f, 0.00001f, 0.0f));
}

bool ResolveSdfCollision(
    inout float3 currentPosition,
    inout float3 velocity,
    float particleSize,
    float bounce,
    float friction)
{
    const float particleRadius = max(particleSize * 0.5f, 0.005f);
    float nearestDistance = 3.402823466e+38f;
    uint nearestColliderIndex = 0u;

    [loop]
    for (uint colliderIndex = 0u;
        colliderIndex < min(gParticleUpdate.colliderCount, 32u);
        colliderIndex++)
    {
        const float colliderDistance = EvaluateCollisionProxyDistance(
            currentPosition,
            gCollisionProxies[colliderIndex]);

        if (colliderDistance < nearestDistance)
        {
            nearestDistance = colliderDistance;
            nearestColliderIndex = colliderIndex;
        }
    }

    if (gParticleUpdate.colliderCount == 0u || nearestDistance > particleRadius)
    {
        return false;
    }

    const ParticleCollisionProxy collisionProxy = gCollisionProxies[nearestColliderIndex];
    const float normalEpsilon = max(particleRadius * 0.1f, 0.0025f);
    float3 surfaceNormal = EstimateCollisionProxyNormal(
        currentPosition,
        collisionProxy,
        normalEpsilon);

    const float normalVelocity = dot(velocity, surfaceNormal);
    const float3 tangentVelocity = velocity - surfaceNormal * normalVelocity;

    if (normalVelocity < 0.0f)
    {
        velocity =
            tangentVelocity * (1.0f - saturate(friction)) -
            surfaceNormal * normalVelocity * saturate(bounce);
    }

    currentPosition += surfaceNormal * (particleRadius - nearestDistance + 0.001f);
    return true;
}

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

    const float3 previousPosition = particle.positionLifetime.xyz;
    particle.velocitySize.xyz += (noise + motionAcceleration) * gParticleUpdate.deltaTime;
    particle.velocitySize.y -= particle.physics.x * gParticleUpdate.deltaTime;
    particle.velocitySize.xyz *= dragRate;
    particle.positionLifetime.xyz += particle.velocitySize.xyz * gParticleUpdate.deltaTime;

    const uint collisionMode = (uint)round(particle.rendering.z);

    if (collisionMode == 1u)
    {
        ResolveDepthCollision(
            previousPosition,
            particle.positionLifetime.xyz,
            particle.velocitySize.xyz,
            particle.velocitySize.w,
            particle.motion2.w,
            particle.rendering.w);
    }
    else if (collisionMode == 2u)
    {
        ResolveSdfCollision(
            particle.positionLifetime.xyz,
            particle.velocitySize.xyz,
            particle.velocitySize.w,
            particle.motion2.w,
            particle.rendering.w);
    }
    particle.motion2.y += particle.motion2.z * gParticleUpdate.deltaTime;

    const float lifeRate = saturate(particle.lifeSize.x / max(particle.lifeSize.y, 0.0001f));
    particle.velocitySize.w = lerp(particle.lifeSize.z, particle.lifeSize.w, lifeRate);

    uint aliveWriteIndex = 0u;
    InterlockedAdd(gAliveList[0], 1u, aliveWriteIndex);
    gAliveList[aliveWriteIndex + 1u] = index;

    gParticles[index] = particle;
}
