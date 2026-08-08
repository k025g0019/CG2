Texture2D gSceneColor : register(t0);
Texture2D gFogTexture : register(t1);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float4 sceneColor = gSceneColor.Sample(gSampler, input.texcoord);
    const float4 fogColor = gFogTexture.Sample(gSampler, input.texcoord);
    return float4(lerp(sceneColor.rgb, fogColor.rgb, fogColor.a), sceneColor.a);
}
