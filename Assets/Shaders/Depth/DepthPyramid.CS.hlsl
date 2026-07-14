Texture2D<float> gSceneDepth : register(t0);
RWTexture2D<float2> gDepthPyramidLevel : register(u0);

cbuffer DepthPyramidConstants : register(b0)
{
    uint2 gSourceSize;
    uint2 gDestinationSize;
    row_major float4x4 gUnusedMatrix;
};

//================================================================
// Scene Depth を深度ピラミッドの level 0 へ複製
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gDestinationSize))
    {
        return;
    }

    const uint2 sourcePosition = min(dispatchThreadId.xy, gSourceSize - 1u);
    const float sceneDepth = gSceneDepth.Load(int3(sourcePosition, 0));
    gDepthPyramidLevel[dispatchThreadId.xy] = float2(sceneDepth, sceneDepth);
}
