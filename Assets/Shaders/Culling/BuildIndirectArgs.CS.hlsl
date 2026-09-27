struct CullingObject
{
    float3 boundsCenter;
    float3 boundsExtent;
    int gameObjectId;
    uint vertexCount;
    uint indexCount;
    uint isIndexed;
};

struct IndirectArguments
{
    uint vertexOrIndexCountPerInstance;
    uint instanceCount;
    uint startVertexOrIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
	uint padding;
};

StructuredBuffer<CullingObject> gCullingObjects : register(t0);
StructuredBuffer<uint> gVisibility : register(t1);
RWStructuredBuffer<IndirectArguments> gDrawArguments : register(u0);

cbuffer CullingConstants : register(b0)
{
    uint gObjectCount;
    uint2 gDepthPyramidSize;
    float gDepthBias;
    row_major float4x4 gViewProjection;
    float4 gUnusedParameters;
};

//================================================================
// 非表示物体の頂点数を 0 にした D3D12_DRAW_ARGUMENTS を生成する
//================================================================

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;

    if (objectIndex >= gObjectCount)
    {
        return;
    }

    IndirectArguments drawArguments;
    drawArguments.vertexOrIndexCountPerInstance = gVisibility[objectIndex] != 0u
        ? (gCullingObjects[objectIndex].isIndexed != 0u
            ? gCullingObjects[objectIndex].indexCount
            : gCullingObjects[objectIndex].vertexCount)
        : 0u;
    drawArguments.instanceCount = gVisibility[objectIndex] != 0u ? 1u : 0u;
    drawArguments.startVertexOrIndexLocation = 0u;
    drawArguments.baseVertexLocation = 0;
    drawArguments.startInstanceLocation = 0u;
	drawArguments.padding = 0u;
    gDrawArguments[objectIndex] = drawArguments;
}
