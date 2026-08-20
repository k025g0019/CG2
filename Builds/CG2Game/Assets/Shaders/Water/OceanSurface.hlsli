#ifndef CG2_OCEAN_SURFACE_INCLUDED
#define CG2_OCEAN_SURFACE_INCLUDED

struct OceanSurfaceFrame
{
    float3 macroNormal;
    float3 shadingNormal;
    float slope;
    float crest;
    float trough;
    float curvature;
    float signedCurvature;
    float interpolationVariance;
};

struct OceanNormalLayers
{
    float3 mediumNormal;
    float3 fineNormal;
};

//============================================================
// FFT Pixel Surface
//============================================================

StructuredBuffer<float4> gOceanPixelNormalFoam : register(t15);
StructuredBuffer<float4> gOceanPixelDisplacement : register(t14);

float2 RotateOceanDetailVector(float2 sourceVector, float cosineValue, float sineValue);

uint2 WrapOceanPixelIndex(int2 index, uint fftResolution)
{
    const uint resolutionMask = fftResolution - 1u;
    return uint2(index) & resolutionMask;
}

uint GetOceanPixelIndex(int2 index, uint fftResolution)
{
    const uint2 wrappedIndex = WrapOceanPixelIndex(index, fftResolution);
    return wrappedIndex.y * fftResolution + wrappedIndex.x;
}

float4 SampleOceanPixelBuffer(
    StructuredBuffer<float4> sourceBuffer,
    float2 localPosition,
    uint fftResolution,
    float inverseDomainLength)
{
    const float2 wrappedUv = frac(localPosition * inverseDomainLength + 0.5f);
    const float2 gridPosition = wrappedUv * float(fftResolution);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float4 value00 = sourceBuffer[GetOceanPixelIndex(baseIndex, fftResolution)];
    const float4 value10 = sourceBuffer[GetOceanPixelIndex(baseIndex + int2(1, 0), fftResolution)];
    const float4 value01 = sourceBuffer[GetOceanPixelIndex(baseIndex + int2(0, 1), fftResolution)];
    const float4 value11 = sourceBuffer[GetOceanPixelIndex(baseIndex + int2(1, 1), fftResolution)];
    return lerp(
        lerp(value00, value10, interpolation.x),
        lerp(value01, value11, interpolation.x),
        interpolation.y);
}

//============================================================
// 近景Per-Pixel Displacement
//============================================================

