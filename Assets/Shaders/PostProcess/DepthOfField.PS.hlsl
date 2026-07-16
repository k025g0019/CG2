Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gLinearSampler : register(s0);

cbuffer DepthOfFieldConstants : register(b0) {
    float focusDistance;
    float apertureScale;
    float nearClip;
    float farClip;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float GetLinearDepth(float nonlinearDepth) {
    const float ndcZ = nonlinearDepth * 2.0f - 1.0f;
    return nearClip * farClip / (farClip - ndcZ * (farClip - nearClip));
}

float4 main(PSInput input) : SV_TARGET {
    const float nonlinearDepth = gDepthTexture.Sample(gLinearSampler, input.texcoord);
    const float linearDepth = GetLinearDepth(nonlinearDepth);
    const float coc = saturate(abs(linearDepth - focusDistance) / max(focusDistance, 0.001f)) * apertureScale;

    float3 color = gSceneTexture.Sample(gLinearSampler, input.texcoord).rgb;
    if (coc < 0.001f) {
        return float4(color, 1.0f);
    }

    float3 blurColor = 0.0f;
    float weightSum = 0.0f;
    const int kTapCount = 7;

    for (int y = -kTapCount; y <= kTapCount; ++y) {
        for (int x = -kTapCount; x <= kTapCount; ++x) {
            const float2 offset = float2(x, y) * coc * float2(1.0f / 1920.0f, 1.0f / 1080.0f);
            const float weight = exp(-float(x * x + y * y) / (2.0f * max(coc, 0.5f)));
            blurColor += gSceneTexture.Sample(gLinearSampler, input.texcoord + offset).rgb * weight;
            weightSum += weight;
        }
    }

    color = lerp(color, blurColor / max(weightSum, 0.0001f), saturate(coc));
    return float4(color, 1.0f);
}
