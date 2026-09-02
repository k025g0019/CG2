#include "../Common/AnalyticAtmosphere.hlsli"
#include "../Lighting/IBLRuntime.hlsli"
#include "../Shadow/SoftShadow.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    int useTexture;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
    float reflectionMode;
    float reflectionProbeIntensity;
    float reflectionReserved;
    float materialPadding0;
    float materialPadding1;
    float3 reflectionProbeCenter;
    float reflectionProbeBoxProjection;
    float3 reflectionProbeExtent;
    float materialPadding2;
    row_major float4x4 uvTransform;
    float normalScale;
    float ambientOcclusionStrength;
    float heightScale;
    float alphaCutoff;
    float clearCoat;
    float clearCoatRoughness;
    float transmission;
    float subsurface;
    float anisotropy;
    float anisotropyRotation;
    float specularTint;
    float sheen;
    float3 emissionColor;
    float sheenTint;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAmbientOcclusionMap;
    int useEmissionMap;
    int useHeightMap;
    int useOpacityMap;
    int alphaMode;
    int doubleSided;
    float materialExtensionPadding0;
    float materialExtensionPadding1;
    float materialExtensionPadding2;
    float2 uvTiling;
    float2 uvOffset;
    float oceanEnabled;
    float oceanFoamStrength;
    float oceanRoughness;
    float oceanColorBlendScale;
    float3 oceanDeepColor;
    float oceanPerPixelDisplacementStrength;
    float oceanDetailNormalStrength;
    float oceanFoamThreshold;
    float oceanAbsorptionDistance;
    float oceanRefractionDistortion;
    float oceanWaterDepth;
    float oceanCrestSharpness;
    float oceanPerPixelDisplacementSteps;
    float oceanPerPixelDisplacementDistance;
    int surfaceMode;
    float surfaceMaterialPadding0;
    float surfaceMaterialPadding1;
    float surfaceMaterialPadding2;
    // Ocean Sun Lighting / Glitter 調整項目（C++ 側 EditorCommonTypes.h の Material 拡張と対応）
    float oceanSunDiffuseInfluence;
    float oceanSunSpecularInfluence;
    float oceanSunGlitterInfluence;
    float oceanSkyReflectionInfluence;
    float oceanAmbientInfluence;
    float oceanDiffuseFloor;
    float oceanGlitterIntensity;
    float oceanGlitterSharpness;
    float oceanGlitterDensity;
    float oceanGlitterThreshold;
    float oceanGlitterMaxClamp;
    float oceanLightingExtensionPadding0;
    float oceanMacroReflectionInfluence;
    float oceanCurvatureInfluence;
    float oceanTroughOcclusionStrength;
    float oceanCrestHazeStrength;
    float oceanCrestDetailBoost;
    float oceanSlopeRefractionInfluence;
    float oceanMediumWaveStrength;
    float oceanWaveColorSeparation;
    float oceanShapeRoughnessVariation;
    float oceanDetailFilterSharpness;
    float oceanGrazingShapeVisibility;
    // 旧 oceanShapeLightingPadding0 を流用した Ocean Debug View 番号。
    // float のまま扱うので cbuffer のレイアウト / サイズは一切変わらない。
    float oceanDebugView;
};

#include "../Common/SceneLightData.hlsli"

struct WaterViewConstants
{
    row_major float4x4 inverseViewProjection;
    float2 viewportUvOffset;
    float2 viewportUvScale;
    float3 viewRight;
    float projectionScaleX;
    float3 viewUp;
    float projectionScaleY;
    float projectionWScale;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
    float4 oceanData : TEXCOORD3;
    float4 oceanSamplingData : TEXCOORD4;
    nointerpolation float3 oceanWorldAxisX : TEXCOORD5;
    nointerpolation float3 oceanWorldAxisY : TEXCOORD6;
    nointerpolation float3 oceanWorldAxisZ : TEXCOORD7;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);
ConstantBuffer<WaterViewConstants> gWaterView : register(b3);

Texture2D gShadowMap : register(t1);
Texture2D gEnvironmentTexture : register(t2);
Texture2D<float4> gWaterSceneColor : register(t18);
Texture2D<float> gWaterSceneDepth : register(t19);

SamplerState gTextureSampler : register(s0);
SamplerState gSceneSampler : register(s1);

#include "OceanSurface.hlsli"

static const float kOceanPi = 3.14159265359f;
static const float kOceanEnvironmentMipCount = 8.0f;

//============================================================
// Ocean Debug View (実行時切り替え / gMaterial.oceanDebugView)
//------------------------------------------------------------
// Inspector の Ocean コンポーネント「デバッグ表示」から切り替える。
// シェーダは実行時コンパイルなのでリビルド不要。0 は通常描画で、
// 0 のときはこの機能によって最終色が一切変化しない。
//
//   0  通常描画 (Final)
//   1  太陽 Directional Diffuse 単体
//   2  GGX Specular 単体 (directSpecular / 圧縮表示)
//   3  Sun Glitter 単体 (sunGlitter / 圧縮表示)
//   4  Glitter 反射整列 saturate(dot(N,H)) をグレースケール
//   5  Glitter 最終 Mask をグレースケール
//   6  Medium Normal を色表示 (N * 0.5 + 0.5)
//   7  Fine / Specular Normal を色表示 (N * 0.5 + 0.5)
//   8  |specularNormal - mediumNormal| を x8 でグレースケール
//      (屈折オフセット rippleRefractionOffset の駆動源)
//   9  階層 Normal の傾き量 (ConvertOceanLocalNormalToSlope) をグレースケール
//      1.0 = 傾き 6.25 = max(|N.y|, 0.16) の除算が飽和している状態
//  10  Macro/Medium間の連続な角度差をグレースケール
//  11  Foam をグレースケール
//  12  Sky / Environment 反射単体 (圧縮表示)
//  13  屈折先の Scene サンプル単体 (sceneSample.sceneColor)
//  14  Caustics 単体 (shallowCaustic)
//  15  水中実体の被覆率 (opaqueSurfaceCoverage)
//      白 = 屈折先に水底/物体がある、黒 = 背景の空。外洋で白が出るなら
//      空を水中景として屈折させている状態(波の輪郭線の原因)
//  16  FFTが出力した泡チャンネル (oceanData.x) そのもの
//      Inspectorの「泡の強さ」では消えない値。Caustics・波頭Hazeも駆動する。
//      ここに糸状の筋が出るなら原因はFFT側の泡履歴(移流)であり、
//      海面シェーダのライティングではない
//  17  Fine Delta Slope (Mediumでは説明できない残差Slope)
//      長い線状に見えるなら低周波成分がFineへ混入している
//  18  Fine Lobe Normal (BRDF Lobeへ実際に渡している増幅済みMicro Surface法線)
//  19  Fine Micro Roughness (グレースケール。波の少ない面ほど黒=鋭いGlint)
//  20  Fine Sun Specular 単体 (圧縮表示)
//      短く小さい反射が多数・粗密を伴って分布していれば正常。
//      Debug 2 (Medium Specular) と同じ場所ばかり光るなら差分抽出が効いていない
//  21  Fine Environment Normal Delta (符号付き)。fineLobeNormalを重み1.0でそのまま使い、
//      Roughness/Filter/距離Fadeを含めない「法線を差し替えただけ」の変化を見る。
//      増えた場所=Cyan/Green、減った場所=Magenta/Red、無変化=グレー。
//      Debug 20(太陽方向に集中する高輝度Lobe)とは明確に異なり、水面全体へ低輝度で
//      分布し、正負が入り混じっていれば正常。
//  22  Fine BRDF Lobe だけ OFF にした通常描画 (OFF/ON 比較用。early returnしない)
//  23  Medium Resolved Slope (Debug 17 と同じ色エンコード。帯域分離の比較用)
//  24  Fine Environment Final Delta (符号付き)。Large+Medium主反射へ、Fine専用Sampleの
//      有界差分(environmentFineWeight・Footprint Filter・距離Fade込み)を足した最終結果と
//      Medium基準の差分。
//      Debug 21 と比べて、どこでFilter/距離減衰がFineを弱めているか確認できる。
//  25  Environment Normal Diagnostic。Debug21用Normal(fineLobeNormal)とFine専用Sample用
//      Normal(environmentReflectionNormal)の角度差。Environment Sampleを
//      通さないため、21/24の違いが入力Normal由来かSample応答由来かを切り分けられる。
//  26  Fine GGX D 単体 (異方性NDF、グレースケール)。広い領域で明るいならRoughnessが
//      広すぎる。小さい点状ならRoughnessは健全。
//  27  Fine NdotH 単体。画素ごとに細かく変動していれば高周波が保たれている。
//  28  Fine Raw Lobe (Medium Normalとの差分抽出前、Fine単体のLobe)
//  29  Medium Reference Lobe (同じRoughnessをMedium Normalで評価したLobe)。
//      Debug 28とほぼ同じ範囲で明るいのが正常(その差分がDebug 20になる)。
//  30-40  Debug 21/24が一致していた原因を段階別に切り分ける一時診断。
//      30 Reflect Direction 21   31 Reflect Direction 24   32 両者の差分
//      33 Raw Environment Sample 21   34 同 24   35 両者の差分(Compression前)
//      36 Compression後の差分(21-24)   37 delta21-delta24(Luminance化した符号色)
//      38 RGB Signed Delta 21 (EncodeSignedRgbDeltaForDebug、非飽和)   39 同 24
//      40 delta21-delta24 を直接、非飽和RGBで表示
//      35で差が出るのにSampleOceanEnvironment直後で消えるなら方向依存性/Blurを疑い、
//      36で35より差が縮むならCompressOceanHighlightが差を潰している。
//      21/24/38/39はEncodeSignedRgbDeltaForDebug(saturateしないSoft Knee圧縮)を使う。
//      以前の saturate(delta*4+0.5) は、delta21/delta24が個別に大きい成分を持つ場合、
//      どちらも同じ0または1へ張り付いて「値は違うが表示が同じ」になっていた。
//  41-43  Debug表示経路そのものの検査用。他の計算に一切依存しない純粋な定数
//      (41=黒 42=50%グレー 43=赤)を他のDebug Viewと全く同じreturn位置から返す。
//      単色にならない場合、原因はDebug計算式ではなくOcean Pixel Shaderのreturnより
//      後(Bloom/ToneMapping/FinalComposite等)かBlend設定にある。
//  44-46  絶対量Debug。R=length(delta21) G=length(delta24) B=length(delta21-delta24)を
//      倍率44=x4 45=x64 46=x256で表示。信号自体がほぼ0なのか、信号はあるが小さすぎて
//      見えていなかっただけなのかを切り分ける。
//============================================================
static const int kOceanDebugViewFinal = 0;

#define OCEAN_PRIMARY_LIGHT gDirectionalLight.lights[0]

float2 MakeOceanEnvironmentUv(float3 direction)
{
    const float3 safeDirection = normalize(direction);
    return float2(
        atan2(safeDirection.z, safeDirection.x) * (0.5f / kOceanPi) + 0.5f,
        acos(clamp(safeDirection.y, -1.0f, 1.0f)) / kOceanPi);
}

float3 RotateOceanEnvironment(float3 direction, float rotation)
{
    const float rotationSin = sin(rotation);
    const float rotationCos = cos(rotation);
    return float3(
        direction.x * rotationCos - direction.z * rotationSin,
        direction.y,
        direction.x * rotationSin + direction.z * rotationCos);
}

//============================================================
// Oceanへ映り込む太陽
//============================================================

float3 EvaluateOceanReflectedSun(float3 direction, float roughness)
{
    // Point / Spotは局所光として別経路で評価し、空の太陽には使用しない。
    if (OCEAN_PRIMARY_LIGHT.lightType != 0)
    {
        return 0.0f;
    }

    const float3 safeDirection = normalize(direction);
    const float3 sunDirection = normalize(-OCEAN_PRIMARY_LIGHT.direction);
    const float sunVisibility = smoothstep(-0.08f, 0.04f, sunDirection.y);
    const float sunAlignment = saturate(dot(safeDirection, sunDirection));
    const float safeRoughness = saturate(roughness);

    // 環境側は太陽円盤と狭い光冠だけを担当する。
    // 粗い面でも過度に広げず、広域の微細反射は後段のFine GGXへ任せる。
    const float sunDiscExponent = lerp(8192.0f, 1024.0f, safeRoughness);
    const float sunDiscEnergy = lerp(1.8f, 0.35f, safeRoughness);
    const float sunDisc = pow(sunAlignment, sunDiscExponent) * sunDiscEnergy;

    // 光冠は円盤の直近だけに限定する。広い散乱をここへ足すと、太陽高度が
    // 高いときに多数の波頭が同じ色へ染まり、微小面反射に見えなくなる。
    const float aureoleExponent = lerp(512.0f, 128.0f, safeRoughness);
    const float aureoleEnergy = lerp(0.015f, 0.004f, safeRoughness);
    const float sunAureole = pow(sunAlignment, aureoleExponent) * aureoleEnergy;
    const float3 sunRadiance =
        max(OCEAN_PRIMARY_LIGHT.color.rgb, 0.0f) *
        max(OCEAN_PRIMARY_LIGHT.intensity, 0.0f);

    return sunRadiance * sunVisibility *
        (sunDisc + sunAureole);
}

float3 SampleOceanEnvironmentInternal(
    float3 direction,
    float roughness,
    float reflectedSunWeight)
{
    const float3 safeDirection = normalize(direction);
    const float3 sunDirection = normalize(-OCEAN_PRIMARY_LIGHT.direction);
    const float3 atmosphereBase = EvaluateAtmosphereBase(
        safeDirection,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyIntensity);
    const float3 atmosphereDetail = EvaluateAtmosphereDirectionalDetail(
        safeDirection,
        sunDirection,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyIntensity,
        0.0f);
    const float3 atmosphere = atmosphereBase + atmosphereDetail;
    const float3 generatedEnvironment = gPrefilterCube.SampleLevel(
        gIblSampler,
        safeDirection,
        saturate(roughness) * (kOceanEnvironmentMipCount - 1.0f)).rgb;
    const float3 rotatedDirection = RotateOceanEnvironment(
        safeDirection,
        OCEAN_PRIMARY_LIGHT.environmentTextureRotation);
    const float3 textureEnvironment = gEnvironmentTexture.SampleLevel(
        gIblSampler,
        MakeOceanEnvironmentUv(rotatedDirection),
        max(
            saturate(roughness) * (kOceanEnvironmentMipCount - 1.0f) +
            OCEAN_PRIMARY_LIGHT.environmentTextureMipBias,
            0.0f)).rgb *
        max(OCEAN_PRIMARY_LIGHT.environmentTextureIntensity, 0.0f);
    const float3 generatedSky = lerp(generatedEnvironment, atmosphere, 0.42f);
    const float3 environment = lerp(
        generatedSky,
        textureEnvironment,
        saturate(OCEAN_PRIMARY_LIGHT.environmentTextureEnabled));

    // HDRI使用時もSceneのSUNを反射へ残し、色温度・強度・高度を一致させる。
    return max(
        environment +
            EvaluateOceanReflectedSun(safeDirection, roughness) *
            saturate(reflectedSunWeight),
        0.0f);
}

