#ifndef REFLECTIONCOMMON_HLSLI
#define REFLECTIONCOMMON_HLSLI

float3 ComputeReflectionVector(float3 normal, float3 viewDirection)
{
    return normalize(reflect(-viewDirection, normalize(normal)));
}

float ReflectionFresnel(float3 normal, float3 viewDirection, float power)
{
    return pow(1.0f - saturate(dot(normalize(normal), normalize(viewDirection))), power);
}

float2 ClipToUv(float4 clipPosition)
{
    float2 ndc = clipPosition.xy / max(clipPosition.w, 0.0001f);
    return ndc * float2(0.5f, -0.5f) + 0.5f;
}

#endif
