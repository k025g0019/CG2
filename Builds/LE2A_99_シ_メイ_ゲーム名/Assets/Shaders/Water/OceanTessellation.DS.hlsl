//============================================================
// Ocean GPU Tessellation Domain Shader
//============================================================

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
    float4 oceanRenderParams;
    float4 temporalParams;
    row_major float4x4 previousWVP;
};

struct OceanControlPoint
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct OceanPatchConstants
{
    float edgeFactors[3] : SV_TessFactor;
    float insideFactor : SV_InsideTessFactor;
};

struct DomainShaderOutput
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
    float reflectionClipDistance : SV_ClipDistance0;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b4);

#include "OceanSpectrum.hlsli"
#include "OceanFftSampling.hlsli"

void ApplyOceanTessellatedDisplacement(
    inout float4 localPosition,
    inout float3 localNormal,
    out float4 oceanData)
{
    oceanData = 0.0f;
    const float2 cameraRelativePosition = localPosition.xz;
    localPosition.xz += gTransformationMatrix.oceanParams5.zw;

    if (gTransformationMatrix.oceanParams0.x >= 1.5f)
    {
        ApplyOceanFftDisplacement(
            localPosition,
            localNormal,
            cameraRelativePosition,
            oceanData);
        const float oceanTime =
            gTransformationMatrix.oceanParams0.y *
            gTransformationMatrix.oceanParams3.z;
        ApplyOceanInteraction(
            gTransformationMatrix.surfaceParams0,
            oceanTime,
            localPosition,
            localNormal);
        ApplyOceanInteraction(
            gTransformationMatrix.surfaceParams1,
            oceanTime,
            localPosition,
            localNormal);
        return;
    }

    const OceanSpectrumResult oceanResult = EvaluatePreparedOceanSpectrum(
        localPosition.xz,
        cameraRelativePosition);
    localPosition.xz += oceanResult.horizontalOffset;
    localPosition.y += oceanResult.height;
    localNormal = normalize(float3(
        -oceanResult.gradient.x,
        1.0f,
        -oceanResult.gradient.y));
    const float normalizedWaveHeight = clamp(
        oceanResult.height / max(gTransformationMatrix.oceanParams0.w, 0.001f),
        -1.0f,
        1.0f);
    oceanData = float4(
        oceanResult.compression,
        oceanResult.time,
        oceanResult.detailWeight,
        normalizedWaveHeight);
}

[domain("tri")]
DomainShaderOutput main(
    OceanPatchConstants patchConstants,
    const OutputPatch<OceanControlPoint, 3> patch,
    float3 barycentricCoordinates : SV_DomainLocation)
{
    DomainShaderOutput output;
    float4 localPosition =
        patch[0].position * barycentricCoordinates.x +
        patch[1].position * barycentricCoordinates.y +
        patch[2].position * barycentricCoordinates.z;
    float3 localNormal = normalize(
        patch[0].normal * barycentricCoordinates.x +
        patch[1].normal * barycentricCoordinates.y +
        patch[2].normal * barycentricCoordinates.z);
    output.texcoord =
        patch[0].texcoord * barycentricCoordinates.x +
        patch[1].texcoord * barycentricCoordinates.y +
        patch[2].texcoord * barycentricCoordinates.z;
    const float4 fftMetadata = gTransformationMatrix.oceanWaveData1[15];
    const float2 oceanBasePosition =
        localPosition.xz + gTransformationMatrix.oceanParams5.zw;
    ApplyOceanTessellatedDisplacement(
        localPosition,
        localNormal,
        output.oceanData);

    const float4 worldPosition = mul(localPosition, gTransformationMatrix.World);
    output.position = mul(localPosition, gTransformationMatrix.WVP);
    output.normal = normalize(mul(
        float4(localNormal, 0.0f),
        gTransformationMatrix.World).xyz);
    output.worldPosition = worldPosition.xyz;
    output.shadowPosition = mul(localPosition, gTransformationMatrix.lightWVP);
    output.oceanSamplingData = gTransformationMatrix.oceanParams0.x >= 1.5f
        ? float4(oceanBasePosition, fftMetadata.x, fftMetadata.z)
        : float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.oceanWorldAxisX = mul(
        float4(1.0f, 0.0f, 0.0f, 0.0f),
        gTransformationMatrix.World).xyz;
    output.oceanWorldAxisY = mul(
        float4(0.0f, 1.0f, 0.0f, 0.0f),
        gTransformationMatrix.World).xyz;
    output.oceanWorldAxisZ = mul(
        float4(0.0f, 0.0f, 1.0f, 0.0f),
        gTransformationMatrix.World).xyz;
    output.reflectionClipDistance = gTransformationMatrix.reflectionClipParams.x > 0.5f
        ? dot(worldPosition, gTransformationMatrix.reflectionClipPlane)
        : 1.0f;
    return output;
}
