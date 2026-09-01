struct PlanarReflectionCB
{
	float horizonY;
	float intensity;
	float fadeDistance;
	float roughness;
	float2 texelSize;
	float ssaoStrength;
	float pad0;
};

ConstantBuffer<PlanarReflectionCB> gPlanarReflection : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSSAOTexture : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

float3 SampleReflection(float2 uv)
{
	float2 mirrorUv = float2(uv.x, gPlanarReflection.horizonY * 2.0f - uv.y);
	float3 color = float3(0.0f, 0.0f, 0.0f);
	float totalWeight = 0.0f;
	float blurRadius = lerp(0.75f, 6.0f, saturate(gPlanarReflection.roughness));

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			float2 offset = float2(x, y) * gPlanarReflection.texelSize * blurRadius;
			float weight = 1.0f - saturate(length(float2(x, y)) * 0.22f);
			color += gSceneColor.Sample(gSampler, mirrorUv + offset).rgb * weight;
			totalWeight += weight;
		}
	}

	return color / max(totalWeight, 0.0001f);
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
	float3 sceneColor = gSceneColor.Sample(gSampler, input.texcoord).rgb;
	float4 materialMask = gMaterialMask.Sample(gSampler, input.texcoord);
	float reflectMaskFromMaterial = saturate(materialMask.x);
	float reflectionMode = materialMask.z;
	float reflectionProbeIntensity = max(materialMask.w, 0.0f);
	float isPlanar = reflectionMode >= 1.5f ? 1.0f : 0.0f;
	float reflectionMask = saturate((input.texcoord.y - gPlanarReflection.horizonY) / max(gPlanarReflection.fadeDistance, 0.0001f));
	reflectionMask *= 1.0f - saturate(abs(input.texcoord.x - 0.5f) * 0.18f);
	reflectionMask *= reflectMaskFromMaterial * isPlanar * saturate(reflectionProbeIntensity);

	float ssao = gSSAOTexture.Sample(gSampler, input.texcoord).r;
	reflectionMask *= lerp(1.0f, ssao, saturate(gPlanarReflection.ssaoStrength));

	float3 reflectionColor = SampleReflection(input.texcoord);
	float fresnel = pow(saturate(input.texcoord.y), 2.0f);
	float reflectionRate = reflectionMask * fresnel * max(gPlanarReflection.intensity, 0.0f);
	float3 finalColor = lerp(sceneColor, reflectionColor, saturate(reflectionRate));
	return float4(finalColor, 1.0f);
}
