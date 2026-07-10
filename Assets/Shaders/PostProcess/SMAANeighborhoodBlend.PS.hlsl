Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float4> gBlendWeightTexture : register(t1);
SamplerState gLinearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float3 sceneColor = gSceneTexture.Sample(gLinearSampler, input.texcoord).rgb;
    const float blendWeight = gBlendWeightTexture.Sample(gLinearSampler, input.texcoord).r;
    const float3 filteredColor = lerp(sceneColor, sceneColor * 0.98f, blendWeight);
    return float4(filteredColor, 1.0f);
}
