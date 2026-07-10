struct SSAOBlurCB
{
    float2 texelSize;
    float2 pad;
};

ConstantBuffer<SSAOBlurCB> gSSAOBlur : register(b0);
Texture2D<float> gSSAOInput : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float main(PixelShaderInput input) : SV_TARGET0
{
    float ao = 0.0f;
    float totalWeight = 0.0f;
    const float SIGMA = 1.5f;
    const int KERNEL_RADIUS = 2;
    [unroll]
    for (int y = -KERNEL_RADIUS; y <= KERNEL_RADIUS; y++)
    {
        [unroll]
        for (int x = -KERNEL_RADIUS; x <= KERNEL_RADIUS; x++)
        {
            float2 offset = float2(x, y) * gSSAOBlur.texelSize;
            float weight = exp(-(x * x + y * y) / (2.0f * SIGMA * SIGMA));
            ao += gSSAOInput.Sample(gSampler, input.texcoord + offset) * weight;
            totalWeight += weight;
        }
    }
    return ao / totalWeight;
}