void ApplyOceanPerPixelDisplacement(
    inout float4 oceanSamplingData,
    float3 worldAxisX,
    float3 worldAxisY,
    float3 worldAxisZ,
    float3 cameraPosition,
    float displacementStrength,
    float requestedStepCount,
    float maximumDistance,
    out float displacementWeight,
    inout float3 worldPosition)
{
    displacementWeight = 0.0f;
    const uint fftResolution = uint(oceanSamplingData.z + 0.5f);
    const float inverseDomainLength = oceanSamplingData.w;
    const float safeStrength = clamp(displacementStrength, 0.0f, 0.5f);

    if (fftResolution < 2u ||
        inverseDomainLength <= 0.000001f ||
        safeStrength <= 0.0001f)
    {
        return;
    }

    const float3 axisX = normalize(worldAxisX);
    const float3 axisY = normalize(worldAxisY);
    const float3 axisZ = normalize(worldAxisZ);
    const float3 worldViewVector = cameraPosition - worldPosition;
    const float cameraDistance = length(worldViewVector);
    const float safeMaximumDistance = max(maximumDistance, 5.0f);
    const float distanceWeight = 1.0f - smoothstep(
        safeMaximumDistance * 0.72f,
        safeMaximumDistance,
        cameraDistance);

    if (distanceWeight <= 0.0001f)
    {
        return;
    }

    const float3 localViewVector = float3(
        dot(worldViewVector, axisX),
        dot(worldViewVector, axisY),
        dot(worldViewVector, axisZ));
    const float2 localViewDirection = localViewVector.xz /
        max(abs(localViewVector.y), length(localViewVector.xz) * 0.18f + 0.35f);
    const float2 limitedViewDirection = clamp(
        localViewDirection,
        float2(-2.5f, -2.5f),
        float2(2.5f, 2.5f));
    const float convergenceWeight = lerp(
        0.82f,
        1.0f,
        saturate((requestedStepCount - 1.0f) / 5.0f));
    const float2 baseSamplingPosition = oceanSamplingData.xy;
    const float fftCellSize = 1.0f /
        max(inverseDomainLength * float(fftResolution), 0.0001f);
    const float worldFootprint = max(
        length(ddx(baseSamplingPosition)),
        length(ddy(baseSamplingPosition)));
    const float bandRadius = clamp(
        max(fftCellSize * 1.5f, worldFootprint * 1.25f),
        fftCellSize * 1.5f,
        fftCellSize * 4.0f);
    const float2 filterOffsetX = float2(bandRadius, 0.0f);
    const float2 filterOffsetZ = float2(0.0f, bandRadius);
    // 同じFFT地点の中心値と周辺平均との差だけをPixel視差へ使う。
    // 別縮尺のFFTを貼り重ねないため、Geometry波からMedium波へ連続する。
    const float4 centerDisplacement = SampleOceanPixelBuffer(
        gOceanPixelDisplacement,
        baseSamplingPosition,
        fftResolution,
        inverseDomainLength);
    const float4 surroundingDisplacement = (
        SampleOceanPixelBuffer(
            gOceanPixelDisplacement,
            baseSamplingPosition + filterOffsetX,
            fftResolution,
            inverseDomainLength) +
        SampleOceanPixelBuffer(
            gOceanPixelDisplacement,
            baseSamplingPosition - filterOffsetX,
            fftResolution,
            inverseDomainLength) +
        SampleOceanPixelBuffer(
            gOceanPixelDisplacement,
            baseSamplingPosition + filterOffsetZ,
            fftResolution,
            inverseDomainLength) +
        SampleOceanPixelBuffer(
            gOceanPixelDisplacement,
            baseSamplingPosition - filterOffsetZ,
            fftResolution,
            inverseDomainLength)) * 0.25f;
    const float3 unresolvedDisplacement =
        centerDisplacement.xyz - surroundingDisplacement.xyz;
    const float unresolvedHeight =
        unresolvedDisplacement.y * safeStrength * distanceWeight;
    const float opticalHeight = unresolvedHeight * convergenceWeight;
    float2 opticalOffset = (
        limitedViewDirection * unresolvedHeight +
        unresolvedDisplacement.xz * safeStrength * distanceWeight * 0.18f) *
        convergenceWeight;

    // 1 FFTセル未満へ制限し、視点移動時に水たまり状の領域が膨張するのを防ぐ。
    const float maximumOffset = fftCellSize * 0.85f;
    const float offsetLength = length(opticalOffset);

    if (offsetLength > maximumOffset)
    {
        opticalOffset *= maximumOffset / max(offsetLength, 0.0001f);
    }

    // 視差位置を変えるだけでは後段のMedium Normalが従来強度のままになり、
    // 見た目への寄与がほぼ消える。近景で有効な量を後段へ明示して、
    // 同じFFT Medium Waveを反射・屈折の形状分割にも使用する。
    displacementWeight = distanceWeight * saturate(safeStrength / 0.28f);
    oceanSamplingData.xy += opticalOffset;
    worldPosition +=
        axisX * opticalOffset.x +
        axisY * opticalHeight +
        axisZ * opticalOffset.y;
}

void ResolveOceanPixelSurface(
    float4 oceanSamplingData,
    float3 worldAxisX,
    float3 worldAxisY,
    float3 worldAxisZ,
    inout float3 fftWorldNormal,
    inout float4 oceanData)
{
    const uint fftResolution = uint(oceanSamplingData.z + 0.5f);
    const float inverseDomainLength = oceanSamplingData.w;

    if (fftResolution < 2u || inverseDomainLength <= 0.000001f)
    {
        return;
    }

    const float3 interpolatedWorldNormal = normalize(fftWorldNormal);
    const float4 normalFoam = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam,
        oceanSamplingData.xy,
        fftResolution,
        inverseDomainLength);
    const float3 localNormal = normalize(normalFoam.xyz);
    const float3 pixelWorldNormal = normalize(
        localNormal.x * normalize(worldAxisX) +
        localNormal.y * normalize(worldAxisY) +
        localNormal.z * normalize(worldAxisZ));

    // 画素がFFTセルより大きくなった領域だけ、VSで低域化済みの法線へ戻す。
    // 追加サンプルなしで遠景の点状反射とモアレを抑え、近景の波形は維持する。
    const float fftCellSize = 1.0f /
        max(inverseDomainLength * float(fftResolution), 0.0001f);
    const float worldFootprint = max(
        length(ddx(oceanSamplingData.xy)),
        length(ddy(oceanSamplingData.xy)));
    const float footprintInCells = worldFootprint / fftCellSize;
    const float normalFilterWeight = smoothstep(0.75f, 3.5f, footprintInCells);
    fftWorldNormal = normalize(lerp(
        pixelWorldNormal,
        interpolatedWorldNormal,
        normalFilterWeight));

    // 圧縮泡だけを画素単位で取得する。波高は低周波なのでVS補間を使い帯域を増やさない。
    oceanData.x = saturate(normalFoam.w);
}

