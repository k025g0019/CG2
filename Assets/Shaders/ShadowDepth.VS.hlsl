struct TransformationMatrix {
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
    float4 reflectionClipPlane;
    float4 reflectionClipParams;
    float4 oceanParams0;
    float4 oceanParams1;
    float4 oceanParams2;
    float4 oceanParams3;
    float4 oceanParams4;
    float4 oceanParams5;
    float4 oceanWaveData0[16];
    float4 oceanWaveData1[16];
    float4 surfaceParams0;
    float4 surfaceParams1;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

#define SURFACE_SHADOW_PASS 1
#include "Common/SurfaceDeformation.hlsli"
#include "Common/Skinning.hlsli"

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
    uint instanceId : SV_InstanceID;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    float4 localPosition = input.position;
    float3 localNormal = input.normal;
    ApplyCurrentSkinning(
        input.boneIndices,
        input.boneWeights,
        localPosition,
        localNormal);
    ApplySurfaceVertexDeformation(
        localPosition,
        localNormal,
        input.texcoord,
        gTransformationMatrix.surfaceParams0,
        gTransformationMatrix.surfaceParams1,
        gTransformationMatrix.oceanParams4,
        gTransformationMatrix.oceanParams5.zw,
        input.instanceId);
    output.position = mul(localPosition, gTransformationMatrix.lightWVP);
    output.texcoord = input.texcoord;
    return output;
}
