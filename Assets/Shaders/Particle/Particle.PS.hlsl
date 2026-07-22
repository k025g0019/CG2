struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 centerOffset = input.texcoord * 2.0f - 1.0f;
    const float circleAlpha = saturate(1.0f - dot(centerOffset, centerOffset));
    return float4(input.color.rgb, input.color.a * circleAlpha);
}
