#include "Particle.hlsli"

//========================================
// インスタンシング用データ
//========================================

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

StructuredBuffer<TransformationMatrix> gTransformationMatrix : register(t0);


//========================================
// 頂点入力
//========================================

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};


//========================================
// 頂点シェーダー
//========================================

VertexShaderOutput main(
    VertexShaderInput input,
    uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    //------------------------------
    // 座標変換
    //------------------------------

    output.position = mul(
        input.position,
        gTransformationMatrix[instanceId].WVP);


    //------------------------------
    // UV・法線変換
    //------------------------------

    output.texcoord = input.texcoord;

    output.normal = normalize(
        mul(
            input.normal,
            (float3x3)gTransformationMatrix[instanceId].World));

    return output;
}