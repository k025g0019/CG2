#ifndef CG2_COMMON_NORMAL_TRANSFORM_HLSLI
#define CG2_COMMON_NORMAL_TRANSFORM_HLSLI

//========================================
// 法線の World 変換
//========================================

// 位置と同じ World 行列をそのまま法線へ掛けると、非一様スケール
// (scale = (2,1,1) のように軸ごとに倍率が違う場合) で面に垂直でなくなる。
// 引き伸ばした面の陰影が、引き伸ばしていない形状のものに見える。
// 正しくは逆転置行列 (M^-1)^T を掛ける。
//
// 逆行列の公式 M^-1 = adj(M) / det(M) より、行ベクトル規約
// (このエンジンの規約。v * M) では
//
//   (M^-1)^T の各行 = cross(r1, r2), cross(r2, r0), cross(r0, r1)
//
// になる (r0..r2 は World 上 3x3 の各行)。これは余因子行列そのもので、
// 本来の逆転置行列を det(M) 倍したもの。結果を正規化するのでスカラー倍の
// 1/det は省略できる。
//
// この式はせん断 (shear) を含む任意の可逆行列に対して正しい。
// 親子階層で非一様スケールと回転が交互に掛かるとせん断が生じるため、
// 行の長さで割る簡易版ではなく余因子を使う。
//
// 回転のみの場合は直交基底なので cross(r1,r2) == r0 となり、
// World をそのまま掛けたときと結果が一致する。既存の見た目は変わらない。
//
// 鏡像変換 (det < 0) では 1/det の符号を落としているため法線が反転する。
// 平面反射は CullMode を FRONT へ切り替えて巻き方向の反転を打ち消しており
// (docs/engine-internals.md の F-14)、法線も同時に反転するのが整合する。

float3 TransformNormalToWorld(float3 localNormal, float4x4 worldMatrix)
{
    //------------------------------
    // World 上 3x3 の各行を取り出す
    //------------------------------

    const float3 worldRow0 = worldMatrix[0].xyz;
    const float3 worldRow1 = worldMatrix[1].xyz;
    const float3 worldRow2 = worldMatrix[2].xyz;

    //------------------------------
    // 余因子行列 = 逆転置行列の det 倍
    //------------------------------

    const float3 cofactorRow0 = cross(worldRow1, worldRow2);
    const float3 cofactorRow1 = cross(worldRow2, worldRow0);
    const float3 cofactorRow2 = cross(worldRow0, worldRow1);

    //------------------------------
    // 行ベクトル規約での適用と正規化
    //------------------------------

    // 各行を法線成分で重み付けして足す (v * M と同じ形)。
    const float3 worldNormal =
        localNormal.x * cofactorRow0 +
        localNormal.y * cofactorRow1 +
        localNormal.z * cofactorRow2;

    // スケール 0 などで行列が退化するとゼロベクトルになる。
    // normalize は NaN を返すため、元の法線を返して破綻を避ける。
    const float worldNormalLengthSquared = dot(worldNormal, worldNormal);
    if (worldNormalLengthSquared <= 1e-20f)
    {
        return normalize(localNormal);
    }

    return worldNormal * rsqrt(worldNormalLengthSquared);
}

#endif
