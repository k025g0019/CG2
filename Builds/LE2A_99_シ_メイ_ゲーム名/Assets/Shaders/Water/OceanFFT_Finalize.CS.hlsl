cbuffer OceanFftConstants : register(b0)
{
    float gOceanTime;
    float gGravity;
    float gWaterDepth;
    uint gFftResolution;
    float gDomainLength;
    float gMaxWaveHeight;
    float gChoppiness;
    float gFoamThreshold;
    float gFoamStrength;
    float gCrestSharpness;
    float gHeightScale;
    uint gSurfaceSampleCount;
    float4 gOceanPadding;
};

struct OceanSurfaceSampleRequest
{
    float2 localPosition;
    uint2 sampleKey;
};

struct OceanSurfaceSampleResult
{
    float4 surfacePositionFoam;
    float4 surfaceNormal;
    float4 displacementAndTime;
    uint2 sampleKey;
    float2 padding;
};

RWStructuredBuffer<float4> gDisplacementOutput : register(u0);
RWStructuredBuffer<float4> gNormalFoamOutput : register(u1);
RWStructuredBuffer<float2> gHeightField : register(u2);
RWStructuredBuffer<float2> gDisplacementXField : register(u3);
RWStructuredBuffer<float2> gDisplacementZField : register(u4);
RWStructuredBuffer<float2> gGradientXField : register(u5);
RWStructuredBuffer<float2> gGradientZField : register(u6);
RWStructuredBuffer<OceanSurfaceSampleResult> gSurfaceSampleOutput : register(u7);
StructuredBuffer<OceanSurfaceSampleRequest> gSurfaceSampleRequests : register(t0);
StructuredBuffer<float4> gPreviousNormalFoam : register(t1);

uint WrapIndex(int index)
{
    return uint(index) & (gFftResolution - 1u);
}

uint GetFieldIndex(int x, int z)
{
    return WrapIndex(z) * gFftResolution + WrapIndex(x);
}

float GetLinearHeight(int x, int z)
{
    return gHeightField[GetFieldIndex(x, z)].x * gHeightScale;
}

float SoftLimitWaveHeight(float height)
{
    const float heightLimit = max(gMaxWaveHeight, 0.001f);
    const float absoluteHeight = abs(height);
    const float softKnee = heightLimit * 0.82f;

    if (absoluteHeight <= softKnee)
    {
        return height;
    }

    const float kneeRange = max(heightLimit - softKnee, 0.001f);
    const float normalizedOverflow = (absoluteHeight - softKnee) / kneeRange;
    const float limitedMagnitude = softKnee +
        kneeRange * (1.0f - exp(-normalizedOverflow));
    return sign(height) * min(limitedMagnitude, heightLimit);
}

float GetHeight(int x, int z)
{
    const uint fieldIndex = GetFieldIndex(x, z);
    const float linearHeight = GetLinearHeight(x, z);
    const float2 primaryDirection = normalize(
        gOceanPadding.yz + float2(0.0001f, 0.0f));
    const float2 linearGradient = float2(
        gGradientXField[fieldIndex].x,
        gGradientZField[fieldIndex].x) * gHeightScale * 0.5f;
    const float directionalSlope = clamp(
        dot(linearGradient, primaryDirection),
        -1.5f,
        1.5f);
    // h * dh/ds は位相の二倍角成分になり、平均海面を動かさず波頭を進行方向へ寄せる。
    const float crestOffset = linearHeight * directionalSlope *
        saturate(gCrestSharpness) * 0.12f;
    return SoftLimitWaveHeight(linearHeight + crestOffset);
}

