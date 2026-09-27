#define OCEAN_PI 3.14159265359f

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

RWStructuredBuffer<float2> gInitialSpectrum : register(u0);
RWStructuredBuffer<float2> gHeightSpectrum : register(u1);
RWStructuredBuffer<float2> gDisplacementXSpectrum : register(u2);
RWStructuredBuffer<float2> gDisplacementZSpectrum : register(u3);
RWStructuredBuffer<float2> gGradientXSpectrum : register(u4);
RWStructuredBuffer<float2> gGradientZSpectrum : register(u5);

float GetWaveNumber(uint index)
{
    const int signedIndex = index < gFftResolution / 2u
        ? int(index)
        : int(index) - int(gFftResolution);
    return 2.0f * OCEAN_PI * float(signedIndex) / max(gDomainLength, 0.001f);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gFftResolution || dispatchThreadId.y >= gFftResolution)
    {
        return;
    }

    const uint spectrumIndex = dispatchThreadId.y * gFftResolution + dispatchThreadId.x;
    const uint mirrorX = (gFftResolution - dispatchThreadId.x) & (gFftResolution - 1u);
    const uint mirrorY = (gFftResolution - dispatchThreadId.y) & (gFftResolution - 1u);
    const uint mirrorIndex = mirrorY * gFftResolution + mirrorX;
    const float waveNumberX = GetWaveNumber(dispatchThreadId.x);
    const float waveNumberZ = GetWaveNumber(dispatchThreadId.y);
    const float waveNumberLength = length(float2(waveNumberX, waveNumberZ));

    if (waveNumberLength <= 0.000001f)
    {
        gHeightSpectrum[spectrumIndex] = 0.0f;
        gDisplacementXSpectrum[spectrumIndex] = 0.0f;
        gDisplacementZSpectrum[spectrumIndex] = 0.0f;
        gGradientXSpectrum[spectrumIndex] = 0.0f;
        gGradientZSpectrum[spectrumIndex] = 0.0f;
        return;
    }

    const float angularFrequency = sqrt(
        max(gGravity, 0.001f) *
        waveNumberLength *
        tanh(waveNumberLength * max(gWaterDepth, 0.001f)));
    const float phase = angularFrequency * gOceanTime;
    const float phaseCos = cos(phase);
    const float phaseSin = sin(phase);
    const float2 initialValue = gInitialSpectrum[spectrumIndex];
    const float2 mirroredValue = gInitialSpectrum[mirrorIndex];

    const float2 positiveFrequency = float2(
        initialValue.x * phaseCos - initialValue.y * phaseSin,
        initialValue.x * phaseSin + initialValue.y * phaseCos);
    const float2 negativeFrequency = float2(
        mirroredValue.x * phaseCos - mirroredValue.y * phaseSin,
        -mirroredValue.x * phaseSin - mirroredValue.y * phaseCos);
    const float2 heightFrequency = positiveFrequency + negativeFrequency;
    const float2 imaginaryHeight = float2(-heightFrequency.y, heightFrequency.x);
    const float inverseWaveNumberLength = rcp(waveNumberLength);

    gHeightSpectrum[spectrumIndex] = heightFrequency;
    gDisplacementXSpectrum[spectrumIndex] =
        -imaginaryHeight * waveNumberX * inverseWaveNumberLength;
    gDisplacementZSpectrum[spectrumIndex] =
        -imaginaryHeight * waveNumberZ * inverseWaveNumberLength;
    gGradientXSpectrum[spectrumIndex] = imaginaryHeight * waveNumberX;
    gGradientZSpectrum[spectrumIndex] = imaginaryHeight * waveNumberZ;
}
