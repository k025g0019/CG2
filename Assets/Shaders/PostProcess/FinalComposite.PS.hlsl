#include "ToneMappingCommon.hlsli"

struct FinalCompositeConstants
{
    float exposure;
    float whitePoint;
    float toneMappingMode;
    float bloomIntensity;
    float saturation;
    float contrast;
    float vignetteStrength;
    float vignetteRadius;
    float filmGrain;
    float chromaticAberration;
    float ambientOcclusionStrength;
    float padding;
};

ConstantBuffer<FinalCompositeConstants> gFinalComposite : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gBloomTexture : register(t1);
Texture2D<float> gAmbientOcclusionTexture : register(t2);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

//================================================================
// HDR Scene、Bloom、AO を一度だけ LDR / sRGB へ最終合成
//================================================================

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 centeredUv = input.texcoord - float2(0.5f, 0.5f);
    const float2 aberrationOffset =
        centeredUv * max(gFinalComposite.chromaticAberration, 0.0f) * 0.006f;

    const float redChannel = gSceneColor.Sample(gSampler, input.texcoord + aberrationOffset).r;
    const float greenChannel = gSceneColor.Sample(gSampler, input.texcoord).g;
    const float blueChannel = gSceneColor.Sample(gSampler, input.texcoord - aberrationOffset).b;
    const float3 sceneColor = float3(redChannel, greenChannel, blueChannel);
    const float3 bloomColor = gBloomTexture.Sample(gSampler, input.texcoord).rgb;
    const float ambientOcclusion = gAmbientOcclusionTexture.Sample(gSampler, input.texcoord).r;

    float3 color = sceneColor * max(gFinalComposite.exposure, 0.0001f);
    color *= lerp(
        1.0f,
        ambientOcclusion,
        saturate(gFinalComposite.ambientOcclusionStrength));
    color += bloomColor * max(gFinalComposite.bloomIntensity, 0.0f);

    const float whitePoint = max(gFinalComposite.whitePoint, 0.0001f);

    if (gFinalComposite.toneMappingMode >= 0.5f && gFinalComposite.toneMappingMode < 1.5f)
    {
        color = ReinhardToneMapExt(color, whitePoint);
    }
    else if (gFinalComposite.toneMappingMode >= 1.5f && gFinalComposite.toneMappingMode < 2.5f)
    {
        color = FilmicToneMap(color / whitePoint);
    }
    else if (gFinalComposite.toneMappingMode >= 2.5f && gFinalComposite.toneMappingMode < 3.5f)
    {
        color = TimothyToneMap(color / whitePoint);
    }
    else if (gFinalComposite.toneMappingMode >= 3.5f && gFinalComposite.toneMappingMode < 4.5f)
    {
        color = Uncharted2ToneMap(color / whitePoint);
    }
    else
    {
        color = ACESFilmToneMap(color / whitePoint);
    }

    color = ApplyContrast(color, max(gFinalComposite.contrast, 0.0f));
    color = ApplySaturation(color, max(gFinalComposite.saturation, 0.0f));
    color *= ApplyVignette(
        input.texcoord,
        gFinalComposite.vignetteStrength,
        gFinalComposite.vignetteRadius);

    const float grain = Hash12(input.position.xy) - 0.5f;
    color += grain * max(gFinalComposite.filmGrain, 0.0f) * 0.015f;

    return float4(LinearToSRGB(max(color, 0.0f)), 1.0f);
}
