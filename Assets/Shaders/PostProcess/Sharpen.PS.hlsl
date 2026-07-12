#include "lygia/color/dither/interleavedGradientNoise.hlsl"
#include "lygia/generative/random.hlsl"
#include "FidelityFX/CAS/ShaderLibDX/ffx_a.h"

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

    //============================================================
    // sharpen 後に微小 dithering を足して、
    // 空や反射の帯状ノイズが見えにくい出力へ寄せる。
    // FidelityFX 側は A_2PI を使い、位相の安定化に利用する。
    //============================================================
    const float hashVariation =
        frac(cos(random(input.texcoord * 4096.0f) * A_2PI) * 0.5f + 0.5f);

    const float3 ditheredColor = ditherInterleavedGradientNoise(
        sharpened + (hashVariation - 0.5f) / 255.0f,
        input.position.xy,
        0.0f);

    return float4(saturate(ditheredColor), 1.0f);
}
