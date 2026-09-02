//============================================================
// Light Probe GI 実行時サンプリング
//------------------------------------------------------------
// 周囲8個のProbeをトライリニア補間し、DDGI式の重み付けで
// 壁越しの光漏れを抑える:
//   ・背面重み  : Probeが面の裏側にあるほど寄与を下げる
//   ・可視性重み: 八面体距離マップのChebyshev不等式で、Probeと
//                 対象点の間に遮蔽物があるProbeを弾く
//============================================================
#ifndef CG2_PROBE_SAMPLING_HLSLI
#define CG2_PROBE_SAMPLING_HLSLI

#include "ProbeCommon.hlsli"

void LoadProbeShCoefficients(
    StructuredBuffer<float4> probeShBuffer,
    int probeIndex,
    out float3 shCoefficients[CG2_PROBE_SH_COEFFICIENT_COUNT])
{
    const int baseIndex = probeIndex * CG2_PROBE_SH_COEFFICIENT_COUNT;

    [unroll]
    for (int coefficientIndex = 0; coefficientIndex < CG2_PROBE_SH_COEFFICIENT_COUNT; coefficientIndex++)
    {
        shCoefficients[coefficientIndex] = probeShBuffer[baseIndex + coefficientIndex].rgb;
    }
}

// 八面体タイルを手動バイリニアで読む。
// タイル内でクランプするので、隣のProbeのタイルへ滲まない。
float2 SampleProbeVisibilityMoments(
    Texture2D<float2> probeVisibilityAtlas,
    LightProbeGridData grid,
    int probeIndex,
    float2 octahedralUv)
{
    const int tilesPerRow = max(grid.visibilityTilesPerRow, 1);
    const int tileX = probeIndex % tilesPerRow;
    const int tileY = probeIndex / tilesPerRow;
    const float tileSize = (float)CG2_PROBE_VISIBILITY_TILE_SIZE;

    const float2 texelPosition = octahedralUv * tileSize - 0.5f;
    const float2 baseTexel = floor(texelPosition);
    const float2 lerpFactor = texelPosition - baseTexel;

    float2 moments = float2(0.0f, 0.0f);

    [unroll]
    for (int cornerIndex = 0; cornerIndex < 4; cornerIndex++)
    {
        const float2 cornerOffset = float2(
            (float)(cornerIndex & 1),
            (float)((cornerIndex >> 1) & 1));
        const float2 clampedTexel = clamp(
            baseTexel + cornerOffset,
            float2(0.0f, 0.0f),
            float2(tileSize - 1.0f, tileSize - 1.0f));
        const int2 atlasTexel = int2(
            tileX * (int)tileSize + (int)clampedTexel.x,
            tileY * (int)tileSize + (int)clampedTexel.y);
        const float cornerWeight =
            (cornerOffset.x > 0.5f ? lerpFactor.x : 1.0f - lerpFactor.x) *
            (cornerOffset.y > 0.5f ? lerpFactor.y : 1.0f - lerpFactor.y);
        moments += probeVisibilityAtlas.Load(int3(atlasTexel, 0)) * cornerWeight;
    }

    return moments;
}

