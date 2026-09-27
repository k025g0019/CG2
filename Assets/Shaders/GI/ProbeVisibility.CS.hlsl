//============================================================
// Light Probe Bake: キューブ距離キャプチャ -> 八面体の距離モーメント
//------------------------------------------------------------
// Probeごとに「その方向にある遮蔽物までの距離」の平均と2乗平均を持つ。
// 実行時にChebyshevの不等式で「Probeと対象点の間に壁があるか」を
// 判定し、壁越しの光漏れを防ぐ(DDGIのProbe Visibilityと同じ考え方)。
//============================================================
#include "ProbeCommon.hlsli"

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

Texture2D<float> gCaptureDistance : register(t1);

RWTexture2D<float2> gProbeVisibilityAtlas : register(u1);

// バッチ内スロットの距離キューブを、任意方向で1点読む。
float SampleCaptureDistance(int batchSlot, float3 direction)
{
    const int faceSize = max(gFaceSize, 1);
    const uint faceIndex = GetProbeCubeFaceIndex(direction);
    const float2 faceUv = GetProbeCubeFaceUv(faceIndex, direction);
    const int2 faceTexel = clamp(
        int2(faceUv * (float)faceSize),
        int2(0, 0),
        int2(faceSize - 1, faceSize - 1));
    const int2 atlasTexel = int2(
        (int)faceIndex * faceSize + faceTexel.x,
        batchSlot * faceSize + faceTexel.y);
    return gCaptureDistance.Load(int3(atlasTexel, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int tileSize = max(gVisibilityTileSize, 1);

    if ((int)dispatchThreadId.x >= tileSize || (int)dispatchThreadId.y >= tileSize)
    {
        return;
    }

    const int batchSlot = (int)dispatchThreadId.z;

    if (batchSlot >= gBatchProbeCount)
    {
        return;
    }

    const float2 octahedralUv =
        (float2(dispatchThreadId.xy) + 0.5f) / (float)tileSize;
    const float3 centerDirection = DecodeProbeOctahedral(octahedralUv);

    // 1テクセル分の広がりを持つ円錐でならし、分散(=遮蔽の曖昧さ)を作る。
    float3 tangent = abs(centerDirection.y) < 0.99f
        ? normalize(cross(float3(0.0f, 1.0f, 0.0f), centerDirection))
        : normalize(cross(float3(1.0f, 0.0f, 0.0f), centerDirection));
    const float3 bitangent = cross(centerDirection, tangent);
    const float coneRadius = 1.4f / (float)tileSize;

    float weightedDistance = 0.0f;
    float weightedSquaredDistance = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < 9; sampleIndex++)
    {
        float3 sampleDirection = centerDirection;

        if (sampleIndex > 0)
        {
            const float sampleAngle = (float)(sampleIndex - 1) * (6.28318530718f / 8.0f);
            sampleDirection = normalize(
                centerDirection +
                (tangent * cos(sampleAngle) + bitangent * sin(sampleAngle)) * coneRadius);
        }

        const float sampledDistance = SampleCaptureDistance(batchSlot, sampleDirection);
        const float sampleWeight = sampleIndex == 0 ? 2.0f : 1.0f;
        weightedDistance += sampledDistance * sampleWeight;
        weightedSquaredDistance += sampledDistance * sampledDistance * sampleWeight;
        totalWeight += sampleWeight;
    }

    const float meanDistance = weightedDistance / max(totalWeight, 0.0001f);
    const float meanSquaredDistance = weightedSquaredDistance / max(totalWeight, 0.0001f);

    const int probeIndex = gBaseProbeIndex + batchSlot;
    const int tilesPerRow = max(gVisibilityTilesPerRow, 1);
    const int tileX = probeIndex % tilesPerRow;
    const int tileY = probeIndex / tilesPerRow;
    const uint2 writeTexel = uint2(
        (uint)(tileX * tileSize) + dispatchThreadId.x,
        (uint)(tileY * tileSize) + dispatchThreadId.y);

    const float hysteresis = saturate(gHysteresis);
    const float2 previousMoments = gProbeVisibilityAtlas[writeTexel];
    const float2 bakedMoments = float2(meanDistance, meanSquaredDistance);
    gProbeVisibilityAtlas[writeTexel] = lerp(bakedMoments, previousMoments, hysteresis);
}