float FilterPersistentFoamSource(float rawFoam)
{
    // 表示閾値付近の弱いJacobian値は現在フレームだけに使い、履歴へ拡散させない。
    // 二値で切ると履歴が点状のスパース場になり、移流で細い糸状の筋へ伸びるため、
    // 閾値の前後を滑らかに繋いで履歴側の空間周波数を上げすぎないようにする。
    const float persistenceThreshold = min(
        saturate(gFoamThreshold) + 0.08f,
        0.96f);
    const float persistenceFade = smoothstep(
        persistenceThreshold - 0.06f,
        persistenceThreshold + 0.06f,
        rawFoam);
    return rawFoam * persistenceFade;
}

void SamplePreviousFoamNeighborhood(
    float2 gridPosition,
    out float advectedFoam,
    out float diffusedFoam)
{
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float foam00 = FilterPersistentFoamSource(
        gPreviousNormalFoam[GetFieldIndex(baseIndex.x, baseIndex.y)].w);
    const float foam10 = FilterPersistentFoamSource(
        gPreviousNormalFoam[GetFieldIndex(baseIndex.x + 1, baseIndex.y)].w);
    const float foam01 = FilterPersistentFoamSource(
        gPreviousNormalFoam[GetFieldIndex(baseIndex.x, baseIndex.y + 1)].w);
    const float foam11 = FilterPersistentFoamSource(
        gPreviousNormalFoam[GetFieldIndex(baseIndex.x + 1, baseIndex.y + 1)].w);

    advectedFoam = lerp(
        lerp(foam00, foam10, interpolation.x),
        lerp(foam01, foam11, interpolation.x),
        interpolation.y);
    diffusedFoam = (foam00 + foam10 + foam01 + foam11) * 0.25f;
}

float ResolvePersistentFoam(int x, int z, float generatedFoam)
{
    const float deltaTime = max(gOceanPadding.x, 0.0f);

    if (deltaTime <= 0.000001f)
    {
        return generatedFoam;
    }

    const float cellSize = max(gDomainLength / float(gFftResolution), 0.001f);
    const float2 advectionDirection = normalize(
        gOceanPadding.yz + float2(0.0001f, 0.0f));
    const float representativeWaveNumber = 2.0f * 3.14159265359f * 8.0f /
        max(gDomainLength, 0.001f);
    const float phaseVelocity = sqrt(
        max(gGravity, 0.001f) /
        max(representativeWaveNumber, 0.0001f));
    const float2 backtracedGridPosition = float2(x, z) -
        advectionDirection * phaseVelocity * deltaTime / cellSize;
    float historyFoam = 0.0f;
    float diffusedFoam = 0.0f;
    SamplePreviousFoamNeighborhood(
        backtracedGridPosition,
        historyFoam,
        diffusedFoam);
    // 拡散はフレームレート非依存の指数レートで与える。従来の saturate(deltaTime * 0.85f) は
    // 60fpsで約1.4%しか拡散せず、実質「拡散のない純移流」だった。拡散を伴わない移流は
    // スパースな泡を移流方向へ細いフィラメントとして引き伸ばし続けるため、水面へ糸状の
    // 筋が焼き付いたまま消えない(泡表示をOFFにしても、この値はCausticsや波頭Hazeも
    // 駆動するため線が残る)。実際の海の泡列も筋にはなるが、乱流拡散で常に幅を持って崩れる。
    // 拡散を強くしすぎると、泡が波頭の筋ではなく広いベタ面へ均されてしまう。
    // Jacobian閾値が正しく負値判定になった今は泡の発生源自体が疎なので、
    // 糸状の焼き付きを防げる最小限まで拡散を下げ、筋・破片の形を保つ。
    const float foamDiffusionRate = 4.0f;  // 1秒あたりの拡散の速さ
    const float diffusionWeight = saturate(
        1.0f - exp(-deltaTime * foamDiffusionRate));
    const float persistentFoam = lerp(
        historyFoam,
        diffusedFoam,
        diffusionWeight);
    const float foamLifetime = max(gOceanPadding.w, 0.25f);
    const float decayedFoam = persistentFoam * exp(-deltaTime / foamLifetime);
    return saturate(max(generatedFoam, decayedFoam));
}

