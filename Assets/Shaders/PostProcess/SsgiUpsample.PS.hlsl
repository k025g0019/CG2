//============================================================
// SSGI Upsample
//------------------------------------------------------------
// 半解像度で解決したSSGIを、フル解像度のHDRへ加算合成する。
// 間接光は元々低周波なので、バイリニア拡大で十分。
//============================================================
Texture2D<float4> gSsgi : register(t0);
SamplerState gLinearSampler : register(s0);

struct SsgiUpsampleConstants
{
    float2 halfInverseRenderSize;
    float2 halfViewportOffset;
    float2 halfViewportSize;
    float2 upsamplePadding;
};

ConstantBuffer<SsgiUpsampleConstants> gUpsample : register(b0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 screenUv =
        (gUpsample.halfViewportOffset + input.texcoord * gUpsample.halfViewportSize) *
        gUpsample.halfInverseRenderSize;
    return float4(max(gSsgi.SampleLevel(gLinearSampler, screenUv, 0.0f).rgb, 0.0f), 0.0f);
}
