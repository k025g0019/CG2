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
    float3 bloomTint;
    float padding1;
    float autoExposureEnabled;
    float temperature;
    float tint;
    float gamma;
    float3 lift;
    float padding2;
    float3 gain;
    float padding3;
    float colorLutStrength;
    float gamutCompression;
    float localContrast;
    float outputDither;
    float heatIntensity;
    float heatHorizonCenter;
    float heatHorizonWidth;
    float heatSunInfluence;
    float heatDistortionScale;
    float heatTime;
    float2 heatSunUv;
};

ConstantBuffer<FinalCompositeConstants> gFinalComposite : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gBloomTexture : register(t1);
Texture2D<float> gAmbientOcclusionTexture : register(t2);
Texture2D<float4> gAutoExposureTexture : register(t5);
Texture2D<float4> gColorGradingLut : register(t6);
Texture2D<float> gSceneDepth : register(t7);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

//================================================================
// HDR Scene、Bloom、AO を一度だけ LDR / sRGB へ最終合成
//================================================================

float3 ApplyBradfordWhiteBalance(float3 color, float temperature, float tint)
{
    const float3 xyz = float3(
        dot(color, float3(0.4124564f, 0.3575761f, 0.1804375f)),
        dot(color, float3(0.2126729f, 0.7151522f, 0.0721750f)),
        dot(color, float3(0.0193339f, 0.1191920f, 0.9503041f)));
    float3 lms = float3(
        dot(xyz, float3(0.8951000f, 0.2664000f, -0.1614000f)),
        dot(xyz, float3(-0.7502000f, 1.7135000f, 0.0367000f)),
        dot(xyz, float3(0.0389000f, -0.0685000f, 1.0296000f)));

    const float3 sourceWhiteLms = float3(0.9414300f, 1.0404200f, 1.0895300f);
    const float3 targetWhiteLms = sourceWhiteLms * max(
        float3(
            1.0f + temperature * 0.075f + tint * 0.018f,
            1.0f - tint * 0.050f,
            1.0f - temperature * 0.090f + tint * 0.012f),
        0.05f);
    lms *= targetWhiteLms / sourceWhiteLms;

    const float3 balancedXyz = float3(
        dot(lms, float3(0.9869929f, -0.1470543f, 0.1599627f)),
        dot(lms, float3(0.4323053f, 0.5183603f, 0.0492912f)),
        dot(lms, float3(-0.0085287f, 0.0400428f, 0.9684867f)));
    return float3(
        dot(balancedXyz, float3(3.2404542f, -1.5371385f, -0.4985314f)),
        dot(balancedXyz, float3(-0.9692660f, 1.8760108f, 0.0415560f)),
        dot(balancedXyz, float3(0.0556434f, -0.2040259f, 1.0572252f)));
}

float3 CompressDisplayGamut(float3 color, float strength)
{
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    const float maximumChannel = max(color.r, max(color.g, color.b));
    const float minimumChannel = min(color.r, min(color.g, color.b));
    const float excursion = max(maximumChannel - 1.0f, -minimumChannel);
    const float compression = rcp(1.0f + max(excursion, 0.0f) * max(strength, 0.0f));
    return lerp(luminance.xxx, color, compression);
}

float3 SampleColorGradingLut(float3 color)
{
    const float lutSize = 32.0f;
    const float lutLastIndex = lutSize - 1.0f;
    const float3 lutCoordinate = saturate(color) * lutLastIndex;
    const float lowerBlueSlice = floor(lutCoordinate.b);
    const float upperBlueSlice = min(lowerBlueSlice + 1.0f, lutLastIndex);
    const float2 lowerUv = float2(
        (lowerBlueSlice * lutSize + lutCoordinate.r + 0.5f) / (lutSize * lutSize),
        (lutCoordinate.g + 0.5f) / lutSize);
    const float2 upperUv = float2(
        (upperBlueSlice * lutSize + lutCoordinate.r + 0.5f) / (lutSize * lutSize),
        lowerUv.y);
    const float3 lowerColor = gColorGradingLut.SampleLevel(gSampler, lowerUv, 0.0f).rgb;
    const float3 upperColor = gColorGradingLut.SampleLevel(gSampler, upperUv, 0.0f).rgb;
    return lerp(lowerColor, upperColor, frac(lutCoordinate.b));
}

//================================================================
// 局所コントラスト
//================================================================

float3 ApplyLocalContrast(float3 centerColor, float2 texcoord, float strength)
{
    if (strength <= 0.0001f)
    {
        return centerColor;
    }

    uint sceneWidth = 1u;
    uint sceneHeight = 1u;
    gSceneColor.GetDimensions(sceneWidth, sceneHeight);
    const float2 texelSize = rcp(max(float2(sceneWidth, sceneHeight), 1.0f));
    const float2 horizontalOffset = float2(texelSize.x, 0.0f);
    const float2 verticalOffset = float2(0.0f, texelSize.y);
    const float surroundingLuminance = 0.25f * (
        Luminance(gSceneColor.SampleLevel(gSampler, saturate(texcoord + horizontalOffset), 0.0f).rgb) +
        Luminance(gSceneColor.SampleLevel(gSampler, saturate(texcoord - horizontalOffset), 0.0f).rgb) +
        Luminance(gSceneColor.SampleLevel(gSampler, saturate(texcoord + verticalOffset), 0.0f).rgb) +
        Luminance(gSceneColor.SampleLevel(gSampler, saturate(texcoord - verticalOffset), 0.0f).rgb));
    const float centerLuminance = Luminance(centerColor);
    const float relativeDetail = clamp(
        (centerLuminance - surroundingLuminance) /
            max(surroundingLuminance, 0.04f),
        -0.35f,
        0.35f);
    const float midtoneWeight =
        smoothstep(0.015f, 0.12f, centerLuminance) *
        (1.0f - smoothstep(2.0f, 8.0f, centerLuminance));
    const float detailScale = max(
        1.0f + relativeDetail * saturate(strength) * midtoneWeight,
        0.0f);
    return centerColor * detailScale;
}

