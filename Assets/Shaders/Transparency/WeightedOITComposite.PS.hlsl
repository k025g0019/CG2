Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float4> gAccumulationTexture : register(t1);
Texture2D<float> gRevealageTexture : register(t2);
SamplerState gLinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 sceneColor = gSceneTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f);
    const float4 accumulation = gAccumulationTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f);
    const float revealage = saturate(
        gRevealageTexture.SampleLevel(gLinearSampler, input.texcoord, 0.0f));
    const float3 transparentColor = accumulation.rgb / max(accumulation.a, 0.00001f);
    const float coverage = 1.0f - revealage;
    return float4(lerp(sceneColor.rgb, transparentColor, coverage), sceneColor.a);
}
