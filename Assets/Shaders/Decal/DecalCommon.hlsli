#ifndef DECALCOMMON_HLSLI
#define DECALCOMMON_HLSLI

float3 DecodeNormal(float3 encodedNormal)
{
    return normalize(encodedNormal * 2.0f - 1.0f);
}

#endif