float3 SampleOceanEnvironment(float3 direction, float roughness)
{
    return SampleOceanEnvironmentInternal(direction, roughness, 1.0f);
}

float3 SampleOceanEnvironmentWithoutReflectedSun(
    float3 direction,
    float roughness)
{
    return SampleOceanEnvironmentInternal(direction, roughness, 0.0f);
}

float CalculateOceanSpotAttenuation(
    float3 lightDirection,
    float3 spotForward,
    DirectionalLightData light)
{
    const float spotCosine = dot(lightDirection, normalize(spotForward));
    const float innerCosine = max(light.spotCosInner, light.spotCosOuter);
    const float outerCosine = min(light.spotCosInner, light.spotCosOuter);
    return saturate(
        (spotCosine - outerCosine) /
        max(innerCosine - outerCosine, 0.0001f));
}

void BuildOceanLight(
    float3 worldPosition,
    DirectionalLightData light,
    out float3 lightDirection,
    out float3 radiance)
{
    if (light.lightType == 0)
    {
        lightDirection = normalize(-light.direction);
        radiance = max(light.color.rgb, 0.0f) * max(light.intensity, 0.0f);
        return;
    }

    const float3 toLight = light.position - worldPosition;
    const float distanceToLight = max(length(toLight), 0.001f);
    lightDirection = toLight / distanceToLight;
    const float range = max(light.range, 0.01f);
    const float normalizedDistance = saturate(distanceToLight / range);
    const float rangeAttenuation =
        saturate(1.0f - normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance);
    float attenuation =
        rangeAttenuation * rangeAttenuation /
        max(distanceToLight * distanceToLight, 0.35f);

    if (light.lightType == 2)
    {
        attenuation *= CalculateOceanSpotAttenuation(
            -lightDirection,
            light.direction,
            light);
    }

    radiance =
        max(light.color.rgb, 0.0f) *
        max(light.intensity, 0.0f) *
        attenuation;
}

//============================================================
// Ocean Shadow
//============================================================

// perspectiveNearClip が 0 以下なら平行投影(Sun/Spot)として従来の固定バイアスを使う。
// 正の値を渡すと透視投影(Point Lightのキューブ面)として深度依存バイアスへ切り替える。
float SampleOceanShadowProjection(
    float3 worldPosition,
    float normalDotLight,
    row_major float4x4 shadowViewProjection,
    float4 atlasTransform,
    float filterRadius,
    float perspectiveNearClip,
    float perspectiveFarClip)
{
    const float4 shadowPosition = mul(
        float4(worldPosition, 1.0f),
        shadowViewProjection);

    if (shadowPosition.w <= 0.000001f)
    {
        return 1.0f;
    }

    const float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;
    const float2 shadowUv = float2(
        shadowNdc.x * 0.5f + 0.5f,
        -shadowNdc.y * 0.5f + 0.5f);

    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
        shadowUv.y < 0.0f || shadowUv.y > 1.0f)
    {
        return 1.0f;
    }

    uint shadowMapWidth = 1u;
    uint shadowMapHeight = 1u;
    gShadowMap.GetDimensions(shadowMapWidth, shadowMapHeight);
    const float2 texelSize = rcp(max(
        float2(shadowMapWidth, shadowMapHeight),
        1.0f));
    const float2 tileMinimumUv = atlasTransform.zw + texelSize * 2.0f;
    const float2 tileMaximumUv =
        atlasTransform.zw +
        atlasTransform.xy -
        texelSize * 2.0f;
    const float2 atlasUv = shadowUv * atlasTransform.xy + atlasTransform.zw;
    const float receiverDepth = saturate(shadowNdc.z);
    float receiverBias = lerp(
        0.0020f,
        0.00035f,
        saturate(normalDotLight));

    if (perspectiveNearClip > 0.0f)
    {
        // 透視投影の深度は 1/z 分布なので、NDC上で一定のバイアスは
        // 遠方ではワールド換算で巨大なズレになり影が消える。
        const float worldBias = lerp(0.055f, 0.014f, saturate(normalDotLight));
        const float viewDepth = max(shadowPosition.w, 0.0001f);
        const float ndcPerWorldUnit =
            (perspectiveNearClip * perspectiveFarClip) /
            (max(perspectiveFarClip - perspectiveNearClip, 0.0001f) * viewDepth * viewDepth);
        receiverBias = clamp(worldBias * ndcPerWorldUnit, 0.00002f, 0.01f);
    }

    const float filteredShadow = SampleSoftShadow9Tap(
        gShadowMap,
        gSceneSampler,
        atlasUv,
        receiverDepth - receiverBias,
        texelSize,
        filterRadius,
        tileMinimumUv,
        tileMaximumUv);
    // PCF結果を再度smoothstepすると半影が二値化され、波面上へ濃い帯ができる。
    // フィルタ済みの可視率をそのまま直射光へ使い、間接光は別経路で残す。
    return saturate(filteredShadow);
}

float SampleOceanShadowAtlas(
    float3 worldPosition,
    float normalDotLight,
    DirectionalLightData light)
{
    if (light.shadowEnabled < 0.5f)
    {
        return 1.0f;
    }

    // 近距離まで固定半径のPCFを掛けると波面に投影された影が常にぼける。
    // Tap数は変えず、遠距離だけ従来の半径へ戻す。
    const float cameraDistance = length(worldPosition - light.cameraPosition);
    const float shadowFilterRadius = lerp(
        0.72f,
        1.18f,
        smoothstep(28.0f, 180.0f, cameraDistance));

    const bool usesCascadedShadow =
        light.lightType == 0 &&
        light.shadowCascadeCount > 1.5f;

    const bool usesCubeShadow =
        light.lightType == 1 &&
        light.shadowCascadeCount > 1.5f;

    if (usesCubeShadow)
    {
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

        return SampleOceanShadowProjection(
            worldPosition,
            normalDotLight,
            light.shadowCascadeVP[faceIndex],
            light.shadowCascadeAtlas[faceIndex],
            shadowFilterRadius,
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
        return SampleOceanShadowProjection(
            worldPosition,
            normalDotLight,
            light.shadowVP,
            atlasTransform,
            shadowFilterRadius,
            0.0f,
            0.0f);
    }

    uint cascadeIndex = 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
    cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
    cascadeIndex = min(cascadeIndex, 3u);

    const float currentShadow = SampleOceanShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex],
        light.shadowCascadeAtlas[cascadeIndex],
        shadowFilterRadius,
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
    const float blendStartDistance = lerp(
        cascadeNearDistance,
        cascadeFarDistance,
        0.88f);
    const float cascadeBlend = saturate(
        (cameraDistance - blendStartDistance) /
        max(cascadeFarDistance - blendStartDistance, 0.0001f));

    if (cascadeBlend <= 0.0f)
    {
        return currentShadow;
    }

    const float nextShadow = SampleOceanShadowProjection(
        worldPosition,
        normalDotLight,
        light.shadowCascadeVP[cascadeIndex + 1u],
        light.shadowCascadeAtlas[cascadeIndex + 1u],
        shadowFilterRadius,
        0.0f,
        0.0f);
    return lerp(currentShadow, nextShadow, cascadeBlend);
}

float GetOceanShadowVisibility(
    float3 normal,
    float3 lightDirection,
    float3 worldPosition,
    DirectionalLightData light)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));

    if (normalDotLight <= 0.0001f)
    {
        return 1.0f;
    }

    return SampleOceanShadowAtlas(
        worldPosition,
        normalDotLight,
        light);
}

float2 GetOceanViewportUv(float2 screenUv)
{
    const float2 safeViewportScale = max(
        gWaterView.viewportUvScale,
        float2(0.00001f, 0.00001f));
    return saturate(
        (screenUv - gWaterView.viewportUvOffset) /
        safeViewportScale);
}

float2 GetOceanScreenUv(float2 viewportUv)
{
    return gWaterView.viewportUvOffset +
        saturate(viewportUv) * gWaterView.viewportUvScale;
}

float2 ClampOceanScreenUv(float2 screenUv)
{
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f)));
    const float2 minimumUv = gWaterView.viewportUvOffset + texelSize * 1.5f;
    const float2 maximumUv =
        gWaterView.viewportUvOffset +
        gWaterView.viewportUvScale -
        texelSize * 1.5f;
    return clamp(screenUv, minimumUv, maximumUv);
}

bool ProjectOceanWorldPosition(
    float3 worldPosition,
    out float2 screenUv)
{
    const float3 cameraRelativePosition =
        worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition;
    const float3 viewPosition = float3(
        dot(cameraRelativePosition, gWaterView.viewRight),
        dot(cameraRelativePosition, gWaterView.viewUp),
        dot(
            cameraRelativePosition,
            normalize(cross(gWaterView.viewRight, gWaterView.viewUp))));
    const float clipW =
        viewPosition.z * abs(gWaterView.projectionWScale);

    if (clipW <= 0.00001f)
    {
        screenUv = 0.0f;
        return false;
    }

    const float2 viewportUv = float2(
        viewPosition.x * gWaterView.projectionScaleX / clipW * 0.5f + 0.5f,
        -viewPosition.y * gWaterView.projectionScaleY / clipW * 0.5f + 0.5f);
    screenUv = GetOceanScreenUv(viewportUv);
    return all(viewportUv >= 0.0f) &&
        all(viewportUv <= 1.0f);
}

float3 ReconstructOceanWorldPosition(float2 screenUv, float deviceDepth)
{
    const float2 viewportUv = GetOceanViewportUv(screenUv);
    const float2 ndc = float2(
        viewportUv.x * 2.0f - 1.0f,
        1.0f - viewportUv.y * 2.0f);
    const float4 worldPosition = mul(
        float4(ndc, deviceDepth, 1.0f),
        gWaterView.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

struct OceanSceneSample
{
    float3 sceneColor;
    float waterThickness;
    float shoreFoam;
    float shallowWater;
    float opaqueSurfaceCoverage;
};

struct OceanScreenReflection
{
    float3 color;
    float confidence;
};

OceanSceneSample SampleOceanOpaqueScene(
    PixelShaderInput input,
    float3 rippleRefractionOffset)
{
    OceanSceneSample sceneSample;
    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 sceneSize = max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f));
    const float2 screenUv = saturate(input.position.xy / sceneSize);
    const float opaqueDepth = gWaterSceneDepth.SampleLevel(
        gSceneSampler,
        screenUv,
        0.0f);
    clip(opaqueDepth + 0.00002f - input.position.z);

    const float maximumWaterDepth = max(gMaterial.oceanWaterDepth, 0.1f);
    const bool hasOpaqueSurface = opaqueDepth < 0.99999f;
    sceneSample.opaqueSurfaceCoverage = hasOpaqueSurface ? 1.0f : 0.0f;
    const float3 opaqueWorldPosition = ReconstructOceanWorldPosition(
        screenUv,
        opaqueDepth);
    const float viewThickness = hasOpaqueSurface
        ? length(opaqueWorldPosition - input.worldPosition)
        : maximumWaterDepth;
    const float verticalDepth = hasOpaqueSurface
        ? max(input.worldPosition.y - opaqueWorldPosition.y, 0.0f)
        : maximumWaterDepth;
    sceneSample.waterThickness = clamp(
        viewThickness,
        0.02f,
        maximumWaterDepth);
    sceneSample.shoreFoam =
        (1.0f - smoothstep(
            maximumWaterDepth * 0.0125f,
            maximumWaterDepth * 0.12f,
        verticalDepth)) *
        saturate(gMaterial.oceanFoamStrength);
    sceneSample.shallowWater = 1.0f - smoothstep(
        maximumWaterDepth * 0.025f,
        maximumWaterDepth * 0.22f,
        verticalDepth);

    const float normalizedDepth = saturate(verticalDepth / maximumWaterDepth);
    float2 projectedSurfaceUv = screenUv;
    float2 projectedNormalUv = screenUv;
    ProjectOceanWorldPosition(
        input.worldPosition,
        projectedSurfaceUv);
    ProjectOceanWorldPosition(
        input.worldPosition +
            rippleRefractionOffset * lerp(0.28f, 1.10f, normalizedDepth),
        projectedNormalUv);
    const float cameraDistance = length(
        input.worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
    const float oceanDomainLength = rcp(max(input.oceanSamplingData.w, 0.000001f));
    const float depthAwareRefraction = lerp(
        0.42f,
        1.0f,
        smoothstep(0.015f, 0.62f, normalizedDepth));
    const float nearRefractionStability = lerp(
        0.72f,
        1.0f,
        smoothstep(24.0f, 110.0f, cameraDistance));
    const float refractionDistanceFade = 1.0f - smoothstep(
        oceanDomainLength * 0.06f,
        oceanDomainLength * 0.34f,
        cameraDistance);
    const float2 refractionOffset = clamp(
        (projectedNormalUv - projectedSurfaceUv) *
            max(gMaterial.oceanRefractionDistortion, 0.0f) *
            refractionDistanceFade *
            depthAwareRefraction *
            nearRefractionStability,
        -gWaterView.viewportUvScale * 0.018f,
        gWaterView.viewportUvScale * 0.018f);
    const float2 refractedUv = ClampOceanScreenUv(screenUv + refractionOffset);
    const float refractedDepth = gWaterSceneDepth.SampleLevel(
        gSceneSampler,
        refractedUv,
        0.0f);
    // 屈折先に不透明物が無い(=背景の空)場合は、屈折させない。
    // 空は水中景ではないため、波形に沿ってUVをずらして拾うと空の明度勾配が
    // そのまま波の輪郭をなぞる発光線として水面へ焼き付く。現実でも水面から
    // 空が見えるのは反射であって屈折ではない(屈折で空が見えるのは水中からの
    // Snell's Windowだけ)。よって屈折は水中に実体がある画素へのみ適用する。
    const bool refractedHasOpaqueSurface = refractedDepth < 0.99999f;
    const float refractionValidity = step(
        input.position.z - 0.00002f,
        refractedDepth) * (refractedHasOpaqueSurface ? 1.0f : 0.0f);
    const float2 resolvedRefractionUv = lerp(
        screenUv,
        refractedUv,
        refractionValidity);
    sceneSample.sceneColor = gWaterSceneColor.SampleLevel(
        gSceneSampler,
        resolvedRefractionUv,
        0.0f).rgb;
    // 水中実体の有無は中心画素だけでなく屈折先も見る。中心に水底があっても
    // 屈折先が空へ外れる画素(水平線際や物体の輪郭際)で二値に切り替わると、
    // その境界自体が細い線として現れるため、両者の小さい方を採用して
    // 屈折成分が空へ滲み出さないようにする。
    sceneSample.opaqueSurfaceCoverage = min(
        sceneSample.opaqueSurfaceCoverage,
        refractedHasOpaqueSurface ? 1.0f : 0.0f);
    return sceneSample;
}

float OceanDistributionGgx(float normalDotHalf, float roughness)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alphaSquared = alpha * alpha;
    const float denominator =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared /
        max(kOceanPi * denominator * denominator, 0.00001f);
}

