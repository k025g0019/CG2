#include "BatchedInstanceCommon.hlsli"
#include "../Common/NormalTransform.hlsli"

struct DrawParameters
{
    row_major float4x4 unusedWVP;
    row_major float4x4 unusedWorld;
    row_major float4x4 unusedLightWVP;
    float4 reflectionClipPlane;
    float4 reflectionClipParams;
};

ConstantBuffer<DrawParameters> gDrawParameters : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
    float4 oceanData : TEXCOORD3;
    float4 oceanSamplingData : TEXCOORD4;
    nointerpolation float3 oceanWorldAxisX : TEXCOORD5;
    nointerpolation float3 oceanWorldAxisY : TEXCOORD6;
    nointerpolation float3 oceanWorldAxisZ : TEXCOORD7;
    float reflectionClipDistance : SV_ClipDistance0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    const BatchedInstanceData instanceData = gBatchedInstances[instanceId];
    VertexShaderOutput output = (VertexShaderOutput)0;
	if (!IsBatchedInstanceVisible(instanceData))
	{
		output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
		output.reflectionClipDistance = 1.0f;
		return output;
	}
    const float4 worldPosition = mul(input.position, instanceData.World);
    output.position = mul(input.position, instanceData.WVP);
    output.texcoord = input.texcoord;
    output.normal = TransformNormalToWorld(input.normal, instanceData.World);
    output.worldPosition = worldPosition.xyz;
    output.shadowPosition = mul(input.position, instanceData.lightWVP);
    output.oceanData = 0.0f;
    output.oceanSamplingData = 0.0f;
    output.oceanWorldAxisX = mul(float4(1.0f, 0.0f, 0.0f, 0.0f), instanceData.World).xyz;
    output.oceanWorldAxisY = mul(float4(0.0f, 1.0f, 0.0f, 0.0f), instanceData.World).xyz;
    output.oceanWorldAxisZ = mul(float4(0.0f, 0.0f, 1.0f, 0.0f), instanceData.World).xyz;
    output.reflectionClipDistance = gDrawParameters.reflectionClipParams.x > 0.5f
        ? dot(worldPosition, gDrawParameters.reflectionClipPlane)
        : 1.0f;
    return output;
}
