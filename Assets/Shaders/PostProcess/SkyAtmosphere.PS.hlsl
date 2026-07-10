struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct SkyAtmosphereCB
{
    float3 skyTopColor;
    float skyIntensity;
    float3 skyBottomColor;
    float horizonSharpness;
};

ConstantBuffer<SkyAtmosphereCB> gSkyAtmosphere : register(b0);

float4 main(PSInput input) : SV_TARGET0
{
    const float viewY = saturate(1.0f - input.texcoord.y);
    const float blend = pow(viewY, max(gSkyAtmosphere.horizonSharpness, 0.001f));
    const float3 color = lerp(gSkyAtmosphere.skyBottomColor, gSkyAtmosphere.skyTopColor, blend) * gSkyAtmosphere.skyIntensity;
    return float4(color, 1.0f);
}
