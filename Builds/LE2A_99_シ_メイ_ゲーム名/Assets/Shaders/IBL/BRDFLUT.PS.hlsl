#include "../Common/PBRCommon.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 integrated = IntegrateBRDF(saturate(input.texcoord.x), saturate(input.texcoord.y));
    return float4(integrated, 0.0f, 1.0f);
}
