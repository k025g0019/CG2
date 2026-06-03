struct Material {
    float4 color;
    int enableLighting;
    int useTexture;
    float2 padding;
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
    float4 baseColor = gMaterial.useTexture != 0 ? textureColor : float4(1.0f, 1.0f, 1.0f, 1.0f);
    output.color = gMaterial.color * baseColor;

    if (gMaterial.enableLighting != 0) {
        float3 normalizedNormal = normalize(input.normal);
        float3 normalizedLightDirection = normalize(gDirectionalLight.direction);
        float cosine = dot(normalizedNormal, -normalizedLightDirection);
		float halfLambert = saturate(cosine * 0.5f + 0.5f);

		output.color.rgb *= gDirectionalLight.color.rgb * halfLambert * gDirectionalLight.intensity;
    }

    output.color.rgb = pow(abs(output.color.rgb), 1.0f / 2.2f);
    return output;
}

