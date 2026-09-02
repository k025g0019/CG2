struct Material
{
    float4 color;
    int enableLighting;
    int useTexture;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
    float reflectionMode;
    float reflectionProbeIntensity;
    float reflectionReserved;
    float materialPadding0;
    float materialPadding1;
    float3 reflectionProbeCenter;
    float reflectionProbeBoxProjection;
    float3 reflectionProbeExtent;
    float materialPadding2;
    row_major float4x4 uvTransform;
    float normalScale;
    float ambientOcclusionStrength;
    float heightScale;
    float alphaCutoff;
    float clearCoat;
    float clearCoatRoughness;
    float transmission;
    float subsurface;
    float anisotropy;
    float anisotropyRotation;
    float specularTint;
    float sheen;
    float3 emissionColor;
    float sheenTint;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAmbientOcclusionMap;
    int useEmissionMap;
    int useHeightMap;
    int useOpacityMap;
    int alphaMode;
    int doubleSided;
    float materialExtensionPadding0;
    float materialExtensionPadding1;
    float materialExtensionPadding2;
    float2 uvTiling;
    float2 uvOffset;
    float oceanEnabled;
    float oceanFoamStrength;
    float oceanRoughness;
    float oceanColorBlendScale;
    float3 oceanDeepColor;
    float oceanPerPixelDisplacementStrength;
    float oceanDetailNormalStrength;
    float oceanFoamThreshold;
    float oceanAbsorptionDistance;
    float oceanRefractionDistortion;
    float oceanWaterDepth;
    float oceanCrestSharpness;
    float oceanPerPixelDisplacementSteps;
    float oceanPerPixelDisplacementDistance;
    int surfaceMode;
    float surfaceMaterialPadding0;
    float surfaceMaterialPadding1;
    float surfaceMaterialPadding2;
};

#include "../Water/OceanSurface.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);

#include "../Common/SceneLightData.hlsli"

ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b1);
Texture2D gBaseColorMap : register(t0);
Texture2D gNormalMap : register(t7);
Texture2D gMetallicMap : register(t8);
Texture2D gRoughnessMap : register(t9);
Texture2D gAmbientOcclusionMap : register(t10);
Texture2D gEmissionMap : register(t11);
Texture2D gHeightMap : register(t12);
Texture2D gOpacityMap : register(t13);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 oceanData : TEXCOORD2;
    float4 currentClipPosition : TEXCOORD3;
    float4 previousClipPosition : TEXCOORD4;
    float2 motionVectorScale : TEXCOORD5;
    float4 oceanSamplingData : TEXCOORD6;
    nointerpolation float3 oceanWorldAxisX : TEXCOORD7;
    nointerpolation float3 oceanWorldAxisY : TEXCOORD8;
    nointerpolation float3 oceanWorldAxisZ : TEXCOORD9;
    bool isFrontFace : SV_IsFrontFace;
};

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 material : SV_TARGET2;
    float4 emission : SV_TARGET3;
    float2 motionVector : SV_TARGET4;
};

float2 BuildMaterialUv(float2 texcoord)
{
    const float2 tiledUv = texcoord * gMaterial.uvTiling + gMaterial.uvOffset;
    return mul(float4(tiledUv, 0.0f, 1.0f), gMaterial.uvTransform).xy;
}

void BuildCotangentFrame(
    float3 worldPosition,
    float2 texcoord,
    float3 surfaceNormal,
    out float3 tangent,
    out float3 bitangent)
{
    const float3 positionDx = ddx(worldPosition);
    const float3 positionDy = ddy(worldPosition);
    const float2 uvDx = ddx(texcoord);
    const float2 uvDy = ddy(texcoord);
    const float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;

    if (abs(determinant) > 0.000001f)
    {
        const float inverseDeterminant = rcp(determinant);
        tangent = normalize((positionDx * uvDy.y - positionDy * uvDx.y) * inverseDeterminant);
        bitangent = normalize((positionDy * uvDx.x - positionDx * uvDy.x) * inverseDeterminant);
        return;
    }

    const float3 fallbackAxis = abs(surfaceNormal.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(fallbackAxis, surfaceNormal));
    bitangent = normalize(cross(surfaceNormal, tangent));
}

