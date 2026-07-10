#ifndef TRANSPARENCYCOMMON_HLSLI
#define TRANSPARENCYCOMMON_HLSLI

float ComputeAlphaFade(float alpha, float cutoff)
{
    return saturate((alpha - cutoff) / max(1.0f - cutoff, 0.0001f));
}

#endif
