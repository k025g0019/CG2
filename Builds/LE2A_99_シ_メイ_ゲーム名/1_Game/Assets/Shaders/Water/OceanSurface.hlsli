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
    float3 fineNormal;        // Medium接平面へRNM合成済みのOptical Normal(Medium+Fine)。屈折・環境反射・Roughness AAで共用する保守的な強度。
    float3 fineMicroNormal;   // Fine Delta Slopeだけから作った合成前のDetail法線(World空間)
    float3 fineLobeNormal;    // Fine BRDF Lobe専用に傾きを増幅してRNM合成したMicro Surface法線(World空間)
    float3 fineTangentWorld;  // 異方性Fine BRDFの主軸(World空間)。Fine slope方向とMedium slope方向を連続ブレンドして安定化済み。
    float2 fineDeltaSlope;    // Mediumでは説明できない残差Slope(Local空間、Fineの実体)
    float2 fineLobeSlope;     // fineDeltaSlopeをLobe用に増幅したもの。Micro Roughnessの分散項もこれを使う。
    float2 mediumResolvedSlope;  // Medium帯のSlope(Local空間)。Debug比較用にFineと同じ形式で公開する。
    float fineFilter;         // 画素Footprintに基づく帯域制限weight(0=遠景/オーバーサンプリング, 1=近景)
    float fineHighPassVariance;  // 高域残差の局所分散(近傍4点のばらつき)。単一Sampleの振幅ではなくMicrofacet群の統計的粗さに使う。
};

//============================================================
// Fine Micro Surface の傾き増幅率
//------------------------------------------------------------
// detailNormalStrength は「残差のどこまでをFine帯として取り込むか」という帯域選択の重みで、
// Fine法線を何度傾けるかという振幅とは本来別概念。両者を共用していたため、Fine法線の傾きは
//   |unresolvedSlope| * (1 - mediumBandWeight) * (1 - exp(-detailNormalStrength))
// ≒ 残差の3% ≒ 0.2〜1度 にしかならず、Fine GGX Lobe の角幅 alpha = roughness^2 ≒ 0.07〜0.2度
// と同程度だった。法線の揺れがLobe幅を超えないと Fine Lobe と Medium Lobe はほぼ完全に重なり、
// 差分抽出しても「小さな残差」しか出ず、独立したハイライトとして分離しない。
//
// そこで BRDF Lobe へ渡す法線だけを増幅する。共用の fineNormal(屈折・環境反射・Roughness AA)は
// 従来の保守的な強度のまま据え置くので、以前の海蛇模様の経路(Fineが色や屈折を線状に動かす)は
// 太らせない。増幅すると法線の画素あたり変化率が上がるため、ハイライトは「大きく」ではなく
// 「小さく・多く」なる方向へ動く(理想画像の「大量の短い微細反射」はこの向き)。
static const float kOceanFineLobeSlopeGain = 8.0f;

//============================================================
// FFT Pixel Surface
//============================================================

StructuredBuffer<float4> gOceanPixelNormalFoam : register(t15);
StructuredBuffer<float4> gOceanPixelDisplacement : register(t14);

float2 RotateOceanDetailVector(float2 sourceVector, float cosineValue, float sineValue);
float2 ConvertOceanLocalNormalToSlope(float3 localNormal);
float2 ResolveOceanFineHighPassSlope(
    float4 oceanSamplingData,
    float highPassRadiusInCells,
    out float highPassVariance);

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

