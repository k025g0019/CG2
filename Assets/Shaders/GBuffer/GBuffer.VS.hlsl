struct TransformationMatrix
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
    float4 reflectionClipPlane;
    float4 reflectionClipParams;
    float4 oceanParams0;
    float4 oceanParams1;
    float4 oceanParams2;
    float4 oceanParams3;
    float4 oceanParams4;
    float4 oceanParams5;
    float4 oceanWaveData0[16];
    float4 oceanWaveData1[16];
    float4 surfaceParams0;
    float4 surfaceParams1;
    row_major float4x4 previousWVP;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

#include "../Water/OceanSpectrum.hlsli"
#include "../Water/OceanFftSampling.hlsli"
#include "../Common/SurfaceDeformation.hlsli"
#include "../Common/Skinning.hlsli"

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
    uint instanceId : SV_InstanceID;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 oceanData : TEXCOORD2;
    float4 currentClipPosition : TEXCOORD3;
    float4 previousClipPosition : TEXCOORD4;
};

float2 NormalizeOceanDirection(float2 direction, float2 fallbackDirection)
{
    const float directionLength = length(direction);
    return directionLength > 0.0001f ? direction / directionLength : fallbackDirection;
}

void AccumulateOceanWave(
    float2 basePosition,
    float2 direction,
    float waveLength,
    float amplitude,
    float speed,
    float time,
    float choppiness,
    float phaseOffset,
    inout float height,
    inout float2 gradient,
    inout float2 horizontalOffset)
{
    const float safeWaveLength = max(waveLength, 0.1f);
    const float waveNumber = 6.28318530718f / safeWaveLength;
    const float angularFrequency = sqrt(9.81f * waveNumber);
    const float phase = dot(basePosition, direction) * waveNumber
        + time * speed * angularFrequency
        + phaseOffset;
    const float waveSin = sin(phase);
    const float waveCos = cos(phase);

    height += waveSin * amplitude;
    gradient += direction * waveCos * amplitude * waveNumber;
    horizontalOffset += direction * waveCos * amplitude * choppiness;
}

void ApplyOceanDisplacement(inout float4 localPosition, inout float3 localNormal)
{
    if (gTransformationMatrix.oceanParams0.x < 0.5f)
    {
        return;
    }

    const float waveHeight = max(gTransformationMatrix.oceanParams0.z, 0.0f);
    const float maxWaveHeight = max(gTransformationMatrix.oceanParams0.w, 0.0f);
    const float waveLength = max(gTransformationMatrix.oceanParams1.z, 0.1f);
    const float waveSpeed = gTransformationMatrix.oceanParams1.w;
    const float secondaryScale = max(gTransformationMatrix.oceanParams2.z, 0.0f);
    const float choppiness = gTransformationMatrix.oceanParams2.w;
    const float rippleScale = max(gTransformationMatrix.oceanParams3.x, 0.02f);
    const float rippleStrength = max(gTransformationMatrix.oceanParams3.y, 0.0f);
    const float oceanTime = gTransformationMatrix.oceanParams0.y * gTransformationMatrix.oceanParams3.z;
    const float2 primaryDirection = NormalizeOceanDirection(
        gTransformationMatrix.oceanParams1.xy,
        float2(1.0f, 0.0f));
    const float2 secondaryDirection = NormalizeOceanDirection(
        gTransformationMatrix.oceanParams2.xy,
        float2(0.0f, 1.0f));
    const float2 crossDirection = NormalizeOceanDirection(
        primaryDirection + float2(-secondaryDirection.y, secondaryDirection.x),
        float2(0.7071f, 0.7071f));

    float height = 0.0f;
    float2 gradient = 0.0f;
    float2 horizontalOffset = 0.0f;
    AccumulateOceanWave(localPosition.xz, primaryDirection, waveLength, waveHeight * 0.62f,
        waveSpeed, oceanTime, choppiness, 0.0f, height, gradient, horizontalOffset);
    AccumulateOceanWave(localPosition.xz, secondaryDirection, waveLength * 0.58f,
        waveHeight * secondaryScale * 0.48f, waveSpeed * 1.12f, oceanTime, choppiness * 0.72f,
        1.37f, height, gradient, horizontalOffset);
    AccumulateOceanWave(localPosition.xz, crossDirection, waveLength * 1.65f,
        waveHeight * secondaryScale * 0.28f, waveSpeed * 0.74f, oceanTime, choppiness * 0.45f,
        2.61f, height, gradient, horizontalOffset);
    AccumulateOceanWave(localPosition.xz, -crossDirection, waveLength * rippleScale,
        waveHeight * rippleStrength, waveSpeed * 1.85f, oceanTime, choppiness * 0.16f,
        4.13f, height, gradient, horizontalOffset);

    localPosition.xz += horizontalOffset;
    localPosition.y += clamp(height, -maxWaveHeight, maxWaveHeight);
    localNormal = normalize(float3(-gradient.x, 1.0f, -gradient.y));
}

