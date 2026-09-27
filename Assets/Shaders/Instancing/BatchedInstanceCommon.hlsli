struct BatchedInstanceData
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
    row_major float4x4 previousWVP;
    float4 temporalParams;
	uint cullingObjectIndex;
	uint3 padding;
};

StructuredBuffer<BatchedInstanceData> gBatchedInstances : register(t16);
StructuredBuffer<uint> gBatchedVisibility : register(t17);

bool IsBatchedInstanceVisible(BatchedInstanceData instanceData)
{
	return instanceData.cullingObjectIndex == 0xffffffffu ||
		gBatchedVisibility[instanceData.cullingObjectIndex] != 0u;
}
