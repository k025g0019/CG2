Texture2D<float> gSceneDepth : register(t0);
Texture2D<float4> gSceneAlbedo : register(t1);
Texture2D<float4> gSceneMaterial : register(t2);
Texture2D<float4> gSceneEmission : register(t3);
SamplerState gPointSampler : register(s0);

struct SsgiConstants
{
    float2 inverseRenderSize;
    float intensity;
    float radiusPixels;
    row_major float4x4 inverseViewProjection;
    float2 viewportOffset;
    float2 viewportSize;
};

ConstantBuffer<SsgiConstants> gSsgi : register(b0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float2 LocalToScreenUv(float2 localUv)
{
    const float2 pixelPosition = gSsgi.viewportOffset + localUv * gSsgi.viewportSize;
    return pixelPosition * gSsgi.inverseRenderSize;
}

float3 ReconstructWorldPosition(float2 localUv, float deviceDepth)
{
    const float2 ndc = float2(localUv.x * 2.0f - 1.0f, 1.0f - localUv.y * 2.0f);
    const float4 worldPosition = mul(float4(ndc, deviceDepth, 1.0f), gSsgi.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 screenUv = LocalToScreenUv(input.texcoord);
    const float centerDepth = gSceneDepth.SampleLevel(gPointSampler, screenUv, 0.0f);

    if (centerDepth >= 0.99999f)
    {
        return 0.0f;
    }

    const float3 centerPosition = ReconstructWorldPosition(input.texcoord, centerDepth);
    const float3 positionDx = ddx(centerPosition);
    const float3 positionDy = ddy(centerPosition);
    float3 centerNormal = normalize(cross(positionDy, positionDx));

    if (!all(isfinite(centerNormal)))
    {
        return 0.0f;
    }

    float3 indirectLight = 0.0f;
    float accumulatedWeight = 0.0f;
    const float2 localTexelSize = rcp(max(gSsgi.viewportSize, 1.0f));
    const float radiusPixels = max(gSsgi.radiusPixels, 1.0f);

    [unroll]
    for (uint directionIndex = 0u; directionIndex < 8u; directionIndex++)
    {
        const float angle = ((float)directionIndex + 0.5f) * 0.78539816339f;
        const float2 direction = float2(cos(angle), sin(angle));

        [unroll]
        for (uint stepIndex = 1u; stepIndex <= 2u; stepIndex++)
        {
            const float stepRate = (float)stepIndex * 0.5f;
            const float2 sampleLocalUv = input.texcoord + direction * radiusPixels * stepRate * localTexelSize;

            if (any(sampleLocalUv <= 0.0f) || any(sampleLocalUv >= 1.0f))
            {
                continue;
            }

            const float2 sampleScreenUv = LocalToScreenUv(sampleLocalUv);
            const float sampleDepth = gSceneDepth.SampleLevel(gPointSampler, sampleScreenUv, 0.0f);

            if (sampleDepth >= 0.99999f)
            {
                continue;
            }

            const float3 samplePosition = ReconstructWorldPosition(sampleLocalUv, sampleDepth);
            const float3 receiverToSample = samplePosition - centerPosition;
            const float sampleDistance = length(receiverToSample);

            if (sampleDistance <= 0.0001f)
            {
                continue;
            }

            const float3 sampleDirection = receiverToSample / sampleDistance;
            const float receiverWeight = saturate(dot(centerNormal, sampleDirection));
            const float distanceWeight = rcp(1.0f + sampleDistance * sampleDistance * 0.08f);
            const float materialOcclusion = gSceneMaterial.SampleLevel(gPointSampler, sampleScreenUv, 0.0f).b;
            const float3 sampleAlbedo = gSceneAlbedo.SampleLevel(gPointSampler, sampleScreenUv, 0.0f).rgb;
            const float3 sampleEmission = gSceneEmission.SampleLevel(gPointSampler, sampleScreenUv, 0.0f).rgb;
            const float sampleWeight = receiverWeight * distanceWeight * lerp(0.35f, 1.0f, materialOcclusion);
            indirectLight += (sampleAlbedo * 0.12f + sampleEmission) * sampleWeight;
            accumulatedWeight += sampleWeight;
        }
    }

    indirectLight /= max(accumulatedWeight, 1.0f);
    return float4(max(indirectLight, 0.0f) * max(gSsgi.intensity, 0.0f), 0.0f);
}
