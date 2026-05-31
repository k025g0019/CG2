struct Material {
    float4 color;
    int enableLighting;
    float3 padding;
    float4x4 uvTransform;
};

struct DirectionalLight {
    float4 color;
    float3 direction;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input) {
    PixelShaderOutput output;
    float4 transformedTexcoord = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedTexcoord.xy);
    output.color = gMaterial.color * textureColor;

    if (gMaterial.enableLighting != 0) {
        float3 normalizedNormal = normalize(input.normal);
        float3 normalizedLightDirection = normalize(gDirectionalLight.direction);
        float cosine = dot(normalizedNormal, -normalizedLightDirection);
        float halfLambert = pow(saturate(cosine * 0.5f + 0.5f), 2.0f);

        output.color.rgb *= gDirectionalLight.color.rgb * halfLambert * gDirectionalLight.intensity;
    }

    return output;
}

