Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

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
    row_major float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 uvPosition = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    const float4 textureColor = gMaterial.useTexture != 0 ? gTexture.Sample(gSampler, uvPosition.xy) : 1.0f.xxxx;
    const float3 lit = abs(normalize(input.normal)) * 0.35f + 0.65f;
    return float4(textureColor.rgb * gMaterial.color.rgb * lit, textureColor.a * gMaterial.color.a);
}
