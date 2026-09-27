Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
StructuredBuffer<float4> gOceanFftDisplacement : register(t4);
SamplerState gLinearSampler : register(s0);

cbuffer UnderwaterConstants : register(b0)
{
    row_major float4x4 inverseViewProjection;
    float3 cameraPosition;
    float waterHeight;
    float3 deepColor;
    float causticsIntensity;
    float3 shallowColor;
    float absorptionDistance;
    float2 inverseRenderSize;
    float elapsedTime;
    float distortionStrength;
    float2 viewportUvOffset;
    float2 viewportUvScale;
    float4 oceanWorldToLocalX;
    float4 oceanWorldToLocalZ;
    float fftResolution;
    float inverseDomainLength;
    float oceanLocalHalfExtent;
    float oceanVerticalScale;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct OceanSurfaceDifferential
{
    float2 slope;
    float curvature;
};

uint2 WrapOceanFftIndex(int2 index, uint resolution)
{
    const uint resolutionMask = resolution - 1u;
    return uint2(index) & resolutionMask;
}

float SampleOceanFftHeight(float2 localPosition)
{
    const uint resolution = max(uint(fftResolution + 0.5f), 1u);
    const float2 wrappedUv = frac(localPosition * max(inverseDomainLength, 0.000001f) + 0.5f);
    const float2 gridPosition = wrappedUv * float(resolution);
    const int2 baseIndex = int2(floor(gridPosition));
    const float2 interpolation = frac(gridPosition);
    const uint2 index00 = WrapOceanFftIndex(baseIndex, resolution);
    const uint2 index10 = WrapOceanFftIndex(baseIndex + int2(1, 0), resolution);
    const uint2 index01 = WrapOceanFftIndex(baseIndex + int2(0, 1), resolution);
    const uint2 index11 = WrapOceanFftIndex(baseIndex + int2(1, 1), resolution);
    const float height00 = gOceanFftDisplacement[index00.y * resolution + index00.x].y;
    const float height10 = gOceanFftDisplacement[index10.y * resolution + index10.x].y;
    const float height01 = gOceanFftDisplacement[index01.y * resolution + index01.x].y;
    const float height11 = gOceanFftDisplacement[index11.y * resolution + index11.x].y;
    return lerp(
        lerp(height00, height10, interpolation.x),
        lerp(height01, height11, interpolation.x),
        interpolation.y);
}

float2 GetOceanLocalPosition(float3 worldPosition)
{
    const float4 homogeneousPosition = float4(worldPosition, 1.0f);
    return float2(
        dot(homogeneousPosition, oceanWorldToLocalX),
        dot(homogeneousPosition, oceanWorldToLocalZ));
}

float ComputeOceanBoundsMask(float2 localPosition)
{
    const float halfExtent = max(oceanLocalHalfExtent, 0.001f);
    const float edgeDistance = halfExtent - max(abs(localPosition.x), abs(localPosition.y));
    const float edgeSoftness = max(halfExtent * 0.0025f, 0.05f);
    return smoothstep(0.0f, edgeSoftness, edgeDistance);
}

float GetOceanSurfaceHeight(float2 localPosition)
{
    return waterHeight + SampleOceanFftHeight(localPosition) * max(abs(oceanVerticalScale), 0.0001f);
}

OceanSurfaceDifferential ComputeOceanSurfaceDifferential(float2 localPosition)
{
    OceanSurfaceDifferential differential;
    const float resolution = max(fftResolution, 1.0f);
    const float cellSize = 1.0f / max(inverseDomainLength * resolution, 0.0001f);
    const float centerHeight = SampleOceanFftHeight(localPosition);
    const float heightLeft = SampleOceanFftHeight(localPosition - float2(cellSize, 0.0f));
    const float heightRight = SampleOceanFftHeight(localPosition + float2(cellSize, 0.0f));
    const float heightBack = SampleOceanFftHeight(localPosition - float2(0.0f, cellSize));
    const float heightFront = SampleOceanFftHeight(localPosition + float2(0.0f, cellSize));
    const float verticalScale = oceanVerticalScale;
    differential.slope =
        float2(heightRight - heightLeft, heightFront - heightBack) *
        verticalScale /
        max(cellSize * 2.0f, 0.0001f);
    differential.curvature =
        (heightLeft + heightRight + heightBack + heightFront - centerHeight * 4.0f) *
        verticalScale /
        max(cellSize * cellSize, 0.0001f);
    return differential;
}

float3 ReconstructWorldPosition(float2 texcoord, float deviceDepth)
{
    const float2 ndc = float2(texcoord.x * 2.0f - 1.0f, 1.0f - texcoord.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, deviceDepth, 1.0f), inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

float2 ClampUnderwaterSourceUv(float2 sourceUv)
{
    const float2 minimumUv = viewportUvOffset + inverseRenderSize * 1.5f;
    const float2 maximumUv =
        viewportUvOffset +
        viewportUvScale -
        inverseRenderSize * 1.5f;
    return clamp(sourceUv, minimumUv, maximumUv);
}

float ComputeCaustics(
    float3 worldPosition,
    float2 oceanLocalPosition,
    OceanSurfaceDifferential differential)
{
    const float2 focusedCoordinate =
        oceanLocalPosition * 0.48f - differential.slope * 1.7f;
    const float2 flowA = float2(elapsedTime * 0.22f, elapsedTime * -0.17f);
    const float2 flowB = float2(elapsedTime * -0.13f, elapsedTime * 0.19f);
    const float2 coordinateA = focusedCoordinate + flowA;
    const float2 coordinateB = worldPosition.xz * 0.31f + flowB;
    const float waveA = sin(coordinateA.x + sin(coordinateA.y * 1.7f));
    const float waveB = sin(coordinateB.y + sin(coordinateB.x * 1.4f));
    const float focusedLight = 1.0f - saturate(abs(waveA + waveB) * 0.7f);
    const float slopeFocus = rcp(
        1.0f + dot(differential.slope, differential.slope) * 0.22f);
    const float resolution = max(fftResolution, 1.0f);
    const float cellSize = 1.0f / max(inverseDomainLength * resolution, 0.0001f);
    const float curvatureFocus = lerp(
        0.82f,
        1.65f,
        saturate(abs(differential.curvature) * cellSize * 0.65f));
    return focusedLight * focusedLight * focusedLight * slopeFocus * curvatureFocus;
}

float3 ApplyUnderwaterOptics(
    float3 sourceColor,
    float waterPathLength)
{
    const float safeAbsorptionDistance = max(absorptionDistance, 0.1f);
    const float safePathLength = max(waterPathLength, 0.0f);
    const float normalizedPathLength = safePathLength / safeAbsorptionDistance;
    const float3 absorptionCoefficient =
        max(1.0f - saturate(deepColor), 0.06f) /
        safeAbsorptionDistance;
    const float3 transmittance = exp(
        -absorptionCoefficient * safePathLength);
    const float scatteringWeight =
        1.0f - exp(-normalizedPathLength * 0.72f);
    const float3 scatteringColor = lerp(
        shallowColor,
        deepColor,
        smoothstep(0.08f, 1.65f, normalizedPathLength));
    return sourceColor * transmittance +
        scatteringColor * (1.0f - transmittance) *
        lerp(0.72f, 0.92f, scatteringWeight);
}

float4 main(PSInput input) : SV_TARGET0
{
    const float2 sourceUv = viewportUvOffset + input.texcoord * viewportUvScale;
    const float depth = gDepthTexture.SampleLevel(gLinearSampler, sourceUv, 0.0f);
    const float2 cameraOceanPosition = GetOceanLocalPosition(cameraPosition);
    const float cameraBoundsMask = ComputeOceanBoundsMask(cameraOceanPosition);
    const float cameraSurfaceHeight = GetOceanSurfaceHeight(cameraOceanPosition);
    const float boundarySoftness = max(abs(oceanVerticalScale) * 0.04f, 0.035f);
    const float cameraSubmergedDepth = cameraSurfaceHeight - cameraPosition.y;
    const float cameraUnderwater =
        smoothstep(-boundarySoftness, boundarySoftness, cameraSubmergedDepth) *
        cameraBoundsMask;

    if (depth >= 0.99999f)
    {
        const OceanSurfaceDifferential cameraDifferential =
            ComputeOceanSurfaceDifferential(cameraOceanPosition);
        const float2 distortion =
            cameraDifferential.slope *
            max(distortionStrength, 0.0f) *
            0.0015f *
            cameraUnderwater;
        const float3 sourceColor = gSceneTexture.SampleLevel(
            gLinearSampler,
            ClampUnderwaterSourceUv(sourceUv + distortion),
            0.0f).rgb;
        const float skyWaterPath =
            max(absorptionDistance * 1.5f, cameraSubmergedDepth);
        const float3 underwaterSkyColor = ApplyUnderwaterOptics(
            sourceColor,
            skyWaterPath);
        return float4(
            lerp(sourceColor, underwaterSkyColor, cameraUnderwater),
            1.0f);
    }

    const float3 worldPosition = ReconstructWorldPosition(input.texcoord, depth);
    const float2 oceanLocalPosition = GetOceanLocalPosition(worldPosition);
    const float boundsMask = ComputeOceanBoundsMask(oceanLocalPosition);
    const float surfaceHeight = GetOceanSurfaceHeight(oceanLocalPosition);
    const float submergedDepth = max(surfaceHeight - worldPosition.y, 0.0f);
    const float geometryBoundarySoftness = max(
        boundarySoftness,
        fwidth(surfaceHeight - worldPosition.y) * 1.5f);
    const float geometryUnderwater =
        smoothstep(
            -geometryBoundarySoftness,
            geometryBoundarySoftness,
            surfaceHeight - worldPosition.y) *
        boundsMask;
    const OceanSurfaceDifferential oceanDifferential =
        ComputeOceanSurfaceDifferential(oceanLocalPosition);
    const float3 oceanSurfaceNormal = normalize(float3(
        -oceanDifferential.slope.x,
        1.0f,
        -oceanDifferential.slope.y));
    const float3 cameraToGeometry = worldPosition - cameraPosition;
    const float viewDistance = length(cameraToGeometry);
    const float3 viewDirection = cameraToGeometry /
        max(viewDistance, 0.0001f);
    const float surfaceIncidence = max(
        abs(dot(viewDirection, oceanSurfaceNormal)),
        0.12f);
    const float entryWaterPath = submergedDepth / surfaceIncidence;
    const float waterPathLength = lerp(
        entryWaterPath,
        viewDistance,
        cameraUnderwater);
    const float caustics = ComputeCaustics(
        worldPosition,
        oceanLocalPosition,
        oceanDifferential) *
        exp(-submergedDepth * 0.14f) * max(causticsIntensity, 0.0f) * geometryUnderwater;
    const float3 causticsColor = shallowColor * caustics;
    const float effectMask = saturate(max(geometryUnderwater * 0.72f, cameraUnderwater));
    const float2 distortion = oceanDifferential.slope *
        max(distortionStrength, 0.0f) *
        0.0015f *
        effectMask;
    const float3 sourceColor = gSceneTexture.SampleLevel(
        gLinearSampler,
        ClampUnderwaterSourceUv(sourceUv + distortion),
        0.0f).rgb;
    const float3 opticalColor = ApplyUnderwaterOptics(
        sourceColor,
        waterPathLength);
    const float3 absorbedColor = lerp(
        sourceColor,
        opticalColor + causticsColor,
        effectMask);
    return float4(absorbedColor, 1.0f);
}
