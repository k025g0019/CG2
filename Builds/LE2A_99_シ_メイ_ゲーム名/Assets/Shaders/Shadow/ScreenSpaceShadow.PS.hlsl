Texture2D<float> gDepthTexture : register(t0);
Texture2D<float3> gNormalTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer ScreenSpaceShadowConstants : register(b0) {
    float3 lightDirection;
    float shadowLength;
    float thickness;
    float intensity;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float depth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
    const float3 normal = normalize(gNormalTexture.Sample(gLinearSampler, input.texcoord) * 2.0f - 1.0f);
    const float ndl = saturate(dot(normal, -normalize(lightDirection)));

    const float selfShadow = saturate((1.0f - ndl) * intensity);
    const float depthWeight = saturate(depth * shadowLength + thickness);
    const float shadow = selfShadow * depthWeight;

    return float4(shadow.xxx, 1.0f);
}
