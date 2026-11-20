//========================================
// ライトの計算
//========================================

#ifndef MANOENGINE_LIGHTING_HLSLI
#define MANOENGINE_LIGHTING_HLSLI

//----------------------------------------
// 定数
//----------------------------------------

// LightGroup.h の kMaxPointLights / kMaxSpotLights と必ず揃える。
// ずれると配列の読み出し位置が狂い、別の光の値を読む。
#define MANO_MAX_POINT_LIGHTS 16
#define MANO_MAX_SPOT_LIGHTS 8

//----------------------------------------
// ライトのデータ
//----------------------------------------

// LightTypes.h の各構造体と並びを合わせる。
// HLSL は配列の要素を 16 byte 境界へ置くため、padding を消してはいけない。

struct DirectionalLightData
{
    float4 color;
    float3 direction; // 光が進む向き。正規化済み
    float intensity;
    int isEnabled;
    float3 padding;
};

struct PointLightData
{
    float4 color;
    float3 position;
    float intensity;
    float range;
    float decay;
    int isEnabled;
    float padding;
};

struct SpotLightData
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float range;
    float decay;
    float cosOuterAngle;
    float cosInnerAngle;
    int isEnabled;
};

struct AmbientLightData
{
    float3 color;
    float intensity;
};

struct LightGroupData
{
    DirectionalLightData directional;
    PointLightData pointLights[MANO_MAX_POINT_LIGHTS];
    SpotLightData spotLights[MANO_MAX_SPOT_LIGHTS];

    int pointLightCount;
    int spotLightCount;
    float2 padding;

    AmbientLightData ambient;
};

//----------------------------------------
// 拡散の強さ
//----------------------------------------

// lightingMode: 0=ライティングなし、1=Lambert、2=Half Lambert
//
// Lambert は光の当たらない側が完全に暗くなる。Half Lambert は裏側も
// 少し明るく残るため、形が読み取りやすい。
float ComputeDiffuseFactor(float3 normal, float3 toLight, int lightingMode)
{
    const float cosAngle = dot(normal, toLight);

    if (lightingMode == 1)
    {
        return saturate(cosAngle);
    }

    const float halfLambert = cosAngle * 0.5f + 0.5f;
    return halfLambert * halfLambert;
}

//----------------------------------------
// 距離による減衰
//----------------------------------------

// range を超えたら 0 になる。decay が大きいほど、光源の近くだけが明るくなる。
// 距離の比を先に 0〜1 へ丸めてから累乗するので、range の外で負にならない。
float ComputeDistanceAttenuation(float distanceToLight, float range, float decay)
{
    const float normalizedDistance = saturate(1.0f - distanceToLight / max(range, 0.0001f));
    return pow(normalizedDistance, max(decay, 0.0f));
}

//----------------------------------------
// 円錐による減衰
//----------------------------------------

// 内側の角度までは 1、外側で 0 になり、その間を滑らかにつなぐ。
// 余弦で比べているため、角度が大きいほど値は小さい。
float ComputeSpotAttenuation(float3 toSurface, float3 spotDirection, float cosOuter, float cosInner)
{
    const float cosAngle = dot(toSurface, spotDirection);

    // 内側と外側が同じ角度だと 0 除算になる。その場合は縁をくっきり出す。
    const float angleRange = cosInner - cosOuter;
    if (angleRange < 0.0001f)
    {
        return (cosAngle >= cosOuter) ? 1.0f : 0.0f;
    }

    const float ratio = saturate((cosAngle - cosOuter) / angleRange);
    return ratio * ratio;
}

//----------------------------------------
// すべての光を足し合わせる
//----------------------------------------

// shadow は平行光源だけに掛ける。影を落とせるのは平行光源だけのため。
// 戻り値は baseColor へ掛ける前の「光の量」。
float3 ComputeLighting(
    LightGroupData lights,
    float3 worldPosition,
    float3 normal,
    int lightingMode,
    float shadow)
{
    //------------------------------
    // 環境光
    //------------------------------

    // どの面にも一様に足す。影の中が真っ黒になるのを防ぐ。
    float3 lightAmount = lights.ambient.color * lights.ambient.intensity;

    //------------------------------
    // 平行光源
    //------------------------------

    if (lights.directional.isEnabled != 0)
    {
        // direction は光が進む向き。面から光源へ向かう向きは逆。
        const float3 toLight = -lights.directional.direction;
        const float diffuseFactor = ComputeDiffuseFactor(normal, toLight, lightingMode);

        lightAmount +=
            lights.directional.color.rgb * diffuseFactor * lights.directional.intensity * shadow;
    }

    //------------------------------
    // 点光源
    //------------------------------

    for (int pointIndex = 0; pointIndex < lights.pointLightCount; ++pointIndex)
    {
        const PointLightData pointLight = lights.pointLights[pointIndex];

        if (pointLight.isEnabled == 0)
        {
            continue;
        }

        const float3 offset = pointLight.position - worldPosition;
        const float distanceToLight = length(offset);

        // 範囲の外は計算を打ち切る。光が届かない面で減衰や内積を求めても無駄になる。
        if (distanceToLight > pointLight.range)
        {
            continue;
        }

        const float3 toLight = offset / max(distanceToLight, 0.0001f);
        const float diffuseFactor = ComputeDiffuseFactor(normal, toLight, lightingMode);
        const float attenuation =
            ComputeDistanceAttenuation(distanceToLight, pointLight.range, pointLight.decay);

        lightAmount += pointLight.color.rgb * diffuseFactor * pointLight.intensity * attenuation;
    }

    //------------------------------
    // スポットライト
    //------------------------------

    for (int spotIndex = 0; spotIndex < lights.spotLightCount; ++spotIndex)
    {
        const SpotLightData spotLight = lights.spotLights[spotIndex];

        if (spotLight.isEnabled == 0)
        {
            continue;
        }

        const float3 offset = spotLight.position - worldPosition;
        const float distanceToLight = length(offset);

        if (distanceToLight > spotLight.range)
        {
            continue;
        }

        const float3 toLight = offset / max(distanceToLight, 0.0001f);

        // 円錐の判定は「光源から面へ向かう向き」と「光の向き」を比べる。
        const float spotAttenuation = ComputeSpotAttenuation(
            -toLight,
            spotLight.direction,
            spotLight.cosOuterAngle,
            spotLight.cosInnerAngle);

        if (spotAttenuation <= 0.0f)
        {
            continue; // 円錐の外
        }

        const float diffuseFactor = ComputeDiffuseFactor(normal, toLight, lightingMode);
        const float distanceAttenuation =
            ComputeDistanceAttenuation(distanceToLight, spotLight.range, spotLight.decay);

        lightAmount +=
            spotLight.color.rgb * diffuseFactor * spotLight.intensity * distanceAttenuation * spotAttenuation;
    }

    return lightAmount;
}

#endif
