Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float2> gMotionTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer MotionBlurConstants : register(b0) {
    float blurStrength;
    float sampleCount;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    const float2 velocity = gMotionTexture.Sample(gLinearSampler, input.texcoord) * blurStrength;

    float3 color = 0.0f;
    float weightSum = 0.0f;
    const int taps = max((int)sampleCount, 1);

    for (int tapIndex = 0; tapIndex < taps; ++tapIndex) {
        const float factor = (taps <= 1) ? 0.0f : ((float)tapIndex / (float)(taps - 1) - 0.5f);
        const float2 sampleUv = input.texcoord + velocity * factor;
        color += gSceneTexture.Sample(gLinearSampler, sampleUv).rgb;
        weightSum += 1.0f;
    }

    color /= max(weightSum, 0.0001f);
    return float4(color, 1.0f);
}
