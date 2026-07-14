struct PlanarReflectionCB
{
    float2 inverseRenderSize;
    float maxBlurRadius;
    float intensityScale;
    row_major float4x4 inverseViewProjection;
    row_major float4x4 reflectionViewProjection;
    float4 viewport;
};

ConstantBuffer<PlanarReflectionCB> gPlanarReflection : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
Texture2D<float4> gPlanarReflectionColor : register(t3);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 SamplePlanarReflection(float2 uv, float roughness)
{
    const float blurRadius = roughness * roughness * max(gPlanarReflection.maxBlurRadius, 0.0f);

    if (blurRadius <= 0.25f)
    {
        return gPlanarReflectionColor.SampleLevel(gSampler, uv, 0.0f).rgb;
    }

    const float2 texelOffset = gPlanarReflection.inverseRenderSize * blurRadius;
    float3 reflectionColor =
        gPlanarReflectionColor.SampleLevel(gSampler, uv, 0.0f).rgb * 0.24f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv + float2(texelOffset.x, 0.0f), 0.0f).rgb * 0.12f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv - float2(texelOffset.x, 0.0f), 0.0f).rgb * 0.12f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv + float2(0.0f, texelOffset.y), 0.0f).rgb * 0.12f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv - float2(0.0f, texelOffset.y), 0.0f).rgb * 0.12f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv + texelOffset, 0.0f).rgb * 0.07f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv - texelOffset, 0.0f).rgb * 0.07f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv + float2(texelOffset.x, -texelOffset.y), 0.0f).rgb * 0.07f;
    reflectionColor += gPlanarReflectionColor.SampleLevel(gSampler, uv + float2(-texelOffset.x, texelOffset.y), 0.0f).rgb * 0.07f;

    return reflectionColor;
}

bool IsInsideViewport(float2 pixelPosition, float4 viewport)
{
    return pixelPosition.x >= viewport.x &&
        pixelPosition.y >= viewport.y &&
        pixelPosition.x < viewport.x + viewport.z &&
        pixelPosition.y < viewport.y + viewport.w &&
        viewport.z > 1.0f &&
        viewport.w > 1.0f;
}

float3 ReconstructWorldPosition(float2 pixelPosition, float depth)
{
    const float2 viewUv = (pixelPosition - gPlanarReflection.viewport.xy) /
        max(gPlanarReflection.viewport.zw, float2(1.0f, 1.0f));
    const float4 clipPosition = float4(
        viewUv.x * 2.0f - 1.0f,
        1.0f - viewUv.y * 2.0f,
        depth,
        1.0f);
    const float4 worldPosition = mul(clipPosition, gPlanarReflection.inverseViewProjection);

    return worldPosition.xyz / max(abs(worldPosition.w), 0.0001f);
}

bool TryMakeReflectionUv(float3 worldPosition, out float2 reflectionUv)
{
    const float4 reflectionClip = mul(float4(worldPosition, 1.0f), gPlanarReflection.reflectionViewProjection);

    if (reflectionClip.w <= 0.0001f)
    {
        reflectionUv = 0.0f;
        return false;
    }

    const float2 reflectionNdc = reflectionClip.xy / reflectionClip.w;
    const float2 viewUv = float2(
        reflectionNdc.x * 0.5f + 0.5f,
        0.5f - reflectionNdc.y * 0.5f);

    if (any(viewUv < 0.0f) || any(viewUv > 1.0f))
    {
        reflectionUv = 0.0f;
        return false;
    }

    const float2 reflectionPixel = gPlanarReflection.viewport.xy + viewUv * gPlanarReflection.viewport.zw;
    reflectionUv = reflectionPixel * gPlanarReflection.inverseRenderSize;

    return true;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const int2 pixelPosition = int2(input.position.xy);
    const float4 sceneColor = gSceneColor.Load(int3(pixelPosition, 0));
    const float4 materialMask = gMaterialMask.Load(int3(pixelPosition, 0));

    // materialMask.z == 2.0f の画素だけを、平面反射の合成対象にする。
    // 通常モデルの輪郭へ反射が漏れないよう、反射マスクを先に見る。
    if (materialMask.z < 1.5f)
    {
        return sceneColor;
    }

    if (!IsInsideViewport(input.position.xy, gPlanarReflection.viewport))
    {
        return sceneColor;
    }

    const float depth = gSceneDepth.Load(int3(pixelPosition, 0));

    if (depth >= 1.0f)
    {
        return sceneColor;
    }

    const float3 worldPosition = ReconstructWorldPosition(input.position.xy, depth);
    float2 reflectionUv = 0.0f;

    if (!TryMakeReflectionUv(worldPosition, reflectionUv))
    {
        return sceneColor;
    }

    const float roughness = saturate(1.0f - materialMask.y);
    const float3 reflectionColor = SamplePlanarReflection(reflectionUv, roughness);
    const float reflectionIntensity = max(materialMask.w * gPlanarReflection.intensityScale, 0.0f);

    // 鏡面は元の床色を混ぜず、反射RTの色をそのまま置く。
    // lerp すると半透明板に見えるため、Planar は反射色で置き換える。
    return float4(reflectionColor * reflectionIntensity, 1.0f);
}
