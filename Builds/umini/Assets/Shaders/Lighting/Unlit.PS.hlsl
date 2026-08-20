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
    float padding0;
    float padding1;
    float padding2;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float4 transformedTexcoord = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 texColor = gTexture.Sample(gSampler, transformedTexcoord.xy);
    float4 baseColor = gMaterial.useTexture != 0 ? texColor : float4(1.0f, 1.0f, 1.0f, 1.0f);
    return gMaterial.color * baseColor;
}
