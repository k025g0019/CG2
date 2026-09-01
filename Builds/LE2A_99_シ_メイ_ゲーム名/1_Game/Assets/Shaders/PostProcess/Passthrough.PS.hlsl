Texture2D<float4> gSceneTexture : register(t0);
SamplerState gLinearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    return gSceneTexture.Sample(gLinearSampler, input.texcoord);
}
