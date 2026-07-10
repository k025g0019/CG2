struct FXAACB
{
	float2 texelSize;
	float subpixelBlend;
	float edgeThreshold;
};

ConstantBuffer<FXAACB> gFXAA : register(b0);
Texture2D<float4> gSceneColor : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float RgbToLuma(float3 rgb)
{
	return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
	float3 colorCenter = gSceneColor.Sample(gSampler, input.texcoord).rgb;
	float lumaCenter = RgbToLuma(colorCenter);
	float lumaN = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(0.0f, -1.0f) * gFXAA.texelSize).rgb);
	float lumaS = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(0.0f, 1.0f) * gFXAA.texelSize).rgb);
	float lumaW = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(-1.0f, 0.0f) * gFXAA.texelSize).rgb);
	float lumaE = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(1.0f, 0.0f) * gFXAA.texelSize).rgb);
	float lumaMin = min(lumaCenter, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	float lumaMax = max(lumaCenter, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	float lumaRange = lumaMax - lumaMin;

	if (lumaRange < max(max(gFXAA.edgeThreshold, 0.001f), lumaMax * 0.0625f)) {
		return float4(colorCenter, 1.0f);
	}

	float lumaNW = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(-1.0f, -1.0f) * gFXAA.texelSize).rgb);
	float lumaNE = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(1.0f, -1.0f) * gFXAA.texelSize).rgb);
	float lumaSW = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(-1.0f, 1.0f) * gFXAA.texelSize).rgb);
	float lumaSE = RgbToLuma(gSceneColor.Sample(gSampler, input.texcoord + float2(1.0f, 1.0f) * gFXAA.texelSize).rgb);

	float2 edgeDirection;
	edgeDirection.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	edgeDirection.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

	float directionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125f, 0.0078125f);
	float inverseDirectionAdjustment = 1.0f / (min(abs(edgeDirection.x), abs(edgeDirection.y)) + directionReduce);
	edgeDirection = clamp(edgeDirection * inverseDirectionAdjustment, -8.0f, 8.0f) * gFXAA.texelSize;

	float3 rgbA =
		0.5f * (
			gSceneColor.Sample(gSampler, input.texcoord + edgeDirection * (1.0f / 3.0f - 0.5f)).rgb +
			gSceneColor.Sample(gSampler, input.texcoord + edgeDirection * (2.0f / 3.0f - 0.5f)).rgb);
	float3 rgbB =
		rgbA * 0.5f +
		0.25f * (
			gSceneColor.Sample(gSampler, input.texcoord + edgeDirection * -0.5f).rgb +
			gSceneColor.Sample(gSampler, input.texcoord + edgeDirection * 0.5f).rgb);
	float lumaB = RgbToLuma(rgbB);
	float3 antiAliasedColor = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
	float3 subpixelColor =
		(colorCenter + gSceneColor.Sample(gSampler, input.texcoord + float2(0.0f, -1.0f) * gFXAA.texelSize).rgb +
		gSceneColor.Sample(gSampler, input.texcoord + float2(0.0f, 1.0f) * gFXAA.texelSize).rgb +
		gSceneColor.Sample(gSampler, input.texcoord + float2(-1.0f, 0.0f) * gFXAA.texelSize).rgb +
		gSceneColor.Sample(gSampler, input.texcoord + float2(1.0f, 0.0f) * gFXAA.texelSize).rgb) * 0.2f;
	float blendRate = saturate(gFXAA.subpixelBlend);
	return float4(lerp(antiAliasedColor, subpixelColor, blendRate * 0.35f), 1.0f);
}