// Fine帯のBand Pass本体。
//------------------------------------------------------------
// 中心Sampleと、実世界スケール固定半径の4近傍平均(Low-Pass)との差をHigh-Passとして返す。
//
// 重要なのは「中心もLow-Passも同一の生バッファ(gOceanPixelNormalFoam)から取る」こと。
// 前版はHigh-Pass側にresolvedSlope(=ResolveOceanPixelSurfaceでvertex法線とfootprint混合
// 済みの値)を使い、Low-Pass側だけ生バッファを読んでいたため、距離が離れるほど
// 「footprint平滑化された信号 − 生信号の平均」という別物同士の引き算になり、
// 残差にMediumスケール成分が戻ってきていた。同一ソースにすることで、残差は常に
// 「半径以下の波長」だけを含む本物のFine帯になる。
//
// 半径はカメラ距離に依存しない固定Cell数。距離LODはこの後段でfineFilterを掛けて
// 行うので、帯域の定義(どの波長がFineか)と表示量の制御(どこまで見せるか)が混ざらない。
//
// highPassVariance は近傍4点それぞれのHigh-Pass量のばらつき。単一Sampleの振幅ではなく
// 「その周辺にどれだけ微細な傾きが散らばっているか」というMicrofacet群の統計量なので、
// Micro Roughnessの元情報として単一波形の模様が出にくい。
float2 ResolveOceanFineHighPassSlope(
    float4 oceanSamplingData,
    float highPassRadiusInCells,
    out float highPassVariance)
{
    highPassVariance = 0.0f;
    const uint fftResolution = uint(oceanSamplingData.z + 0.5f);
    const float inverseDomainLength = oceanSamplingData.w;

    if (fftResolution < 2u || inverseDomainLength <= 0.000001f)
    {
        return float2(0.0f, 0.0f);
    }

    const float fftCellSize = 1.0f /
        max(inverseDomainLength * float(fftResolution), 0.0001f);
    const float radius = fftCellSize * max(highPassRadiusInCells, 0.5f);
    const float2 basePosition = oceanSamplingData.xy;

    const float3 centerNormal = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam, basePosition, fftResolution, inverseDomainLength).xyz;
    const float3 rightNormal = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam, basePosition + float2(radius, 0.0f), fftResolution, inverseDomainLength).xyz;
    const float3 leftNormal = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam, basePosition - float2(radius, 0.0f), fftResolution, inverseDomainLength).xyz;
    const float3 forwardNormal = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam, basePosition + float2(0.0f, radius), fftResolution, inverseDomainLength).xyz;
    const float3 backNormal = SampleOceanPixelBuffer(
        gOceanPixelNormalFoam, basePosition - float2(0.0f, radius), fftResolution, inverseDomainLength).xyz;

    const float2 centerSlope = ConvertOceanLocalNormalToSlope(normalize(centerNormal));
    const float2 rightSlope = ConvertOceanLocalNormalToSlope(normalize(rightNormal));
    const float2 leftSlope = ConvertOceanLocalNormalToSlope(normalize(leftNormal));
    const float2 forwardSlope = ConvertOceanLocalNormalToSlope(normalize(forwardNormal));
    const float2 backSlope = ConvertOceanLocalNormalToSlope(normalize(backNormal));

    // Low-Passは4近傍平均(中心を含めない純粋な周辺平均)。中心を含めると高域が
    // Low-Pass側へ漏れて分離が甘くなる。
    const float2 lowPassSlope =
        (rightSlope + leftSlope + forwardSlope + backSlope) * 0.25f;
    const float2 highPassSlope = centerSlope - lowPassSlope;

    // 近傍各点のHigh-Pass量の二乗平均。Microfacet群としての統計的な散らばりを表す。
    const float2 rightHighPass = rightSlope - lowPassSlope;
    const float2 leftHighPass = leftSlope - lowPassSlope;
    const float2 forwardHighPass = forwardSlope - lowPassSlope;
    const float2 backHighPass = backSlope - lowPassSlope;
    highPassVariance = (
        dot(rightHighPass, rightHighPass) +
        dot(leftHighPass, leftHighPass) +
        dot(forwardHighPass, forwardHighPass) +
        dot(backHighPass, backHighPass)) * 0.25f;

    return highPassSlope;
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
    const uint stepCount = clamp(
        uint(requestedStepCount + 0.5f),
        1u,
        6u);
    const float maximumOffset = fftCellSize * 0.85f;
    float opticalHeight = 0.0f;
    float2 opticalOffset = float2(0.0f, 0.0f);

    // 視差位置で同じFFTを再評価する固定点反復。反復数を品質と収束へ実際に反映する。
    [loop]
    for (uint stepIndex = 0u; stepIndex < stepCount; stepIndex++)
    {
        const float4 centerDisplacement = SampleOceanPixelBuffer(
            gOceanPixelDisplacement,
            baseSamplingPosition + opticalOffset,
            fftResolution,
            inverseDomainLength);
        const float3 unresolvedDisplacement =
            centerDisplacement.xyz - surroundingDisplacement.xyz;
        const float targetHeight =
            unresolvedDisplacement.y * safeStrength * distanceWeight;
        float2 targetOffset =
            limitedViewDirection * targetHeight +
            unresolvedDisplacement.xz * safeStrength * distanceWeight * 0.18f;
        const float targetOffsetLength = length(targetOffset);

        if (targetOffsetLength > maximumOffset)
        {
            targetOffset *= maximumOffset / max(targetOffsetLength, 0.0001f);
        }

        opticalHeight = lerp(opticalHeight, targetHeight, 0.68f);
        opticalOffset = lerp(opticalOffset, targetOffset, 0.68f);
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

    // 画素のFFTセル占有率に応じ、VSで低域化済みの法線へ連続的に戻す。
    // smoothstepの開始・終了境界をカメラが横切ると波形状の線が出入りするため、
    // 閾値を持たない有理減衰で近景から遠景まで同じ応答にする。
    const float fftCellSize = 1.0f /
        max(inverseDomainLength * float(fftResolution), 0.0001f);
    const float worldFootprint = max(
        length(ddx(oceanSamplingData.xy)),
        length(ddy(oceanSamplingData.xy)));
    const float footprintInCells = worldFootprint / fftCellSize;
    const float normalFilterRatio = footprintInCells / 1.5f;
    const float normalFilterWeight =
        (normalFilterRatio * normalFilterRatio) /
        (1.0f + normalFilterRatio * normalFilterRatio);
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
    normalLayers.fineMicroNormal = normalLayers.mediumNormal;
    normalLayers.fineLobeNormal = normalLayers.mediumNormal;
    normalLayers.fineTangentWorld = normalize(worldAxisX);
    normalLayers.fineDeltaSlope = float2(0.0f, 0.0f);
    normalLayers.fineLobeSlope = float2(0.0f, 0.0f);
    normalLayers.mediumResolvedSlope = float2(0.0f, 0.0f);
    normalLayers.fineFilter = 0.0f;
    normalLayers.fineHighPassVariance = 0.0f;
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

    // Medium/Fineとも距離閾値でON/OFFせず、画素占有率に対して単調に減衰させる。
    // これによりカメラ移動時の帯域切替が等高線として見える状態を防ぐ。
    const float mediumFootprintRatio =
        footprintInCells * 2.15f / max(0.82f * filterSharpness, 0.10f);
    const float fineFootprintRatio =
        footprintInCells * 5.35f / max(0.56f * filterSharpness, 0.10f);
    const float mediumFilter = rcp(
        1.0f + mediumFootprintRatio * mediumFootprintRatio);
    const float fineFilter = rcp(
        1.0f + fineFootprintRatio * fineFootprintRatio);

    // 谷では細波を少し抑え、曲率を伴う波頭ほど細かな乱れを増やす。
    // Foamの白さとは独立し、Normalの帯域強度だけを変える。
    const float crestDetailScale = lerp(
        0.88f,
        1.0f + max(crestDetailBoost, 0.0f),
        saturate(crestResponse));
    // crestDetailScaleはcrestResponse(Macro波頭)由来の包絡なので、そのままFineの振幅へ
    // 掛けるとFineの分布がMacro波頭の形をなぞってしまう(Debug 10「中波構造」とDebug 20
    // 「Fine Sun Specular」が強く相関していた直接原因)。Inspectorの「波頭の細かさ」を
    // 完全に殺さないよう影響は残すが、包絡として支配的にならない比率まで落とす。
    // Medium側のmediumCrestScaleは従来どおり(Medium構造は変更しない)。
    const float fineCrestScale = lerp(1.0f, crestDetailScale, 0.22f);
    const float resolvedDetailStrength =
        (1.0f - exp(-safeDetailStrength)) * fineCrestScale;

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

    // Fine = Mediumとの引き算ではなく、生FFT法線バッファ上でのBand Pass残差として定義する。
    // 従来の引き算は
    //   fineResolvedSlope - mediumResolvedSlope
    //     = lerp(mediumResolvedSlope, resolvedSlope, fineBandWeight) - mediumResolvedSlope
    //     = fineBandWeight * (1 - mediumBandWeight) * unresolvedSlope
    // と展開でき、MediumもFineも同じunresolvedSlope(Macro以上の全周波数)をスカラー係数だけ
    // 変えて使っていただけで、周波数としては全く分離されていなかった。
    // ResolveOceanFineHighPassSlopeは中心もLow-Passも同一の生バッファから取るため、
    // 残差は常に「半径以下の波長」だけを含む本物のFine帯になる。
    const float fineHighPassRadiusInCells = 2.0f;
    float fineHighPassVariance = 0.0f;
    const float2 fineHighPassSlope = ResolveOceanFineHighPassSlope(
        oceanSamplingData,
        fineHighPassRadiusInCells,
        fineHighPassVariance);
    // fineBandWeight(footprint LOD × detailNormalStrength)は周波数選択には使わず、
    // 既存どおり「どこまで見せるか」というLOD/強度ゲートとしてだけ最後に掛ける。
    const float2 fineDeltaSlope = fineHighPassSlope * fineBandWeight;

    const float3 mediumLayeredLocalNormal = normalize(float3(
        -mediumResolvedSlope.x,
        1.0f,
        -mediumResolvedSlope.y));
    // Fine Micro Normal: Fine Delta Slopeだけから作る、Medium形状を含まない小さな法線。
    const float3 fineMicroLocalNormal = normalize(float3(
        -fineDeltaSlope.x,
        1.0f,
        -fineDeltaSlope.y));
    // MediumのLocal接平面上へFine Micro NormalをRNM系(CombineOceanReorientedNormal)で合成する。
    // normalize(medium+fine)ではなく、Mediumの傾きを基準にFineを再配向するため、
    // 急斜面でもFineが浮いたり潰れたりしない。
    const float3 fineLayeredLocalNormal = CombineOceanReorientedNormal(
        mediumLayeredLocalNormal,
        fineMicroLocalNormal);

    // BRDF Lobe専用の増幅版Micro Surface。共用のfineNormalとは別に作り、同じRNM系で
    // Mediumの接平面へ合成する。CombineOceanReorientedNormalは合成後の垂直成分を
    // max(..., 0.08)で下限クランプするため、増幅しても法線が裏返らない。
    const float2 fineLobeSlope = fineDeltaSlope * kOceanFineLobeSlopeGain;
    const float3 fineLobeMicroLocalNormal = normalize(float3(
        -fineLobeSlope.x,
        1.0f,
        -fineLobeSlope.y));
    const float3 fineLobeLayeredLocalNormal = CombineOceanReorientedNormal(
        mediumLayeredLocalNormal,
        fineLobeMicroLocalNormal);

    normalLayers.mediumNormal = normalize(
        mediumLayeredLocalNormal.x * axisX +
        mediumLayeredLocalNormal.y * axisY +
        mediumLayeredLocalNormal.z * axisZ);
    normalLayers.fineNormal = normalize(
        fineLayeredLocalNormal.x * axisX +
        fineLayeredLocalNormal.y * axisY +
        fineLayeredLocalNormal.z * axisZ);
    normalLayers.fineMicroNormal = normalize(
        fineMicroLocalNormal.x * axisX +
        fineMicroLocalNormal.y * axisY +
        fineMicroLocalNormal.z * axisZ);
    normalLayers.fineLobeNormal = normalize(
        fineLobeLayeredLocalNormal.x * axisX +
        fineLobeLayeredLocalNormal.y * axisY +
        fineLobeLayeredLocalNormal.z * axisZ);

    // 異方性の主軸。Fine slopeが十分強い所ではその方向を、弱い所では低周波で安定している
    // Medium slope方向へ連続的に倒す(細波は中波の斜面に沿って走るので物理的にも自然)。
    // 閾値でパチンと切り替えないため、凪いだ面でも軸が毎フレーム回転して点滅することがない。
    const float fineDeltaSlopeLength = length(fineDeltaSlope);
    const float mediumSlopeLength = length(mediumResolvedSlope);
    const float2 stableFallbackDirection = mediumSlopeLength > 0.0001f
        ? mediumResolvedSlope / mediumSlopeLength
        : float2(1.0f, 0.0f);
    const float2 fineLocalDirection = fineDeltaSlopeLength > 0.0001f
        ? fineDeltaSlope / fineDeltaSlopeLength
        : stableFallbackDirection;
    const float directionConfidence = saturate(fineDeltaSlopeLength * 60.0f);
    const float2 blendedDirection = lerp(
        stableFallbackDirection,
        fineLocalDirection,
        directionConfidence);
    const float blendedDirectionLength = length(blendedDirection);
    const float2 resolvedTangentDirection = blendedDirectionLength > 0.0001f
        ? blendedDirection / blendedDirectionLength
        : stableFallbackDirection;
    normalLayers.fineTangentWorld = normalize(
        axisX * resolvedTangentDirection.x +
        axisZ * resolvedTangentDirection.y);
    normalLayers.fineDeltaSlope = fineDeltaSlope;
    normalLayers.fineLobeSlope = fineLobeSlope;
    normalLayers.mediumResolvedSlope = mediumResolvedSlope;
    normalLayers.fineFilter = fineFilter;
    // 局所分散もLOD/強度ゲートと同じ重みの二乗で絞る(振幅がw倍なら分散はw^2倍)。
    normalLayers.fineHighPassVariance =
        fineHighPassVariance * fineBandWeight * fineBandWeight;
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
    // 波頭Maskだけでは斜面全体が泡候補になりやすい。波頭を二乗して中心へ
    // 寄せ、十分に急な斜面だけを砕波として扱うことで細い白波を残す。
    const float breakingFoam =
        surfaceFrame.crest * surfaceFrame.crest *
        smoothstep(0.24f, 0.78f, surfaceFrame.slope);
    const float foamCoverage = max(
        compressionFoam,
        breakingFoam * lerp(0.62f, 0.92f, saturate(crestSharpness)));

    //============================================================
    // 泡の破砕 (3階層 breakup)
    //============================================================
    // 泡がどこに出るかは compressionFoam / breakingFoam(波頭・急Slope)が決める。
    // ここではその「泡候補領域の内部だけ」を破砕し、白い板を千切れた泡片へ変える。
    // 水面全体へ一様なノイズは乗せない(foamCoverage が 0 の所は何も起きない)。
    const float2 foamPosition = worldPosition.xz;
    const float foamTime = oceanData.y;

    // 大: 帯そのものを長い区間で分断し、画面端まで一本に繋がらないようにする
    const float largeBreakup =
        sin(dot(foamPosition, float2(0.41f, -0.36f)) * 0.34f + foamTime * 0.42f) *
        sin(dot(foamPosition, float2(-0.28f, 0.52f)) * 0.27f - foamTime * 0.29f);
    // 中: 幅を一定にせず、太い所と細い所と枝分かれを作る
    const float mediumBreakup =
        sin(dot(foamPosition, float2(0.73f, -0.68f)) * 1.05f + foamTime * 0.85f) *
        sin(dot(foamPosition, float2(-0.31f, 0.95f)) * 0.88f - foamTime * 0.58f);
    // 細: 輪郭を細かく崩し、泡片の縁をギザつかせる
    const float fineBreakup =
        sin(dot(foamPosition, float2(1.40f, 0.60f)) * 2.55f + foamTime * 1.25f) *
        sin(dot(foamPosition, float2(-0.85f, 1.25f)) * 2.10f - foamTime * 0.95f);

    const float combinedBreakup =
        (largeBreakup * 0.5f + 0.5f) * 0.46f +
        (mediumBreakup * 0.5f + 0.5f) * 0.34f +
        (fineBreakup * 0.5f + 0.5f) * 0.20f;
    // 3層の加重平均は0.5付近へ集中するため、そのまま閾値に掛けると
    // 全面が閾値際=中間濃度になってしまう。コントラストを広げて0/1側へ寄せる。
    const float breakupNoise = saturate((combinedBreakup - 0.5f) * 2.4f + 0.5f);

    // 破砕は「乗算してから閾値で戻す」方式にする。
    // 従来の (coverage - e) / (1 - e) は coverage が 1.0 の内部で常に 1.0 を返すため、
    // 大きな泡領域の中に穴が一切開かず、白い板のまま残っていた。
    // 乗算なら内部(coverage=1.0)でも brokenCoverage = noise となり、
    // ノイズが低い場所へ実際に穴が開く。閾値を超えた画素は完全な白へ戻るので、
    // ノイズによって灰色の中間色が生まれることはない。
    const float brokenCoverage = foamCoverage * breakupNoise;

    // 中間濃度を潰して「泡が有るか無いか」へ寄せる。
    // 完全な二値はちらつくため、fwidth ぶん(輪郭1〜2px相当)だけAA幅を残す。
    // ここを広げて半透明グラデーションにはしない。
    const float hardenWidth = max(fwidth(brokenCoverage) * 1.2f, 0.045f);
    const float hardenedCoverage = smoothstep(
        0.36f - hardenWidth,
        0.36f + hardenWidth,
        brokenCoverage);

    return saturate(hardenedCoverage * max(foamStrength, 0.0f));
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
