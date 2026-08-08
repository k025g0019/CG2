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

uint WrapIndex(int index)
{
    return uint(index) & (gFftResolution - 1u);
}

uint GetFieldIndex(int x, int z)
{
    return WrapIndex(z) * gFftResolution + WrapIndex(x);
}

float GetHeight(int x, int z)
{
    return clamp(
        gHeightField[GetFieldIndex(x, z)].x * gHeightScale,
        -gMaxWaveHeight,
        gMaxWaveHeight);
}

float2 GetHorizontalDisplacement(int x, int z)
{
    const uint fieldIndex = GetFieldIndex(x, z);
    const float cellSize = max(gDomainLength / float(gFftResolution), 0.001f);
    const float displacementLimit = min(
        max(gMaxWaveHeight * 2.0f, 0.01f),
        cellSize * 8.0f);
    return clamp(
        float2(
            gDisplacementXField[fieldIndex].x,
            gDisplacementZField[fieldIndex].x) * gHeightScale *
            (-gChoppiness * 2.2f),
        -displacementLimit,
        displacementLimit);
}

float GetSafeGradientDenominator(float value)
{
    if (abs(value) < 0.08f)
    {
        return value < 0.0f ? -0.08f : 0.08f;
    }

    return value;
}

float4 GetNormalFoamAtIndex(int x, int z)
{
    const uint fieldIndex = GetFieldIndex(x, z);
    const float cellSize = max(gDomainLength / float(gFftResolution), 0.001f);
    const float centerHeight = GetHeight(x, z);
    const float2 leftDisplacement = GetHorizontalDisplacement(x - 1, z);
    const float2 rightDisplacement = GetHorizontalDisplacement(x + 1, z);
    const float2 backDisplacement = GetHorizontalDisplacement(x, z - 1);
    const float2 frontDisplacement = GetHorizontalDisplacement(x, z + 1);
    const float2 displacementDerivativeX =
        (rightDisplacement - leftDisplacement) / (2.0f * cellSize);
    const float2 displacementDerivativeZ =
        (frontDisplacement - backDisplacement) / (2.0f * cellSize);
    float gradientX = gGradientXField[fieldIndex].x * gHeightScale * 0.5f;
    float gradientZ = gGradientZField[fieldIndex].x * gHeightScale * 0.5f;
    gradientX /= GetSafeGradientDenominator(1.0f + displacementDerivativeX.x);
    gradientZ /= GetSafeGradientDenominator(1.0f + displacementDerivativeZ.y);
    float2 surfaceSlope = float2(gradientX, gradientZ);
    const float slopeLength = length(surfaceSlope);
    const float maximumSlope = 3.5f;

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
    const float compressionFoam = saturate((0.98f - jacobian) / 0.38f);
    const float leftHeight = GetHeight(x - 1, z);
    const float rightHeight = GetHeight(x + 1, z);
    const float backHeight = GetHeight(x, z - 1);
    const float frontHeight = GetHeight(x, z + 1);
    const float laplacian =
        (leftHeight + rightHeight + backHeight + frontHeight - centerHeight * 4.0f) /
        max(cellSize * cellSize, 0.001f);
    const float curvatureFoam = saturate(
        (-laplacian * cellSize - 0.025f) *
        lerp(1.8f, 5.0f, saturate(gCrestSharpness)));
    const float slopeAmount = length(surfaceNormal.xz) / max(surfaceNormal.y, 0.001f);
    const float slopeMask = saturate((slopeAmount - 0.14f) * 1.55f);
    const float heightMask = saturate(
        centerHeight / max(gMaxWaveHeight * 0.55f, 0.001f));
    const float breakingFoam =
        curvatureFoam * slopeMask * (0.24f + heightMask * 0.52f);
    const float foam = saturate(max(compressionFoam, breakingFoam));
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
