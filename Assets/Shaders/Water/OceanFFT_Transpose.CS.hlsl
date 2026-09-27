cbuffer OceanFftConstants : register(b0)
{
    float gOceanTime;
    float gGravity;
    float gWaterDepth;
    uint gFftResolution;
    float gDomainLength;
    float gMaxWaveHeight;
    float gChoppiness;
    float gFoamThreshold;
    float gFoamStrength;
    float gCrestSharpness;
    float gHeightScale;
    uint gSurfaceSampleCount;
    float4 gOceanPadding;
};

RWStructuredBuffer<float2> gInput : register(u0);
RWStructuredBuffer<float2> gOutput : register(u1);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gFftResolution || dispatchThreadId.y >= gFftResolution)
    {
        return;
    }

    gOutput[dispatchThreadId.y * gFftResolution + dispatchThreadId.x] =
        gInput[dispatchThreadId.x * gFftResolution + dispatchThreadId.y];
}
