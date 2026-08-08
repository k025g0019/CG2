struct WaterTransform
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    float time;
    float amplitude;
    float frequency;
    float speed;
};

ConstantBuffer<WaterTransform> gWaterTransform : register(b0);

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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPosition = input.position;
    localPosition.y += sin(input.position.x * gWaterTransform.frequency + gWaterTransform.time * gWaterTransform.speed) * gWaterTransform.amplitude;
    const float4 worldPosition = mul(localPosition, gWaterTransform.World);
    output.position = mul(localPosition, gWaterTransform.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(input.normal, 0.0f), gWaterTransform.World).xyz);
    output.worldPosition = worldPosition.xyz;
    return output;
}
