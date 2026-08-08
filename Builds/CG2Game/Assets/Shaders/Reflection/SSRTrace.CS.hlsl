#include "../Temporal/TemporalCommon.hlsli"

Texture2D<float4> gMaterialMask : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gWorldNormal : register(t2);
Texture2D<float2> gDepthPyramid0 : register(t3);
Texture2D<float2> gDepthPyramid1 : register(t4);
Texture2D<float2> gDepthPyramid2 : register(t5);
Texture2D<float2> gDepthPyramid3 : register(t6);
Texture2D<float2> gDepthPyramid4 : register(t7);
RWTexture2D<float4> gTraceResult : register(u0);

float2 SampleDepthPyramid(float2 screenUv, uint depthLevel)
{
    if (depthLevel == 0u)
    {
        return gDepthPyramid0.SampleLevel(gPointClampSampler, screenUv, 0.0f);
    }

    if (depthLevel == 1u)
    {
        return gDepthPyramid1.SampleLevel(gPointClampSampler, screenUv, 0.0f);
    }

    if (depthLevel == 2u)
    {
        return gDepthPyramid2.SampleLevel(gPointClampSampler, screenUv, 0.0f);
    }

    if (depthLevel == 3u)
    {
        return gDepthPyramid3.SampleLevel(gPointClampSampler, screenUv, 0.0f);
    }

    return gDepthPyramid4.SampleLevel(gPointClampSampler, screenUv, 0.0f);
}

//================================================================
// ワールド反射レイを画面へ射影し、Hi-Z 深度との交差を探す
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPosition;

    if (!ResolveViewportDispatchPixel(dispatchThreadId.xy, pixelPosition))
    {
        return;
    }

    const float2 screenUv = GetScreenUv(pixelPosition);
    const float depth = gSceneDepth.Load(int3(pixelPosition, 0));
    const float4 materialMask = gMaterialMask.Load(int3(pixelPosition, 0));
    const float roughness = saturate(materialMask.g);
    const bool isSsrMaterial = materialMask.b < 0.5f;

    if (depth >= 0.999999f ||
        !isSsrMaterial ||
        materialMask.r <= 0.0001f ||
        roughness >= 0.98f)
    {
        gTraceResult[pixelPosition] = 0.0f;
        return;
    }

    const float3 worldPosition = ReconstructWorldPosition(screenUv, depth, gMatrixA);
    const float3 worldNormal = normalize(gWorldNormal.Load(int3(pixelPosition, 0)).xyz * 2.0f - 1.0f);
    const float3 cameraPosition = gTemporalParameters.xyz;
    const float3 viewDirection = normalize(worldPosition - cameraPosition);
    const float3 reflectionDirection = normalize(reflect(viewDirection, worldNormal));
    const float maximumDistance = max(gTemporalParameters.w, 1.0f);

    const float rayOriginOffset = lerp(0.025f, 0.08f, roughness);
    const float3 rayOrigin = worldPosition + worldNormal * rayOriginOffset;
    const float baseStepDistance = max(maximumDistance / 640.0f, 0.04f);
    float previousDistance = 0.0f;
    float accumulatedDistance = baseStepDistance * 16.0f;
    uint depthLevel = 4u;

    [loop]
    for (uint stepIndex = 0u; stepIndex < 72u; stepIndex++)
    {
        if (accumulatedDistance > maximumDistance)
        {
            break;
        }

        const float3 rayPosition = rayOrigin + reflectionDirection * accumulatedDistance;
        const float4 clipPosition = mul(float4(rayPosition, 1.0f), gMatrixB);

        if (clipPosition.w <= 0.00001f)
        {
            break;
        }

        const float3 ndc = clipPosition.xyz / clipPosition.w;
        const float2 rayUv = ProjectWorldPosition(rayPosition, gMatrixB);

        if (!IsScreenUvValid(rayUv) || ndc.z < 0.0f || ndc.z > 1.0f)
        {
            break;
        }

        const float2 depthRange = SampleDepthPyramid(rayUv, depthLevel);
        const float thickness = 0.0015f + accumulatedDistance * 0.00015f;

        if (ndc.z < depthRange.x - thickness)
        {
            previousDistance = accumulatedDistance;
            const float hierarchyStep = baseStepDistance * float(1u << depthLevel);
            accumulatedDistance += hierarchyStep;
            depthLevel = min(depthLevel + 1u, 4u);
            continue;
        }

        if (depthLevel > 0u)
        {
            depthLevel--;
            const float refinedStep = baseStepDistance * float(1u << depthLevel);
            accumulatedDistance = max(previousDistance + refinedStep, baseStepDistance);
            continue;
        }

        float lowerDistance = previousDistance;
        float upperDistance = accumulatedDistance;

        [unroll]
        for (uint refinementIndex = 0u; refinementIndex < 5u; refinementIndex++)
        {
            const float middleDistance = (lowerDistance + upperDistance) * 0.5f;
            const float3 middlePosition = rayOrigin + reflectionDirection * middleDistance;
            const float4 middleClipPosition = mul(float4(middlePosition, 1.0f), gMatrixB);
            const float3 middleNdc = middleClipPosition.xyz / max(middleClipPosition.w, 0.00001f);
            const float2 middleUv = ProjectWorldPosition(middlePosition, gMatrixB);
            const float middleSceneDepth = gSceneDepth.SampleLevel(gPointClampSampler, middleUv, 0.0f);

            if (middleNdc.z < middleSceneDepth - thickness)
            {
                lowerDistance = middleDistance;
            }
            else
            {
                upperDistance = middleDistance;
            }
        }

        const float3 hitPosition = rayOrigin + reflectionDirection * upperDistance;
        const float4 hitClipPosition = mul(float4(hitPosition, 1.0f), gMatrixB);
        const float3 hitNdc = hitClipPosition.xyz / max(hitClipPosition.w, 0.00001f);
        const float2 hitUv = ProjectWorldPosition(hitPosition, gMatrixB);
        const float hitSceneDepth = gSceneDepth.SampleLevel(gPointClampSampler, hitUv, 0.0f);
        const bool isRefinedHit =
            IsScreenUvValid(hitUv) &&
            hitSceneDepth < 0.999999f &&
            abs(hitNdc.z - hitSceneDepth) <= thickness * 2.0f;

        if (isRefinedHit)
        {
            const float2 rayViewportUv = ScreenUvToViewportUv(hitUv);
            const float edgeFade = saturate(
                min(
                    min(rayViewportUv.x, rayViewportUv.y),
                    min(1.0f - rayViewportUv.x, 1.0f - rayViewportUv.y)) *
                16.0f);
            const float facingFade = saturate(1.0f - abs(dot(viewDirection, worldNormal)) * 0.35f);
            const float roughnessFade = 1.0f - smoothstep(0.55f, 0.98f, roughness);
            gTraceResult[pixelPosition] = float4(
                hitUv,
                depth,
                edgeFade * facingFade * roughnessFade);
            return;
        }

        previousDistance = accumulatedDistance;
        accumulatedDistance += baseStepDistance;
    }

    gTraceResult[pixelPosition] = 0.0f;
}
