Texture2D<float4> gSceneTexture : register(t0);
SamplerState gLinearSampler : register(s0);

cbuffer SharpenConstants : register(b0) {
    float2 inverseResolution;
    float sharpenStrength;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float3 center = gSceneTexture.Sample(gLinearSampler, input.texcoord).rgb;
    const float3 left = gSceneTexture.Sample(gLinearSampler, input.texcoord + float2(-inverseResolution.x, 0.0f)).rgb;
    const float3 right = gSceneTexture.Sample(gLinearSampler, input.texcoord + float2(inverseResolution.x, 0.0f)).rgb;
    const float3 up = gSceneTexture.Sample(gLinearSampler, input.texcoord + float2(0.0f, -inverseResolution.y)).rgb;
    const float3 down = gSceneTexture.Sample(gLinearSampler, input.texcoord + float2(0.0f, inverseResolution.y)).rgb;

    const float3 neighborAverage = (left + right + up + down) * 0.25f;
    const float3 highFrequency = center - neighborAverage;
    const float3 sharpened = saturate(center + highFrequency * sharpenStrength);
    return float4(sharpened, 1.0f);
}