// クランプ前の生の横変位。頂点位置には使わず、法線補正とJacobian(泡)の判定だけに使う。
// クランプ後の値で勾配を取ると、折返し防止で潰した分だけJacobianが1.0側へ寄ってしまい、
// 「実際に折り重なった場所」を検出できなくなる(元実装も生の変位で判定している)。
float2 GetRawHorizontalDisplacement(int x, int z)
{
    const uint fieldIndex = GetFieldIndex(x, z);
    return float2(
        gDisplacementXField[fieldIndex].x,
        gDisplacementZField[fieldIndex].x) *
        gHeightScale * (-gChoppiness * 2.2f);
}

float2 GetHorizontalDisplacement(int x, int z)
{
    const uint fieldIndex = GetFieldIndex(x, z);
    const float cellSize = max(gDomainLength / float(gFftResolution), 0.001f);

    //============================================================
    // 水平変位の折返し防止
    //============================================================

    // Choppy Waveの横変位は波頭を尖らせ、折り重なった場所を白波にする主役。
    // 上限を cellSize の定数倍で決めると、解像度を上げるほど cellSize が縮んで
    // 押し出し量まで勝手に減ってしまう。元のFFT海面実装(FFocean3D)は
    //   上限 = cellSize * 34.0 * (解像度 / 2048)
    // と比率側を解像度で補正して、ワールド座標での押し出し量を一定に保っていた。
    // これは cellSize = domainLength / 解像度 なので domainLength だけで決まる。
    // 移植時にこの補正が落ちて解像度依存になっていたため、同じ式へ戻す。
    const float kHorizontalClampPerDomain = 34.0f / 2048.0f;
    const float displacementLimit = min(
        max(gMaxWaveHeight * 2.0f, 0.01f),
        gDomainLength * kHorizontalClampPerDomain);
    const float2 rawDisplacement =
        float2(
            gDisplacementXField[fieldIndex].x,
            gDisplacementZField[fieldIndex].x) *
        gHeightScale * (-gChoppiness * 2.2f);

    return tanh(rawDisplacement / max(displacementLimit, 0.001f)) *
        displacementLimit;
}

float GetSafeGradientDenominator(float value)
{
    // 水平面の向きが反転した値を法線へ伝播させない。
    return max(value, 0.08f);
}

