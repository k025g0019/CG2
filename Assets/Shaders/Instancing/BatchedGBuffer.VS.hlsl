#include "BatchedInstanceCommon.hlsli"

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
    float4 oceanData : TEXCOORD2;
    float4 currentClipPosition : TEXCOORD3;
    float4 previousClipPosition : TEXCOORD4;
    float2 motionVectorScale : TEXCOORD5;
    float4 oceanSamplingData : TEXCOORD6;
    nointerpolation float3 oceanWorldAxisX : TEXCOORD7;
    nointerpolation float3 oceanWorldAxisY : TEXCOORD8;
    nointerpolation float3 oceanWorldAxisZ : TEXCOORD9;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    const BatchedInstanceData instanceData = gBatchedInstances[instanceId];
    VertexShaderOutput output = (VertexShaderOutput)0;
	if (!IsBatchedInstanceVisible(instanceData))
	{
		output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
		return output;
	}
    const float4 worldPosition = mul(input.position, instanceData.World);
    output.position = mul(input.position, instanceData.WVP);
    output.currentClipPosition = output.position;
    output.previousClipPosition = mul(input.position, instanceData.previousWVP);
    output.motionVectorScale = instanceData.temporalParams.zw;
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(float4(input.normal, 0.0f), instanceData.World).xyz);
    output.worldPosition = worldPosition.xyz;
    output.oceanData = 0.0f;
    output.oceanSamplingData = 0.0f;
    output.oceanWorldAxisX = mul(float4(1.0f, 0.0f, 0.0f, 0.0f), instanceData.World).xyz;
    output.oceanWorldAxisY = mul(float4(0.0f, 1.0f, 0.0f, 0.0f), instanceData.World).xyz;
    output.oceanWorldAxisZ = mul(float4(0.0f, 0.0f, 1.0f, 0.0f), instanceData.World).xyz;
    return output;
}