float2 RotateOceanDetailVector(float2 sourceVector, float cosineValue, float sineValue)
{
    return float2(
        sourceVector.x * cosineValue - sourceVector.y * sineValue,
        sourceVector.x * sineValue + sourceVector.y * cosineValue);
}

float2 ConvertOceanLocalNormalToSlope(float3 localNormal)
{
    const float safeVerticalNormal = max(abs(localNormal.y), 0.16f);
    return -localNormal.xz / safeVerticalNormal;
}

//============================================================
// 階層Normal合成
//============================================================

float3 CombineOceanReorientedNormal(float3 baseNormal, float3 detailNormal)
{
    const float3 safeBaseNormal = normalize(baseNormal);
    const float3 safeDetailNormal = normalize(detailNormal);
    const float2 combinedHorizontal =
        safeBaseNormal.xz * safeDetailNormal.y +
        safeDetailNormal.xz * safeBaseNormal.y;
    const float combinedVertical =
        safeBaseNormal.y * safeDetailNormal.y -
        dot(safeBaseNormal.xz, safeDetailNormal.xz);

    // Largeの方向へDetailを接続するRNM相当の合成。
    // 単純なNormal加算と異なり、急斜面でもLargeの接平面を基準に中波を保持する。
    return normalize(float3(
        combinedHorizontal.x,
        max(combinedVertical, 0.08f),
        combinedHorizontal.y));
}

