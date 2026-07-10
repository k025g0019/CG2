struct GBufferTransform
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
};

ConstantBuffer<GBufferTransform> gTransform : register(b0);

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
    float3 normal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 shadowPosition : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(input.position, gTransform.World);
    output.position = mul(input.position, gTransform.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(input.normal, 0.0f), gTransform.World).xyz);
    output.worldPosition = worldPosition.xyz;
    output.shadowPosition = mul(input.position, gTransform.lightWVP);
    return output;
}
