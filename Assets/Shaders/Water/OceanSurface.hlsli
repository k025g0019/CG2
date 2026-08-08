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
    float interpolationVariance;
};

//============================================================
// FFT Pixel Surface
//============================================================

StructuredBuffer<float4> gOceanPixelNormalFoam : register(t15);

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

float3 ResolveOceanLayeredOpticalNormal(
    float4 oceanSamplingData,
    float3 worldAxisX,
    float3 worldAxisY,
    float3 worldAxisZ,
    float3 resolvedFftWorldNormal,
    float detailNormalStrength,
    float crestResponse)
{
    const uint fftResolution = uint(oceanSamplingData.z + 0.5f);
    const float inverseDomainLength = oceanSamplingData.w;
    const float safeDetailStrength = max(detailNormalStrength, 0.0f);

    if (fftResolution < 2u ||
        inverseDomainLength <= 0.000001f ||
        safeDetailStrength <= 0.0001f)
    {
        return normalize(resolvedFftWorldNormal);
    }

    const float3 axisX = normalize(worldAxisX);
    const float3 axisY = normalize(worldAxisY);
    const float3 axisZ = normalize(worldAxisZ);
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

    // 高周波ほど早くフェードさせ、遠景で1画素未満になる帯域を残さない。
    const float mediumFilter = 1.0f - smoothstep(
        0.35f,
        1.65f,
        footprintInCells * 2.15f);
    const float fineFilter = 1.0f - smoothstep(
        0.18f,
        1.10f,
        footprintInCells * 5.35f);

    if (mediumFilter <= 0.0001f)
    {
        return normalize(resolvedFftWorldNormal);
    }

    // 同じFFT場を異なる向きと縮尺で読む。乱数法線を足さないため、
    // 大波の連続性を壊さず中波と微細波だけを光学法線へ補う。
    const float mediumCosine = 0.819152f;
    const float mediumSine = 0.573576f;
    const float fineCosine = 0.906308f;
    const float fineSine = -0.422618f;
    const float domainLength = rcp(inverseDomainLength);
    const float2 mediumPosition =
        RotateOceanDetailVector(
            oceanSamplingData.xy,
            mediumCosine,
            mediumSine) * 2.15f +
        domainLength * float2(0.173f, -0.291f);
    const float3 mediumLocalNormal = normalize(SampleOceanPixelBuffer(
        gOceanPixelNormalFoam,
        mediumPosition,
        fftResolution,
        inverseDomainLength).xyz);
    float3 fineLocalNormal = float3(0.0f, 1.0f, 0.0f);

    if (fineFilter > 0.0001f)
    {
        const float2 finePosition =
            RotateOceanDetailVector(
                oceanSamplingData.xy,
                fineCosine,
                fineSine) * 5.35f +
            domainLength * float2(-0.347f, 0.119f);
        fineLocalNormal = normalize(SampleOceanPixelBuffer(
            gOceanPixelNormalFoam,
            finePosition,
            fftResolution,
            inverseDomainLength).xyz);
    }

    const float crestDetailScale = lerp(
        0.86f,
        1.14f,
        saturate(crestResponse));
    const float resolvedDetailStrength =
        (1.0f - exp(-safeDetailStrength)) * crestDetailScale;
    const float2 mediumSlope = RotateOceanDetailVector(
        ConvertOceanLocalNormalToSlope(mediumLocalNormal),
        mediumCosine,
        -mediumSine);
    const float2 fineSlope = RotateOceanDetailVector(
        ConvertOceanLocalNormalToSlope(fineLocalNormal),
        fineCosine,
        -fineSine);
    const float2 combinedSlope =
        ConvertOceanLocalNormalToSlope(resolvedLocalNormal) +
        mediumSlope * (0.18f * resolvedDetailStrength * mediumFilter) +
        fineSlope * (0.075f * resolvedDetailStrength * fineFilter);
    const float3 layeredLocalNormal = normalize(float3(
        -combinedSlope.x,
        1.0f,
        -combinedSlope.y));
    return normalize(
        layeredLocalNormal.x * axisX +
        layeredLocalNormal.y * axisY +
        layeredLocalNormal.z * axisZ);
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

    // 曲率は泡の発生補助だけへ使い、Albedoを直接暗くする用途には使わない。
    surfaceFrame.curvature = saturate(
        (length(ddx(surfaceFrame.macroNormal)) + length(ddy(surfaceFrame.macroNormal))) * 1.5f);
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
        saturate(surfaceFrame.slope * 0.65f + surfaceFrame.curvature * 0.55f);
    const float foamCoverage = max(
        compressionFoam,
        breakingFoam * lerp(0.62f, 0.92f, saturate(crestSharpness)));
    const float breakup =
        sin(dot(worldPosition.xz, float2(0.73f, -0.68f)) * 0.82f + oceanData.y * 0.48f) *
        sin(dot(worldPosition.xz, float2(-0.31f, 0.95f)) * 0.37f - oceanData.y * 0.21f);
    const float continuousFoam = lerp(0.86f, 1.0f, breakup * 0.5f + 0.5f);
    const float antiAliasedCoverage = smoothstep(
        0.0f,
        max(fwidth(foamCoverage) * 1.5f, 0.035f),
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
