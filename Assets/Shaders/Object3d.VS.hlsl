struct TransformationMatrix
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
    float4 reflectionClipPlane;
    float4 reflectionClipParams;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
    float reflectionClipDistance : SV_ClipDistance0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    const float4 worldPosition = mul(input.position, gTransformationMatrix.World);
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(input.normal, 0.0f), gTransformationMatrix.World).xyz);
    output.worldPosition = worldPosition.xyz;
    output.shadowPosition = mul(input.position, gTransformationMatrix.lightWVP);

    output.reflectionClipDistance = gTransformationMatrix.reflectionClipParams.x > 0.5f
        ? dot(worldPosition, gTransformationMatrix.reflectionClipPlane)
        : 1.0f;

    return output;
}
