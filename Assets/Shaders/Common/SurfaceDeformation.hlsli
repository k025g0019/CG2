#ifndef SURFACE_DEFORMATION_HLSLI
#define SURFACE_DEFORMATION_HLSLI

Texture2D<float> gSurfaceHeightDensityMap : register(t12);
SamplerState gSurfaceSampler : register(s0);

float SurfaceHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return (float)(value & 0x00ffffffu) / 16777215.0f;
}

void HideSurfaceInstance(inout float4 localPosition)
{
    localPosition = float4(0.0f, -1000000.0f, 0.0f, 1.0f);
}

void ApplyTerrainHeight(
    inout float4 localPosition,
    inout float3 localNormal,
    float4 surfaceSceneParams)
{
    if (surfaceSceneParams.x < 0.5f)
    {
        return;
    }

    const float2 terrainSize = max(surfaceSceneParams.zw, 1.0f);
    const float2 heightUv = saturate(localPosition.xz / terrainSize + 0.5f);
    uint textureWidth = 1u;
    uint textureHeight = 1u;
    gSurfaceHeightDensityMap.GetDimensions(textureWidth, textureHeight);
    const float2 texelSize = rcp(max(float2(textureWidth, textureHeight), 1.0f));
    const float heightScale = max(surfaceSceneParams.y, 0.0f);
    const float centerHeight = gSurfaceHeightDensityMap.SampleLevel(
        gSurfaceSampler,
        heightUv,
        0.0f);
    const float rightHeight = gSurfaceHeightDensityMap.SampleLevel(
        gSurfaceSampler,
        saturate(heightUv + float2(texelSize.x, 0.0f)),
        0.0f);
    const float forwardHeight = gSurfaceHeightDensityMap.SampleLevel(
        gSurfaceSampler,
        saturate(heightUv + float2(0.0f, texelSize.y)),
        0.0f);

    localPosition.y += (centerHeight - 0.5f) * heightScale;
    const float worldTexelX = max(terrainSize.x * texelSize.x, 0.0001f);
    const float worldTexelZ = max(terrainSize.y * texelSize.y, 0.0001f);
    localNormal = normalize(float3(
        -(rightHeight - centerHeight) * heightScale / worldTexelX,
        1.0f,
        -(forwardHeight - centerHeight) * heightScale / worldTexelZ));
}

void ApplyFoliageInstance(
    inout float4 localPosition,
    inout float3 localNormal,
    float2 texcoord,
    float4 surfaceParams0,
    float4 surfaceParams1,
    float4 surfaceSceneParams,
    float2 cameraLocalPosition,
    uint instanceId)
{
    const float2 areaSize = max(surfaceSceneParams.zw, 1.0f);
    const float randomX = SurfaceHash(instanceId * 4u + 0u);
    const float randomZ = SurfaceHash(instanceId * 4u + 1u);
    const float randomKeep = SurfaceHash(instanceId * 4u + 2u);
    const float randomShape = SurfaceHash(instanceId * 4u + 3u);
    const float2 instanceUv = float2(randomX, randomZ);
    const float densityMap = surfaceParams1.w >= 0.5f
        ? gSurfaceHeightDensityMap.SampleLevel(
            gSurfaceSampler,
            instanceUv,
            0.0f)
        : 1.0f;
    const float finalDensity = saturate(surfaceSceneParams.x) * densityMap;
    const float2 instanceOffset = (instanceUv - 0.5f) * areaSize;
    const float cameraDistance = length(instanceOffset - cameraLocalPosition);
    const float lodDistance = max(surfaceSceneParams.y, 1.0f);
    uint lodStride = cameraDistance > lodDistance * 0.66f
        ? 4u
        : (cameraDistance > lodDistance * 0.33f ? 2u : 1u);

#if defined(SURFACE_SHADOW_PASS)
    lodStride *= 2u;
#endif

    if (randomKeep > finalDensity ||
        cameraDistance > lodDistance ||
        instanceId % lodStride != 0u)
    {
        HideSurfaceInstance(localPosition);
        return;
    }

    const float randomAngle = randomShape * 6.283185307f;
    const float randomScale = lerp(0.72f, 1.28f, SurfaceHash(instanceId * 7u + 11u));
    const float sinAngle = sin(randomAngle);
    const float cosAngle = cos(randomAngle);
    const float2 rotatedPosition = float2(
        localPosition.x * cosAngle - localPosition.z * sinAngle,
        localPosition.x * sinAngle + localPosition.z * cosAngle) * randomScale;
    const float2 rotatedNormal = float2(
        localNormal.x * cosAngle - localNormal.z * sinAngle,
        localNormal.x * sinAngle + localNormal.z * cosAngle);
    localPosition.xz = rotatedPosition + instanceOffset;
    localPosition.y *= randomScale;
    localNormal.xz = rotatedNormal;

    const float2 rawWindDirection = surfaceParams1.xy;
    const float windDirectionLength = length(rawWindDirection);
    const float2 windDirection = windDirectionLength > 0.0001f
        ? rawWindDirection / windDirectionLength
        : float2(1.0f, 0.0f);
    const float anchorWeight = saturate(1.0f - texcoord.y);
    const float bendWeight = anchorWeight * anchorWeight;
    const float spatialPhase = dot(localPosition.xz, windDirection) * surfaceParams1.z;
    const float primaryPhase = spatialPhase + surfaceParams0.y * surfaceParams0.w;
    const float secondaryPhase = spatialPhase * 1.73f - surfaceParams0.y * surfaceParams0.w * 1.31f;
    const float windOffset =
        (sin(primaryPhase) * 0.72f + sin(secondaryPhase) * 0.28f) *
        surfaceParams0.z * bendWeight;

    localPosition.xz += windDirection * windOffset;
    localPosition.y += abs(windOffset) * 0.08f * bendWeight;

    const float3 bendNormal = normalize(float3(
        -windDirection.x * windOffset * 0.35f,
        1.0f,
        -windDirection.y * windOffset * 0.35f));
    localNormal = normalize(lerp(localNormal, bendNormal, bendWeight * 0.45f));
}

void ApplySurfaceVertexDeformation(
    inout float4 localPosition,
    inout float3 localNormal,
    float2 texcoord,
    float4 surfaceParams0,
    float4 surfaceParams1,
    float4 surfaceSceneParams,
    float2 cameraLocalPosition,
    uint instanceId)
{
    const uint surfaceMode = (uint)round(surfaceParams0.x);

    if (surfaceMode == 1u)
    {
        ApplyTerrainHeight(localPosition, localNormal, surfaceSceneParams);
        return;
    }

    if (surfaceMode == 2u)
    {
        ApplyFoliageInstance(
            localPosition,
            localNormal,
            texcoord,
            surfaceParams0,
            surfaceParams1,
            surfaceSceneParams,
            cameraLocalPosition,
            instanceId);
    }
}

#endif