float2 ApplyHeightMap(
    float2 texcoord,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float3 viewDirection)
{
    if (gMaterial.useHeightMap == 0 || abs(gMaterial.heightScale) <= 0.000001f)
    {
        return texcoord;
    }

    const float3 tangentViewDirection = float3(
        dot(viewDirection, tangent),
        dot(viewDirection, bitangent),
        dot(viewDirection, normal));
    const float height = gHeightMap.Sample(gSampler, texcoord).r - 0.5f;
    return texcoord -
        tangentViewDirection.xy /
        max(abs(tangentViewDirection.z), 0.15f) *
        height *
        gMaterial.heightScale;
}

float3 ApplyNormalMap(
    float2 texcoord,
    float3 normal,
    float3 tangent,
    float3 bitangent)
{
    if (gMaterial.useNormalMap == 0)
    {
        return normal;
    }

    float3 tangentNormal = gNormalMap.Sample(gSampler, texcoord).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= gMaterial.normalScale;
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        normal * max(tangentNormal.z, 0.001f));
}

float ComputeOceanShallowWeight(float normalDotView, float normalizedWaveHeight)
{
    const float opticalPathLength =
        max(gMaterial.oceanWaterDepth, 0.1f) /
        max(saturate(normalDotView), 0.24f);
    return EvaluateOceanShallowWeight(
        opticalPathLength,
        normalizedWaveHeight,
        gMaterial.oceanAbsorptionDistance,
        gMaterial.oceanColorBlendScale);
}

