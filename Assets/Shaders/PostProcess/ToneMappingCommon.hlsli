float Luminance(float3 color)
{
	return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float Hash12(float2 value)
{
	float3 value3 = frac(float3(value.xyx) * 0.1031f);
	value3 += dot(value3, value3.yzx + 33.33f);
	return frac((value3.x + value3.y) * value3.z);
}

float3 LinearToSRGBFast(float3 c)
{
	c = saturate(c);
	return pow(c, 1.0f / 2.2f);
}

float3 LinearToSRGB(float3 linearColor)
{
    return max(1.055f * pow(abs(linearColor), 1.0f / 2.2f) - 0.055f, 0.0f);
}

float3 ReinhardToneMap(float3 color)
{
    return color / (1.0f + color);
}

float3 ReinhardToneMapExt(float3 color, float whitePoint)
{
    float3 whiteSqr = whitePoint * whitePoint;
    return (color * (1.0f + color / whiteSqr)) / (1.0f + color);
}

float3 ACESFilmToneMap(float3 color)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
	return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float ColToneB(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
	return
		-((-pow(midIn, contrast) + (midOut * (pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) -
		pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut)) /
		(pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut)) /
		(pow(midIn, contrast * shoulder) * midOut));
}

float ColToneC(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
	return
		(pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) -
		pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut) /
		(pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut);
}

float ColTone(float value, float4 parameters)
{
	float raisedValue = pow(max(value, 0.0f), parameters.r);
	return raisedValue / max(pow(raisedValue, parameters.g) * parameters.b + parameters.a, 0.000001f);
}

float3 TimothyToneMap(float3 color)
{
	float hdrMax = 16.0f;
	float contrast = 2.0f;
	float shoulder = 1.0f;
	float midIn = 0.18f;
	float midOut = 0.18f;
	float curveB = ColToneB(hdrMax, contrast, shoulder, midIn, midOut);
	float curveC = ColToneC(hdrMax, contrast, shoulder, midIn, midOut);
	float peak = max(color.r, max(color.g, color.b));
	peak = max(peak, 0.000001f);

	float3 ratio = color / peak;
	peak = ColTone(peak, float4(contrast, shoulder, curveB, curveC));

	float crosstalk = 4.0f;
	float saturation = contrast;
	float crossSaturation = contrast * 16.0f;
	ratio = pow(abs(ratio), saturation / crossSaturation);
	ratio = lerp(ratio, float3(1.0f, 1.0f, 1.0f), pow(peak, crosstalk));
	ratio = pow(abs(ratio), crossSaturation);
	return peak * ratio;
}

float3 Uncharted2ToneMapOperator(float3 color)
{
	float a = 0.15f;
	float b = 0.50f;
	float c = 0.10f;
	float d = 0.20f;
	float e = 0.02f;
	float f = 0.30f;
	return ((color * (a * color + c * b) + d * e) / (color * (a * color + b) + d * f)) - e / f;
}

float3 Uncharted2ToneMap(float3 color)
{
	float white = 11.2f;
	return Uncharted2ToneMapOperator(2.0f * color) / Uncharted2ToneMapOperator(white);
}

float3 FilmicToneMap(float3 color)
{
	float3 x = max(0.0f, color - 0.004f);
	return (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
}

float3 ApplySaturation(float3 color, float saturation)
{
	float luminance = Luminance(color);
	return max(lerp(luminance.xxx, color, saturation), 0.0f);
}

float3 ApplyContrast(float3 color, float contrast)
{
	return max((color - 0.18f.xxx) * contrast + 0.18f.xxx, 0.0f);
}

float ApplyVignette(float2 texcoord, float strength, float radius)
{
	float2 centeredUv = texcoord * 2.0f - 1.0f;
	float distanceFromCenter = dot(centeredUv, centeredUv);
	float safeRadius = max(radius, 0.0001f);
	float vignette = 1.0f - smoothstep(safeRadius, 1.0f, distanceFromCenter);
	return lerp(1.0f, vignette, saturate(strength));
}
