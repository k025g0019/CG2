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

float2 ComputeOceanSlope(float2 localPosition)
{
    const float resolution = max(fftResolution, 1.0f);
    const float cellSize = 1.0f / max(inverseDomainLength * resolution, 0.0001f);
    const float heightLeft = SampleOceanFftHeight(localPosition - float2(cellSize, 0.0f));
    const float heightRight = SampleOceanFftHeight(localPosition + float2(cellSize, 0.0f));
    const float heightBack = SampleOceanFftHeight(localPosition - float2(0.0f, cellSize));
    const float heightFront = SampleOceanFftHeight(localPosition + float2(0.0f, cellSize));
    return float2(heightRight - heightLeft, heightFront - heightBack) /
        max(cellSize * 2.0f, 0.0001f);
}

float3 ReconstructWorldPosition(float2 texcoord, float deviceDepth)
{
    const float2 ndc = float2(texcoord.x * 2.0f - 1.0f, 1.0f - texcoord.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, deviceDepth, 1.0f), inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

float ComputeCaustics(float3 worldPosition, float2 oceanLocalPosition, float2 oceanSlope)
{
    const float2 focusedCoordinate =
        oceanLocalPosition * 0.48f - oceanSlope * 1.7f;
    const float2 flowA = float2(elapsedTime * 0.22f, elapsedTime * -0.17f);
    const float2 flowB = float2(elapsedTime * -0.13f, elapsedTime * 0.19f);
    const float2 coordinateA = focusedCoordinate + flowA;
    const float2 coordinateB = worldPosition.xz * 0.31f + flowB;
    const float waveA = sin(coordinateA.x + sin(coordinateA.y * 1.7f));
    const float waveB = sin(coordinateB.y + sin(coordinateB.x * 1.4f));
    const float focusedLight = 1.0f - saturate(abs(waveA + waveB) * 0.7f);
    const float slopeFocus = rcp(1.0f + dot(oceanSlope, oceanSlope) * 0.22f);
    return focusedLight * focusedLight * focusedLight * slopeFocus;
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
        const float2 cameraSlope = ComputeOceanSlope(cameraOceanPosition);
        const float2 distortion =
            cameraSlope * max(distortionStrength, 0.0f) * 0.0015f * cameraUnderwater;
        const float3 sourceColor = gSceneTexture.SampleLevel(
            gLinearSampler,
            saturate(sourceUv + distortion),
            0.0f).rgb;
        const float3 skyTint = lerp(sourceColor, deepColor, 0.42f);
        return float4(lerp(sourceColor, skyTint, cameraUnderwater), 1.0f);
    }

    const float3 worldPosition = ReconstructWorldPosition(input.texcoord, depth);
    const float2 oceanLocalPosition = GetOceanLocalPosition(worldPosition);
    const float boundsMask = ComputeOceanBoundsMask(oceanLocalPosition);
    const float surfaceHeight = GetOceanSurfaceHeight(oceanLocalPosition);
    const float submergedDepth = max(surfaceHeight - worldPosition.y, 0.0f);
    const float geometryUnderwater =
        smoothstep(-boundarySoftness, boundarySoftness, surfaceHeight - worldPosition.y) *
        boundsMask;
    const float waterPathLength = max(submergedDepth, max(cameraSubmergedDepth, 0.0f));
    const float absorption = 1.0f - exp(-waterPathLength / max(absorptionDistance, 0.1f));
    const float3 waterTint = lerp(shallowColor, deepColor, saturate(absorption));
    const float2 oceanSlope = ComputeOceanSlope(oceanLocalPosition);
    const float caustics = ComputeCaustics(worldPosition, oceanLocalPosition, oceanSlope) *
        exp(-submergedDepth * 0.14f) * max(causticsIntensity, 0.0f) * geometryUnderwater;
    const float3 causticsColor = shallowColor * caustics;
    const float effectMask = saturate(max(geometryUnderwater * 0.72f, cameraUnderwater));
    const float2 distortion = oceanSlope *
        max(distortionStrength, 0.0f) *
        0.0015f *
        effectMask;
    const float3 sourceColor = gSceneTexture.SampleLevel(
        gLinearSampler,
        saturate(sourceUv + distortion),
        0.0f).rgb;
    const float3 absorbedColor = lerp(
        sourceColor,
        sourceColor * waterTint + causticsColor,
        effectMask);
    return float4(absorbedColor, 1.0f);
}
