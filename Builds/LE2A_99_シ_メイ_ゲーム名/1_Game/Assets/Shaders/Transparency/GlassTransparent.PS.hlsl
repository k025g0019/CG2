Texture2D gSceneColor : register(t0);
Texture2D gRefractionMask : register(t1);
SamplerState gSampler : register(s0);

struct GlassTransparentCB
{
    float4 tintColor;
    float refractionScale;
    float reflectionMix;
    float padding0;
    float padding1;
};

ConstantBuffer<GlassTransparentCB> gGlassTransparent : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float mask = gRefractionMask.Sample(gSampler, input.texcoord).r;
    const float2 offset = (input.texcoord - 0.5f) * gGlassTransparent.refractionScale * mask;
    const float3 refractedColor = gSceneColor.Sample(gSampler, input.texcoord + offset).rgb;
    const float3 color = lerp(refractedColor, gGlassTransparent.tintColor.rgb, gGlassTransparent.reflectionMix);
    return float4(color, gGlassTransparent.tintColor.a * mask);
}
