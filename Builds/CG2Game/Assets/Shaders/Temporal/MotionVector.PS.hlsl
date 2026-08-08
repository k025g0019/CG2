struct PSInput
{
    float4 position : SV_POSITION;
    float4 currentClip : TEXCOORD0;
    float4 previousClip : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 currentNdc = input.currentClip.xy / max(input.currentClip.w, 0.0001f);
    const float2 previousNdc = input.previousClip.xy / max(input.previousClip.w, 0.0001f);
    const float2 motion = currentNdc - previousNdc;
    return float4(motion * 0.5f + 0.5f, 0.0f, 1.0f);
}
