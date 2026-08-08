#ifndef INSTANCINGCOMMON_HLSLI
#define INSTANCINGCOMMON_HLSLI

struct InstanceTransform
{
    row_major float4x4 world;
    float4 color;
};

#endif
