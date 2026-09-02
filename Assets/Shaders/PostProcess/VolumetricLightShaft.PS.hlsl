// Volumetric Light Shaft (Sun Beams / God Ray) ポストエフェクト。
//
// これは Object3d.PS.hlsl 内で表面Pixelにだけ足していたボリュメトリック散乱を、
// 深度バッファを使った画面全体のレイマーチへ置き換えたもの。表面の有無に関係なく
// 「隙間から漏れた光がその場の空気で散乱して見える」筋そのものを描画できる。
// 数式(Shadow判定・Henyey-Greenstein位相・レイマーチ)はObject3d側と同じロジック。
#include "../Shadow/SoftShadow.hlsli"

static const float PI = 3.14159265359f;

// DirectionalLightData / DirectionalLightArray は Common/SceneLightData.hlsli で共有する。
#include "../Common/SceneLightData.hlsli"
// directionalLightResourceをそのまま束縛するのでlights[0]がSun。
ConstantBuffer<DirectionalLightArray> gDirectionalLight : register(b0);

struct VolumetricConstants
{
    row_major float4x4 inverseViewProjection;
    float2 viewportOffset;
    float2 viewportSize;
    float2 inverseRenderSize;
    float2 volumetricPadding;
};

ConstantBuffer<VolumetricConstants> gVolumetric : register(b1);

Texture2D<float> gSceneDepth : register(t0);
Texture2D gShadowMap : register(t1);
SamplerState gDepthSampler : register(s0);
SamplerState gShadowSampler : register(s1);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float2 LocalToScreenUv(float2 localUv)
{
    const float2 pixelPosition = gVolumetric.viewportOffset + localUv * gVolumetric.viewportSize;
    return pixelPosition * gVolumetric.inverseRenderSize;
}

