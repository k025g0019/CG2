struct DepthTransform
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
};

ConstantBuffer<DepthTransform> gDepthTransform : register(b0);

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, gDepthTransform.WVP);
    output.texcoord = input.texcoord;
    return output;
}
