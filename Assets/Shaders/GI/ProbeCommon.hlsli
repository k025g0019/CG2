//============================================================
// Light Probe GI 共通定義
//------------------------------------------------------------
// Probe配置・SH9(球面調和 L2)・八面体マップの数式をまとめる。
// Bake側(Compute)と実行時参照側(Object3d.PS)の両方から使うため、
// リソース宣言は持たず、必要なものは引数で受け取る。
//============================================================
#ifndef CG2_PROBE_COMMON_HLSLI
#define CG2_PROBE_COMMON_HLSLI

// SH L2 = 9係数。C++側の kProbeShCoefficientCount と一致させる。
#define CG2_PROBE_SH_COEFFICIENT_COUNT 9
// 1Probeあたりの八面体可視性マップの1辺。C++側の kProbeVisibilityTileSize と一致させる。
#define CG2_PROBE_VISIBILITY_TILE_SIZE 16

static const float kProbePi = 3.14159265359f;

//------------------------------------------------------------
// Probeグリッドの定義。EmissiveLightArray(b2)の末尾に同じ並びで載せる。
//------------------------------------------------------------
struct LightProbeGridData
{
    float3 gridOrigin;    // 最小コーナーにあるProbeの中心座標
    float normalBias;     // 自己遮蔽を避けるため法線方向へ押し出す量(m)
    float3 gridSpacing;   // Probe間隔(m)
    float intensity;      // GIの強さ倍率。0ならGI無効
    int3 gridCounts;      // 各軸のProbe数
    int visibilityTilesPerRow;  // 可視性アトラス1行あたりのProbe数
    float visibilityInverseAtlasWidth;
    float visibilityInverseAtlasHeight;
    float probeGridPadding0;
    float probeGridPadding1;
};

//------------------------------------------------------------
// 八面体マップ: 方向 <-> [0,1]^2
//------------------------------------------------------------
float2 EncodeProbeOctahedral(float3 direction)
{
    const float3 normalized = direction / max(
        abs(direction.x) + abs(direction.y) + abs(direction.z),
        0.00001f);
    float2 octahedral = normalized.z >= 0.0f
        ? normalized.xy
        : (1.0f - abs(normalized.yx)) *
            float2(
                normalized.x >= 0.0f ? 1.0f : -1.0f,
                normalized.y >= 0.0f ? 1.0f : -1.0f);
    return octahedral * 0.5f + 0.5f;
}

float3 DecodeProbeOctahedral(float2 uv)
{
    const float2 f = uv * 2.0f - 1.0f;
    float3 direction = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    const float fold = saturate(-direction.z);
    direction.x += direction.x >= 0.0f ? -fold : fold;
    direction.y += direction.y >= 0.0f ? -fold : fold;
    return normalize(direction);
}

//------------------------------------------------------------
// キューブ6面のローカル基底。C++のキャプチャ用View行列と同じ並びにする。
// 面順は既存のPoint Lightキューブ影と同じ +X,-X,+Y,-Y,+Z,-Z。
//------------------------------------------------------------
void GetProbeCubeFaceBasis(
    uint faceIndex,
    out float3 faceForward,
    out float3 faceUp,
    out float3 faceRight)
{
    if (faceIndex == 0u) { faceForward = float3(1.0f, 0.0f, 0.0f); faceUp = float3(0.0f, 1.0f, 0.0f); }
    else if (faceIndex == 1u) { faceForward = float3(-1.0f, 0.0f, 0.0f); faceUp = float3(0.0f, 1.0f, 0.0f); }
    else if (faceIndex == 2u) { faceForward = float3(0.0f, 1.0f, 0.0f); faceUp = float3(0.0f, 0.0f, -1.0f); }
    else if (faceIndex == 3u) { faceForward = float3(0.0f, -1.0f, 0.0f); faceUp = float3(0.0f, 0.0f, 1.0f); }
    else if (faceIndex == 4u) { faceForward = float3(0.0f, 0.0f, 1.0f); faceUp = float3(0.0f, 1.0f, 0.0f); }
    else { faceForward = float3(0.0f, 0.0f, -1.0f); faceUp = float3(0.0f, 1.0f, 0.0f); }

    // 左手系。C++側の MakeLookAt と同じ向きに揃える。
    faceRight = normalize(cross(faceUp, faceForward));
}

// 面内UV(テクセル中心, [0,1]^2)からワールド方向を作る。
// vは下向きなのでNDCのyへ変換してから基底へ乗せる。
float3 GetProbeCubeFaceDirection(uint faceIndex, float2 faceUv)
{
    float3 faceForward;
    float3 faceUp;
    float3 faceRight;
    GetProbeCubeFaceBasis(faceIndex, faceForward, faceUp, faceRight);

    const float ndcX = faceUv.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - faceUv.y * 2.0f;
    return normalize(faceForward + faceRight * ndcX + faceUp * ndcY);
}

