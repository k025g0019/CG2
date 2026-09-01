Texture2D gEquirectangularMap : register(t0);
SamplerState gLinearClamp : register(s0);

static const float PI = 3.14159265359f;

float2 DirectionToEquirectUV(float3 dir)
{
    dir = normalize(dir);
    float u = atan2(dir.z, dir.x) / (2.0f * PI) + 0.5f;
    float v = asin(saturate(dir.y) * 2.0f - 1.0f) / PI + 0.5f;
    return float2(u, 1.0f - v);
}

struct PSInput
{
    float4 positionCS : SV_POSITION;
    float3 direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 dir = normalize(input.direction);
    float2 uv = DirectionToEquirectUV(dir);
    float3 color = gEquirectangularMap.Sample(gLinearClamp, uv).rgb;
    return float4(color, 1.0f);
}
