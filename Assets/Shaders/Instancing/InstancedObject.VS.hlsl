#include "InstancingCommon.hlsli"

struct CameraTransform
{
    row_major float4x4 viewProjection;
};

ConstantBuffer<CameraTransform> gCameraTransform : register(b0);
StructuredBuffer<InstanceTransform> gInstanceTransforms : register(t0);

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
    float4 color : COLOR0;
};

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    const InstanceTransform instanceTransform = gInstanceTransforms[instanceId];
    const float4 worldPosition = mul(input.position, instanceTransform.world);
    output.position = mul(worldPosition, gCameraTransform.viewProjection);
    output.texcoord = input.texcoord;
    output.color = instanceTransform.color;
    return output;
}
