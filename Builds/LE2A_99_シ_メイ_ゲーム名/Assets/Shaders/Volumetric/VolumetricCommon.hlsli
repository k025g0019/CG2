#ifndef VOLUMETRICCOMMON_HLSLI
#define VOLUMETRICCOMMON_HLSLI

float ComputeFogFactor(float distance, float density)
{
    return 1.0f - exp(-distance * max(density, 0.0001f));
}

#endif
