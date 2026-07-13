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
    float2 viewportOrigin;
    float2 viewportSize;
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

<<<<<<< HEAD
float3 ReconstructWorldPosition(float2 texcoord, float depth)
{
    float4 clipPosition = float4(
        texcoord.x * 2.0f - 1.0f,
        -(texcoord.y * 2.0f - 1.0f),
=======
float3 ReconstructWorldPosition(float2 pixelPos, float depth)
{
    float2 viewportLocalUv = (pixelPos - gPlanarReflection.viewportOrigin) / gPlanarReflection.viewportSize;
    float4 clipPosition = float4(
        viewportLocalUv.x * 2.0f - 1.0f,
        -(viewportLocalUv.y * 2.0f - 1.0f),
>>>>>>> コミット
        depth,
        1.0f);

    float4 worldPosition = mul(clipPosition, gPlanarReflection.invViewProjection);
    return worldPosition.xyz / max(worldPosition.w, 0.0001f);
}

float ComputePlaneAreaMask(float3 worldPosition)
{
    float3 planeNormal = normalize(gPlanarReflection.planeNormal);
    float3 planeTangent = normalize(gPlanarReflection.planeTangent);
    float3 planeBitangent = normalize(gPlanarReflection.planeBitangent);
    float3 fromPlane = worldPosition - gPlanarReflection.planePoint;

    float distanceFromPlane = abs(dot(fromPlane, planeNormal));
    float planeDistanceMask = 1.0f - smoothstep(0.02f, 0.15f, distanceFromPlane);

    float tangentDistance = abs(dot(fromPlane, planeTangent));
    float bitangentDistance = abs(dot(fromPlane, planeBitangent));
    float edgeDistanceX = gPlanarReflection.halfExtentX - tangentDistance;
    float edgeDistanceZ = gPlanarReflection.halfExtentZ - bitangentDistance;
    float edgeDistance = min(edgeDistanceX, edgeDistanceZ);
    float edgeFadeWidth = max(gPlanarReflection.fadeDistance, 0.001f);
    float edgeMask = smoothstep(0.0f, edgeFadeWidth, edgeDistance);

    return saturate(planeDistanceMask * edgeMask);
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    int2 pixel = int2(input.position.xy);

    float4 sceneColor = gSceneColor.Load(int3(pixel, 0));
    float depth = gDepth.Load(int3(pixel, 0));
    float4 materialMask = gMaterialMask.Load(int3(pixel, 0));

    if (depth >= 0.9999f || materialMask.z < 1.5f)
    {
        return sceneColor;
    }

<<<<<<< HEAD
    float3 worldPosition = ReconstructWorldPosition(input.texcoord, depth);
=======
    float3 worldPosition = ReconstructWorldPosition(input.position.xy, depth);
>>>>>>> コミット
    float planeAreaMask = ComputePlaneAreaMask(worldPosition);

    if (planeAreaMask <= 0.0001f)
    {
        return sceneColor;
    }

    // 反射 RT はメインカメラを鏡面で反転したカメラで描く。
    // 鏡面上の画素はメイン画面と同じスクリーン座標に対応するため、再投影で UV を作らない。
    float4 reflectionColor = gPlanarReflectionColor.SampleLevel(gSampler, input.texcoord, 0.0f);
    float reflectionStrength =
        saturate(materialMask.x * materialMask.y * max(gPlanarReflection.intensity, 0.0f) * planeAreaMask);

    return float4(lerp(sceneColor.rgb, reflectionColor.rgb, reflectionStrength), sceneColor.a);
}
