#define OCEAN_PI 3.14159265359f
#define OCEAN_MAX_FFT_RESOLUTION 2048

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

groupshared float2 gSharedSpectrum[OCEAN_MAX_FFT_RESOLUTION];

uint ReverseBits(uint value, uint bitCount)
{
    uint reversedValue = 0u;

    for (uint bitIndex = 0u; bitIndex < bitCount; bitIndex++)
    {
        reversedValue = (reversedValue << 1u) | (value & 1u);
        value >>= 1u;
    }

    return reversedValue;
}

uint GetLog2Resolution()
{
    uint stageCount = 0u;
    uint remainingResolution = gFftResolution;

    while (remainingResolution > 1u)
    {
        remainingResolution >>= 1u;
        stageCount++;
    }

    return stageCount;
}

[numthreads(1024, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint threadIndex = groupThreadId.x;
    const uint activeThreadCount = gFftResolution / 2u;
    const bool isActiveThread = threadIndex < activeThreadCount;
    const uint fftStageCount = GetLog2Resolution();
    uint firstIndex = 0u;
    uint secondIndex = 0u;

    if (isActiveThread)
    {
        firstIndex = threadIndex;
        secondIndex = threadIndex + activeThreadCount;
        const uint rowOffset = groupId.x * gFftResolution;
        gSharedSpectrum[firstIndex] =
            gInput[rowOffset + ReverseBits(firstIndex, fftStageCount)];
        gSharedSpectrum[secondIndex] =
            gInput[rowOffset + ReverseBits(secondIndex, fftStageCount)];
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint fftStage = 1u; fftStage <= fftStageCount; fftStage++)
    {
        if (isActiveThread)
        {
            const uint halfBlockSize = 1u << (fftStage - 1u);
            const uint blockSize = halfBlockSize * 2u;
            const uint blockIndex = threadIndex / halfBlockSize;
            const uint butterflyIndex = threadIndex % halfBlockSize;
            const uint butterflyFirst = blockIndex * blockSize + butterflyIndex;
            const uint butterflySecond = butterflyFirst + halfBlockSize;
            const float angle =
                2.0f * OCEAN_PI * float(butterflyIndex) / float(blockSize);
            const float2 twiddle = float2(cos(angle), sin(angle));
            const float2 secondValue = gSharedSpectrum[butterflySecond];
            const float2 weightedSecond = float2(
                twiddle.x * secondValue.x - twiddle.y * secondValue.y,
                twiddle.x * secondValue.y + twiddle.y * secondValue.x);
            const float2 firstValue = gSharedSpectrum[butterflyFirst];
            gSharedSpectrum[butterflyFirst] = firstValue + weightedSecond;
            gSharedSpectrum[butterflySecond] = firstValue - weightedSecond;
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (isActiveThread)
    {
        const float inverseResolution = rcp(float(gFftResolution));
        const uint rowOffset = groupId.x * gFftResolution;
        gOutput[rowOffset + firstIndex] = gSharedSpectrum[firstIndex] * inverseResolution;
        gOutput[rowOffset + secondIndex] = gSharedSpectrum[secondIndex] * inverseResolution;
    }
}