void ApplyOceanSpectrumDisplacement(
    inout float4 localPosition,
    inout float3 localNormal,
    out float4 oceanData)
{
    oceanData = 0.0f;

    if (gTransformationMatrix.oceanParams0.x < 0.5f)
    {
        return;
    }

    // Scene / Game の各カメラ位置へ同じ連続 LOD メッシュを個別に追従させる。
    const float2 cameraRelativePosition = localPosition.xz;
    localPosition.xz += gTransformationMatrix.oceanParams5.zw;

    if (gTransformationMatrix.oceanParams0.x >= 1.5f)
    {
        ApplyOceanFftDisplacement(
            localPosition,
            localNormal,
            cameraRelativePosition,
            oceanData);
        return;
    }

    const OceanSpectrumResult oceanResult = EvaluatePreparedOceanSpectrum(
        localPosition.xz,
        cameraRelativePosition);
    localPosition.xz += oceanResult.horizontalOffset;
    localPosition.y += oceanResult.height;
    localNormal = normalize(float3(-oceanResult.gradient.x, 1.0f, -oceanResult.gradient.y));
    const float normalizedCrestHeight = saturate(
        oceanResult.height / max(gTransformationMatrix.oceanParams0.w, 0.001f));
    oceanData = float4(
        oceanResult.compression,
        oceanResult.time,
        oceanResult.detailWeight,
        normalizedCrestHeight);
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    float4 localPosition = input.position;
    float3 localNormal = input.normal;
    float4 previousLocalPosition = ApplyPreviousSkinning(
        input.position,
        input.boneIndices,
        input.boneWeights);
    float3 previousLocalNormal = input.normal;
    float4 previousOceanData = 0.0f;
    ApplyCurrentSkinning(
        input.boneIndices,
        input.boneWeights,
        localPosition,
        localNormal);
    ApplyOceanSpectrumDisplacement(localPosition, localNormal, output.oceanData);
    ApplyOceanSpectrumDisplacement(
        previousLocalPosition,
        previousLocalNormal,
        previousOceanData);
    ApplySurfaceVertexDeformation(
        localPosition,
        localNormal,
        input.texcoord,
        gTransformationMatrix.surfaceParams0,
        gTransformationMatrix.surfaceParams1,
        gTransformationMatrix.oceanParams4,
        gTransformationMatrix.oceanParams5.zw,
        input.instanceId);
    ApplySurfaceVertexDeformation(
        previousLocalPosition,
        previousLocalNormal,
        input.texcoord,
        gTransformationMatrix.surfaceParams0,
        gTransformationMatrix.surfaceParams1,
        gTransformationMatrix.oceanParams4,
        gTransformationMatrix.oceanParams5.zw,
        input.instanceId);

    const float4 worldPosition = mul(localPosition, gTransformationMatrix.World);
    output.position = mul(localPosition, gTransformationMatrix.WVP);
    output.currentClipPosition = output.position;
    output.previousClipPosition = mul(previousLocalPosition, gTransformationMatrix.previousWVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(localNormal, 0.0f), gTransformationMatrix.World).xyz);
    output.worldPosition = worldPosition.xyz;
    return output;
}
