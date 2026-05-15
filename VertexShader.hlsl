struct TransformationMatrix {
    float4x4 WVP;
};

cbuffer gTransformationMatrix : register(b0) {
    TransformationMatrix transformationMatrix;
}

struct VertexShaderOutput {
    float4 position :  SV_POSITION;

};

struct VertexShaderInput {
    float4 position : POSITION0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(input.position, transformationMatrix.WVP);
    return output;
}
