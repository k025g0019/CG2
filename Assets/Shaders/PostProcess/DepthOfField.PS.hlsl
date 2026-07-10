Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer DepthOfFieldConstants : register(b0) {
    float focusDepth;
    float focusRange;
    float blurScale;
    float2 inverseResolution;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float depth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
    const float coc = saturate(abs(depth - focusDepth) / max(focusRange, 0.0001f)) * blurScale;

    float3 color = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int sampleIndex = -4; sampleIndex <= 4; ++sampleIndex) {
        const float2 offset = float2(sampleIndex, 0.0f) * inverseResolution * coc;
        const float weight = 1.0f - abs(sampleIndex) / 4.0f;
        color += gSceneTexture.Sample(gLinearSampler, input.texcoord + offset).rgb * weight;
        weightSum += weight;
    }

    color /= max(weightSum, 0.0001f);
    return float4(color, 1.0f);
}