// 戻り値は既存のIrradiance Cubeと同じ「E/pi」の尺度。
// isValid が false の場合、呼び出し側は従来の環境光へフォールバックする。
float3 SampleLightProbeGi(
    StructuredBuffer<float4> probeShBuffer,
    Texture2D<float2> probeVisibilityAtlas,
    LightProbeGridData grid,
    float3 worldPosition,
    float3 normal,
    float3 viewDirection,
    out bool isValid)
{
    isValid = false;

    if (grid.intensity <= 0.0001f ||
        grid.gridCounts.x <= 0 ||
        grid.gridCounts.y <= 0 ||
        grid.gridCounts.z <= 0)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    // 自分自身の面で遮蔽判定が誤爆しないよう、法線と視線方向へ少し押し出す。
    const float3 biasedPosition =
        worldPosition +
        normal * grid.normalBias +
        viewDirection * (grid.normalBias * 0.8f);

    const float3 safeSpacing = max(grid.gridSpacing, float3(0.0001f, 0.0001f, 0.0001f));
    const float3 gridCoordinate = (biasedPosition - grid.gridOrigin) / safeSpacing;
    const int3 maxCoord = grid.gridCounts - int3(1, 1, 1);
    const int3 baseCoord = clamp(int3(floor(gridCoordinate)), int3(0, 0, 0), max(maxCoord - int3(1, 1, 1), int3(0, 0, 0)));
    const float3 lerpFactor = saturate(gridCoordinate - float3(baseCoord));

    float3 accumulatedIrradiance = float3(0.0f, 0.0f, 0.0f);
    float accumulatedWeight = 0.0f;

    for (int cornerIndex = 0; cornerIndex < 8; cornerIndex++)
    {
        const int3 cornerOffset = int3(
            cornerIndex & 1,
            (cornerIndex >> 1) & 1,
            (cornerIndex >> 2) & 1);
        const int3 probeCoord = clamp(baseCoord + cornerOffset, int3(0, 0, 0), maxCoord);
        const int probeIndex = GetProbeLinearIndex(probeCoord, grid.gridCounts);
        const float3 probeWorldPosition = GetProbeWorldPosition(probeCoord, grid);

        // トライリニア重み
        const float3 cornerLerp = float3(
            cornerOffset.x != 0 ? lerpFactor.x : 1.0f - lerpFactor.x,
            cornerOffset.y != 0 ? lerpFactor.y : 1.0f - lerpFactor.y,
            cornerOffset.z != 0 ? lerpFactor.z : 1.0f - lerpFactor.z);
        float weight = cornerLerp.x * cornerLerp.y * cornerLerp.z;

        // 背面重み: 面の裏側にあるProbeの寄与を滑らかに落とす。
        const float3 toProbe = probeWorldPosition - worldPosition;
        const float toProbeLength = length(toProbe);
        const float3 directionToProbe = toProbeLength > 0.0001f
            ? toProbe / toProbeLength
            : normal;
        const float wrappedCosine = dot(directionToProbe, normal) * 0.5f + 0.5f;
        weight *= wrappedCosine * wrappedCosine + 0.2f;

        // 可視性重み: Probeから対象点までの距離分布と実距離を比較する。
        const float3 probeToPoint = biasedPosition - probeWorldPosition;
        const float distanceToProbe = length(probeToPoint);

        if (distanceToProbe > 0.0001f)
        {
            const float2 octahedralUv = EncodeProbeOctahedral(probeToPoint / distanceToProbe);
            const float2 moments = SampleProbeVisibilityMoments(
                probeVisibilityAtlas,
                grid,
                probeIndex,
                octahedralUv);
            const float meanDistance = moments.x;

            if (distanceToProbe > meanDistance)
            {
                const float variance = max(moments.y - meanDistance * meanDistance, 0.0f);
                const float distanceDifference = distanceToProbe - meanDistance;
                float chebyshev = variance / (variance + distanceDifference * distanceDifference);
                // 3乗して、遮蔽されたProbeの残り火をはっきり落とす。
                chebyshev = chebyshev * chebyshev * chebyshev;
                weight *= max(chebyshev, 0.0f);
            }
        }

        // 重みが極端に小さい領域でノイズが出るため、下限を切ってから正規化する。
        const float kMinimumWeight = 0.0001f;

        if (weight < kMinimumWeight)
        {
            continue;
        }

        float3 shCoefficients[CG2_PROBE_SH_COEFFICIENT_COUNT];
        LoadProbeShCoefficients(probeShBuffer, probeIndex, shCoefficients);
        accumulatedIrradiance += EvaluateProbeShIrradiance(shCoefficients, normal) * weight;
        accumulatedWeight += weight;
    }

    if (accumulatedWeight <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    isValid = true;
    return (accumulatedIrradiance / accumulatedWeight) * grid.intensity;
}

#endif
