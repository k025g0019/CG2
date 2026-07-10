Texture2D<float4> gEdgeTexture : register(t0);
SamplerState gLinearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float edge = gEdgeTexture.Sample(gLinearSampler, input.texcoord).r;
    const float weight = saturate(edge * 2.0f);
    return float4(weight.xxx, 1.0f);
}
