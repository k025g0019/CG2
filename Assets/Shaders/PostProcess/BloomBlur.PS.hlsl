struct BloomBlurCB
{
    float2 texelSize;
    float direction;
    float pad;
};

ConstantBuffer<BloomBlurCB> gBloomBlur : register(b0);
Texture2D<float4> gSource : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float WEIGHTS[13] =
{
	0.002216f,
	0.008764f,
	0.026995f,
	0.064759f,
	0.120985f,
	0.176033f,
	0.199471f,
	0.176033f,
	0.120985f,
	0.064759f,
	0.026995f,
	0.008764f,
	0.002216f
};

static const float OFFSETS[13] =
{
	-6.0f,
	-5.0f,
	-4.0f,
	-3.0f,
	-2.0f,
	-1.0f,
	0.0f,
	1.0f,
	2.0f,
	3.0f,
	4.0f,
	5.0f,
	6.0f
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float4 result = 0.0f;
	[unroll]
	for (int i = 0; i < 13; i++)
	{
		float2 offset = gBloomBlur.direction == 0
			? float2(OFFSETS[i] * gBloomBlur.texelSize.x, 0.0f)
			: float2(0.0f, OFFSETS[i] * gBloomBlur.texelSize.y);
		result += gSource.Sample(gSampler, input.texcoord + offset) * WEIGHTS[i];
    }
    return result;
}
