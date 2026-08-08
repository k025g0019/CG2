#ifndef DEPTHCOMMON_HLSLI
#define DEPTHCOMMON_HLSLI

float LinearizeDepth(float depth, float nearClip, float farClip)
{
    return (nearClip * farClip) / max(farClip - depth * (farClip - nearClip), 0.0001f);
}

#endif