OceanNormalLayers ResolveOceanLayeredOpticalNormals(
    float4 oceanSamplingData,
    float3 worldAxisX,
    float3 worldAxisY,
    float3 worldAxisZ,
    float3 geometryFftWorldNormal,
    float3 resolvedFftWorldNormal,
    float perPixelDisplacementWeight,
    float detailNormalStrength,
    float mediumWaveStrength,
    float detailFilterSharpness,
    float crestResponse,
    float crestDetailBoost)
{
    OceanNormalLayers normalLayers;
    normalLayers.mediumNormal = normalize(geometryFftWorldNormal);
    normalLayers.fineNormal = normalLayers.mediumNormal;
    const uint fftResolution = uint(oceanSamplingData.z + 0.5f);
    const float inverseDomainLength = oceanSamplingData.w;
    const float safeDetailStrength = max(detailNormalStrength, 0.0f);

    if (fftResolution < 2u || inverseDomainLength <= 0.000001f)
    {
        return normalLayers;
    }

    const float3 axisX = normalize(worldAxisX);
    const float3 axisY = normalize(worldAxisY);
    const float3 axisZ = normalize(worldAxisZ);
    const float3 geometryLocalNormal = normalize(float3(
        dot(geometryFftWorldNormal, axisX),
        dot(geometryFftWorldNormal, axisY),
        dot(geometryFftWorldNormal, axisZ)));
    const float3 resolvedLocalNormal = normalize(float3(
        dot(resolvedFftWorldNormal, axisX),
        dot(resolvedFftWorldNormal, axisY),
        dot(resolvedFftWorldNormal, axisZ)));

    const float fftCellSize = 1.0f /
        max(inverseDomainLength * float(fftResolution), 0.0001f);
    const float worldFootprint = max(
        length(ddx(oceanSamplingData.xy)),
        length(ddy(oceanSamplingData.xy)));
    const float footprintInCells = worldFootprint / fftCellSize;
    const float filterSharpness = clamp(detailFilterSharpness, 0.5f, 2.5f);

    // 周波数別の画素占有率で帯域を落とす。従来はFineが0.18セルから減衰し、
    // 近距離でも細部を失っていたため、視認可能な帯域を少し長く保持する。
    const float mediumFilter = 1.0f - smoothstep(
        0.35f * filterSharpness,
        1.65f * filterSharpness,
        footprintInCells * 2.15f);
    const float fineFilter = 1.0f - smoothstep(
        0.18f * filterSharpness,
        1.10f * filterSharpness,
        footprintInCells * 5.35f);

    if (mediumFilter <= 0.0001f)
    {
        return normalLayers;
    }

    // 谷では細波を少し抑え、曲率を伴う波頭ほど細かな乱れを増やす。
    // Foamの白さとは独立し、Normalの帯域強度だけを変える。
    const float crestDetailScale = lerp(
        0.88f,
        1.0f + max(crestDetailBoost, 0.0f),
        saturate(crestResponse));
    const float resolvedDetailStrength =
        (1.0f - exp(-safeDetailStrength)) * crestDetailScale;

    // MediumとFineを別帯域として扱う。従来はMediumまでFine用Detail強度へ
    // 乗算していたため、Detailが低いSceneでは中波がほぼ消えていた。
    const float resolvedMediumStrength =
        1.0f - exp(-max(mediumWaveStrength, 0.0f));
    const float2 geometrySlope = ConvertOceanLocalNormalToSlope(geometryLocalNormal);
    const float2 resolvedSlope = ConvertOceanLocalNormalToSlope(resolvedLocalNormal);
    const float2 unresolvedSlope = resolvedSlope - geometrySlope;

    // GeometryのNyquist境界を跨いで急にNormalへ切り替えず、同一FFT場の残差を
    // MediumからFineへ連続配分する。高低差と表面の細かさを別々に制御する。
    const float mediumCrestScale = lerp(
        0.94f,
        crestDetailScale,
        0.38f);
    const float mediumBandWeight = saturate(
        mediumFilter *
        lerp(0.52f, 0.88f, resolvedMediumStrength) *
        mediumCrestScale *
        lerp(1.0f, 1.08f, saturate(perPixelDisplacementWeight)));
    const float fineBandWeight = saturate(
        fineFilter * resolvedDetailStrength);
    const float2 mediumResolvedSlope =
        geometrySlope + unresolvedSlope * mediumBandWeight;
    const float2 fineResolvedSlope = lerp(
        mediumResolvedSlope,
        resolvedSlope,
        fineBandWeight);
    const float3 mediumLayeredLocalNormal = normalize(float3(
        -mediumResolvedSlope.x,
        1.0f,
        -mediumResolvedSlope.y));
    const float3 fineLayeredLocalNormal = normalize(float3(
        -fineResolvedSlope.x,
        1.0f,
        -fineResolvedSlope.y));
    normalLayers.mediumNormal = normalize(
        mediumLayeredLocalNormal.x * axisX +
        mediumLayeredLocalNormal.y * axisY +
        mediumLayeredLocalNormal.z * axisZ);
    normalLayers.fineNormal = normalize(
        fineLayeredLocalNormal.x * axisX +
        fineLayeredLocalNormal.y * axisY +
        fineLayeredLocalNormal.z * axisZ);
    return normalLayers;
}

float3 BuildOceanDisplacedGeometryNormal(
    float3 worldPosition,
    float3 interpolatedFftNormal)
{
    const float fftNormalLengthSquared = dot(interpolatedFftNormal, interpolatedFftNormal);

    if (fftNormalLengthSquared > 0.0000001f)
    {
        // FFT法線は連続場なので、強い鏡面光でも三角形単位の面法線へ戻さない。
        return interpolatedFftNormal * rsqrt(fftNormalLengthSquared);
    }

    const float3 positionDx = ddx(worldPosition);
    const float3 positionDy = ddy(worldPosition);
    float3 geometryNormal = cross(positionDx, positionDy);
    const float geometryNormalLengthSquared = dot(geometryNormal, geometryNormal);

    if (geometryNormalLengthSquared <= 0.0000001f)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }

    geometryNormal *= rsqrt(geometryNormalLengthSquared);
    return geometryNormal.y < 0.0f ? -geometryNormal : geometryNormal;
}