// 方向から面番号を選ぶ。GetProbeCubeFaceBasis と同じ並びであること。
uint GetProbeCubeFaceIndex(float3 direction)
{
    const float3 absDirection = abs(direction);

    if (absDirection.x >= absDirection.y && absDirection.x >= absDirection.z)
    {
        return direction.x >= 0.0f ? 0u : 1u;
    }

    if (absDirection.y >= absDirection.x && absDirection.y >= absDirection.z)
    {
        return direction.y >= 0.0f ? 2u : 3u;
    }

    return direction.z >= 0.0f ? 4u : 5u;
}

// 方向 -> 面内UV。GetProbeCubeFaceDirection の逆変換。
float2 GetProbeCubeFaceUv(uint faceIndex, float3 direction)
{
    float3 faceForward;
    float3 faceUp;
    float3 faceRight;
    GetProbeCubeFaceBasis(faceIndex, faceForward, faceUp, faceRight);

    const float forwardAmount = dot(direction, faceForward);

    if (forwardAmount <= 0.00001f)
    {
        return float2(0.5f, 0.5f);
    }

    const float ndcX = dot(direction, faceRight) / forwardAmount;
    const float ndcY = dot(direction, faceUp) / forwardAmount;
    return float2(ndcX * 0.5f + 0.5f, 0.5f - ndcY * 0.5f);
}

// キューブ面テクセルの立体角。視野90度なので (2/N)^2 / (1+s^2+t^2)^1.5。
float GetProbeCubeTexelSolidAngle(float2 faceUv, float faceSize)
{
    const float s = faceUv.x * 2.0f - 1.0f;
    const float t = 1.0f - faceUv.y * 2.0f;
    const float lengthSquared = 1.0f + s * s + t * t;
    const float texelArea = 4.0f / (faceSize * faceSize);
    return texelArea / (lengthSquared * sqrt(lengthSquared));
}

//------------------------------------------------------------
// SH L2 基底
//------------------------------------------------------------
void EvaluateProbeShBasis(float3 direction, out float shBasis[CG2_PROBE_SH_COEFFICIENT_COUNT])
{
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;

    shBasis[0] = 0.282094792f;
    shBasis[1] = 0.488602512f * y;
    shBasis[2] = 0.488602512f * z;
    shBasis[3] = 0.488602512f * x;
    shBasis[4] = 1.092548431f * x * y;
    shBasis[5] = 1.092548431f * y * z;
    shBasis[6] = 0.315391565f * (3.0f * z * z - 1.0f);
    shBasis[7] = 1.092548431f * x * z;
    shBasis[8] = 0.546274215f * (x * x - y * y);
}

// SH係数からLambert面の放射照度を復元する。
// Ramamoorthi-Hanrahan のコサインローブ畳み込み係数 (pi, 2pi/3, pi/4) を
// piで割った値を使い、既存のIrradiance Cubeと同じ「E/pi」の尺度で返す。
float3 EvaluateProbeShIrradiance(
    float3 shCoefficients[CG2_PROBE_SH_COEFFICIENT_COUNT],
    float3 normal)
{
    float shBasis[CG2_PROBE_SH_COEFFICIENT_COUNT];
    EvaluateProbeShBasis(normal, shBasis);

    const float bandWeight0 = 1.0f;
    const float bandWeight1 = 2.0f / 3.0f;
    const float bandWeight2 = 0.25f;

    float3 irradiance = shCoefficients[0] * shBasis[0] * bandWeight0;
    irradiance += (shCoefficients[1] * shBasis[1] +
        shCoefficients[2] * shBasis[2] +
        shCoefficients[3] * shBasis[3]) * bandWeight1;
    irradiance += (shCoefficients[4] * shBasis[4] +
        shCoefficients[5] * shBasis[5] +
        shCoefficients[6] * shBasis[6] +
        shCoefficients[7] * shBasis[7] +
        shCoefficients[8] * shBasis[8]) * bandWeight2;

    return max(irradiance, 0.0f);
}

//------------------------------------------------------------
// Probeインデックス計算
//------------------------------------------------------------
int GetProbeLinearIndex(int3 probeCoord, int3 gridCounts)
{
    return probeCoord.x +
        probeCoord.y * gridCounts.x +
        probeCoord.z * gridCounts.x * gridCounts.y;
}

float3 GetProbeWorldPosition(int3 probeCoord, LightProbeGridData grid)
{
    return grid.gridOrigin + float3(probeCoord) * grid.gridSpacing;
}

#endif
