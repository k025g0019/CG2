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

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D gBaseColorTexture : register(t0);
Texture2D gOpacityTexture : register(t13);
SamplerState gTextureSampler : register(s0);

void main(PixelShaderInput input)
{
    const float2 tiledUv = input.texcoord * gMaterial.uvTiling + gMaterial.uvOffset;
    const float2 materialUv = mul(float4(tiledUv, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    float materialAlpha = gMaterial.color.a;

    if (gMaterial.useTexture != 0)
    {
        materialAlpha *= gBaseColorTexture.Sample(gTextureSampler, materialUv).a;
    }

    if (gMaterial.useOpacityMap != 0)
    {
        materialAlpha *= gOpacityTexture.Sample(gTextureSampler, materialUv).r;
    }

    clip(materialAlpha - saturate(gMaterial.alphaCutoff));
}
