Texture2D<float2> gSourceDepthLevel : register(t0);
RWTexture2D<float2> gDestinationDepthLevel : register(u0);

cbuffer DepthDownsampleConstants : register(b0)
{
    uint2 gSourceSize;
    uint2 gDestinationSize;
    row_major float4x4 gUnusedMatrix;
};

float2 LoadDepthClamped(uint2 sourcePosition)
{
    return gSourceDepthLevel.Load(int3(min(sourcePosition, gSourceSize - 1u), 0));
}

//================================================================
// 標準 Z の SSR と遮蔽判定で使う 2x2 の最小・最大深度を残す
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gDestinationSize))
    {
        return;
    }

    const uint2 sourceBase = dispatchThreadId.xy * 2u;
    const float2 depth00 = LoadDepthClamped(sourceBase);
    const float2 depth10 = LoadDepthClamped(sourceBase + uint2(1u, 0u));
    const float2 depth01 = LoadDepthClamped(sourceBase + uint2(0u, 1u));
    const float2 depth11 = LoadDepthClamped(sourceBase + uint2(1u, 1u));
    const float minimumDepth = min(min(depth00.x, depth10.x), min(depth01.x, depth11.x));
    const float maximumDepth = max(max(depth00.y, depth10.y), max(depth01.y, depth11.y));
    gDestinationDepthLevel[dispatchThreadId.xy] = float2(minimumDepth, maximumDepth);
}