GBufferOutput main(PixelShaderInput input)
{
    float3 geometricNormal = normalize(input.normal);

    if (gMaterial.doubleSided != 0 && !input.isFrontFace)
    {
        geometricNormal = -geometricNormal;
    }

    float2 materialUv = BuildMaterialUv(input.texcoord);
    float3 tangent;
    float3 bitangent;
    BuildCotangentFrame(
        input.worldPosition,
        materialUv,
        geometricNormal,
        tangent,
        bitangent);

    const float3 viewDirection = normalize(
        gDirectionalLight.lights[0].cameraPosition - input.worldPosition);
    materialUv = ApplyHeightMap(
        materialUv,
        tangent,
        bitangent,
        geometricNormal,
        viewDirection);

    float4 baseColor = gMaterial.color;

    if (gMaterial.useTexture != 0)
    {
        baseColor *= gBaseColorMap.Sample(gSampler, materialUv);
    }

    if (gMaterial.useOpacityMap != 0)
    {
        baseColor.a *= gOpacityMap.Sample(gSampler, materialUv).r;
    }

    if (gMaterial.alphaMode == 1)
    {
        clip(baseColor.a - saturate(gMaterial.alphaCutoff));
    }

    const float metallicMap = gMaterial.useMetallicMap != 0
        ? gMetallicMap.Sample(gSampler, materialUv).r
        : 1.0f;
    const float roughnessMap = gMaterial.useRoughnessMap != 0
        ? gRoughnessMap.Sample(gSampler, materialUv).r
        : 1.0f;
    const float ambientOcclusionMap = gMaterial.useAmbientOcclusionMap != 0
        ? gAmbientOcclusionMap.Sample(gSampler, materialUv).r
        : 1.0f;
    const float3 emissionMap = gMaterial.useEmissionMap != 0
        ? gEmissionMap.Sample(gSampler, materialUv).rgb
        : float3(1.0f, 1.0f, 1.0f);

    float metallic = saturate(gMaterial.metallic * metallicMap);
    float roughness = clamp(gMaterial.roughness * roughnessMap, 0.035f, 1.0f);
    float surfaceTransmission = saturate(gMaterial.transmission);
    const float ambientOcclusion = lerp(
        1.0f,
        ambientOcclusionMap,
        saturate(gMaterial.ambientOcclusionStrength));
    float3 worldNormal = ApplyNormalMap(
        materialUv,
        geometricNormal,
        tangent,
        bitangent);
    float3 surfaceAlbedo = max(baseColor.rgb, 0.0f);

    float4 oceanSurfaceData = input.oceanData;

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        float3 oceanFftNormal =
            gMaterial.doubleSided != 0 && !input.isFrontFace ? -input.normal : input.normal;
        ResolveOceanPixelSurface(
            input.oceanSamplingData,
            input.oceanWorldAxisX,
            input.oceanWorldAxisY,
            input.oceanWorldAxisZ,
            oceanFftNormal,
            oceanSurfaceData);
        const OceanSurfaceFrame oceanSurfaceFrame = EvaluateOceanSurfaceFrame(
            oceanFftNormal,
            input.worldPosition,
            oceanSurfaceData,
            gMaterial.oceanDetailNormalStrength,
            gMaterial.oceanCrestSharpness);
        // Deferred側もForward側と同じ滑らかな光学法線を格納する。
        // 微細法線をそのままGBufferへ入れると、強い光で粒状の反射へ戻る。
        worldNormal = EvaluateOceanOpticalNormal(oceanSurfaceFrame);
        const float normalDotView = saturate(dot(worldNormal, viewDirection));
        const float shallowWeight = ComputeOceanShallowWeight(normalDotView, oceanSurfaceData.w);
        const float foam = EvaluateOceanFoam(
            input.worldPosition,
            oceanSurfaceData,
            oceanSurfaceFrame,
            gMaterial.oceanFoamStrength,
            gMaterial.oceanFoamThreshold,
            gMaterial.oceanCrestSharpness);
        const float oceanCrest = smoothstep(0.38f, 0.92f, saturate(oceanSurfaceData.w));
        surfaceAlbedo = EvaluateOceanWaterColor(
            gMaterial.oceanDeepColor,
            baseColor.rgb,
            shallowWeight,
            foam,
            oceanSurfaceFrame,
            oceanSurfaceData.w,
            normalDotView);
        metallic = 0.0f;
        roughness = EvaluateOceanRoughness(
            clamp(gMaterial.oceanRoughness, 0.055f, 1.0f),
            worldNormal,
            oceanSurfaceFrame.interpolationVariance,
            oceanCrest,
            foam);
        surfaceTransmission *=
            (1.0f - foam) *
            lerp(0.55f, 1.0f, shallowWeight);
    }

    const float safeIor = max(gMaterial.ior, 1.0001f);
    const float dielectricF0Root = (safeIor - 1.0f) / (safeIor + 1.0f);
    const float dielectricF0 = max(0.04f, dielectricF0Root * dielectricF0Root);
    const float metalF0 = max(
        dot(surfaceAlbedo, float3(0.2126f, 0.7152f, 0.0722f)),
        0.0f);
    const float dielectricSpecularScale = lerp(
        1.0f,
        2.0f,
        saturate(gMaterial.reflectance));
    float materialF0 = lerp(
        saturate(dielectricF0 * dielectricSpecularScale),
        metalF0,
        metallic);

    if (gMaterial.oceanEnabled >= 0.5f)
    {
        materialF0 = dielectricF0Root * dielectricF0Root;
    }

    GBufferOutput output;
    output.albedo = float4(surfaceAlbedo, saturate(baseColor.a));
    output.normal = float4(worldNormal * 0.5f + 0.5f, 1.0f);
    // Deferred Lighting 用契約: x=roughness、y=metallic、z=AO、w=F0。
    // SSR / Planar は Object3dReflectionMask の専用 RT を参照する。
    output.material = float4(
        roughness,
        metallic,
        ambientOcclusion,
        saturate(materialF0));
    output.emission = float4(
        emissionMap * max(gMaterial.emissionColor, 0.0f) * max(gMaterial.emissionStrength, 0.0f),
        surfaceTransmission);
    const float2 currentNdc =
        input.currentClipPosition.xy / max(abs(input.currentClipPosition.w), 0.00001f);
    const float2 previousNdc =
        input.previousClipPosition.xy / max(abs(input.previousClipPosition.w), 0.00001f);
    const float2 currentUv = float2(currentNdc.x * 0.5f + 0.5f, 0.5f - currentNdc.y * 0.5f);
    const float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);
    output.motionVector =
        (currentUv - previousUv) * saturate(input.motionVectorScale);
    return output;
}
