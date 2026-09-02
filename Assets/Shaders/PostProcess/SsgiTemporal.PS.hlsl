//============================================================
// SSGI Temporal Reprojection
//------------------------------------------------------------
// 半解像度で撮ったSSGIを、Motion Vectorで前フレームの結果へ位置合わせして
// 混ぜる。サンプル数の少なさから来るちらつきを、時間方向で均して消す。
// 動いた物体の残像(ゴースト)を防ぐため、履歴は近傍の色範囲へ押し込める。
//============================================================
Texture2D<float4> gCurrentSsgi : register(t0);
Texture2D<float4> gHistorySsgi : register(t1);
Texture2D<float2> gMotionVector : register(t2);
SamplerState gLinearSampler : register(s0);

struct SsgiTemporalConstants
{
    float2 halfInverseRenderSize;  // 1 / 半解像度RT全体の大きさ
    float2 halfViewportOffset;
    float2 halfViewportSize;
    float2 fullInverseRenderSize;  // 1 / フル解像度RT全体の大きさ
    float2 fullViewportOffset;
    float2 fullViewportSize;
    float historyBlend;  // 履歴の重み。1に近いほど滑らかで追従が遅い
    float historyValid;  // 0 なら履歴を使わない(初回・リサイズ直後)
};

ConstantBuffer<SsgiTemporalConstants> gTemporal : register(b0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float2 HalfLocalToScreenUv(float2 localUv)
{
    return (gTemporal.halfViewportOffset + localUv * gTemporal.halfViewportSize) *
        gTemporal.halfInverseRenderSize;
}

float2 FullLocalToScreenUv(float2 localUv)
{
    return (gTemporal.fullViewportOffset + localUv * gTemporal.fullViewportSize) *
        gTemporal.fullInverseRenderSize;
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 currentScreenUv = HalfLocalToScreenUv(input.texcoord);
    const float3 currentColor = gCurrentSsgi.SampleLevel(gLinearSampler, currentScreenUv, 0.0f).rgb;

    if (gTemporal.historyValid < 0.5f)
    {
        return float4(currentColor, 1.0f);
    }

    // Motion Vector は Viewport ローカルUVの差分なので、そのまま引ける。
    const float2 motionVector = gMotionVector.SampleLevel(
        gLinearSampler,
        FullLocalToScreenUv(input.texcoord),
        0.0f);
    const float2 previousLocalUv = input.texcoord - motionVector;

    // 画面外へ出た場所には履歴が無い。
    if (any(previousLocalUv < 0.0f) || any(previousLocalUv > 1.0f))
    {
        return float4(currentColor, 1.0f);
    }

    // 近傍3x3の色範囲を作り、履歴をそこへ押し込める(ゴースト対策)。
    float3 neighborMinimum = currentColor;
    float3 neighborMaximum = currentColor;
    const float2 halfTexelStep = gTemporal.halfInverseRenderSize;

    [unroll]
    for (int offsetIndex = 0; offsetIndex < 9; offsetIndex++)
    {
        if (offsetIndex == 4)
        {
            continue;
        }

        const float2 neighborOffset = float2(
            (float)(offsetIndex % 3) - 1.0f,
            (float)(offsetIndex / 3) - 1.0f);
        const float3 neighborColor = gCurrentSsgi.SampleLevel(
            gLinearSampler,
            currentScreenUv + neighborOffset * halfTexelStep,
            0.0f).rgb;
        neighborMinimum = min(neighborMinimum, neighborColor);
        neighborMaximum = max(neighborMaximum, neighborColor);
    }

    const float3 historyColor = gHistorySsgi.SampleLevel(
        gLinearSampler,
        HalfLocalToScreenUv(previousLocalUv),
        0.0f).rgb;
    const float3 clampedHistory = clamp(historyColor, neighborMinimum, neighborMaximum);

    return float4(
        lerp(currentColor, clampedHistory, saturate(gTemporal.historyBlend)),
        1.0f);
}
