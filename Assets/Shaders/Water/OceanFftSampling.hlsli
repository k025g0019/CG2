StructuredBuffer<float4> gOceanFftDisplacement : register(t14);
StructuredBuffer<float4> gOceanFftNormalFoam : register(t15);

uint2 WrapOceanFftIndex(int2 index, uint fftResolution)
{
    const uint resolutionMask = fftResolution - 1u;
    return uint2(index) & resolutionMask;
}

uint GetOceanFftIndex(int2 index, uint fftResolution)
{
    const uint2 wrappedIndex = WrapOceanFftIndex(index, fftResolution);
    return wrappedIndex.y * fftResolution + wrappedIndex.x;
}

float4 SampleOceanFftBuffer(
    StructuredBuffer<float4> sourceBuffer,
    float2 localPosition,
    uint fftResolution,
    float inverseDomainLength)
{
    const float2 wrappedUv = frac(localPosition * inverseDomainLength + 0.5f);
    const float2 gridPosition = wrappedUv * float(fftResolution);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const float4 value00 = sourceBuffer[GetOceanFftIndex(baseIndex, fftResolution)];
    const float4 value10 = sourceBuffer[GetOceanFftIndex(baseIndex + int2(1, 0), fftResolution)];
    const float4 value01 = sourceBuffer[GetOceanFftIndex(baseIndex + int2(0, 1), fftResolution)];
    const float4 value11 = sourceBuffer[GetOceanFftIndex(baseIndex + int2(1, 1), fftResolution)];
    return lerp(
        lerp(value00, value10, interpolation.x),
        lerp(value01, value11, interpolation.x),
        interpolation.y);
}

void ApplyOceanFftDisplacement(
    inout float4 localPosition,
    inout float3 localNormal,
    float2 cameraRelativePosition,
    out float4 oceanData)
{
    const float4 fftMetadata = gTransformationMatrix.oceanWaveData1[15];
    const uint fftResolution = max(uint(fftMetadata.x + 0.5f), 1u);
    const float inverseDomainLength = max(fftMetadata.z, 0.000001f);
    const float lodBaseSize = max(gTransformationMatrix.oceanParams3.w, 1.0f);
    const float cameraDistance = length(cameraRelativePosition);
    const float4 displacement = SampleOceanFftBuffer(
        gOceanFftDisplacement,
        localPosition.xz,
        fftResolution,
        inverseDomainLength);
    float4 normalFoam = SampleOceanFftBuffer(
        gOceanFftNormalFoam,
        localPosition.xz,
        fftResolution,
        inverseDomainLength);

    normalFoam.xyz = normalize(normalFoam.xyz);
    localPosition.xyz += displacement.xyz;
    localNormal = normalize(normalFoam.xyz);
    const float farDetailWeight = saturate(
        1.0f - (cameraDistance - lodBaseSize * 1.5f) /
        (lodBaseSize * 5.5f));
    oceanData = float4(
        saturate(normalFoam.w),
        gTransformationMatrix.oceanParams0.y * gTransformationMatrix.oceanParams3.z,
        max(farDetailWeight, 0.35f),
        saturate(displacement.w));
}