float OceanGeometrySmithCorrelated(
    float normalDotView,
    float normalDotLight,
    float roughness)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alphaSquared = alpha * alpha;
    const float viewLambda = normalDotLight * sqrt(max(
        (-normalDotView * alphaSquared + normalDotView) * normalDotView +
        alphaSquared,
        0.00001f));
    const float lightLambda = normalDotView * sqrt(max(
        (-normalDotLight * alphaSquared + normalDotLight) * normalDotLight +
        alphaSquared,
        0.00001f));
    return 0.5f / max(viewLambda + lightLambda, 0.00001f);
}

float3 OceanFresnelSchlick(float cosineTheta, float fresnelBase)
{
    return fresnelBase +
        (1.0f - fresnelBase) * pow(1.0f - saturate(cosineTheta), 5.0f);
}

float OceanLuminance(float3 color)
{
    return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

float3 CompressOceanHighlight(float3 color, float maximumLuminance)
{
    const float safeMaximumLuminance = max(maximumLuminance, 0.01f);
    const float luminance = OceanLuminance(color);
    const float compression = safeMaximumLuminance /
        (safeMaximumLuminance + luminance);
    return max(color, 0.0f) * compression;
}

// Debug専用: 符号付きRGB Deltaを、Luminance化・abs・saturateのいずれも使わずに可視化する。
// 修正前の saturate(delta*4.0+0.5) は、delta21とdelta24の絶対値がどちらも0.125を超えると
// 両方とも同じ0または1へ張り付き(saturateによるHard Clamp)、値そのものは違うのに
// 画面上は同じ色になっていた。ここではチャンネルごとにx/(|x|+knee)型の非飽和Soft Knee圧縮を
// 使う。この関数はどんな入力に対しても [-1,1] の範囲へ漸近するだけで、途中で2つの異なる
// 入力が同じ出力へ完全一致することがない(scale/kneeの選び方に関わらず単調写像)。
// production側のCompressOceanHighlight/最終合成には使わない、Debug可視化専用関数。
float3 EncodeSignedRgbDeltaForDebug(float3 delta, float scale)
{
    const float3 softCompressed = delta * scale / (abs(delta) * scale + 1.0f);
    return 0.5f.xxx + softCompressed * 0.5f;
}

// ------------------------------------------------------------
// Fine Micro BRDF Lobe
// ------------------------------------------------------------
// Broad Reflection(数十mスケールの太陽光路)・Medium Specular(波1個単位の反射、directSpecular)
// とは別枠の第三のLobe。Fine Micro Normal専用で、数cm〜数十cm相当の表面傾斜による
// 微細反射(短いGlint)だけを扱う。GGX/Fresnel/Visibility項は既存関数を再利用し、
// このLobeのためだけの重複実装は行わない。

// 異方性GGX法線分布。Fine Delta Slopeの方向(tangent)へRoughnessを分け、
// 点ではなく「少し伸びた」反射にする。tangent/bitangentはnormalへ直交させて渡すこと。
float OceanAnisotropicDistributionGgx(
    float3 halfDirection,
    float3 normal,
    float3 tangent,
    float3 bitangent,
    float roughnessAlongTangent,
    float roughnessAlongBitangent)
{
    const float alphaTangent = max(roughnessAlongTangent * roughnessAlongTangent, 0.0025f);
    const float alphaBitangent = max(roughnessAlongBitangent * roughnessAlongBitangent, 0.0025f);
    const float halfDotTangent = dot(halfDirection, tangent);
    const float halfDotBitangent = dot(halfDirection, bitangent);
    const float halfDotNormal = max(dot(halfDirection, normal), 0.0001f);
    const float denominatorTerm =
        (halfDotTangent * halfDotTangent) / (alphaTangent * alphaTangent) +
        (halfDotBitangent * halfDotBitangent) / (alphaBitangent * alphaBitangent) +
        halfDotNormal * halfDotNormal;
    return 1.0f / max(
        kOceanPi * alphaTangent * alphaBitangent * denominatorTerm * denominatorTerm,
        0.00001f);
}

// Fine Micro Specular本体。D(異方性GGX) * Visibility(Height-Correlated Smith) * Fresnel。
// OceanGeometrySmithCorrelatedは1/(4・NdotV・NdotL)を内包したVisibility項を返す実装のため、
// 教科書通りの D*G*F/(4*NdotV*NdotL) 表記に対して、ここでは係数4での二重除算はしない
// (directSpecularの既存計算と同じ規約に揃える)。
float3 EvaluateOceanFineMicroSpecular(
    float3 shadingNormal,
    float3 tangent,
    float3 bitangent,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance,
    float roughnessAlongTangent,
    float roughnessAlongBitangent,
    float fresnelBase)
{
    const float normalDotView = dot(shadingNormal, viewDirection);
    const float normalDotLight = dot(shadingNormal, lightDirection);

    if (normalDotView <= 0.0f || normalDotLight <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float viewDotHalf = saturate(dot(viewDirection, halfDirection));
    const float distribution = OceanAnisotropicDistributionGgx(
        halfDirection,
        shadingNormal,
        tangent,
        bitangent,
        roughnessAlongTangent,
        roughnessAlongBitangent);
    const float averageRoughness = (roughnessAlongTangent + roughnessAlongBitangent) * 0.5f;
    const float visibility = OceanGeometrySmithCorrelated(
        saturate(normalDotView),
        saturate(normalDotLight),
        averageRoughness);
    const float3 fresnel = OceanFresnelSchlick(viewDotHalf, fresnelBase);
    const float rawResponse = distribution * visibility * saturate(normalDotLight);
    // 無制限加算を避けるため、Lobe自体をここで有限化する(呼び出し側でも再度圧縮する)。
    const float finiteResponse = rawResponse / (1.0f + rawResponse * 0.20f);
    return radiance * finiteResponse * fresnel;
}

// ------------------------------------------------------------
// Sun Irradiance Response
// ------------------------------------------------------------
// SUN強度をそのまま鏡面反射へ渡すと、強度を上げた時に反射だけが膜状に広がる。
// 輝度1.0を基準に保った対数応答へ変換し、色相を維持したまま広い強度範囲を扱う。
float3 EvaluateOceanSunIrradiance(float3 radiance)
{
    const float radianceLuminance = max(
        dot(max(radiance, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)),
        0.0f);

    if (radianceLuminance <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float responsiveLuminance = log2(1.0f + radianceLuminance);
    return max(radiance, 0.0f) * (responsiveLuminance / radianceLuminance);
}

// ------------------------------------------------------------
// Sun Glitter
// ------------------------------------------------------------
// 水面に斑点模様が出ないよう、座標Hashやfloor()による点マスクは使わない。
// 太陽方向と水面法線の関係だけで連続した反射帯を作る。

float3 EvaluateOceanSunGlitter(
    float3 macroNormal,
    float3 mediumNormal,
    float3 opticalNormal,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance,
    float roughness,
    float fresnelBase,
    float slope,
    float shadowVisibility,
    float glitterIntensity,
    float glitterSharpness,
    float glitterDensity,
    float glitterThreshold,
    float glitterMaxClamp,
    out float debugReflectionAlignment,
    out float debugContinuousMask,
    out float debugFinalMask)
{
    // (A) 反射整列条件: H = normalize(V + L), reflectionAlignment = saturate(dot(N, H))。
    // ここでは座標Hashによる間引きは使わない。Hash点滅は水面に斑点模様として残るため、
    // SUN反射は連続したSpecular帯として扱う。
    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float largeReflectionAlignment = saturate(dot(macroNormal, halfDirection));
    const float mediumReflectionAlignment = saturate(dot(mediumNormal, halfDirection));
    const float reflectionAlignment = saturate(dot(opticalNormal, halfDirection));

    // Fine NormalがHalf Vectorへ向く微小面の割合をGGX分布で評価する。
    // 座標Noiseではなく実際の法線方向で決まるため、Camera/SUN移動へ連続的に追従する。
    // 太陽方向の反射帯を「一本の線」ではなく、波で途切れながら太く広がる帯にするため、
    // 微細面ラフネスを底上げして粒を大きく・重なりやすくする。
    const float glitterMicrofacetRoughness = clamp(
        (lerp(0.16f, 0.035f, saturate(glitterSharpness)) + roughness * 0.12f) * 1.08f,
        0.035f,
        0.24f);
    const float glitterDistribution = OceanDistributionGgx(
        reflectionAlignment,
        glitterMicrofacetRoughness);
    const float glintCandidate = glitterDistribution /
        (glitterDistribution + 8.0f);

    // 閾値は滑らかに使う。離散的なstepやfloor座標を使うと斑点が発生する。
    const float effectiveThreshold = lerp(0.02f, 0.72f, saturate(glitterThreshold)) * 0.97f;
    const float glintMask = smoothstep(effectiveThreshold, 1.0f, glintCandidate);

    // Large Normalで太陽反射の通り道を決め、Medium Normalで帯の内部を分割する。
    // Fine Normalは最後のGlitterだけへ使い、大波の反射帯を細波で白い板へ変えない。
    const float largePath = pow(largeReflectionAlignment, lerp(5.0f, 15.0f, saturate(roughness)));
    const float mediumPartition = pow(mediumReflectionAlignment, lerp(10.0f, 28.0f, saturate(roughness)));
    const float broadReflection = largePath * lerp(0.34f, 1.0f, mediumPartition);

    const float normalDotLight = saturate(dot(macroNormal, lightDirection));
    const float normalDotView = saturate(dot(opticalNormal, viewDirection));
    const float surfaceVisibility =
        smoothstep(0.015f, 0.18f, normalDotLight) *
        smoothstep(0.01f, 0.22f, normalDotView);
    // Glitter Pathの帯を波の傾斜でわずかに強調する。
    const float glitterCoverage = lerp(
        0.85f,
        1.15f,
        saturate(slope));

    // 広い反射帯は通常のGGX Sun Specularが担当する。
    // broadReflection単体をGlitterへ混ぜると、Fine Normalが太陽へ向いていない面まで
    // 半透明の銀色領域になり、カメラ距離で水たまり状に拡縮して見える。
    // 最終寄与を1.05倍し、太陽方向へ伸びる帯をわずかに太く見せる
    // (glintMaskは変えていないので、点状の強いGlitterまで増幅されるわけではない)。
    const float finalGlitterMask = saturate(
        glintMask * broadReflection * 1.05f *
        surfaceVisibility *
        lerp(0.65f, 1.0f, saturate(glitterDensity)));

    debugReflectionAlignment = largeReflectionAlignment;
    debugContinuousMask = saturate(glintMask * broadReflection);
    debugFinalMask = saturate(finalGlitterMask);

    const float3 fresnel = OceanFresnelSchlick(
        normalDotView,
        fresnelBase);
    const float3 glitterRadiance =
        radiance *
        fresnel *
        finalGlitterMask *
        glitterCoverage *
        shadowVisibility *
        max(glitterIntensity, 0.0f);

    // HDR値はBloomへ渡すが、自動露出を破壊する単一画素ピークにはしない。
    // ここは「明るさの安全弁」。空間的な点マスクは使わない。
    return CompressOceanHighlight(glitterRadiance, max(glitterMaxClamp, 0.1f));
}

OceanScreenReflection TraceOceanScreenReflection(
    float3 worldPosition,
    float3 surfaceNormal,
    float3 viewDirection,
    float roughness,
    float reflectionVisibility)
{
    OceanScreenReflection reflection;
    reflection.color = 0.0f;
    reflection.confidence = 0.0f;

    const float3 reflectionDirection = normalize(reflect(
        -viewDirection,
        surfaceNormal));

    if (reflectionDirection.y <= -0.015f ||
        roughness >= 0.72f ||
        reflectionVisibility <= 0.035f)
    {
        return reflection;
    }

    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gWaterSceneDepth.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(max(
        float2(sceneWidth, sceneHeight),
        float2(1.0f, 1.0f)));
    const float maximumTraceDistance = min(
        max(gMaterial.oceanWaterDepth * 1.5f, 48.0f),
        180.0f);
    const float3 rayOrigin = worldPosition + surfaceNormal * 0.08f;
    float rayDistance = 0.30f;

    [loop]
    for (int traceIndex = 0; traceIndex < 14; traceIndex++)
    {
        const float traceRatio =
            float(traceIndex) / 13.0f;
        rayDistance += lerp(0.35f, 7.0f, traceRatio * traceRatio);

        if (rayDistance >= maximumTraceDistance)
        {
            break;
        }

        const float3 rayPosition =
            rayOrigin + reflectionDirection * rayDistance;
        float2 rayScreenUv = 0.0f;

        if (!ProjectOceanWorldPosition(
            rayPosition,
            rayScreenUv))
        {
            break;
        }

        const float sceneDepth = gWaterSceneDepth.SampleLevel(
            gSceneSampler,
            rayScreenUv,
            0.0f);

        if (sceneDepth >= 0.99999f)
        {
            continue;
        }

        const float3 sceneWorldPosition = ReconstructOceanWorldPosition(
            rayScreenUv,
            sceneDepth);
        const float rayCameraDistance = length(
            rayPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float sceneCameraDistance = length(
            sceneWorldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float depthDelta = rayCameraDistance - sceneCameraDistance;
        const float hitThickness = max(
            0.20f,
            rayDistance * lerp(0.018f, 0.045f, traceRatio));

        if (depthDelta <= 0.0f || depthDelta >= hitThickness)
        {
            continue;
        }

        // 近距離の低粗さ反射は中心Sampleを優先し、固定5Tapによる面全体の軟化を避ける。
        // 遠距離と高粗さでは従来相当の平滑化へ連続的に戻す。
        const float surfaceCameraDistance = length(
            worldPosition - OCEAN_PRIMARY_LIGHT.cameraPosition);
        const float distanceBlur = smoothstep(
            24.0f,
            140.0f,
            surfaceCameraDistance);
        const float blurRadius =
            lerp(0.18f, 1.0f, distanceBlur) +
            roughness * roughness * 3.0f;
        const float sideSampleWeight = lerp(
            0.055f,
            0.15f,
            saturate(roughness * 1.4f + distanceBlur * 0.45f));
        const float centerSampleWeight = 1.0f - sideSampleWeight * 4.0f;
        const float2 blurOffset = texelSize * blurRadius;
        float3 reflectedColor =
            gWaterSceneColor.SampleLevel(gSceneSampler, rayScreenUv, 0.0f).rgb * centerSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(blurOffset.x, 0.0f)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(blurOffset.x, 0.0f)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv + float2(0.0f, blurOffset.y)),
            0.0f).rgb * sideSampleWeight;
        reflectedColor += gWaterSceneColor.SampleLevel(
            gSceneSampler,
            ClampOceanScreenUv(rayScreenUv - float2(0.0f, blurOffset.y)),
            0.0f).rgb * sideSampleWeight;

        const float2 viewportUv = GetOceanViewportUv(rayScreenUv);
        const float edgeDistance = min(
            min(viewportUv.x, 1.0f - viewportUv.x),
            min(viewportUv.y, 1.0f - viewportUv.y));
        reflection.color = max(reflectedColor, 0.0f);
        reflection.confidence =
            smoothstep(0.01f, 0.08f, edgeDistance) *
            (1.0f - rayDistance / maximumTraceDistance) *
            (1.0f - smoothstep(0.18f, 0.72f, roughness)) *
            smoothstep(-0.015f, 0.08f, reflectionDirection.y);
        return reflection;
    }

    return reflection;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    // Ocean自身がDepth Test/Writeを行うため、水平変位で折り返した面もDepthへ任せる。
    // SV_IsFrontFaceで片面をclipすると急斜面が欠け、奥の波が輪郭線状に見えてしまう。
    // projectionWScaleの符号はCameraが水面の上側か下側かの判定だけに使用する。
    const bool cameraIsAboveSurface = gWaterView.projectionWScale >= 0.0f;
    const bool isUnderwaterSurface = !cameraIsAboveSurface;

    // Geometry波のDepthと輪郭は維持し、近景だけFFT Medium Waveの光学評価位置を補正する。
    // 浮力や衝突へ影響しないため、ゲーム物理は従来と同じ水面を参照する。
    const float3 geometryWorldPosition = input.worldPosition;
    float perPixelDisplacementWeight = 0.0f;
    ApplyOceanPerPixelDisplacement(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        OCEAN_PRIMARY_LIGHT.cameraPosition,
        gMaterial.oceanPerPixelDisplacementStrength,
        gMaterial.oceanPerPixelDisplacementSteps,
        gMaterial.oceanPerPixelDisplacementDistance,
        perPixelDisplacementWeight,
        input.worldPosition);
    const float3 viewDirection = normalize(
        OCEAN_PRIMARY_LIGHT.cameraPosition - input.worldPosition);

    // 曲率と波頭判定は常にFFTの物理的な表向き法線から求める。
    // 裏面だけ先に反転すると、同じ波が表裏で別の波頭として評価されて二重像になる。
    const float3 interpolatedMacroNormal = input.normal;
    float3 resolvedFftNormal = interpolatedMacroNormal;
    float4 oceanData = input.oceanData;
    ResolveOceanPixelSurface(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        resolvedFftNormal,
        oceanData);

    // 大波の陰影は補間済みFFT法線、反射と屈折は画素FFT法線を使う。
    // 同じFFT場の帯域だけを分け、疑似ノイズによる水玉模様を発生させない。
    const OceanSurfaceFrame macroSurfaceFrame = EvaluateOceanSurfaceFrame(
        interpolatedMacroNormal,
        geometryWorldPosition,
        oceanData,
        0.0f,
        gMaterial.oceanCrestSharpness);
    float3 macroNormal = macroSurfaceFrame.macroNormal;
    // 画面微分Curvatureを色・粗さ・反射の主Maskへ使うと、急斜面でQuad境界が
    // 三角形状の暗部として露出する。連続なFFT波高と斜面から形状Maskを作る。
    const float curvatureInfluence = saturate(gMaterial.oceanCurvatureInfluence);
    const float stableSlopeMask = smoothstep(0.08f, 0.72f, macroSurfaceFrame.slope);
    const float worldCrestCurvature = smoothstep(
        0.012f,
        0.11f,
        macroSurfaceFrame.signedCurvature);
    const float worldTroughCurvature = smoothstep(
        0.012f,
        0.11f,
        -macroSurfaceFrame.signedCurvature);
    const float curvatureRefinement = curvatureInfluence * 0.20f;
    const float crestCurvatureMask =
        macroSurfaceFrame.crest *
        lerp(0.42f, 1.0f, stableSlopeMask) *
        lerp(0.92f, lerp(0.96f, 1.08f, worldCrestCurvature), curvatureRefinement);
    const float troughCurvatureMask =
        macroSurfaceFrame.trough *
        lerp(0.46f, 1.0f, stableSlopeMask) *
        lerp(0.92f, lerp(0.96f, 1.08f, worldTroughCurvature), curvatureRefinement);
    const float crestResponse = saturate(
        macroSurfaceFrame.crest * lerp(0.46f, 1.0f, stableSlopeMask));
    const float crestPreStage =
        smoothstep(0.08f, 0.42f, crestResponse) *
        (1.0f - smoothstep(0.54f, 0.76f, crestResponse));
    const float crestThinStage =
        smoothstep(0.34f, 0.70f, crestResponse) *
        (1.0f - smoothstep(0.78f, 0.96f, crestResponse));
    const float crestBreakingStage = smoothstep(
        0.68f,
        0.94f,
        crestResponse);
    const OceanNormalLayers normalLayers = ResolveOceanLayeredOpticalNormals(
        input.oceanSamplingData,
        input.oceanWorldAxisX,
        input.oceanWorldAxisY,
        input.oceanWorldAxisZ,
        interpolatedMacroNormal,
        resolvedFftNormal,
        perPixelDisplacementWeight,
        gMaterial.oceanDetailNormalStrength,
        gMaterial.oceanMediumWaveStrength,
        gMaterial.oceanDetailFilterSharpness,
        crestResponse,
        gMaterial.oceanCrestDetailBoost);
    float3 mediumNormal = normalLayers.mediumNormal;
    float3 specularNormal = normalLayers.fineNormal;
    float3 fineMicroNormal = normalLayers.fineMicroNormal;
    float3 fineLobeNormal = normalLayers.fineLobeNormal;
    const float3 fineTangentWorld = normalLayers.fineTangentWorld;
    const float2 fineDeltaSlope = normalLayers.fineDeltaSlope;
    const float2 fineLobeSlope = normalLayers.fineLobeSlope;
    const float2 mediumResolvedSlope = normalLayers.mediumResolvedSlope;
    const float fineFootprintFilter = normalLayers.fineFilter;
    const float fineHighPassVariance = normalLayers.fineHighPassVariance;
    const float cameraDistance = length(
        OCEAN_PRIMARY_LIGHT.cameraPosition - input.worldPosition);
    const float nearSurfaceDetail = 1.0f - smoothstep(
        28.0f,
        150.0f,
        cameraDistance);
    // Fine Micro Specular専用の距離LOD。近景=明確、中景=減衰、遠景=ほぼ消す。
    // nearSurfaceDetail(Roughness等の帯域切替用、~150mで0)よりも狭い範囲で0にし、
    // 遠景でFineのモアレ・ちらつきが出ないようにする。
    const float fineSpecularDistanceFade = 1.0f - smoothstep(
        26.0f,
        120.0f,
        cameraDistance);

    // Depth Test/Write後もOceanは両面描画するが、法線を視線との内積で
    // 個別反転してはならない。dot(N,V)=0の位置に不連続な反射境界ができ、
    // Medium/Fineの強度に応じて波形状の明線として見えてしまう。
    // Cameraが水面の上か下かという一つの判定で全帯域を同じ向きへ揃える。
    const float opticalSurfaceOrientation = cameraIsAboveSurface ? 1.0f : -1.0f;
    macroNormal *= opticalSurfaceOrientation;
    mediumNormal *= opticalSurfaceOrientation;
    specularNormal *= opticalSurfaceOrientation;
    fineMicroNormal *= opticalSurfaceOrientation;
    fineLobeNormal *= opticalSurfaceOrientation;

    const float reflectionNormalBlend = saturate(
        gMaterial.oceanMacroReflectionInfluence * 0.20f);
    const float3 reflectionNormal = normalize(lerp(
        macroNormal,
        mediumNormal,
        reflectionNormalBlend));
    const float troughOcclusionStrength = clamp(
        gMaterial.oceanTroughOcclusionStrength,
        0.0f,
        0.25f);
    // 急斜面へ谷遮蔽を掛けると、波周期ごとに細い暗帯が並ぶ。
    // 空が隠れるのは広い谷底として扱い、斜面では遮蔽を弱める。
    const float skyEnclosure = saturate(
        troughCurvatureMask *
        lerp(1.0f, 0.32f, stableSlopeMask));
    const float skyVisibility = 1.0f - skyEnclosure * troughOcclusionStrength;
    const float3 oceanWorldUp = normalize(input.oceanWorldAxisY);
    const float bentNormalWeight =
        skyEnclosure *
        saturate(troughOcclusionStrength / 0.25f) *
        0.22f;
    const float3 environmentReflectionNormalBase = normalize(lerp(
        reflectionNormal,
        normalize(reflectionNormal + oceanWorldUp * 0.34f),
        bentNormalWeight));
    // Fine Environment Reflectionは主反射Normalへ直接混ぜない。
    // 直接混ぜると、空の水平線を跨ぐ境界がMedium面全体へ長い白帯として現れる。
    // Fine用の別Sample方向だけを作り、後段でMedium基準との差分を小さく合成する。
    const float fineEnvironmentVariance = saturate(
        1.0f - dot(environmentReflectionNormalBase, fineLobeNormal));
    const float fineEnvironmentCoverage = smoothstep(
        0.0005f,
        0.0200f,
        fineEnvironmentVariance);
    const float environmentFineBlend = saturate(
        0.55f * fineSpecularDistanceFade * fineFootprintFilter);
    const float environmentFineWeight = saturate(
        0.45f * fineEnvironmentCoverage *
        fineSpecularDistanceFade * fineFootprintFilter);
    const float3 environmentReflectionNormal = normalize(lerp(
        environmentReflectionNormalBase,
        fineLobeNormal,
        environmentFineBlend));
    // 屈折はFineとMediumの差だけを使う。Large Waveの傾斜は歪み量を増減するが、
    // 形状そのものを二重に屈折へ加えない。
    const float3 rippleRefractionOffset =
        (specularNormal - mediumNormal) *
        (1.0f + macroSurfaceFrame.slope *
            max(gMaterial.oceanSlopeRefractionInfluence, 0.0f));
    const float normalDotView = saturate(dot(mediumNormal, viewDirection));
    const float specularNormalDotView = saturate(dot(specularNormal, viewDirection));
    // Macro/Mediumの差は診断用の連続値としてのみ保持する。
    // 狭いsmoothstepを材質へ流すと、その境界が発光する等高線に見える。
    const float mediumStructure = saturate(
        (1.0f - saturate(dot(macroNormal, mediumNormal))) * 4.0f);
    const OceanSceneSample sceneSample = SampleOceanOpaqueScene(
        input,
        rippleRefractionOffset);
    const float surfaceFoam = max(
        EvaluateOceanFoam(
            input.worldPosition,
            oceanData,
            macroSurfaceFrame,
            gMaterial.oceanFoamStrength,
            gMaterial.oceanFoamThreshold,
            gMaterial.oceanCrestSharpness),
        sceneSample.shoreFoam);
    // 水中側から表面用Foamを描くと、波頭の裏に同じ輪郭がもう一枚現れる。
    const float foam = isUnderwaterSurface ? 0.0f : surfaceFoam;

    const float normalizedHeight =
        clamp(oceanData.w, -1.0f, 1.0f) * 0.5f + 0.5f;
    const float smoothHeight = smoothstep(0.02f, 0.98f, normalizedHeight);
    const float heightPathScale = lerp(1.28f, 0.72f, smoothHeight);
    const float opticalThickness =
        sceneSample.waterThickness * heightPathScale;
    const float absorptionDistance = max(
        gMaterial.oceanAbsorptionDistance,
        0.1f);
    const float3 deepColor = max(gMaterial.oceanDeepColor, 0.0f);
    const float3 shallowColor = max(gMaterial.color.rgb, 0.0f);
    const float3 environmentWaterTint = max(
        lerp(
            OCEAN_PRIMARY_LIGHT.skyLowerColor,
            OCEAN_PRIMARY_LIGHT.skyUpperColor,
            0.22f),
        0.0f);
    const float3 midWaterColor = max(
        lerp(shallowColor, deepColor, 0.46f) * float3(0.86f, 1.08f, 1.04f) +
        environmentWaterTint * 0.08f,
        0.0f);
    const float3 absorptionCoefficient =
        max(1.0f - saturate(deepColor), 0.025f) /
        absorptionDistance;
    const float3 transmittance = exp(
        -absorptionCoefficient * opticalThickness);
    const float shallowWeight = exp(
        -opticalThickness / absorptionDistance);
    const float maximumWaterDepth = max(gMaterial.oceanWaterDepth, 0.1f);
    const float clearDepthEnd = min(
        3.0f,
        max(maximumWaterDepth * 0.08f, 0.5f));
    const float fogDepthEnd = min(
        15.0f,
        max(maximumWaterDepth * 0.30f, clearDepthEnd + 2.0f));
    const float deepDepthEnd = min(
        70.0f,
        max(maximumWaterDepth * 0.88f, fogDepthEnd + 5.0f));
    const float clearWaterLayer = 1.0f - smoothstep(
        clearDepthEnd * 0.55f,
        clearDepthEnd * 1.45f,
        sceneSample.waterThickness);
    const float fogWaterLayer =
        smoothstep(
            clearDepthEnd * 0.65f,
            fogDepthEnd,
            sceneSample.waterThickness) *
        (1.0f - smoothstep(
            fogDepthEnd * 0.82f,
            deepDepthEnd,
            sceneSample.waterThickness));
    const float deepWaterLayer = smoothstep(
        fogDepthEnd * 0.82f,
        deepDepthEnd,
        sceneSample.waterThickness);
    const float waterLayerWeight = max(
        clearWaterLayer + fogWaterLayer + deepWaterLayer,
        0.0001f);
    const float3 volumeColor = (
        shallowColor * clearWaterLayer +
        midWaterColor * fogWaterLayer +
        deepColor * deepWaterLayer) /
        waterLayerWeight;
    const float viewThroughWater = saturate(
        normalDotView * 0.78f + shallowWeight * 0.42f);
    const float shallowCausticFocus = saturate(
        oceanData.x * 0.48f +
        crestResponse * 0.24f);
    const float shallowCaustic =
        sceneSample.shallowWater *
        shallowCausticFocus *
        (1.0f - saturate(foam)) *
        0.18f;
    // 不透明な水底や物体がない画素では、Opaque Colorは水中景ではなく背景の空。
    // これをFine Normalで屈折すると、照明・環境反射をOFFにしても空の明度勾配が
    // 発光する波線として水面全体へ現れる。水底がない場合は体積水色へ収束させる。
    const float3 transmittedSceneColor = lerp(
        volumeColor,
        sceneSample.sceneColor * (1.0f + shallowCaustic),
        sceneSample.opaqueSurfaceCoverage);
    const float3 physicallyRefractedColor =
        transmittedSceneColor * transmittance +
        volumeColor * (1.0f - transmittance) *
            lerp(0.72f, 1.08f, viewThroughWater);
    const float3 clearWaterColor = lerp(
        transmittedSceneColor,
        physicallyRefractedColor,
        0.32f);
    const float3 fogWaterColor = lerp(
        midWaterColor,
        environmentWaterTint,
        0.10f);
    float3 refractedColor = lerp(
        clearWaterColor,
        fogWaterColor,
        saturate(fogWaterLayer * 0.82f));
    refractedColor = lerp(
        refractedColor,
        deepColor,
        saturate(deepWaterLayer * 0.94f));

    // 波の谷だけ、外洋(水底が無く常にdeepWaterLayerへ寄る)でも一段濃紺側へ寄せる。
    // litVolumeはこの後bodyLightingを掛けるので、ここで色を暗くしても照明応答は
    // そのまま残る(等高線のように照明を無視した塗り分けにはならない)。
    // shallowColor自体・海全体の明るさは変えない。谷(troughCurvatureMask)にだけ効く。
    refractedColor = lerp(
        refractedColor,
        deepColor,
        saturate(troughCurvatureMask) * 0.30f);

    // 浅瀬色は水底までの実水深だけへ使用する。
    // 波頭・谷Maskで浅瀬色と深海色を直接塗り分けると、照明に依存しない等高線になる。
    // ただし波頭/谷の粗さ変化(shapeRoughnessFactor)には、色を塗らずに符号だけ使う。
    const float shapeMaterialSignal = clamp(
        crestResponse * 0.62f - troughCurvatureMask * 0.54f,
        -1.0f,
        1.0f);

    float3 directBodyLight = float3(0.0f, 0.0f, 0.0f);
    float3 directSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 sunGlitter = float3(0.0f, 0.0f, 0.0f);
    // Fine Micro Specular: broadReflection/sunPathPartitionの通り道に絞らず、光を受ける
    // 波面全体へ分布する微細キラキラ。Fine Normal(specularNormal)専用で、
    // Medium GGX(directSpecular)/Sun Glitter(sunGlitter)とは別枠で積む。
    // 同じLobeをMedium Normalでも評価し、その差分(max(fine-medium,0))だけを採用する。
    // これによりMediumと同じ場所を単に鋭くしただけの領域は自動的に0へ近づき、
    // Fine法線が実際に新しい反射方向を作っている場所だけが視認できる強さで残る。
    float3 fineSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 crestTransmissionLight = float3(0.0f, 0.0f, 0.0f);
    // 大波Normalと太陽方向だけから作るDirectional Diffuse。
    // 波の高さそのものではなく、法線とライト方向の関係(NdotL)で明暗差を出す。
    float3 sunDirectionalDiffuse = float3(0.0f, 0.0f, 0.0f);
    const float shapeRoughnessStrength =
        clamp(gMaterial.oceanShapeRoughnessVariation, 0.0f, 0.5f) * 0.10f;
    const float shapeRoughnessFactor =
        1.0f - shapeMaterialSignal * shapeRoughnessStrength;
    const float baseRoughness = clamp(
        gMaterial.oceanRoughness * shapeRoughnessFactor *
            lerp(1.0f, 0.92f, nearSurfaceDetail),
        0.055f,
        1.0f);
    // Roughness AAも画面微分ではなく、MediumとFineの連続なNormal差を使う。
    // これによりポリゴン境界の微分値が黒い三角形として反射へ出ない。
    const float fineNormalVariance = saturate(
        1.0f - dot(mediumNormal, specularNormal));
    const float stableSpecularVariance = saturate(
        fineNormalVariance * 1.35f +
        macroSurfaceFrame.interpolationVariance * 0.55f);
    const float filteredRoughness = clamp(
        sqrt(baseRoughness * baseRoughness + stableSpecularVariance *
            lerp(0.18f, 0.12f, nearSurfaceDetail)),
        0.075f,
        1.0f);
    const float waterIor = max(gMaterial.ior, 1.0001f);
    const float fresnelRoot = (waterIor - 1.0f) / (waterIor + 1.0f);
    const float fresnelBase = fresnelRoot * fresnelRoot;

    // ------------------------------------------------------------
    // Fine Micro Roughness / 異方性接線フレーム (ループ外・光源に依存しない)
    // ------------------------------------------------------------
    // Roughnessは「Lobe Peak(明るさ)」「Lobe Size(1個の広がり)」「Lobe Density(数)」の
    // うちSizeだけを担当させ、分散はroughness^2(=α)空間で加算する。線形加算だと分散の
    // 合成として正しくなく、近景で鋭さが残らないまま遠景だけ潰れる挙動になる。
    // BaseはgMaterial.oceanRoughness(空間変化を持たないMaterial定数)から直接作る。
    // 従来は filteredRoughness * 0.30 を使っていたが、filteredRoughness は
    //   baseRoughness   ← shapeRoughnessFactor ← shapeMaterialSignal
    //                     (= crestResponse*0.62 - troughCurvatureMask*0.54 = Macro波頭/谷)
    //   stableSpecularVariance ← macroSurfaceFrame.interpolationVariance * 0.55 (Macro)
    // を含むため、Fine Micro RoughnessがMacro/Medium波の輪郭そのものを描いていた
    // (Debug 19が波面単位の大きな構造を引きずっていた直接原因)。Fineの空間変化は
    // 以下の(a)(b)というFine由来の量だけが担当する。
    const float fineBaseRoughness = max(gMaterial.oceanRoughness * 0.30f, 0.035f);
    // (a) 高域残差の「局所分散」: 単一Sampleの振幅(dot(slope,slope))だと1本のFine波形が
    //     そのままRoughness模様として見えてしまう。近傍4点のばらつき(Microfacet群としての
    //     統計量)を使い、増幅率の二乗を掛けてLobe法線の実際の揺れ幅と整合させる。
    const float fineLobeSlopeVariance =
        fineHighPassVariance * (kOceanFineLobeSlopeGain * kOceanFineLobeSlopeGain);
    // (b) Footprint由来のSpecular AA (Toksvig / LEAN 相当の軽量版):
    //     画素Footprintが広くなるほど、その画素内で解像できないFine法線の揺れを
    //     Roughnessへ移す。強度を0へ落として「消す」のではなくLobeを広げるので、
    //     遠景ではFineが消失するのではなく「平均化された少し広い反射」へ統合される。
    //     これがサブピクセル反射由来のちらつき(砂嵐)を構造的に防ぐ。
    const float fineFootprintVariance = (1.0f - fineFootprintFilter) * 0.022f;
    // fineLobeSlopeVarianceは fineHighPassVariance(実測の局所分散) × gain^2(64) という
    // 増幅を経ており、波の急な場所ではこの項が支配的になってLobeがMedium波面スケールへ
    // 広がっていた可能性がある(Debug 20が「数十pixel〜波斜面単位」になっていた一因)。
    // Micro Roughnessは「Microfacet群の統計的粗さ」であって「Fineの強さそのもの」では
    // ないため、ここに明示的な予算(上限)を設け、どれだけ波が急でもRoughnessが
    // Medium/Broad並みへ膨らまないようにする。Fine反射位置の疎密自体はNormal(NdotH)と
    // Density Maskが担い、Roughnessはそれを鋭いまま保つ役目に限定する。
    const float cappedFineLobeSlopeVariance = min(fineLobeSlopeVariance, 0.02f);
    const float fineMicroRoughness = clamp(
        sqrt(
            fineBaseRoughness * fineBaseRoughness +
            cappedFineLobeSlopeVariance * 0.65f +
            fineFootprintVariance),
        0.02f,
        0.16f);
    // 弱い異方性: 主軸(fineTangentWorld、Fine slope方向とMedium slope方向の連続ブレンド)へ
    // 沿ってはやや鋭く、直交方向へはやや広くする。点ではなく「少し伸びた」反射にするための
    // 調整で、強い異方性(長い線)にはしない。
    const float fineRoughnessAlongTangent = clamp(fineMicroRoughness * 0.70f, 0.025f, 0.34f);
    const float fineRoughnessAlongBitangent = clamp(fineMicroRoughness * 1.40f, 0.03f, 0.40f);
    // 接線フレームはLobe法線基準で直交化する(法線・接線・従法線が同じMicro Surfaceを指す)。
    const float3 fineBitangentWorld = normalize(cross(fineLobeNormal, fineTangentWorld));
    const float3 fineOrthoTangentWorld = normalize(cross(fineBitangentWorld, fineLobeNormal));

    float debugGlitterAlignment = 0.0f;
    float debugGlitterContinuousMask = 0.0f;
    float debugGlitterFinalMask = 0.0f;
    // Macro/MediumのSUN方向NdotLをループ外へ持ち出す。中波の斜面内部の明暗差を
    // 最終的な水色の混合比へ使うための値で、通常のLambert拡散には使わない。
    float sunMacroNdotL = 0.0f;
    float sunMediumNdotL = 0.0f;
    // Fine Lobeサイズ診断用にループ内の値をループ外へ持ち出す(Debug 26-29専用)。
    float debugFineGgxD = 0.0f;
    float debugFineNdotH = 0.0f;
    float3 debugFineRawLobe = float3(0.0f, 0.0f, 0.0f);
    float3 debugMediumReferenceLobe = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int lightIndex = 0; lightIndex < 4; lightIndex++)
    {
        const DirectionalLightData light = gDirectionalLight.lights[lightIndex];

        if (light.shadowEnabled < -0.5f)
        {
            break;
        }

        if (light.shadowEnabled < 0.5f && light.intensity <= 0.0001f)
        {
            continue;
        }

        float3 lightDirection;
        float3 radiance;
        BuildOceanLight(
            input.worldPosition,
            light,
            lightDirection,
            radiance);
        const float normalDotLight = dot(macroNormal, lightDirection);
        const float shadowVisibility = GetOceanShadowVisibility(
            macroNormal,
            lightDirection,
            input.worldPosition,
            light);
        const float softShadowVisibility = lerp(
            1.0f,
            shadowVisibility,
            0.72f);
        float directionalSurfaceVisibility = softShadowVisibility;

        // 水は不透明なLambert面ではないため、Point/SpotのNdotLを0まで落とさない。
        // 体積光を基準に弱い面方向差だけを加え、波周期ごとの縞状暗部を防ぐ。
        const float localLightFacing = saturate(
            normalDotLight * 0.5f + 0.5f);
        const float localBodyResponse =
            0.46f + localLightFacing * 0.30f;
        const float forwardScatter =
            pow(saturate(dot(-viewDirection, lightDirection)), 4.0f) *
            (0.18f + macroSurfaceFrame.slope * 0.22f);
        if (light.lightType == 0)
        {
            // Macro/MediumのSUN方向NdotLを記録する(最終色の混合比へ使う)。
            sunMacroNdotL = normalDotLight;
            sunMediumNdotL = dot(mediumNormal, lightDirection);

            // 波面DiffuseはSUNだけを対象にする。Point/Spotを混ぜると、SUN強度を変更しても
            // 既存の局所ライト成分に埋もれて見た目がほとんど変化しなくなる。
            const float sunSlopeLight = saturate(
                normalDotLight * 0.68f + 0.32f);
            const float directionalDiffuseFactor = lerp(
                saturate(gMaterial.oceanDiffuseFloor),
                1.0f,
                sunSlopeLight);
            sunDirectionalDiffuse += radiance *
                directionalDiffuseFactor *
                directionalSurfaceVisibility;

            // 波頭の薄い水だけに弱い透過散乱を出す。
            // SUNが法線の裏側へ回った時だけ発生し、常時発光する波頭にはしない。
            const float crestBackLighting = pow(
                saturate(-normalDotLight),
                0.72f);
            crestTransmissionLight += EvaluateOceanSunIrradiance(radiance) *
                crestBackLighting *
                crestThinStage *
                (0.10f + 0.08f * stableSlopeMask) *
                directionalSurfaceVisibility;
        }
        else
        {
            // Point / Spotは距離減衰を含む局所的な水中散乱として扱う。
            directBodyLight += radiance *
                (localBodyResponse + forwardScatter * 0.45f) *
                softShadowVisibility;
        }

        const float3 halfDirection = normalize(viewDirection + lightDirection);
        // SUNの広い鏡面はLarge+Mediumで評価する。FineはGlitterへ限定する。
        const float3 directSpecularNormal = light.lightType == 0
            ? mediumNormal
            : specularNormal;
        const float normalDotLightOptical = saturate(dot(
            directSpecularNormal,
            lightDirection));
        const float normalDotHalf = saturate(dot(directSpecularNormal, halfDirection));
        const float directNormalDotView = saturate(dot(directSpecularNormal, viewDirection));
        const float viewDotHalf = saturate(dot(viewDirection, halfDirection));
        // Largeは太陽光路の位置だけを決め、Mediumの微小面がHalf Vectorへ
        // 十分向いた場所だけを主鏡面の島として残す。従来のpow値をそのまま
        // lerpへ渡すと、Roughnessが高いSceneで光路全体が白い板になっていた。
        const float largeSunPath = pow(
            saturate(dot(macroNormal, halfDirection)),
            7.0f);
        const float mediumSunAlignment = saturate(dot(
            mediumNormal,
            halfDirection));
        const float mediumSunPartition = smoothstep(
            0.76f,
            0.965f,
            mediumSunAlignment);
        const float mediumSunIslands = mediumSunPartition * mediumSunPartition;
        const float sunPathPartition = light.lightType == 0
            ? largeSunPath * lerp(0.012f, 1.0f, mediumSunIslands)
            : 1.0f;

        if (normalDotLightOptical > 0.0f && directNormalDotView > 0.0f)
        {
            const float distribution = OceanDistributionGgx(
                normalDotHalf,
                filteredRoughness);
            const float geometry = OceanGeometrySmithCorrelated(
                directNormalDotView,
                normalDotLightOptical,
                filteredRoughness);
            const float3 directFresnel = OceanFresnelSchlick(
                viewDotHalf,
                fresnelBase);
            const float rawSpecularResponse =
                distribution * geometry * normalDotLightOptical;
            const float finiteSpecularResponse =
                rawSpecularResponse /
                (1.0f + rawSpecularResponse / 5.5f);
            const float3 specularRadiance = light.lightType == 0
                ? EvaluateOceanSunIrradiance(radiance)
                : radiance;
            directSpecular += specularRadiance *
                finiteSpecularResponse * directFresnel * sunPathPartition *
                (light.lightType == 0
                    ? directionalSurfaceVisibility
                    : softShadowVisibility);
        }

        if (light.lightType == 0)
        {
            // Fine Sun Specular: 専用BRDF Lobe(EvaluateOceanFineMicroSpecular)を使う。
            // Broad Reflection(largeSunPath/mediumSunPartition)やMedium Specular
            // (directSpecular)とは完全に別枠。sunPathPartitionでは絞らず、光を受ける
            // 波面全体を対象にする。異方性Roughnessにより「点ではなく少し伸びた反射」になる。
            const float3 fineLobeAtFineNormal = EvaluateOceanFineMicroSpecular(
                fineLobeNormal,
                fineOrthoTangentWorld,
                fineBitangentWorld,
                viewDirection,
                lightDirection,
                EvaluateOceanSunIrradiance(radiance),
                fineRoughnessAlongTangent,
                fineRoughnessAlongBitangent,
                fresnelBase);
            // Mediumで既に光っている場所は、Fine法線でも同じHalf Vector整列に当たるため、
            // 「Roughnessを絞っただけ」で同じ場所がさらに尖って見え、Fineが独立した
            // ハイライトとして視認できない(Medium Specularへ埋もれる)原因になっていた。
            // 同じLobe・同じRoughnessをMedium Normal(傾きなし)でも評価し、その差分だけを
            // Fineの寄与として採用する。これによりFine法線がMediumと同じ場所を指す領域は
            // 自動的にゼロへ近づき、Fineが実際に法線を傾けている場所だけが浮き上がる。
            const float3 fineLobeAtMediumNormal = EvaluateOceanFineMicroSpecular(
                mediumNormal,
                fineOrthoTangentWorld,
                fineBitangentWorld,
                viewDirection,
                lightDirection,
                EvaluateOceanSunIrradiance(radiance),
                fineRoughnessAlongTangent,
                fineRoughnessAlongBitangent,
                fresnelBase);
            const float3 fineDeltaLobeRadiance = max(
                fineLobeAtFineNormal - fineLobeAtMediumNormal,
                0.0f);
            const float3 fineHalfDirection = normalize(viewDirection + lightDirection);
            const float fineNormalDotHalf = saturate(dot(
                fineLobeNormal,
                fineHalfDirection));
            const float mediumNormalDotHalf = saturate(dot(
                mediumNormal,
                fineHalfDirection));

            // Fine法線がMedium法線よりHalf Vectorへ向いた場所だけを採用する。
            // BRDFの裾を波面全体へ加算せず、微小面の整列で短い反射筋と点状反射を作る。
            const float fineAlignmentExcess = smoothstep(
                0.006f,
                0.028f,
                fineNormalDotHalf - mediumNormalDotHalf);
            const float fineAlignmentThreshold = lerp(
                0.982f,
                0.960f,
                saturate(fineMicroRoughness / 0.16f));
            const float fineAlignmentPeak = smoothstep(
                fineAlignmentThreshold,
                0.995f,
                fineNormalDotHalf);
            const float fineMicroCoverage =
                fineAlignmentExcess * fineAlignmentPeak;

            // Debug 26-29専用: LobeサイズがBRDF(D項)由来かNormal分布由来かを切り分けるため、
            // GGX D項とNdotHを単独で取り出す。EvaluateOceanFineMicroSpecular内部と同じ
            // Half Vector/接線フレームを使う(新規の重複実装ではなく同一入力の再計算)。
            if (light.lightType == 0)
            {
                debugFineNdotH = fineNormalDotHalf;
                debugFineGgxD = OceanAnisotropicDistributionGgx(
                    fineHalfDirection,
                    fineLobeNormal,
                    fineOrthoTangentWorld,
                    fineBitangentWorld,
                    fineRoughnessAlongTangent,
                    fineRoughnessAlongBitangent);
                debugFineRawLobe = fineLobeAtFineNormal;
                debugMediumReferenceLobe = fineLobeAtMediumNormal;
            }
            // Density(反射の数と粗密)は座標Hashではなく、Micro Surfaceが実際にどれだけ
            // 傾いているか = Lobe法線とMedium法線の角度差から決める。強く傾いている面ほど
            // 反射方向のバリエーションが増え、結果として差分Lobeが立つ箇所が増える。
            // 「傾き大 = 常に白」にはしないため、レンジは狭く保つ(あくまでBRDF入力の重み)。
            const float fineLobeVariance = saturate(1.0f - dot(mediumNormal, fineLobeNormal));
            const float fineLobeCoverage = smoothstep(
                0.0010f,
                0.0140f,
                fineLobeVariance);
            const float fineDensityMask = lerp(
                0.72f,
                1.0f,
                saturate(fineLobeVariance * 8.0f));
            fineSpecular += fineDeltaLobeRadiance *
                fineMicroCoverage *
                fineLobeCoverage *
                fineDensityMask *
                1.10f *
                saturate(normalDotLight * 0.6f + 0.4f) *
                directionalSurfaceVisibility *
                fineSpecularDistanceFade *
                fineFootprintFilter;
        }

        if (light.lightType == 0)
        {
            float lightDebugAlignment = 0.0f;
            float lightDebugContinuousMask = 0.0f;
            float lightDebugFinalMask = 0.0f;
            sunGlitter += EvaluateOceanSunGlitter(
                macroNormal,
                mediumNormal,
                specularNormal,
                viewDirection,
                lightDirection,
                EvaluateOceanSunIrradiance(radiance),
                filteredRoughness,
                fresnelBase,
                macroSurfaceFrame.slope,
                directionalSurfaceVisibility,
                gMaterial.oceanGlitterIntensity,
                gMaterial.oceanGlitterSharpness,
                gMaterial.oceanGlitterDensity,
                gMaterial.oceanGlitterThreshold,
                gMaterial.oceanGlitterMaxClamp,
                lightDebugAlignment,
                lightDebugContinuousMask,
                lightDebugFinalMask);
            debugGlitterAlignment = lightDebugAlignment;
            debugGlitterContinuousMask = lightDebugContinuousMask;
            debugGlitterFinalMask = lightDebugFinalMask;
        }
    }

    // Macro(大きな明暗)とMedium(波の斜面内部の明暗)のSUN方向NdotLを合成する。
    // Fineはここでは使わない(微細な光沢はFineの役割のまま維持する)。
    // 通常のLambert拡散としては使わず、以下で水色の混合比を左右する補助係数として使う。
    const float slopeLightFacing = saturate(
        lerp(sunMacroNdotL, sunMediumNdotL, 0.35f) * 0.5f + 0.5f);

    // 同じ深度でも、光を向かない斜面(谷の反対側含む)はDeep側、光を受ける斜面は
    // 現状(Shallow寄り)のまま、という差を水の体積色そのものへ足す。
    // Deep/ShallowColorの値自体は変えず、既存のrefractedColorへ追加ブレンドするだけ。
    // troughCurvatureMaskによる谷の濃紺化(既存)とは独立に、隣り合う波面でも
    // 光の向きだけで濃紺⇔ターコイズの差が出るようにする。
    refractedColor = lerp(
        deepColor,
        refractedColor,
        lerp(0.62f, 1.0f, slopeLightFacing));

    const float normalDotWorldUp = clamp(
        dot(macroNormal, oceanWorldUp),
        -1.0f,
        1.0f);
    const float skyBlend = saturate(normalDotWorldUp * 0.5f + 0.5f);
    const float horizonFacing = 1.0f - abs(normalDotWorldUp);
    const float ambientIntensity = max(
        OCEAN_PRIMARY_LIGHT.ambientIntensity,
        0.0f);
    const float3 skyIrradiance = lerp(
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        skyBlend) *
        ambientIntensity;
    const float3 horizonIrradiance = lerp(
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        0.42f) *
        ambientIntensity;
    // 谷や影面には、水平線と周囲の水面から来る弱い間接光を残す。
    // 一定値を全体へ足さず、法線・谷の開口・水色に応じて変化させる。
    const float3 waterBounceIrradiance =
        lerp(deepColor, midWaterColor, 0.62f) *
        ambientIntensity *
        (0.055f +
            horizonFacing * 0.045f +
            (1.0f - skyVisibility) * 0.070f);
    const float3 compressedDirectBodyLight =
        directBodyLight / (1.0f + directBodyLight * 0.42f);
    // SUNの本体光は鏡面反射とは別に評価する。対数応答は強度1.0を維持し、
    // 強度を上げた時も早期飽和せず波面全体の明るさと斜面差へ反映する。
    const float3 responsiveSunDirectionalDiffuse =
        EvaluateOceanSunIrradiance(sunDirectionalDiffuse);
    const float sunDiffuseInfluence = max(gMaterial.oceanSunDiffuseInfluence, 0.0f);
    const float ambientInfluence = max(gMaterial.oceanAmbientInfluence, 0.0f);
    // SUN由来の水面照明を体積色だけへ入れると、濃い水色に吸われて海だけ暗く見える。
    // 同じSUN Diffuseを浅い水面色側にも少量だけ乗せ、光に反応しつつ白飛びは圧縮で抑える。
    // 混合比は固定値ではなく slopeLightFacing で振る。同じ深度でも、光を受ける斜面は
    // shallowColor(ターコイズ)側、光を向かない斜面はmidWaterColor(暗め)側へ寄る。
    const float3 sunSurfaceLighting =
        responsiveSunDirectionalDiffuse *
        lerp(midWaterColor, shallowColor, lerp(0.30f, 0.82f, slopeLightFacing)) *
        (0.388f + saturate(macroSurfaceFrame.slope) * 0.024f) *
        sunDiffuseInfluence;
    const float3 bodyLighting =
        (skyIrradiance * 0.38f +
            horizonIrradiance * horizonFacing * 0.16f) *
            ambientInfluence * lerp(0.92f, 1.0f, skyVisibility) +
        waterBounceIrradiance * ambientInfluence +
        compressedDirectBodyLight * 0.34f +
        responsiveSunDirectionalDiffuse * 0.42f * sunDiffuseInfluence;
    const float3 litVolume = refractedColor * bodyLighting;

    const float fresnel = fresnelBase +
        (1.0f - fresnelBase) * pow(1.0f - normalDotView, 5.0f);
    // Large+Mediumで主反射面を決める。Fineは主反射との差分を符号付きで混ぜない。
    // 暗い差分まで足すと、空の色境界をFine Normalが横切るたびに、波面全体へ
    // 線状の暗部を作ってしまう。Fineでは新たに明るい環境を拾った成分だけを、
    // 高周波の反射断片として追加する。
    const float3 reflectionDirection = reflect(
        -viewDirection,
        environmentReflectionNormalBase);
    const float reflectionIntensity = max(
        OCEAN_PRIMARY_LIGHT.reflectionIntensity,
        0.0f);
    const float3 environmentReflectionBase = SampleOceanEnvironment(
        reflectionDirection,
        baseRoughness) * reflectionIntensity;
    const float3 fineEnvironmentDirection = reflect(
        -viewDirection,
        environmentReflectionNormal);
    // FineのSUN反射は、上のFine GGXとHalf Vector整列でのみ生成する。
    // 環境差分からSUNを再取得すると、同じ太陽を二重評価して広い着色面になる。
    const float3 fineEnvironmentSample = SampleOceanEnvironmentWithoutReflectedSun(
        fineEnvironmentDirection,
        max(baseRoughness, 0.09f)) * reflectionIntensity;
    const float3 positiveFineEnvironmentDelta = max(
        fineEnvironmentSample - environmentReflectionBase,
        0.0f);
    const float fineEnvironmentNormalDifference = saturate(
        1.0f - dot(
            environmentReflectionNormalBase,
            environmentReflectionNormal));
    const float fineEnvironmentNormalCoverage = smoothstep(
        0.0015f,
        0.0120f,
        fineEnvironmentNormalDifference);
    const float fineEnvironmentBrightness = OceanLuminance(
        positiveFineEnvironmentDelta);
    const float fineEnvironmentBrightnessStart = max(
        OceanLuminance(environmentReflectionBase) * 0.025f,
        0.002f);
    const float fineEnvironmentBrightnessEnd = max(
        OceanLuminance(environmentReflectionBase) * 0.180f,
        0.010f);
    const float fineEnvironmentBrightnessCoverage = smoothstep(
        fineEnvironmentBrightnessStart,
        fineEnvironmentBrightnessEnd,
        fineEnvironmentBrightness);
    const float fineEnvironmentDeltaMagnitude = max(
        max(positiveFineEnvironmentDelta.r, positiveFineEnvironmentDelta.g),
        positiveFineEnvironmentDelta.b);
    const float fineEnvironmentDeltaLimit = max(
        OceanLuminance(environmentReflectionBase) * 0.120f,
        0.006f);
    const float fineEnvironmentDeltaScale =
        fineEnvironmentDeltaLimit /
        (fineEnvironmentDeltaLimit + fineEnvironmentDeltaMagnitude);
    const float3 boundedFineEnvironmentDelta =
        positiveFineEnvironmentDelta * fineEnvironmentDeltaScale;
    const float3 environmentReflection = max(
        environmentReflectionBase +
            boundedFineEnvironmentDelta *
                environmentFineWeight *
                fineEnvironmentNormalCoverage *
                fineEnvironmentBrightnessCoverage,
        0.0f);
    const float reflectionWeight = saturate(
        fresnel * max(gMaterial.reflectance, 0.0f));
    OceanScreenReflection screenReflection;
    screenReflection.color = environmentReflection;
    screenReflection.confidence = 0.0f;

    // SSRは水面上側の反射専用。裏面で走らせると表側のScene像を再取得してしまう。
    if (!isUnderwaterSurface)
    {
        screenReflection = TraceOceanScreenReflection(
            input.worldPosition,
            reflectionNormal,
            viewDirection,
            baseRoughness,
            reflectionWeight);
    }

    const float3 resolvedReflection = lerp(
        environmentReflection,
        screenReflection.color,
        screenReflection.confidence) *
        lerp(1.0f, skyVisibility, 0.88f);

    //============================================================
    // Ocean Debug View (ファイル冒頭の一覧を参照)
    // gMaterial.oceanDebugView は cbuffer 由来の Wave 内一様値なので、
    // ここでの early return は分岐発散も微分の破綻も起こさない。
    // 0 のときは一切 return せず、以降の通常合成へそのまま進む。
    //============================================================
    const int oceanDebugView = int(gMaterial.oceanDebugView + 0.5f);

    // 22 は「Fine BRDF Lobe だけ OFF にした通常描画」。Fine Normal 自体を 0 にすると
    // 屈折・環境反射・Roughness AA まで同時に変わってしまい原因分離できないため、
    // Lobe の寄与だけを落として以降の最終合成へそのまま進む(early return しない)。
    const bool isFineLobeDisabledView = (oceanDebugView == 22);

    if (isFineLobeDisabledView)
    {
        fineSpecular = float3(0.0f, 0.0f, 0.0f);
    }

    if (oceanDebugView != kOceanDebugViewFinal && !isFineLobeDisabledView)
    {
        // Debug表示経路そのものの検査用。他のDebug計算に一切依存しない純粋な定数を、
        // 他のDebug Viewと全く同じreturn位置から返す。もしこれらが完全な単色にならず
        // 波の陰影が残る場合、Ocean Pixel Shader自身のreturnより後(Bloom/ToneMapping/
        // FinalComposite等)か、Ocean自体のBlend設定が原因であり、Debug 21/24/38-40の
        // 値そのものではなく表示経路側に問題があることになる。
        if (oceanDebugView == 41)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        if (oceanDebugView == 42)
        {
            return float4(0.5f, 0.5f, 0.5f, 1.0f);
        }
        if (oceanDebugView == 43)
        {
            return float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        if (oceanDebugView == 1)
        {
            // 太陽 Directional Diffuse 単体。
            return float4(CompressOceanHighlight(
                max(sunDirectionalDiffuse, 0.0f), 1.0f), 1.0f);
        }
        if (oceanDebugView == 2)
        {
            // GGX Specular 単体。Glitter の寄与は含まない。
            return float4(CompressOceanHighlight(directSpecular, 1.0f), 1.0f);
        }
        if (oceanDebugView == 3)
        {
            // Sun Glitter 単体。
            return float4(CompressOceanHighlight(sunGlitter, 1.0f), 1.0f);
        }
        if (oceanDebugView == 4)
        {
            // Glitter 反射整列 saturate(dot(N,H))。
            // 太陽方向へ伸びる帯なら正常。全面が白いなら異常。
            return float4(saturate(debugGlitterAlignment).xxx, 1.0f);
        }
        if (oceanDebugView == 5)
        {
            // Glitter 最終 Mask。連続した反射帯になっているか確認する。
            return float4(saturate(debugGlitterFinalMask).xxx, 1.0f);
        }
        if (oceanDebugView == 6)
        {
            // Medium Normal。滑らかな階調なら健全、細い縞が出たら中波帯が原因。
            return float4(mediumNormal * 0.5f + 0.5f, 1.0f);
        }
        if (oceanDebugView == 7)
        {
            // Fine / Specular Normal。ここだけに縞が出るなら微細法線が原因。
            return float4(specularNormal * 0.5f + 0.5f, 1.0f);
        }
        if (oceanDebugView == 8)
        {
            // |specularNormal - mediumNormal| を x8。屈折オフセットの駆動源。
            const float debugNormalDelta =
                length(specularNormal - mediumNormal) * 8.0f;
            return float4(saturate(debugNormalDelta).xxx, 1.0f);
        }
        if (oceanDebugView == 9)
        {
            // 階層 Normal の傾き量。1.0(白) は max(|N.y|, 0.16) の
            // 除算が飽和し、極端な法線が出ている状態を示す。
            const float3 debugLocalNormal = normalize(float3(
                dot(specularNormal, normalize(input.oceanWorldAxisX)),
                dot(specularNormal, normalize(input.oceanWorldAxisY)),
                dot(specularNormal, normalize(input.oceanWorldAxisZ))));
            const float2 debugSlope =
                ConvertOceanLocalNormalToSlope(debugLocalNormal);
            return float4(saturate(length(debugSlope) * 0.16f).xxx, 1.0f);
        }
        if (oceanDebugView == 10)
        {
            // Macro/Medium間の連続な角度差。材質計算には使用しない。
            return float4(saturate(mediumStructure).xxx, 1.0f);
        }
        if (oceanDebugView == 11)
        {
            // Foam 単体。
            return float4(saturate(foam).xxx, 1.0f);
        }
        if (oceanDebugView == 12)
        {
            // Sky / Environment 反射単体 (SSR 解決後)。
            return float4(CompressOceanHighlight(
                max(resolvedReflection, 0.0f), 1.0f), 1.0f);
        }
        if (oceanDebugView == 13)
        {
            // 屈折先の Scene サンプル単体。
            return float4(CompressOceanHighlight(
                max(sceneSample.sceneColor, 0.0f), 1.0f), 1.0f);
        }
        if (oceanDebugView == 14)
        {
            // Caustics 単体。
            return float4(saturate(shallowCaustic).xxx, 1.0f);
        }
        if (oceanDebugView == 16)
        {
            // FFTが出力した泡チャンネル(oceanData.x)そのもの。
            // Inspectorの「泡の強さ」では消えない値で、Caustics・波頭Hazeも駆動する。
            // ここに糸状の筋が出るなら、原因はFFT側の泡履歴(移流)であって
            // 海面シェーダのライティングではない。
            return float4(saturate(oceanData.x).xxx, 1.0f);
        }
        if (oceanDebugView == 15)
        {
            // 水中実体の被覆率。白=屈折先に水底/物体がある、黒=背景の空。
            // 外洋(水底なし)で白が出る場合、空を水中景として屈折させてしまう
            // 状態であり、波の輪郭をなぞる発光線の原因になる。
            return float4(saturate(sceneSample.opaqueSurfaceCoverage).xxx, 1.0f);
        }
        if (oceanDebugView == 17)
        {
            // Fine Delta Slope (Band Pass後の高域残差)。x/zを色のR/Gへ、
            // 大きさが0のところは灰色(0.5,0.5,0.5)になる。
            // Debug 23 (Medium Resolved Slope) と同じカメラで見比べ、23の大きな縞模様が
            // ここに現れていなければ周波数分離が成立している。長い線状に見えるなら
            // まだ低周波成分がFineへ混ざっている。
            return float4(saturate(fineDeltaSlope * 4.0f + 0.5f), 0.5f, 1.0f);
        }
        if (oceanDebugView == 23)
        {
            // Medium Resolved Slope。Debug 17と同じ色エンコード・同じスケールで表示し、
            // 「Mediumがどの空間スケールを担当しているか」を直接比較できるようにする。
            return float4(saturate(mediumResolvedSlope * 4.0f + 0.5f), 0.5f, 1.0f);
        }
        if (oceanDebugView == 18)
        {
            // Fine Lobe Normal (BRDF Lobeへ実際に渡している増幅済みMicro Surface法線)。
            // Debug 6 (Medium Normal) と比べて明確に細かい階調が乗っていれば正常。
            // 見分けがつかないほど同じなら、Lobe法線の傾きがまだLobe角幅に負けている。
            return float4(fineLobeNormal * 0.5f + 0.5f, 1.0f);
        }
        if (oceanDebugView == 19)
        {
            // Fine Micro Roughness (tangent/bitangent平均、グレースケール)。
            // 波の少ない面ほど黒(鋭いGlint)、細波が多い面ほど白(広いMicro Specular)。
            // 遠景へ向かってFootprint項で連続的に明るくなっていればBand Limitingが機能している。
            const float averageFineRoughness =
                (fineRoughnessAlongTangent + fineRoughnessAlongBitangent) * 0.5f;
            return float4(saturate(averageFineRoughness).xxx, 1.0f);
        }
        if (oceanDebugView == 20)
        {
            // Fine Sun Specular 単体(Medium Normal評価との差分抽出後)。短く鋭いGlintが
            // 多数、途切れながら分布していれば正常。波面全体がうっすら均一に光っている、
            // またはMedium Specular(Debug 2)と同じ場所ばかり光っているなら、差分抽出が
            // 効いておらず埋もれている可能性がある。
            return float4(CompressOceanHighlight(fineSpecular, 1.0f), 1.0f);
        }
        if ((oceanDebugView >= 21 && oceanDebugView <= 24) ||
            (oceanDebugView >= 30 && oceanDebugView <= 40) ||
            (oceanDebugView >= 44 && oceanDebugView <= 46))
        {
            // 21/24が完全一致していた原因を段階ごとに切り分けるため、21用と24用を
            // 三項演算子で切り替えず、常に両方を独立変数として計算する。
            // どちらか一方しか計算しないと、途中経過を比較するDebug自体を作れない。
            const float3 normal21 = fineLobeNormal;               // Debug 21用Normal(重み1.0)
            const float3 normal24 = environmentReflectionNormal;  // Fine専用Sample用Normal
            const float3 normalMedium = environmentReflectionNormalBase;

            const float3 reflectDir21 = reflect(-viewDirection, normal21);
            const float3 reflectDir24 = reflect(-viewDirection, normal24);
            const float3 reflectDirMedium = reflect(-viewDirection, normalMedium);

            const float reflectionIntensity = max(OCEAN_PRIMARY_LIGHT.reflectionIntensity, 0.0f);
            const float3 envSampleMedium =
                SampleOceanEnvironment(reflectDirMedium, baseRoughness) * reflectionIntensity;
            const float3 envSample21 =
                SampleOceanEnvironment(reflectDir21, baseRoughness) * reflectionIntensity;
            // Debug 24は、Large+Medium主反射へFine専用Sampleの有界差分を合成した
            // 本番のenvironmentReflectionをそのまま再利用する。
            const float3 envSample24 = environmentReflection;

            const float3 compressedEnvMedium = CompressOceanHighlight(envSampleMedium, 2.0f);
            const float3 compressedEnv21 = CompressOceanHighlight(envSample21, 2.0f);
            const float3 compressedEnv24 = CompressOceanHighlight(envSample24, 2.0f);

            const float3 delta21 = compressedEnv21 - compressedEnvMedium;
            const float3 delta24 = compressedEnv24 - compressedEnvMedium;

            const float3 luminanceWeights = float3(0.2126f, 0.7152f, 0.0722f);
            // Signed Luminanceの符号色可視化(increase/decrease/neutral)はDebug 35/36/37の
            // ような「1次元に潰しても問題ない値」専用に残す。Debug 21/24自体はRGBを保持する
            // EncodeSignedRgbDeltaForDebugへ切り替えたため、ここでは使わない。
            const float3 increaseColor = float3(0.15f, 1.0f, 0.75f);
            const float3 decreaseColor = float3(1.0f, 0.15f, 0.55f);
            const float3 neutralColor = float3(0.5f, 0.5f, 0.5f);

            if (oceanDebugView == 21)
            {
                // RGB保持・非飽和(EncodeSignedRgbDeltaForDebug)。Luminance化していた
                // debugColor21/signedLuminance21はもう使わない(21/24が画面上でほぼ一致
                // していた原因がsaturateによるHard Clampだったため)。
                return float4(EncodeSignedRgbDeltaForDebug(delta21, 4.0f), 1.0f);
            }
            if (oceanDebugView == 24)
            {
                return float4(EncodeSignedRgbDeltaForDebug(delta24, 4.0f), 1.0f);
            }
            if (oceanDebugView == 30)
            {
                // Reflect Direction 21 (色エンコード: dir*0.5+0.5)
                return float4(reflectDir21 * 0.5f + 0.5f, 1.0f);
            }
            if (oceanDebugView == 31)
            {
                // Reflect Direction 24
                return float4(reflectDir24 * 0.5f + 0.5f, 1.0f);
            }
            if (oceanDebugView == 32)
            {
                // Reflect Direction差分(21-24)。ここが真っ黒(0)なら、
                // reflect()に入るNormal自体は違うのに方向ベクトルとしては一致している
                // = reflect()より前(Normalの実効値)を再確認する必要がある。
                // 何か色が付いていれば、reflectDirは確かに21/24で異なる。
                const float3 reflectDirDelta = reflectDir21 - reflectDir24;
                return float4(saturate(abs(reflectDirDelta) * 20.0f), 1.0f);
            }
            if (oceanDebugView == 33)
            {
                // Raw Environment Sample 21 (CompressOceanHighlightの2.0キャップより広い
                // 4.0キャップで、生のSampleOceanEnvironment出力を見やすくしたもの)。
                return float4(CompressOceanHighlight(envSample21, 4.0f), 1.0f);
            }
            if (oceanDebugView == 34)
            {
                // Raw Environment Sample 24
                return float4(CompressOceanHighlight(envSample24, 4.0f), 1.0f);
            }
            if (oceanDebugView == 35)
            {
                // Raw Environment Sample差分(21-24、Compression前)。ここが真っ黒なら、
                // SampleOceanEnvironment()の時点で既にreflectDir21/24の違いが結果へ
                // 反映されていない(Direction依存性がRoughness Blur/Mip/Atmosphereの
                // どこかで潰れている)ことになる。ここで色が付くならCompression以降を疑う。
                const float3 rawSampleDelta = envSample21 - envSample24;
                const float rawSignedLuminance = dot(rawSampleDelta, luminanceWeights);
                const float rawMagnitude = abs(rawSignedLuminance) / (abs(rawSignedLuminance) + 0.05f);
                return float4(lerp(
                    neutralColor,
                    rawSignedLuminance >= 0.0f ? increaseColor : decreaseColor,
                    rawMagnitude), 1.0f);
            }
            if (oceanDebugView == 36)
            {
                // Compression後の差分(21-24)。Debug 35(Raw)では色が付くのに
                // ここが真っ黒なら、CompressOceanHighlightが差を潰している。
                const float3 compressedDelta = compressedEnv21 - compressedEnv24;
                const float compressedSignedLuminance = dot(compressedDelta, luminanceWeights);
                const float compressedMagnitude = abs(compressedSignedLuminance) /
                    (abs(compressedSignedLuminance) + 0.05f);
                return float4(lerp(
                    neutralColor,
                    compressedSignedLuminance >= 0.0f ? increaseColor : decreaseColor,
                    compressedMagnitude), 1.0f);
            }
            if (oceanDebugView == 37)
            {
                // delta21とdelta24自体の差分(Medium基準を引いた後)。Debug 36と理論上
                // 同じ結果になるはず(Medium基準は21/24で共通のため引き算で相殺される)。
                // ここが36と違う値になるなら計算式に矛盾がある。
                const float3 deltaOfDeltas = delta21 - delta24;
                const float deltaSignedLuminance = dot(deltaOfDeltas, luminanceWeights);
                const float deltaMagnitude = abs(deltaSignedLuminance) / (abs(deltaSignedLuminance) + 0.05f);
                return float4(lerp(
                    neutralColor,
                    deltaSignedLuminance >= 0.0f ? increaseColor : decreaseColor,
                    deltaMagnitude), 1.0f);
            }
            if (oceanDebugView == 38)
            {
                // RGB Signed Delta 21 (Luminanceへ1次元化する前)。
                // 修正前は saturate(delta21 * 4.0 + 0.5) だった。|delta21.channel| が
                // 0.125を超えるとその成分が0または1へ完全に張り付き(Hard Clamp)、
                // delta24側も同じ極へ張り付いていれば「値は違うのに表示上は同じ色」に
                // なっていた(21/24が画面上でほぼ完全一致していた直接の原因)。
                // saturate/abs/Luminance化を使わないEncodeSignedRgbDeltaForDebugへ変更。
                return float4(EncodeSignedRgbDeltaForDebug(delta21, 4.0f), 1.0f);
            }
            if (oceanDebugView == 39)
            {
                // RGB Signed Delta 24
                return float4(EncodeSignedRgbDeltaForDebug(delta24, 4.0f), 1.0f);
            }
            if (oceanDebugView == 40)
            {
                // delta21 - delta24 を直接表示する(RGB保持・非飽和)。Debug 37は
                // Luminance化した符号色だったため、ここでは1次元化せずに直接見る。
                return float4(EncodeSignedRgbDeltaForDebug(delta21 - delta24, 4.0f), 1.0f);
            }
            if (oceanDebugView == 44 || oceanDebugView == 45 || oceanDebugView == 46)
            {
                // 絶対量Debug: R=|delta21|, G=|delta24|, B=|delta21-delta24|。
                // 符号やRGB内訳ではなく「そもそも信号がどれだけ存在するか」だけを見る。
                // 倍率44=x4, 45=x64, 46=x256を切り替え、
                //   全倍率でほぼ真っ黒 → 信号自体がほぼ0(Fine Environmentの実効寄与が
                //     本当に小さいか、delta計算のどこかで0近くまで潰れている)
                //   高倍率でだけ色が出る → 信号はあるが微小で、これまでの表示(x4)では
                //     見えなかっただけ
                // を切り分ける。
                const float magnitudeScale =
                    oceanDebugView == 44 ? 4.0f :
                    oceanDebugView == 45 ? 64.0f : 256.0f;
                const float3 magnitudeColor = float3(
                    length(delta21),
                    length(delta24),
                    length(delta21 - delta24)) * magnitudeScale;
                return float4(saturate(magnitudeColor), 1.0f);
            }
        }
        if (oceanDebugView == 25)
        {
            // Environment Normal Diagnostic: Debug 21が使うNormal(fineLobeNormal、重み1.0)と
            // Fine専用Sampleが使うNormal(environmentReflectionNormal、
            // environmentFineBlend済み)の角度差そのものを、Environment Sampleを一切通さずに
            // 直接見る。21/24の見え方の違いが「入力Normalの差」なのか「Environment Sample
            // 側の応答」なのかを切り分けるための版。
            const float normalDifference =
                1.0f - saturate(dot(fineLobeNormal, environmentReflectionNormal));
            // 角度差は非常に小さい値になりやすいため(environmentFineBlend<=0.035)、
            // 見やすいよう強調するが、これも可視化専用でRendering Weightには使わない。
            return float4(saturate(normalDifference * 40.0f).xxx, 1.0f);
        }
        if (oceanDebugView == 26)
        {
            // Fine GGX D単体(異方性NDF、グレースケール)。ここが波斜面単位の広い領域で
            // 明るいなら、原因はNormal分布ではなくRoughness(D項の広さ)側にある。
            // ここが既に小さい点状ならRoughnessは健全で、広く見える原因は後段
            // (Density Mask / Highlight Compression / Footprint)にある。
            return float4(saturate(debugFineGgxD * 0.02f).xxx, 1.0f);
        }
        if (oceanDebugView == 27)
        {
            // Fine NdotH単体。Medium波面ほど滑らかに変化する(=広い範囲で1.0に近い)なら、
            // Fine Combined Normal自体がMediumに埋もれかけている可能性がある。
            // 画素ごとに細かく変動していれば、NdotH側は健全にFine由来の高周波を保っている。
            return float4(debugFineNdotH.xxx, 1.0f);
        }
        if (oceanDebugView == 28)
        {
            // Fine Raw Lobe (Medium Normalとの差分抽出をする前のFine単体Lobe)。
            return float4(CompressOceanHighlight(debugFineRawLobe, 1.0f), 1.0f);
        }
        if (oceanDebugView == 29)
        {
            // Medium Reference Lobe (同じRoughness/接線フレームをMedium Normalで評価した値)。
            // Debug 28とほぼ同じ範囲で明るいなら、差分抽出(Debug 20)後に小さくなるはずで、
            // Debug 20だけ依然として広いなら差分後の後段(Density/Compression)を疑う。
            return float4(CompressOceanHighlight(debugMediumReferenceLobe, 1.0f), 1.0f);
        }

        // 未定義の番号はマゼンタで通知する。
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    const float viewAngleTransparency = smoothstep(
        0.08f,
        0.72f,
        normalDotView);
    const float depthTransparency = saturate(
        clearWaterLayer +
        fogWaterLayer * 0.58f +
        deepWaterLayer * 0.12f);
    // 反射と透過はエネルギー保存(T = 1 - R)で結び付ける。角度依存はFresnel一つへ集約し、
    // 透過側へ独立した角度カーブを掛けない。
    // 従来は Fresnel の上に
    //   ・透過を水平視で16%まで落とす lerp(0.16, 1.0, viewAngleTransparency)
    //   ・反射を水平視で1.20倍する    lerp(1.20, 0.88, viewAngleTransparency)
    //   ・さらに透過を transmissionWeight と transmissionEnergy で二重に減衰
    // が重なっており、実質 Fresnel の3乗程度の角度依存になっていた。そのため
    // 見下ろす角度では濃紺、水平に近い角度では空反射一色、という極端な二値になり、
    // シーンビューとゲームビューで同じ海が別物に見えていた。
    // 深度由来の透過(depthTransparency)はBeer-Lambert相当の物理量なので残す。
    const float transmissionWeight = lerp(0.34f, 1.0f, depthTransparency);
    const float gameReflectionWeight = saturate(reflectionWeight);
    const float transmissionEnergy = 1.0f - gameReflectionWeight;
    // Medium/Fineの法線差を自己発光量へ変換しない。以前はmediumStructureが
    // 照明0でも青い等高線として残り、微細法線を上げるほど線が強く見えていた。
    const float regularScatteringAmount =
        0.012f +
        saturate(macroSurfaceFrame.slope) * 0.003f;
    const float preCrestScatteringAmount =
        crestPreStage * (1.0f - crestThinStage) * 0.012f;
    const float scatteringVisibility =
        transmissionEnergy *
        lerp(0.35f, 1.0f, viewAngleTransparency) *
        lerp(0.82f, 1.0f, skyVisibility) *
        (1.0f - saturate(foam));
    const float3 scatteringIrradiance =
        skyIrradiance * ambientInfluence * 0.32f +
        compressedDirectBodyLight * 0.12f +
        responsiveSunDirectionalDiffuse * sunDiffuseInfluence * 0.16f;
    const float3 regularScatteringColor =
        lerp(deepColor, midWaterColor, 0.58f) *
        float3(0.72f, 0.96f, 1.02f) *
        scatteringIrradiance;
    float3 surfaceTint = lerp(
        shallowColor,
        midWaterColor,
        saturate(fogWaterLayer + deepWaterLayer * 0.65f));
    const float surfaceTintWeight =
        lerp(0.18f, 0.055f, viewAngleTransparency) *
        (1.0f - saturate(foam) * 0.45f);
    // 水の固有色は自己発光させず、空・太陽・局所光から受けた照度でのみ見せる。
    // 無照明時にMedium/Fineの輪郭だけが水色に残る状態を防ぐ。
    const float3 surfaceTintIrradiance =
        skyIrradiance * ambientInfluence * 0.28f +
        responsiveSunDirectionalDiffuse * sunDiffuseInfluence * 0.14f +
        compressedDirectBodyLight * 0.10f;
    const float skyReflectionInfluence = max(gMaterial.oceanSkyReflectionInfluence, 0.0f);
    const float upperSurfaceVisibility = isUnderwaterSurface ? 0.0f : 1.0f;
    const float underwaterBoundaryReflection = isUnderwaterSurface ? 0.10f : 1.0f;
    float3 finalColor =
        litVolume * transmissionWeight * transmissionEnergy +
        surfaceTint * surfaceTintWeight * surfaceTintIrradiance * transmissionEnergy +
        resolvedReflection * gameReflectionWeight * skyReflectionInfluence *
            underwaterBoundaryReflection +
        sunSurfaceLighting * lerp(0.48f, 1.0f, clearWaterLayer) *
            lerp(0.22f, 1.0f, upperSurfaceVisibility) * transmissionEnergy +
        crestTransmissionLight *
            lerp(shallowColor, float3(0.40f, 0.82f, 0.86f), 0.46f) *
            transmissionEnergy * upperSurfaceVisibility +
        regularScatteringColor *
            (regularScatteringAmount + preCrestScatteringAmount) *
            scatteringVisibility * upperSurfaceVisibility;

    // Foam直前の中間状態。曲率を伴う波頭と圧縮Foam直前だけを青白くし、
    // 完全な白や高さだけの帯にはしない。
    const float preFoamThreshold = max(gMaterial.oceanFoamThreshold - 0.16f, 0.0f);
    const float preFoamCompression =
        smoothstep(
            preFoamThreshold,
            min(gMaterial.oceanFoamThreshold + 0.02f, 1.0f),
            saturate(oceanData.x)) *
        (1.0f - smoothstep(
            min(gMaterial.oceanFoamThreshold + 0.06f, 1.0f),
            min(gMaterial.oceanFoamThreshold + 0.18f, 1.0f),
            saturate(oceanData.x)));
    const float crestHazeMask = saturate(max(
        crestBreakingStage * crestCurvatureMask * 0.82f,
        preFoamCompression * crestBreakingStage * 0.64f)) *
        lerp(0.92f, 1.02f, nearSurfaceDetail) *
        (1.0f - saturate(foam));
    const float3 crestHazeColor = lerp(
        shallowColor,
        float3(0.62f, 0.82f, 0.88f),
        0.34f);
    // Hazeも波形マスク自体を発光源にしない。Mediumの等高線を直接増幅せず、
    // 波頭形状はMask、明るさは実際の照明という役割に分離する。
    const float3 crestHazeIrradiance =
        skyIrradiance * ambientInfluence * 0.34f +
        responsiveSunDirectionalDiffuse * sunDiffuseInfluence * 0.18f +
        compressedDirectBodyLight * 0.12f;
    finalColor += crestHazeColor *
        crestHazeMask *
        max(gMaterial.oceanCrestHazeStrength, 0.0f) *
        crestHazeIrradiance *
        upperSurfaceVisibility;
    const float clearWaterWeight = 1.0f - foam;
    // Water Base/Transmission(litVolume) と Sky Reflection(resolvedReflection) はここまでで合成済み。
    // Sun Specular と Sun Glitter は反射由来の加算項として別枠で足す。水深Tintそのものは白く塗らない。
    finalColor += CompressOceanHighlight(
        directSpecular,
        5.5f) * clearWaterWeight * max(gMaterial.oceanSunSpecularInfluence, 0.0f) *
        upperSurfaceVisibility;
    finalColor += sunGlitter * clearWaterWeight *
        max(gMaterial.oceanSunGlitterInfluence, 0.0f) *
        upperSurfaceVisibility;
    // Fine Micro SpecularはBRDF差分に加え、Half Vectorへ新しく整列した微小面だけへ限定済み。
    // ピークも広い白面へ戻らない範囲に制限し、異方性による短い反射筋だけを残す。
    const float fineReflectionInfluence = max(
        max(gMaterial.oceanSunGlitterInfluence, 0.0f),
        max(gMaterial.oceanSunSpecularInfluence, 0.0f) * 0.35f);
    finalColor += CompressOceanHighlight(
        fineSpecular,
        2.4f) * clearWaterWeight * fineReflectionInfluence *
        upperSurfaceVisibility;

    // 泡は「水面の上に半透明の白い絵を貼る」のではなく、水が砕けて水面そのものが
    // 白い気泡塊へ置き換わったものとして描く。
    const float3 foamLitIrradiance =
        (skyIrradiance * ambientInfluence * 0.58f +
        responsiveSunDirectionalDiffuse * sunDiffuseInfluence * 0.32f +
        compressedDirectBodyLight * 0.22f) * 1.55f;
    // 泡は無数の気泡による多重散乱で入射光をほぼ拡散反射するため、水面本体より明るく
    // 飽和しやすい。照度をそのまま掛けると灰色へ沈むので下限を持たせ、白～青白を保つ。
    const float3 foamIrradiance = max(foamLitIrradiance, 0.62f);
    const float3 foamBaseColor = float3(0.86f, 0.94f, 1.0f) * foamIrradiance;
    // 波頭の芯(泡が濃い所)と、太陽反射が乗っている所だけを純白へ寄せる。
    // 周辺(細い枝・泡片)は青白のまま残し、灰色の中間調を作らない。
    // foam は破砕Maskで閾値化済み(ほぼ二値)なので芯判定には使えない。
    // 生のFFT泡強度 oceanData.x は真の波頭ほど高いので、これを芯の深さとして使う。
    const float sunLitFoamAmount = saturate(
        OceanLuminance(directSpecular + sunGlitter) * 0.6f);
    const float foamCoreAmount = saturate(
        smoothstep(0.35f, 0.85f, saturate(oceanData.x)) + sunLitFoamAmount);
    const float3 foamColor = lerp(
        foamBaseColor,
        float3(1.0f, 1.0f, 1.0f) * max(foamIrradiance, 0.9f),
        foamCoreAmount);
    // 従来は lerp(..., foam * 0.72f) だったため、泡が最大でも72%までしか置き換わらず
    // 常に28%の水色が透けて「半透明のデジタル絵」に見えていた。
    // 芯は完全に不透明にし、周辺だけ短い遷移で水へ戻す。
    const float foamOpacity = smoothstep(0.18f, 0.62f, saturate(foam));
    finalColor = lerp(finalColor, foamColor, foamOpacity);
    finalColor = ApplyAerialPerspective(
        finalColor,
        input.worldPosition,
        OCEAN_PRIMARY_LIGHT.cameraPosition,
        normalize(-OCEAN_PRIMARY_LIGHT.direction),
        OCEAN_PRIMARY_LIGHT.skyUpperColor,
        OCEAN_PRIMARY_LIGHT.skyLowerColor,
        OCEAN_PRIMARY_LIGHT.skyIntensity,
        OCEAN_PRIMARY_LIGHT.skyEmission,
        0.00018f);
    return float4(max(finalColor, 0.0f), 1.0f);
}