float4 GetNormalFoamAtIndex(int x, int z)
{
    const float cellSize = max(gDomainLength / float(gFftResolution), 0.001f);
    const float centerHeight = GetHeight(x, z);
    const float leftHeight = GetHeight(x - 1, z);
    const float rightHeight = GetHeight(x + 1, z);
    const float backHeight = GetHeight(x, z - 1);
    const float frontHeight = GetHeight(x, z + 1);
    const float2 leftDisplacement = GetRawHorizontalDisplacement(x - 1, z);
    const float2 rightDisplacement = GetRawHorizontalDisplacement(x + 1, z);
    const float2 backDisplacement = GetRawHorizontalDisplacement(x, z - 1);
    const float2 frontDisplacement = GetRawHorizontalDisplacement(x, z + 1);
    const float2 displacementDerivativeX =
        (rightDisplacement - leftDisplacement) / (2.0f * cellSize);
    const float2 displacementDerivativeZ =
        (frontDisplacement - backDisplacement) / (2.0f * cellSize);
    // 最終的に尖り補正された高さから法線を作り、GeometryとLightingの形状を一致させる。
    float gradientX = (rightHeight - leftHeight) / (2.0f * cellSize);
    float gradientZ = (frontHeight - backHeight) / (2.0f * cellSize);
    gradientX /= GetSafeGradientDenominator(1.0f + displacementDerivativeX.x);
    gradientZ /= GetSafeGradientDenominator(1.0f + displacementDerivativeZ.y);
    float2 surfaceSlope = float2(gradientX, gradientZ);
    const float slopeLength = length(surfaceSlope);
    const float maximumSlope = 5.5f;

    if (slopeLength > maximumSlope)
    {
        surfaceSlope *= maximumSlope / slopeLength;
    }

    float3 surfaceNormal = normalize(float3(-surfaceSlope.x, 1.0f, -surfaceSlope.y));

    if (surfaceNormal.y < 0.0f)
    {
        surfaceNormal = -surfaceNormal;
    }

    const float jacobian =
        (1.0f + displacementDerivativeX.x) *
        (1.0f + displacementDerivativeZ.y) -
        displacementDerivativeZ.x * displacementDerivativeX.y;
    // Jacobianが負になった場所 = 横変位で水面が実際に折り重なった場所だけを白波にする。
    // 0.98のような正の閾値にすると、平常時1.0前後で振動するJacobianの半分近くが
    // 引っかかり、白波が「波頭の筋」ではなく「面」で出る。
    const float compressionFoam = jacobian < -0.12f
        ? saturate((-jacobian - 0.12f) * 0.9f)
        : 0.0f;
    const float laplacian =
        (leftHeight + rightHeight + backHeight + frontHeight - centerHeight * 4.0f) /
        max(cellSize * cellSize, 0.001f);
    const float curvatureFoam = saturate(
        (-laplacian * cellSize - 0.025f) *
        lerp(1.8f, 5.0f, saturate(gCrestSharpness)));
    const float slopeAmount = length(surfaceNormal.xz) / max(surfaceNormal.y, 0.001f);
    const float slopeMask = saturate((slopeAmount - 0.14f) * 1.55f);
    // 砕波泡は「十分に高い波頭」だけに出す。0からmaxWaveHeightの55%で正規化すると
    // 波の中腹まで泡候補になるため、下限を設けて狭い帯で正規化する(元実装と同じ)。
    const float heightMask = saturate(
        (centerHeight - gMaxWaveHeight * 0.04f) /
        max(gMaxWaveHeight * 0.16f, 0.001f));
    const float breakingFoam =
        curvatureFoam * slopeMask * (0.24f + heightMask * 0.52f);
    // ここでは「泡がどこに出るか」だけを決め、強さは掛けない。
    // Inspectorの「泡の強さ」はPixel Shader側(EvaluateOceanFoam)で一度だけ適用する。
    // 両方で掛けると二乗になり、既定値でもほとんど泡が出なくなる。
    const float generatedFoam = saturate(max(compressionFoam, breakingFoam));
    const float foam = ResolvePersistentFoam(x, z, generatedFoam);
    return float4(surfaceNormal, foam);
}

float2 GetSampleGridPosition(float2 localPosition)
{
    const float2 wrappedUv = frac(localPosition / max(gDomainLength, 0.001f) + 0.5f);
    return wrappedUv * float(gFftResolution);
}

float SampleHeightAtBasePosition(float2 basePosition)
{
    const float2 gridPosition = GetSampleGridPosition(basePosition);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float height00 = GetHeight(baseIndex.x, baseIndex.y);
    const float height10 = GetHeight(baseIndex.x + 1, baseIndex.y);
    const float height01 = GetHeight(baseIndex.x, baseIndex.y + 1);
    const float height11 = GetHeight(baseIndex.x + 1, baseIndex.y + 1);
    return lerp(
        lerp(height00, height10, interpolation.x),
        lerp(height01, height11, interpolation.x),
        interpolation.y);
}

float2 SampleDisplacementAtBasePosition(float2 basePosition)
{
    const float2 gridPosition = GetSampleGridPosition(basePosition);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float2 displacement00 = GetHorizontalDisplacement(baseIndex.x, baseIndex.y);
    const float2 displacement10 = GetHorizontalDisplacement(baseIndex.x + 1, baseIndex.y);
    const float2 displacement01 = GetHorizontalDisplacement(baseIndex.x, baseIndex.y + 1);
    const float2 displacement11 = GetHorizontalDisplacement(baseIndex.x + 1, baseIndex.y + 1);
    return lerp(
        lerp(displacement00, displacement10, interpolation.x),
        lerp(displacement01, displacement11, interpolation.x),
        interpolation.y);
}

