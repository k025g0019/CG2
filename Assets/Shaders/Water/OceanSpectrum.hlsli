#ifndef CG2_OCEAN_SPECTRUM_HLSLI
#define CG2_OCEAN_SPECTRUM_HLSLI

static const uint kOceanWaveCount = 16u;

struct OceanSpectrumResult
{
    float height;
    float2 gradient;
    float2 horizontalOffset;
    float compression;
    float time;
    float detailWeight;
};

void AccumulatePreparedOceanWave(
    uint waveIndex,
    float waveWeight,
    float2 basePosition,
    float waveSpeed,
    float choppiness,
    float crestSharpness,
    inout OceanSpectrumResult oceanResult)
{
    const float4 waveData0 = gTransformationMatrix.oceanWaveData0[waveIndex];
    const float4 waveData1 = gTransformationMatrix.oceanWaveData1[waveIndex];
    const float2 waveDirection = waveData0.xy;
    const float waveNumber = waveData0.z;
    const float amplitude = waveData0.w * waveWeight;
    const float angularFrequency = waveData1.x;
    const float phase = dot(basePosition, waveDirection) * waveNumber
        + oceanResult.time * waveSpeed * angularFrequency
        + waveData1.y;
    float phaseSin = 0.0f;
    float phaseCos = 0.0f;
    sincos(phase, phaseSin, phaseCos);

    // 2倍角は追加の三角関数を呼ばず、同じ波形を積和だけで作る。
    const float doublePhaseSin = 2.0f * phaseSin * phaseCos;
    const float doublePhaseCos = phaseCos * phaseCos - phaseSin * phaseSin;
    const float shapedHeight = phaseSin + crestSharpness * 0.25f * doublePhaseSin;
    const float shapedDerivative = phaseCos + crestSharpness * 0.5f * doublePhaseCos;

    oceanResult.height += amplitude * shapedHeight;
    oceanResult.gradient += waveDirection * amplitude * waveNumber * shapedDerivative;
    oceanResult.horizontalOffset += waveDirection * amplitude * choppiness * phaseCos;
    oceanResult.compression += max(
        amplitude * abs(choppiness) * waveNumber * phaseSin,
        0.0f);
}

OceanSpectrumResult EvaluatePreparedOceanSpectrum(float2 basePosition, float2 cameraRelativePosition)
{
    OceanSpectrumResult oceanResult;
    oceanResult.height = 0.0f;
    oceanResult.gradient = 0.0f;
    oceanResult.horizontalOffset = 0.0f;
    oceanResult.compression = 0.0f;
    oceanResult.time =
        gTransformationMatrix.oceanParams0.y *
        gTransformationMatrix.oceanParams3.z;
    oceanResult.detailWeight = 1.0f;

    const float maxWaveHeight = max(gTransformationMatrix.oceanParams0.w, 0.001f);
    const float waveSpeed = gTransformationMatrix.oceanParams1.w;
    const float choppiness = gTransformationMatrix.oceanParams2.w;
    const float crestSharpness = saturate(gTransformationMatrix.oceanParams5.y);
    const float halfOceanLodSize = max(gTransformationMatrix.oceanParams3.w * 0.5f, 0.1f);
    const float normalizedDistance = saturate(
        max(abs(cameraRelativePosition.x), abs(cameraRelativePosition.y)) /
        halfOceanLodSize);

    // 長いうねりと低周波の風浪は地平線まで残し、高周波成分だけを遠方で減らす。
    const float nearWindWaveWeight = 1.0f - smoothstep(0.58f, 0.94f, normalizedDistance);
    const float horizonWindWaveWeight = lerp(0.18f, 1.0f, nearWindWaveWeight);
    const float rippleWeight = 1.0f - smoothstep(0.18f, 0.52f, normalizedDistance);
    oceanResult.detailWeight = 1.0f - smoothstep(0.18f, 0.72f, normalizedDistance);

    [unroll]
    for (uint waveIndex = 0u; waveIndex < 4u; waveIndex++)
    {
        AccumulatePreparedOceanWave(
            waveIndex,
            1.0f,
            basePosition,
            waveSpeed,
            choppiness,
            crestSharpness,
            oceanResult);
    }

    [unroll]
    for (uint waveIndex = 4u; waveIndex < 8u; waveIndex++)
    {
        AccumulatePreparedOceanWave(
            waveIndex,
            horizonWindWaveWeight,
            basePosition,
            waveSpeed,
            choppiness,
            crestSharpness,
            oceanResult);
    }

    if (nearWindWaveWeight > 0.0001f)
    {
        [unroll]
        for (uint waveIndex = 8u; waveIndex < 12u; waveIndex++)
        {
            AccumulatePreparedOceanWave(
                waveIndex,
                nearWindWaveWeight,
                basePosition,
                waveSpeed,
                choppiness,
                crestSharpness,
                oceanResult);
        }
    }

    if (rippleWeight > 0.0001f)
    {
        [unroll]
        for (uint waveIndex = 12u; waveIndex < kOceanWaveCount; waveIndex++)
        {
            AccumulatePreparedOceanWave(
                waveIndex,
                rippleWeight,
                basePosition,
                waveSpeed,
                choppiness,
                crestSharpness,
                oceanResult);
        }
    }

    const float normalizedHeight = tanh(oceanResult.height / maxWaveHeight);
    const float derivativeScale = 1.0f - normalizedHeight * normalizedHeight;
    oceanResult.height = normalizedHeight * maxWaveHeight;
    oceanResult.gradient *= derivativeScale;
    oceanResult.compression = saturate(oceanResult.compression);
    return oceanResult;
}

#endif
