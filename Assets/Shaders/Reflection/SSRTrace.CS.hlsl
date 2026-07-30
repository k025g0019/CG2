#include "../Temporal/TemporalCommon.hlsli"

Texture2D<float4> gMaterialMask : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gWorldNormal : register(t2);
Texture2D<float2> gDepthPyramid : register(t3);
RWTexture2D<float4> gTraceResult : register(u0);

//================================================================
// ワールド反射レイを画面へ射影し、Hi-Z 深度との交差を探す
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
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
    float3 rayPosition = worldPosition + worldNormal * rayOriginOffset;
    float rayStepDistance = 0.08f;
    float accumulatedDistance = 0.0f;

    [loop]
    for (uint stepIndex = 0u; stepIndex < 48u; stepIndex++)
    {
        rayPosition += reflectionDirection * rayStepDistance;
        accumulatedDistance += rayStepDistance;

        if (accumulatedDistance > maximumDistance)
        {
            break;
        }

        const float4 clipPosition = mul(float4(rayPosition, 1.0f), gMatrixB);

        if (clipPosition.w <= 0.00001f)
        {
            break;
        }

        const float3 ndc = clipPosition.xyz / clipPosition.w;
        const float2 rayUv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

        if (!IsScreenUvValid(rayUv) || ndc.z < 0.0f || ndc.z > 1.0f)
        {
            break;
        }

        const float2 depthRange = gDepthPyramid.SampleLevel(gPointClampSampler, rayUv, 0.0f);
        const float thickness = 0.0015f + accumulatedDistance * 0.00015f;

        if (ndc.z >= depthRange.x - thickness && ndc.z <= depthRange.y + thickness)
        {
            const float edgeFade = saturate(min(min(rayUv.x, rayUv.y), min(1.0f - rayUv.x, 1.0f - rayUv.y)) * 16.0f);
            const float facingFade = saturate(1.0f - abs(dot(viewDirection, worldNormal)) * 0.35f);
            const float roughnessFade = 1.0f - smoothstep(0.55f, 0.98f, roughness);
            gTraceResult[pixelPosition] = float4(
                rayUv,
                depth,
                edgeFade * facingFade * roughnessFade);
            return;
        }

        rayStepDistance = min(rayStepDistance * 1.12f + 0.01f, maximumDistance / 48.0f);
    }

    gTraceResult[pixelPosition] = 0.0f;
}