OceanSurfaceFrame EvaluateOceanSurfaceFrame(
    float3 interpolatedFftNormal,
    float3 worldPosition,
    float4 oceanData,
    float detailNormalStrength,
    float crestSharpness)
{
    OceanSurfaceFrame surfaceFrame;
    const float interpolatedNormalLength = length(interpolatedFftNormal);
    surfaceFrame.macroNormal = BuildOceanDisplacedGeometryNormal(
        worldPosition,
        interpolatedFftNormal);
    const float signedWaveHeight = clamp(oceanData.w, -1.0f, 1.0f);
    surfaceFrame.crest = smoothstep(0.18f, 0.92f, max(signedWaveHeight, 0.0f));
    surfaceFrame.trough = smoothstep(0.12f, 0.88f, max(-signedWaveHeight, 0.0f));
    surfaceFrame.slope = smoothstep(
        0.04f,
        0.62f,
        1.0f - saturate(surfaceFrame.macroNormal.y));

    // FFTスペクトルに含まれる法線を唯一の波面法線とする。
    // 別系統の疑似細波を重ねず、形状・陰影・反射で同じ波を参照する。
    surfaceFrame.shadingNormal = surfaceFrame.macroNormal;

    // 画面微分からワールド長基準の符号付き曲率を作る。
    // 正値は凸方向の波頭、負値は凹方向の谷。追加FFTサンプルは発生しない。
    const float3 positionDerivativeX = ddx(worldPosition);
    const float3 positionDerivativeY = ddy(worldPosition);
    const float3 normalDerivativeX = ddx(surfaceFrame.macroNormal);
    const float3 normalDerivativeY = ddy(surfaceFrame.macroNormal);
    const float positionFootprintSquared =
        dot(positionDerivativeX, positionDerivativeX) +
        dot(positionDerivativeY, positionDerivativeY);
    const float curvatureNumerator =
        dot(normalDerivativeX, positionDerivativeX) +
        dot(normalDerivativeY, positionDerivativeY);
    surfaceFrame.signedCurvature = curvatureNumerator /
        max(positionFootprintSquared, 0.0001f);

    // 画面微分Curvatureは診断値として保持するが、急斜面で発散しない範囲へ制限する。
    surfaceFrame.signedCurvature = clamp(surfaceFrame.signedCurvature, -0.18f, 0.18f);
    surfaceFrame.curvature = saturate(abs(surfaceFrame.signedCurvature) * 3.0f);
    // 単位法線を頂点補間すると長さが縮む。その縮みを法線分散として鏡面AAへ使う。
    surfaceFrame.interpolationVariance = saturate(1.0f - interpolatedNormalLength);
    return surfaceFrame;
}

float EvaluateOceanFoam(
    float3 worldPosition,
    float4 oceanData,
    OceanSurfaceFrame surfaceFrame,
    float foamStrength,
    float foamThreshold,
    float crestSharpness)
{
    if (foamStrength <= 0.0001f)
    {
        return 0.0f;
    }

    const float safeThreshold = min(saturate(foamThreshold), 0.999f);
    const float compressionWidth = max(fwidth(oceanData.x), 0.012f);
    const float compressionFoam = smoothstep(
        max(safeThreshold - compressionWidth, 0.0f),
        min(safeThreshold + compressionWidth * 2.0f, 1.0f),
        saturate(oceanData.x));
    const float breakingFoam =
        surfaceFrame.crest *
        smoothstep(0.08f, 0.72f, surfaceFrame.slope);
    const float foamCoverage = max(
        compressionFoam,
        breakingFoam * lerp(0.62f, 0.92f, saturate(crestSharpness)));
    const float breakup =
        sin(dot(worldPosition.xz, float2(0.73f, -0.68f)) * 0.82f + oceanData.y * 0.48f) *
        sin(dot(worldPosition.xz, float2(-0.31f, 0.95f)) * 0.37f - oceanData.y * 0.21f);
    const float continuousFoam = lerp(0.86f, 1.0f, breakup * 0.5f + 0.5f);
    const float antiAliasedCoverage = smoothstep(
        0.0f,
        max(fwidth(foamCoverage) * 0.90f, 0.020f),
        foamCoverage) * foamCoverage;
    return saturate(antiAliasedCoverage * continuousFoam * max(foamStrength, 0.0f));
}

