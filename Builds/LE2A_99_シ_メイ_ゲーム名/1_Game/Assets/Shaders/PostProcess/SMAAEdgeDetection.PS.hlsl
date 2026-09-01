Texture2D<float4> gSceneTexture : register(t0);
SamplerState gLinearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float3 color = gSceneTexture.Sample(gLinearSampler, input.texcoord).rgb;
    const float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    return float4(luminance.xxx, 1.0f);
}
