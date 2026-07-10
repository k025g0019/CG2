struct MotionVectorTransform
{
    row_major float4x4 currentWVP;
    row_major float4x4 previousWVP;
};

ConstantBuffer<MotionVectorTransform> gMotionTransform : register(b0);

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 currentClip : TEXCOORD0;
    float4 previousClip : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.currentClip = mul(input.position, gMotionTransform.currentWVP);
    output.previousClip = mul(input.position, gMotionTransform.previousWVP);
    output.position = output.currentClip;
    return output;
}