float ApplyOutputDither(float2 pixelPosition)
{
    // 2つの一様乱数の差で三角分布を作り、低周波の階調縞だけを崩す。
    return Hash12(pixelPosition) - Hash12(pixelPosition + float2(37.0f, 17.0f));
}

float2 ResolveAtmosphericHeatUv(float2 texcoord)
{
    const float heatIntensity = saturate(gFinalComposite.heatIntensity);

    if (heatIntensity <= 0.0001f)
    {
        return texcoord;
    }

    // Scene Depthの遠景だけを歪ませ、近距離の海面・船・照準UIは保護する。
    const float sceneDepth = gSceneDepth.SampleLevel(gSampler, texcoord, 0.0f);
    const float farDistanceMask = smoothstep(0.985f, 0.9998f, sceneDepth);
    const float horizonDistance = abs(texcoord.y - gFinalComposite.heatHorizonCenter);
    const float horizonMask = 1.0f - smoothstep(
        max(gFinalComposite.heatHorizonWidth * 0.28f, 0.005f),
        max(gFinalComposite.heatHorizonWidth, 0.01f),
        horizonDistance);
    const float2 sunDelta = texcoord - gFinalComposite.heatSunUv;
    const float sunMask = exp(-dot(sunDelta, sunDelta) * 10.0f);
    const float heatMask = heatIntensity * farDistanceMask * horizonMask *
        lerp(1.0f, 1.0f + sunMask, saturate(gFinalComposite.heatSunInfluence));

    if (heatMask <= 0.0001f)
    {
        return texcoord;
    }

    // 低周波2層だけで連続した屈折を作る。RGB分離やぼかしは行わない。
    const float heatScale = max(gFinalComposite.heatDistortionScale, 0.01f);
    const float2 heatCoordinate = texcoord * float2(12.0f, 7.0f) / heatScale;
    const float slowWave = sin(heatCoordinate.x + gFinalComposite.heatTime * 0.73f) *
        cos(heatCoordinate.y * 0.71f - gFinalComposite.heatTime * 0.46f);
    const float crossWave = sin(
        heatCoordinate.x * 0.43f - heatCoordinate.y * 1.17f +
        gFinalComposite.heatTime * 0.31f);
    const float2 distortionDirection = float2(
        slowWave * 0.62f + crossWave * 0.38f,
        crossWave * 0.72f - slowWave * 0.28f);
    return saturate(texcoord + distortionDirection * heatMask * 0.0018f);
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 sceneUv = ResolveAtmosphericHeatUv(input.texcoord);
    const float2 centeredUv = sceneUv - float2(0.5f, 0.5f);
    const float2 aberrationOffset =
        centeredUv * max(gFinalComposite.chromaticAberration, 0.0f) * 0.006f;

    const float redChannel = gSceneColor.Sample(gSampler, sceneUv + aberrationOffset).r;
    const float greenChannel = gSceneColor.Sample(gSampler, sceneUv).g;
    const float blueChannel = gSceneColor.Sample(gSampler, sceneUv - aberrationOffset).b;
    const float3 sceneColor = float3(redChannel, greenChannel, blueChannel);
    const float3 bloomColor = gBloomTexture.Sample(gSampler, input.texcoord).rgb;
    const float ambientOcclusion = gAmbientOcclusionTexture.Sample(gSampler, input.texcoord).r;

    float automaticExposure = 1.0f;

    if (gFinalComposite.autoExposureEnabled >= 0.5f)
    {
        automaticExposure = max(
            gAutoExposureTexture.SampleLevel(gSampler, float2(0.5f, 0.5f), 0.0f).r,
            0.0001f);
    }

    float3 color = ApplyLocalContrast(
        sceneColor,
        input.texcoord,
        gFinalComposite.localContrast);
    color *= max(gFinalComposite.exposure, 0.0001f) * automaticExposure;
    color = ApplyBradfordWhiteBalance(
        color,
        gFinalComposite.temperature,
        gFinalComposite.tint);
    color *= lerp(
        1.0f,
        ambientOcclusion,
        saturate(gFinalComposite.ambientOcclusionStrength));
    color += bloomColor * max(gFinalComposite.bloomIntensity, 0.0f) * max(gFinalComposite.bloomTint, 0.0f);

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

    color = max(color + gFinalComposite.lift, 0.0f);
    color = pow(color, rcp(max(gFinalComposite.gamma, 0.01f)));
    color *= max(gFinalComposite.gain, 0.0f);
    color = ApplyContrast(color, max(gFinalComposite.contrast, 0.0f));
    color = ApplySaturation(color, max(gFinalComposite.saturation, 0.0f));
    color = CompressDisplayGamut(color, gFinalComposite.gamutCompression);
    color = lerp(
        color,
        SampleColorGradingLut(color),
        saturate(gFinalComposite.colorLutStrength));
    color *= ApplyVignette(
        input.texcoord,
        gFinalComposite.vignetteStrength,
        gFinalComposite.vignetteRadius);

    const float grain = Hash12(input.position.xy) - 0.5f;
    color += grain * max(gFinalComposite.filmGrain, 0.0f) * 0.015f;

    float3 outputColor = LinearToSRGB(max(color, 0.0f));
    outputColor +=
        ApplyOutputDither(input.position.xy) *
        max(gFinalComposite.outputDither, 0.0f) /
        255.0f;
    return float4(saturate(outputColor), 1.0f);
}
