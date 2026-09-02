//============================================================
// Shadow Atlas サンプリング共通
//------------------------------------------------------------
// Sun(Cascade) / Point(Cube 6面) / Spot を同じAtlasから読む処理を
// 1か所へまとめる。描画パスごとに写経すると実装がずれるため、
// テクスチャとサンプラーは引数で受け取って共有する。
//============================================================
#ifndef CG2_SHADOW_SAMPLING_HLSLI
#define CG2_SHADOW_SAMPLING_HLSLI

#include "SoftShadow.hlsli"
#include "../Common/SceneLightData.hlsli"

// perspectiveNearClip が 0 以下なら平行投影(Sun/Spot)として従来の固定バイアスを使う。
// 正の値を渡すと透視投影(Point Lightのキューブ面)として深度依存バイアスへ切り替える。
float SampleShadowProjectionFromAtlas(
    Texture2D shadowMap,
    SamplerState shadowSampler,
    float3 worldPosition,
    float normalDotLight,
    row_major float4x4 shadowViewProjection,
    float4 atlasTransform,
    float perspectiveNearClip,
    float perspectiveFarClip)
{
    float4 shadowPosition = mul(float4(worldPosition, 1.0f), shadowViewProjection);
    if (shadowPosition.w <= 0.000001f) return 1.0f;
    float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f) return 1.0f;
    float2 atlasUv = shadowUv * atlasTransform.xy + atlasTransform.zw;
    float receiverDepth = saturate(shadowNdc.z);
    uint shadowMapWidth = 1u;
    uint shadowMapHeight = 1u;
    shadowMap.GetDimensions(shadowMapWidth, shadowMapHeight);
    const float2 texelSize = rcp(max(float2(shadowMapWidth, shadowMapHeight), 1.0f));
    const float2 tileMinimumUv = atlasTransform.zw + texelSize * 2.0f;
    const float2 tileMaximumUv =
        atlasTransform.zw +
        atlasTransform.xy -
        texelSize * 2.0f;
    float receiverBias = lerp(
        0.0020f,
        0.00035f,
        saturate(normalDotLight));

    if (perspectiveNearClip > 0.0f)
    {
        // 透視投影の深度は 1/z 分布なので、NDC上で一定のバイアスは
        // 遠方ではワールド換算で巨大なズレ(影が対象から浮いて消える)になる。
        // ワールド距離で決めたバイアスを、その深度でのNDC変化率へ変換して使う。
        const float worldBias = lerp(0.055f, 0.014f, saturate(normalDotLight));
        const float viewDepth = max(shadowPosition.w, 0.0001f);
        const float ndcPerWorldUnit =
            (perspectiveNearClip * perspectiveFarClip) /
            (max(perspectiveFarClip - perspectiveNearClip, 0.0001f) * viewDepth * viewDepth);
        receiverBias = clamp(worldBias * ndcPerWorldUnit, 0.00002f, 0.01f);
    }

    return SampleSoftShadow9Tap(
        shadowMap,
        shadowSampler,
        atlasUv,
        receiverDepth - receiverBias,
        texelSize,
        1.35f,
        tileMinimumUv,
        tileMaximumUv);
}

float SampleShadowAtlasForLight(
    Texture2D shadowMap,
    SamplerState shadowSampler,
    float3 worldPosition,
    float normalDotLight,
    DirectionalLightData light)
{
    if (light.shadowEnabled < 0.5f)
    {
        return 1.0f;
    }

    const bool usesCascadedShadow =
        light.lightType == 0 &&
        light.shadowCascadeCount > 1.5f;

    const bool usesCubeShadow =
        light.lightType == 1 &&
        light.shadowCascadeCount > 1.5f;

    if (usesCubeShadow)
    {
        // Point Lightは6面(Cube)分のシャドウをAtlasへ配置しているため、
        // 光源からピクセルへ向かうベクトルの最大成分軸で面を選ぶ。
        const float3 lightToPixel = worldPosition - light.position;
        const float3 absDirection = abs(lightToPixel);
        uint faceIndex = 0u;
        if (absDirection.x >= absDirection.y && absDirection.x >= absDirection.z)
        {
            faceIndex = lightToPixel.x >= 0.0f ? 0u : 1u;
        }
        else if (absDirection.y >= absDirection.x && absDirection.y >= absDirection.z)
        {
            faceIndex = lightToPixel.y >= 0.0f ? 2u : 3u;
        }
        else
        {
            faceIndex = lightToPixel.z >= 0.0f ? 4u : 5u;
        }

        // Point Lightでは shadowCascadeSplits に Near/Far を詰めて渡している。
        return SampleShadowProjectionFromAtlas(
            shadowMap,
            shadowSampler,
            worldPosition,
            normalDotLight,
            light.shadowCascadeVP[faceIndex],
            light.shadowCascadeAtlas[faceIndex],
            light.shadowCascadeSplits.x,
            light.shadowCascadeSplits.y);
    }

    if (!usesCascadedShadow)
    {
        const float4 atlasTransform = float4(
            light.shadowTileUvScaleX,
            light.shadowTileUvScaleY,
            light.shadowTileUvBiasX,
            light.shadowTileUvBiasY);
        return SampleShadowProjectionFromAtlas(
            shadowMap,
            shadowSampler,
            worldPosition,
            normalDotLight,
            light.shadowVP,
            atlasTransform,
            0.0f,
            0.0f);
    }

    const float cameraDistance = length(worldPosition - light.cameraPosition);
    uint cascadeIndex = 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
    cascadeIndex = min(cascadeIndex, 3u);

    const float currentShadow = SampleShadowProjectionFromAtlas(
        shadowMap,
        shadowSampler,
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex],
        light.shadowCascadeAtlas[cascadeIndex],
        0.0f,
        0.0f);

    if (cascadeIndex >= 3u)
    {
        return currentShadow;
    }

    const float cascadeNearDistance = cascadeIndex == 0u
        ? 0.0f
        : light.shadowCascadeSplits[cascadeIndex - 1u];
    const float cascadeFarDistance = light.shadowCascadeSplits[cascadeIndex];
    const float blendStartDistance = lerp(cascadeNearDistance, cascadeFarDistance, 0.88f);
    const float cascadeBlend = saturate(
        (cameraDistance - blendStartDistance) /
        max(cascadeFarDistance - blendStartDistance, 0.0001f));

    if (cascadeBlend <= 0.0f)
    {
        return currentShadow;
    }

    const float nextShadow = SampleShadowProjectionFromAtlas(
        shadowMap,
        shadowSampler,
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex + 1u],
        light.shadowCascadeAtlas[cascadeIndex + 1u],
        0.0f,
        0.0f);
    return lerp(currentShadow, nextShadow, cascadeBlend);
}

#endif
