// CPUのDirectionalLightと同じ配置を全描画パスで共有する。
#ifndef CG2_SCENE_LIGHT_DATA_HLSLI
#define CG2_SCENE_LIGHT_DATA_HLSLI

#include "../GI/ProbeCommon.hlsli"

struct DirectionalLightData
{
    float4 color;
    float3 direction;
    float intensity;
    float3 position;
    float range;
    float3 skyUpperColor;
    float skyIntensity;
    float3 skyLowerColor;
    float skyEmission;
    float ambientIntensity;
    float horizonSharpness;
    float reflectionIntensity;
    float spotCosInner;
    float spotCosOuter;
    int lightType;
    float areaRadius;
    float padding3; // cameraPositionを16バイト境界から始める。
    float3 cameraPosition;
    float environmentTextureEnabled;
    float environmentTextureIntensity;
    float environmentTextureRotation;
    float environmentTextureMipBias;
    float shadowTileIndex;
    float shadowTileUvScaleX;
    float shadowTileUvScaleY;
    float shadowTileUvBiasX;
    float shadowTileUvBiasY;
    float shadowEnabled;
    float volumetricIntensity;
    float volumetricAnisotropy;
    float volumetricDistance;
    row_major float4x4 shadowVP;
    float4 shadowCascadeSplits;
    float shadowCascadeCount;
    float shadowCascadePadding0;
    float shadowCascadePadding1;
    float shadowCascadePadding2;
    row_major float4x4 shadowCascadeVP[6];
    float4 shadowCascadeAtlas[6];
};

struct DirectionalLightArray
{
    DirectionalLightData lights[4];
};

//------------------------------------------------------------
// Emissive / SunPortal / Light Probe グリッドを載せる共有バッファ(b2)。
// C++の EmissiveLightArray と同じ並びにする。
//------------------------------------------------------------
struct EmissiveLightData
{
    float3 position;
    float intensity;
    float3 color;
    float range;
};

struct SunPortalLightData
{
    float3 position;
    float halfWidth;
    float3 outwardNormal;
    float halfHeight;
    float3 right;
    float range;
    float3 up;
    float spreadRate;
    float3 tint;
    float intensityScale;
};

struct EmissiveLightArray
{
    int count;
    float padding0;
    float padding1;
    float padding2;
    EmissiveLightData lights[8];
    int sunPortalCount;
    float sunPortalPadding0;
    float sunPortalPadding1;
    float sunPortalPadding2;
    SunPortalLightData sunPortals[4];
    LightProbeGridData probeGrid;
};

float CalculateSceneSpotAttenuation(
    float3 lightDirection,
    float3 spotForward,
    DirectionalLightData light)
{
    float spotCos = dot(lightDirection, normalize(spotForward));
    float innerCos = max(light.spotCosInner, light.spotCosOuter);
    float outerCos = min(light.spotCosInner, light.spotCosOuter);
    return saturate((spotCos - outerCos) / max(innerCos - outerCos, 0.0001f));
}

// ライト種別ごとの「面へ向かう方向」と「減衰後の強さ」を求める。
// Object3d の直接光と Light Probe のBakeで同じ結果になるよう共有する。
void BuildSceneLightInfo(
    float3 worldPosition,
    out float3 lightDirection,
    out float lightIntensity,
    DirectionalLightData light)
{
    lightIntensity = max(light.intensity, 0.0f);
    if (light.lightType == 0) {
        lightDirection = normalize(-light.direction);
        return;
    }
    float3 toLight = light.position - worldPosition;
    float distanceToLight = length(toLight);
    lightDirection = normalize(toLight);
    lightIntensity *= 1.0f / max(distanceToLight * distanceToLight, 0.35f);
    float range = max(light.range, 0.01f);
    float distanceRate = saturate(distanceToLight / range);
    lightIntensity *= 1.0f - pow(distanceRate, 4.0f);
    lightIntensity *= lightIntensity;
    if (light.lightType == 2)
        lightIntensity *= CalculateSceneSpotAttenuation(-lightDirection, light.direction, light);
}

#endif
