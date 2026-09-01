#ifndef REFLECTIONCOMMON_HLSLI
#define REFLECTIONCOMMON_HLSLI

float3 ComputeReflectionVector(float3 normal, float3 viewDirection)
{
    return normalize(reflect(-viewDirection, normalize(normal)));
}

float ReflectionIorToF0(float ior)
{
    const float safeIor = max(ior, 1.0001f);
    const float f0Root = (safeIor - 1.0f) / (safeIor + 1.0f);
    return saturate(f0Root * f0Root);
}

float ReflectionFresnelSchlick(float normalDotView, float f0)
{
    const float fresnelPower = pow(1.0f - saturate(normalDotView), 5.0f);
    return saturate(f0 + (1.0f - f0) * fresnelPower);
}

float ReflectionFresnelSchlickRoughness(float normalDotView, float f0, float roughness)
{
    const float maximumReflection = max(1.0f - saturate(roughness), f0);
    const float fresnelPower = pow(1.0f - saturate(normalDotView), 5.0f);
    return saturate(f0 + (maximumReflection - f0) * fresnelPower);
}

float ReflectionLuminance(float3 color)
{
    return max(dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
}

float ReflectionFresnel(float3 normal, float3 viewDirection, float power)
{
    const float normalDotView = saturate(dot(normalize(normal), normalize(viewDirection)));
    const float fresnelPower = pow(1.0f - normalDotView, max(power, 1.0f));
    return saturate(0.04f + 0.96f * fresnelPower);
}

float2 ClipToUv(float4 clipPosition)
{
    float2 ndc = clipPosition.xy / max(clipPosition.w, 0.0001f);
    return ndc * float2(0.5f, -0.5f) + 0.5f;
}

#endif
