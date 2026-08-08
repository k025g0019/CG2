struct CullingObject
{
    float3 boundsCenter;
    float3 boundsExtent;
    int gameObjectId;
    uint vertexCount;
};

struct DrawArguments
{
    uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<CullingObject> gCullingObjects : register(t0);
StructuredBuffer<uint> gVisibility : register(t1);
RWStructuredBuffer<DrawArguments> gDrawArguments : register(u0);

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

    DrawArguments drawArguments;
    drawArguments.vertexCountPerInstance = gVisibility[objectIndex] != 0u
        ? gCullingObjects[objectIndex].vertexCount
        : 0u;
    drawArguments.instanceCount = 1u;
    drawArguments.startVertexLocation = 0u;
    drawArguments.startInstanceLocation = 0u;
    gDrawArguments[objectIndex] = drawArguments;
}
