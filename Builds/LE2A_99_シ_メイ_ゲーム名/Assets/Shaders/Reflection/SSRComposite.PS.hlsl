Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gSSRColor : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    float3 sceneColor = gSceneColor.Sample(gSampler, input.texcoord).rgb;
    float4 ssr = gSSRColor.Sample(gSampler, input.texcoord);
    float4 materialMask = gMaterialMask.Sample(gSampler, input.texcoord);
    float3 reflection = ssr.rgb;
    float reflectMask = saturate(materialMask.x);
    float smoothness = saturate(materialMask.y);
    float reflectionMode = materialMask.z;
    float useSsr = reflectionMode < 0.5f ? 1.0f : 0.0f;
    float mask = saturate(ssr.a * reflectMask * smoothness * useSsr);
    return float4(lerp(sceneColor, reflection, mask), 1.0f);
}
