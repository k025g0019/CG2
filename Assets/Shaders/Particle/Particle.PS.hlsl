struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float particleStyle : TEXCOORD1;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET0
{
    const float2 centerOffset = input.texcoord * 2.0f - 1.0f;
    const uint particleStyle = (uint)round(input.particleStyle);
    float alphaShape = saturate(1.0f - dot(centerOffset, centerOffset));
    float3 particleColor = input.color.rgb;

    // 航跡は横へ長い泡を細かく欠けさせ、連続した白い板に見えないようにする。
    if (particleStyle == 3u)
    {
        const float wakeDistance =
            centerOffset.x * centerOffset.x * 0.45f +
            centerOffset.y * centerOffset.y * 3.5f;
        const float wakeBreakup =
            0.62f + 0.38f * sin(input.texcoord.x * 31.0f + input.texcoord.y * 17.0f);
        alphaShape = saturate(1.0f - wakeDistance) * saturate(wakeBreakup);
        particleColor = lerp(particleColor, float3(0.82f, 0.96f, 1.0f), 0.65f);
    }
    // 爆発スタイルは中心の水柱と外周リングを合わせ、着水の瞬間を一枚で表現する。
    else if (particleStyle == 6u)
    {
        const float radialDistance = length(centerOffset);
        const float splashRing = saturate(1.0f - abs(radialDistance - 0.68f) * 10.0f);
        const float splashColumn =
            saturate(1.0f - abs(centerOffset.x) * 5.0f) *
            saturate(1.0f - (centerOffset.y + 0.15f) * 0.8f);
        alphaShape = max(splashRing, splashColumn) * saturate(1.0f - radialDistance * 0.45f);
        particleColor = lerp(particleColor, float3(0.72f, 0.92f, 1.0f), 0.72f);
    }
    // 弾道は明るい先端と後方へ減衰する尾を同じ加算粒子で描く。
    else if (particleStyle == 7u)
    {
        const float trailWidth = saturate(1.0f - abs(centerOffset.y) * 5.5f);
        const float trailLength = saturate(1.0f - input.texcoord.x * 0.82f);
        const float projectileCore = saturate(1.0f - dot(centerOffset, centerOffset) * 5.0f);
        alphaShape = max(projectileCore, trailWidth * trailLength);
        particleColor *= 1.75f;
    }

    return float4(particleColor, input.color.a * alphaShape);
}
