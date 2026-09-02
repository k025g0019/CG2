//============================================================
// Light Probe Bake: キューブキャプチャ -> SH9(球面調和)投影
//------------------------------------------------------------
// 1スレッドグループ = 1Probe。6面すべてのテクセルを立体角で
// 重み付けしながらSH係数へ積分する。
// 何にも当たらなかったテクセル(距離が遠方センチネル)は、その方向の
// 解析的な空を評価して使う。囲まれたProbeは空を一切拾わないので、
// 室内が正しく暗くなる。
//============================================================
#include "ProbeCommon.hlsli"
#include "../Common/SceneLightData.hlsli"
#include "../Common/AnalyticAtmosphere.hlsli"

cbuffer ProbeBakeConstants : register(b0)
{
    int gFaceSize;
    int gBaseProbeIndex;
    int gBatchProbeCount;
    float gHysteresis;
    float gFarDistance;
    int gSkyEnabled;
    int gVisibilityTilesPerRow;
    int gVisibilityTileSize;
    float gBakePadding0;
    float gBakePadding1;
    float gBakePadding2;
    float gBakePadding3;
};

ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);

Texture2D<float4> gCaptureRadiance : register(t0);
Texture2D<float> gCaptureDistance : register(t1);

RWStructuredBuffer<float4> gProbeShBuffer : register(u0);

#define CG2_PROBE_BAKE_THREAD_COUNT 64

groupshared float3 gsShAccumulation[CG2_PROBE_SH_COEFFICIENT_COUNT][CG2_PROBE_BAKE_THREAD_COUNT];

float3 EvaluateProbeSkyRadiance(float3 direction)
{
    if (gSkyEnabled == 0)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    DirectionalLightData sunLight = gDirectionalLight.lights[0];
    const float3 towardSun = normalize(-sunLight.direction);
    return EvaluateAnalyticAtmosphere(
        direction,
        towardSun,
        sunLight.skyUpperColor,
        sunLight.skyLowerColor,
        sunLight.skyIntensity,
        sunLight.skyEmission);
}

[numthreads(CG2_PROBE_BAKE_THREAD_COUNT, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    const int batchSlot = (int)groupId.x;
    const int faceSize = max(gFaceSize, 1);
    const int faceTexelCount = faceSize * faceSize;
    const int totalTexelCount = faceTexelCount * 6;

    float3 shAccumulation[CG2_PROBE_SH_COEFFICIENT_COUNT];

    [unroll]
    for (int clearIndex = 0; clearIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; clearIndex++)
    {
        shAccumulation[clearIndex] = float3(0.0f, 0.0f, 0.0f);
    }

    for (int texelIndex = (int)threadIndex; texelIndex < totalTexelCount; texelIndex += CG2_PROBE_BAKE_THREAD_COUNT)
    {
        const int faceIndex = texelIndex / faceTexelCount;
        const int faceLocalIndex = texelIndex - faceIndex * faceTexelCount;
        const int texelY = faceLocalIndex / faceSize;
        const int texelX = faceLocalIndex - texelY * faceSize;

        const float2 faceUv = (float2((float)texelX, (float)texelY) + 0.5f) / (float)faceSize;
        const float3 direction = GetProbeCubeFaceDirection((uint)faceIndex, faceUv);
        const float solidAngle = GetProbeCubeTexelSolidAngle(faceUv, (float)faceSize);

        const int2 atlasTexel = int2(
            faceIndex * faceSize + texelX,
            batchSlot * faceSize + texelY);
        const float capturedDistance = gCaptureDistance.Load(int3(atlasTexel, 0));

        // 何にも当たっていないテクセルは空とみなす。
        const bool isBackground = capturedDistance >= gFarDistance * 0.999f;
        const float3 radiance = isBackground
            ? EvaluateProbeSkyRadiance(direction)
            : gCaptureRadiance.Load(int3(atlasTexel, 0)).rgb;

        float shBasis[CG2_PROBE_SH_COEFFICIENT_COUNT];
        EvaluateProbeShBasis(direction, shBasis);

        [unroll]
        for (int coefficientIndex = 0; coefficientIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; coefficientIndex++)
        {
            shAccumulation[coefficientIndex] += radiance * shBasis[coefficientIndex] * solidAngle;
        }
    }

    [unroll]
    for (int storeIndex = 0; storeIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; storeIndex++)
    {
        gsShAccumulation[storeIndex][threadIndex] = shAccumulation[storeIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint stride = CG2_PROBE_BAKE_THREAD_COUNT / 2u; stride > 0u; stride >>= 1u)
    {
        if (threadIndex < stride)
        {
            [unroll]
            for (int reduceIndex = 0; reduceIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; reduceIndex++)
            {
                gsShAccumulation[reduceIndex][threadIndex] +=
                    gsShAccumulation[reduceIndex][threadIndex + stride];
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex != 0u)
    {
        return;
    }

    const int probeIndex = gBaseProbeIndex + batchSlot;
    const int writeBaseIndex = probeIndex * CG2_PROBE_SH_COEFFICIENT_COUNT;
    // 毎フレーム全Probeを焼き直せないため、前回値と補間して
    // ちらつきを抑えつつ徐々に収束させる。
    const float hysteresis = saturate(gHysteresis);

    [unroll]
    for (int writeIndex = 0; writeIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; writeIndex++)
    {
        const float3 previousCoefficient = gProbeShBuffer[writeBaseIndex + writeIndex].rgb;
        const float3 bakedCoefficient = gsShAccumulation[writeIndex][0];
        const float3 blendedCoefficient = lerp(bakedCoefficient, previousCoefficient, hysteresis);
        gProbeShBuffer[writeBaseIndex + writeIndex] = float4(blendedCoefficient, 0.0f);
    }
}