float3 ReconstructWorldPosition(float2 localUv, float deviceDepth)
{
    const float2 ndc = float2(localUv.x * 2.0f - 1.0f, 1.0f - localUv.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, deviceDepth, 1.0f), gVolumetric.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

// 空間中の1点が光源から見えているかだけを1タップで判定する。
// サーフェスではないので法線が無く、PCFも法線依存バイアスも使えない。
float SampleShadowVisibilityAtPoint(float3 samplePosition, DirectionalLightData light)
{
    row_major float4x4 shadowViewProjection;
    float4 atlasTransform;

    if (light.lightType == 0 && light.shadowCascadeCount > 1.5f)
    {
        const float cameraDistance = length(samplePosition - light.cameraPosition);
        uint cascadeIndex = 0u;
        cascadeIndex += cameraDistance > light.shadowCascadeSplits.x ? 1u : 0u;
        cascadeIndex += cameraDistance > light.shadowCascadeSplits.y ? 1u : 0u;
        cascadeIndex += cameraDistance > light.shadowCascadeSplits.z ? 1u : 0u;
        cascadeIndex = min(cascadeIndex, 3u);
        shadowViewProjection = light.shadowCascadeVP[cascadeIndex];
        atlasTransform = light.shadowCascadeAtlas[cascadeIndex];
    }
    else
    {
        shadowViewProjection = light.shadowVP;
        atlasTransform = float4(
            light.shadowTileUvScaleX,
            light.shadowTileUvScaleY,
            light.shadowTileUvBiasX,
            light.shadowTileUvBiasY);
    }

    const float4 shadowPosition = mul(float4(samplePosition, 1.0f), shadowViewProjection);
    if (shadowPosition.w <= 0.000001f) return 0.0f;

    const float3 shadowNdc = shadowPosition.xyz / shadowPosition.w;

    // Frustum外は影データが無い。筋を出さない(=光が届いていない扱い)方が安全。
    if (shadowNdc.x < -1.0f || shadowNdc.x > 1.0f ||
        shadowNdc.y < -1.0f || shadowNdc.y > 1.0f ||
        shadowNdc.z < 0.0f || shadowNdc.z > 1.0f)
    {
        return 0.0f;
    }

    const float bias = 0.0015f;
    const float2 shadowUv = float2(
        shadowNdc.x * 0.5f + 0.5f,
        -shadowNdc.y * 0.5f + 0.5f);
    const float2 atlasUv = shadowUv * atlasTransform.xy + atlasTransform.zw;
    const float occluderDepth = gShadowMap.SampleLevel(gShadowSampler, atlasUv, 0).r;
    return (shadowNdc.z - bias) <= occluderDepth ? 1.0f : 0.0f;
}

// Henyey-Greenstein 位相関数。gが大きいほど光源方向へ鋭く前方散乱し、
// 「細い隙間から差し込む光がビームに見える」挙動になる。
float HenyeyGreensteinPhase(float cosTheta, float g)
{
    const float gg = g * g;
    const float denominator = 1.0f + gg - 2.0f * g * cosTheta;
    return (1.0f - gg) / (4.0f * PI * pow(max(denominator, 0.0001f), 1.5f));
}

// カメラから「そのPixelの奥行き」までの空間をレイマーチし、Shadow Mapで
// 照らされている区間だけ散乱光を積算する。奥行きは深度バッファそのものを
// 使うため、実際に物体がある所も、何も無い空(遠方Clip)もどちらも扱える。
// 隙間から漏れた光だけが、宙に浮いた筋として浮かび上がる。
float3 EvaluateVolumetricLight(
    float3 targetPosition,
    DirectionalLightData light,
    float2 pixelPosition)
{
    if (light.volumetricIntensity <= 0.0001f) return float3(0.0f, 0.0f, 0.0f);
    if (light.shadowEnabled < 0.5f) return float3(0.0f, 0.0f, 0.0f);

    const float3 cameraPosition = light.cameraPosition;
    const float3 toTarget = targetPosition - cameraPosition;
    const float targetDistance = length(toTarget);
    if (targetDistance < 0.05f) return float3(0.0f, 0.0f, 0.0f);

    const float3 rayDirection = toTarget / targetDistance;
    const float marchDistance = min(targetDistance, max(light.volumetricDistance, 1.0f));
    const uint stepCount = 24u;
    const float stepSize = marchDistance / float(stepCount);

    // 等間隔サンプルは同心円状のバンドになるため、画面座標のディザで開始位置をずらす。
    const float dither = frac(
        sin(dot(pixelPosition, float2(12.9898f, 78.233f))) * 43758.5453f);
    const float phaseG = clamp(light.volumetricAnisotropy, 0.0f, 0.95f);
    const float3 lightDirection = normalize(-light.direction);
    const float phase = HenyeyGreensteinPhase(dot(rayDirection, -lightDirection), phaseG);
    const float lightIntensity = max(light.intensity, 0.0f);

    float3 scattered = float3(0.0f, 0.0f, 0.0f);

    for (uint stepIndex = 0u; stepIndex < stepCount; stepIndex++)
    {
        const float sampleDistance = (float(stepIndex) + dither) * stepSize;
        const float3 samplePosition = cameraPosition + rayDirection * sampleDistance;

        if (SampleShadowVisibilityAtPoint(samplePosition, light) <= 0.0f)
        {
            continue;
        }

        scattered += max(light.color.rgb, 0.0f) * lightIntensity * phase;
    }

    // 空気の散乱係数(1mあたり)に相当する内部スケール。
    // これが無いと位相関数の正規化の都合で値が大きくなり過ぎて露出が飛ぶ。
    // UIの「強さ」は、この現実的な密度に対する倍率として効かせる。
    const float kScatteringCoefficientPerMeter = 0.015f;
    return scattered * stepSize * kScatteringCoefficientPerMeter * light.volumetricIntensity;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const DirectionalLightData sunLight = gDirectionalLight.lights[0];

    if (sunLight.lightType != 0 ||
        sunLight.volumetricIntensity <= 0.0001f ||
        sunLight.shadowEnabled < 0.5f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const float2 screenUv = LocalToScreenUv(input.texcoord);
    const float deviceDepth = gSceneDepth.SampleLevel(gDepthSampler, screenUv, 0.0f);
    const float3 targetPosition = ReconstructWorldPosition(input.texcoord, deviceDepth);

    if (!all(isfinite(targetPosition)))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const float3 scattered = EvaluateVolumetricLight(targetPosition, sunLight, input.position.xy);
    return float4(max(scattered, 0.0f), 0.0f);
}
