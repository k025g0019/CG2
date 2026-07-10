#include "Common/ToneMappingCommon.hlsli"

struct PlanarReflectionCB
{
    row_major float4x4 invViewProjection;
    row_major float4x4 reflectionViewProjection;
    float3 planeNormal;
    float fadeDistance;
    float3 planePoint;
    float intensity;
    float3 planeTangent;
    float halfExtentX;
    float3 planeBitangent;
    float halfExtentZ;
};

ConstantBuffer<PlanarReflectionCB> gPlanarReflection : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
Texture2D<float4> gPlanarReflectionColor : register(t3);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float4 sceneColor = gSceneColor.Sample(gSampler, input.texcoord);
    float depth = gDepth.Sample(gSampler, input.texcoord).r;

    if (depth >= 0.99999f)
    {
        return sceneColor;
    }

    float4 materialMask = gMaterialMask.Sample(gSampler, input.texcoord);
    float usePlanar = materialMask.z >= 1.5f ? 1.0f : 0.0f;

    if (usePlanar <= 0.0f)
    {
        return sceneColor;
    }

    float3 planeNormal = normalize(gPlanarReflection.planeNormal);
    float fadeDistance = max(gPlanarReflection.fadeDistance, 0.0001f);
    float3 planePoint = gPlanarReflection.planePoint;
    float3 planeTangent = normalize(gPlanarReflection.planeTangent);
    float3 planeBitangent = normalize(gPlanarReflection.planeBitangent);
    float halfExtentX = max(gPlanarReflection.halfExtentX, 0.0001f);
    float halfExtentZ = max(gPlanarReflection.halfExtentZ, 0.0001f);
    float planarIntensity = max(gPlanarReflection.intensity, 0.0f);

    float2 clipUv = float2(input.texcoord.x * 2.0f - 1.0f, 1.0f - input.texcoord.y * 2.0f);
    float4 clipPosition = float4(clipUv, depth, 1.0f);
    float4 reflectionWorldPosition4 = mul(clipPosition, gPlanarReflection.invViewProjection);
    reflectionWorldPosition4.xyz /= max(reflectionWorldPosition4.w, 0.000001f);

    float3 reflectionWorldPosition = reflectionWorldPosition4.xyz;
    float3 planeOffset = reflectionWorldPosition - planePoint;
    float planeLocalX = dot(planeOffset, planeTangent);
    float planeLocalZ = dot(planeOffset, planeBitangent);
    if (abs(planeLocalX) > halfExtentX || abs(planeLocalZ) > halfExtentZ)
    {
        return sceneColor;
    }

    float4 reflectionClip = mul(float4(reflectionWorldPosition, 1.0f), gPlanarReflection.reflectionViewProjection);
    if (reflectionClip.w <= 0.000001f)
    {
        return sceneColor;
    }

    float2 reflectionUv;
    reflectionUv.x = reflectionClip.x / reflectionClip.w * 0.5f + 0.5f;
    reflectionUv.y = -reflectionClip.y / reflectionClip.w * 0.5f + 0.5f;

    if (reflectionUv.x < 0.0f || reflectionUv.x > 1.0f || reflectionUv.y < 0.0f || reflectionUv.y > 1.0f)
    {
        return sceneColor;
    }

    float3 reflectionColor = gPlanarReflectionColor.Sample(gSampler, reflectionUv).rgb;
    float edgeDistanceX = halfExtentX - abs(planeLocalX);
    float edgeDistanceZ = halfExtentZ - abs(planeLocalZ);
    float planeFade = saturate(min(edgeDistanceX, edgeDistanceZ) / fadeDistance);
    float reflectMask = saturate(materialMask.x);
    float smoothness = saturate(materialMask.y);
    float probeIntensity = saturate(materialMask.w);
    float blendRate = saturate(reflectMask * smoothness * probeIntensity * planarIntensity * planeFade);

    return float4(lerp(sceneColor.rgb, reflectionColor, blendRate), sceneColor.a);
}
