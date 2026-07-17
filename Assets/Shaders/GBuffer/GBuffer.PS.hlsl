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
};

ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLightData
{
    float4 color;
    float3 direction;
    float intensity;
    float3 position;
    float range;
    float3 skyUpperColor;
    float skyIntensity;
    float3 skyLowerColor;
    float skyEmission;
    float ambientIntensity;
    float horizonSharpness;
    float reflectionIntensity;
    float spotCosInner;
    float spotCosOuter;
    int lightType;
    float areaRadius;
    float3 cameraPosition;
    float padding3;
    float environmentTextureEnabled;
    float environmentTextureIntensity;
    float environmentTextureRotation;
    float environmentTextureMipBias;
    float shadowTileIndex;
    float shadowTileUvScaleX;
    float shadowTileUvScaleY;
    float shadowTileUvBiasX;
    float shadowTileUvBiasY;
    float shadowEnabled;
    float shadowPadding0;
    float shadowPadding1;
    float shadowPadding2;
    row_major float4x4 shadowVP;
};

struct DirectionalLightArray
{
    DirectionalLightData lights[4];
};

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
    bool isFrontFace : SV_IsFrontFace;
};

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 material : SV_TARGET2;
    float4 emission : SV_TARGET3;
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

    const float metallic = saturate(gMaterial.metallic * metallicMap);
    const float roughness = clamp(gMaterial.roughness * roughnessMap, 0.035f, 1.0f);
    const float ambientOcclusion = lerp(
        1.0f,
        ambientOcclusionMap,
        saturate(gMaterial.ambientOcclusionStrength));
    const float3 worldNormal = ApplyNormalMap(
        materialUv,
        geometricNormal,
        tangent,
        bitangent);

    GBufferOutput output;
    output.albedo = float4(max(baseColor.rgb, 0.0f), saturate(baseColor.a));
    output.normal = float4(worldNormal * 0.5f + 0.5f, 1.0f);
    output.material = float4(
        roughness,
        metallic,
        ambientOcclusion,
        saturate(max(gMaterial.reflectance, gMaterial.reflectionProbeIntensity)));
    output.emission = float4(
        emissionMap * max(gMaterial.emissionColor, 0.0f) * max(gMaterial.emissionStrength, 0.0f),
        saturate(gMaterial.transmission));
    return output;
}