float EvaluateOceanShallowWeight(
    float opticalPathLength,
    float normalizedWaveHeight,
    float absorptionDistance,
    float colorBlendScale)
{
    // Beer-Lambert則を基礎にしつつ、海中散乱を残して深部が黒く潰れないようにする。
    const float signedWaveHeight = clamp(normalizedWaveHeight, -1.0f, 1.0f);
    const float crest = max(signedWaveHeight, 0.0f);
    const float trough = max(-signedWaveHeight, 0.0f);
    const float heightDependentPathLength =
        max(opticalPathLength, 0.0f) *
        lerp(1.0f + trough * 0.08f, 1.0f - crest * 0.06f, step(0.0f, signedWaveHeight));
    const float opticalDepth =
        heightDependentPathLength / max(absorptionDistance, 0.1f);
    const float directTransmission = exp(-opticalDepth);
    const float multipleScattering = rcp(1.0f + opticalDepth * 0.72f);
    const float depthTransmission = lerp(
        directTransmission,
        multipleScattering,
        0.62f);
    const float crestTransmission =
        smoothstep(0.28f, 0.94f, crest) * 0.055f;
    return saturate(
        (depthTransmission + crestTransmission) *
        max(colorBlendScale, 0.01f));
}

float3 EvaluateOceanWaterColor(
    float3 deepColor,
    float3 shallowColor,
    float shallowWeight,
    float foam,
    OceanSurfaceFrame surfaceFrame,
    float normalizedWaveHeight,
    float normalDotView)
{
    const float3 safeDeepColor = max(deepColor, 0.0f);
    const float3 safeShallowColor = max(shallowColor, 0.0f);
    const float3 scatteredDeepColor = lerp(
        safeDeepColor,
        safeShallowColor,
        0.14f);
    const float smoothShallowWeight = smoothstep(
        0.02f,
        0.92f,
        saturate(shallowWeight));
    const float3 volumeColor = lerp(
        scatteredDeepColor,
        safeShallowColor,
        smoothShallowWeight);
    const float horizon = 1.0f - saturate(normalDotView);
    const float macroSlope = saturate(length(surfaceFrame.macroNormal.xz) * 1.4f);
    const float3 absorptionColor = scatteredDeepColor * float3(0.58f, 0.72f, 0.82f);
    const float3 transmittedColor = lerp(
        safeShallowColor * 0.92f,
        scatteredDeepColor * 0.74f,
        macroSlope * 0.62f);
    const float viewThrough =
        saturate(1.0f - horizon * 0.76f) *
        lerp(0.72f, 1.0f, smoothShallowWeight);
    float3 bodyColor = lerp(
        absorptionColor,
        transmittedColor,
        saturate(smoothShallowWeight * 0.58f + viewThrough * 0.28f));
    bodyColor = lerp(bodyColor, volumeColor, 0.28f);
    bodyColor = lerp(
        bodyColor,
        scatteredDeepColor * 0.78f,
        saturate(horizon * 0.10f + macroSlope * 0.14f));

    const float3 foamColor = float3(0.76f, 0.90f, 0.94f);
    return lerp(bodyColor, foamColor, saturate(foam) * 0.64f);
}

float3 EvaluateOceanOpticalNormal(OceanSurfaceFrame surfaceFrame)
{
    // 反射はFFT形状を主に使う。微細法線は輪郭を壊さない範囲だけ残す。
    return normalize(lerp(
        surfaceFrame.macroNormal,
        surfaceFrame.shadingNormal,
        0.24f));
}

float EvaluateOceanRoughness(
    float baseRoughness,
    float3 shadingNormal,
    float interpolationVariance,
    float crest,
    float foam)
{
    const float normalVariance = max(
        dot(ddx(shadingNormal), ddx(shadingNormal)),
        dot(ddy(shadingNormal), ddy(shadingNormal)));
    const float filteredRoughness = sqrt(
        baseRoughness * baseRoughness +
        interpolationVariance * 0.72f +
        min(normalVariance * 0.11f, 0.22f));

    // 波頭の立体感は形状と広い反射で見せ、点状の強い鏡面へは戻さない。
    const float crestRoughness = lerp(
        filteredRoughness,
        max(filteredRoughness * 0.96f, 0.10f),
        saturate(crest) * (1.0f - saturate(foam)));
    return clamp(lerp(crestRoughness, 0.68f, saturate(foam)), 0.10f, 1.0f);
}

#endif
