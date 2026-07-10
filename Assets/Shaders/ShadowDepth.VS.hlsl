struct TransformationMatrix {
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VertexShaderInput input) : SV_POSITION {
    return mul(input.position, gTransformationMatrix.lightWVP);
}
