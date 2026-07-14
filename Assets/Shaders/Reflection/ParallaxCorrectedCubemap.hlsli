#ifndef PARALLAX_CORRECTED_CUBEMAP_HLSLI
#define PARALLAX_CORRECTED_CUBEMAP_HLSLI

//================================================================
// Box Projection による Reflection Probe の視差補正
//================================================================

float GetSafeRayDirectionComponent(float directionComponent)
{
    if (abs(directionComponent) >= 0.00001f)
    {
        return directionComponent;
    }

    return directionComponent >= 0.0f ? 0.00001f : -0.00001f;
}

float3 CorrectParallaxCubemapDirection(
    float3 worldPosition,
    float3 reflectionDirection,
    float3 probeCenter,
    float3 probeExtent)
{
    const float3 safeExtent = max(abs(probeExtent), float3(0.01f, 0.01f, 0.01f));
    const float3 boxMinimum = probeCenter - safeExtent;
    const float3 boxMaximum = probeCenter + safeExtent;
    const float3 safeDirection = float3(
        GetSafeRayDirectionComponent(reflectionDirection.x),
        GetSafeRayDirectionComponent(reflectionDirection.y),
        GetSafeRayDirectionComponent(reflectionDirection.z));
    const float3 targetPlane = float3(
        safeDirection.x >= 0.0f ? boxMaximum.x : boxMinimum.x,
        safeDirection.y >= 0.0f ? boxMaximum.y : boxMinimum.y,
        safeDirection.z >= 0.0f ? boxMaximum.z : boxMinimum.z);
    const float3 planeDistance = (targetPlane - worldPosition) / safeDirection;
    const float hitDistance = min(planeDistance.x, min(planeDistance.y, planeDistance.z));
    const float3 boxHitPosition = worldPosition + safeDirection * max(hitDistance, 0.0f);
    return normalize(boxHitPosition - probeCenter);
}

#endif