float4 SampleNormalFoamAtBasePosition(float2 basePosition)
{
    const float2 gridPosition = GetSampleGridPosition(basePosition);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float4 normalFoam00 = GetNormalFoamAtIndex(baseIndex.x, baseIndex.y);
    const float4 normalFoam10 = GetNormalFoamAtIndex(baseIndex.x + 1, baseIndex.y);
    const float4 normalFoam01 = GetNormalFoamAtIndex(baseIndex.x, baseIndex.y + 1);
    const float4 normalFoam11 = GetNormalFoamAtIndex(baseIndex.x + 1, baseIndex.y + 1);
    float4 normalFoam = lerp(
        lerp(normalFoam00, normalFoam10, interpolation.x),
        lerp(normalFoam01, normalFoam11, interpolation.x),
        interpolation.y);
    normalFoam.xyz = normalize(normalFoam.xyz);
    return normalFoam;
}

void WriteSurfaceSamples()
{
    for (uint sampleIndex = 0u; sampleIndex < gSurfaceSampleCount; sampleIndex++)
    {
        const OceanSurfaceSampleRequest request = gSurfaceSampleRequests[sampleIndex];
        float2 basePosition = request.localPosition;

        [unroll]
        for (uint inverseIteration = 0u; inverseIteration < 4u; inverseIteration++)
        {
            basePosition = request.localPosition -
                SampleDisplacementAtBasePosition(basePosition);
        }

        const float2 surfaceDisplacement =
            SampleDisplacementAtBasePosition(basePosition);
        const float surfaceHeight = SampleHeightAtBasePosition(basePosition);
        const float4 surfaceNormalFoam =
            SampleNormalFoamAtBasePosition(basePosition);
        OceanSurfaceSampleResult sampleResult;
        sampleResult.surfacePositionFoam = float4(
            basePosition.x + surfaceDisplacement.x,
            surfaceHeight,
            basePosition.y + surfaceDisplacement.y,
            surfaceNormalFoam.w);
        sampleResult.surfaceNormal = float4(surfaceNormalFoam.xyz, 0.0f);
        sampleResult.displacementAndTime = float4(
            surfaceDisplacement.x,
            surfaceDisplacement.y,
            gOceanTime,
            0.0f);
        sampleResult.sampleKey = request.sampleKey;
        sampleResult.padding = 0.0f;
        gSurfaceSampleOutput[sampleIndex] = sampleResult;
    }
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gFftResolution || dispatchThreadId.y >= gFftResolution)
    {
        return;
    }

    const int x = int(dispatchThreadId.x);
    const int z = int(dispatchThreadId.y);
    const uint fieldIndex = dispatchThreadId.y * gFftResolution + dispatchThreadId.x;
    const float centerHeight = GetHeight(x, z);
    const float2 centerDisplacement = GetHorizontalDisplacement(x, z);
    const float4 surfaceNormalFoam = GetNormalFoamAtIndex(x, z);
    // 波頭だけでなく谷の深さもPixel Shaderへ渡し、横視点の体積吸収へ使う。
    const float normalizedWaveHeight = clamp(
        centerHeight / max(gMaxWaveHeight, 0.001f),
        -1.0f,
        1.0f);

    gDisplacementOutput[fieldIndex] = float4(
        centerDisplacement.x,
        centerHeight,
        centerDisplacement.y,
        normalizedWaveHeight);
    gNormalFoamOutput[fieldIndex] = surfaceNormalFoam;

    if (dispatchThreadId.x == 0u && dispatchThreadId.y == 0u)
    {
        WriteSurfaceSamples();
    }
}
