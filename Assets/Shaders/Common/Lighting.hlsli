#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#include "Math.hlsli"

struct DirectionalLight
{
    float3 color;
    float intensity;
    float3 direction;
    float pad0;
    float3 skyTopColor;
    float horizonSharpness;
    float3 skyBottomColor;
    float ambientIntensity;
    float3 skyColor;
    float reflectionIntensity;
};

struct PointLight
{
    float3 position;
    float range;
    float3 color;
    float intensity;
};

struct SpotLight
{
    float3 position;
    float range;
    float3 direction;
    float spotAngle;
    float3 color;
    float intensity;
    float innerCos;
    float outerCos;
};

float3 CalculateSkyColor(float3 direction, float3 topColor, float3 bottomColor, float horizonSharpness)
{
    float t = pow(max(direction.y, 0.0f), horizonSharpness);
    return lerp(bottomColor, topColor, t);
}

float3 CalculateDiffuse(float3 lightDir, float3 normal, float3 lightColor, float intensity)
{
    float NdotL = max(dot(normal, lightDir), 0.0f);
    return lightColor * intensity * NdotL;
}

float3 CalculateSpecular(float3 lightDir, float3 viewDir, float3 normal, float3 lightColor, float intensity, float shininess)
{
    float3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float spec = pow(NdotH, shininess);
    return lightColor * intensity * spec;
}

float Attenuation(float distance, float range)
{
    float ratio = distance / range;
    return max(0.0f, 1.0f - ratio * ratio);
    return saturate(1.0f - (distance * distance) / (range * range));
}

float SpotFactor(float3 lightDir, float3 spotDir, float innerCos, float outerCos)
{
    float cosAngle = dot(normalize(-lightDir), normalize(spotDir));
    return smoothstep(outerCos, innerCos, cosAngle);
}

#endif
