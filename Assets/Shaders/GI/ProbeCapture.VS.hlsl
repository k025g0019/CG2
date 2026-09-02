//============================================================
// Light Probe Bake: シーンをProbe位置から6面キューブへ描く頂点シェーダ
//------------------------------------------------------------
// 通常描画のVSは事前計算済みWVPを使うため、任意視点では使えない。
// 影パスと同じく、World行列にRoot定数のViewProjectionを掛ける。
//============================================================
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

cbuffer ProbeCaptureView : register(b3)
{
    row_major float4x4 gProbeViewProjection;
    float3 gProbeWorldPosition;
    float gProbeCaptureFarDistance;
};

#define SURFACE_SHADOW_PASS 1
#include "../Common/SurfaceDeformation.hlsli"
#include "../Common/Skinning.hlsli"

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
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
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
    const float4 worldPosition = mul(localPosition, gTransformationMatrix.World);
    output.position = mul(worldPosition, gProbeViewProjection);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(localNormal, 0.0f), gTransformationMatrix.World).xyz);
    output.worldPosition = worldPosition.xyz;
    return output;
}
