#ifndef ADVANCED_PBR_HLSLI
#define ADVANCED_PBR_HLSLI

static const float kAdvancedPbrPi = 3.14159265359f;
static const float kAdvancedPbrEpsilon = 0.000001f;

// FidelityFX の glTF PBR 方針と RTXGI の距離 windowing を、このエンジンの単一 Object3d PSO 向けにまとめた共通計算。
// 外部サンプルの descriptor / scene 定義は巨大なので、既存 RootSignature に合わせて計算式だけを取り込む。
struct AdvancedPbrMaterial
{
    float3 baseColor;
    float alpha;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
};

struct AdvancedPbrLighting
{
    float3 normal;
    float3 viewDirection;
    float3 lightDirection;
    float3 lightColor;
    float lightIntensity;
    float shadow;
};

float3 AdvancedPbrSafeNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);

    if (lengthSquared <= kAdvancedPbrEpsilon) {
        return fallback;
    }

    return value * rsqrt(lengthSquared);
}

float AdvancedPbrClampRoughness(float roughness)
{
    return clamp(roughness, 0.035f, 1.0f);
}

float AdvancedPbrIorToF0(float ior)
{
    float safeIor = max(ior, 1.0001f);
    float f0 = (safeIor - 1.0f) / (safeIor + 1.0f);
    return saturate(f0 * f0);
}

float3 AdvancedPbrBuildF0(float3 baseColor, float metallic, float reflectance, float ior)
{
    float dielectricF0 = max(0.04f, AdvancedPbrIorToF0(ior));
    float3 nonMetalF0 = float3(dielectricF0, dielectricF0, dielectricF0);
    float3 baseF0 = lerp(nonMetalF0, baseColor, saturate(metallic));

    //============================================================
    // 反射スライダーは、正面から見たときの F0 にも効かせる。
    // これを入れないと、粗さ 0 / 反射 1 でも正面が黒く残りやすい。
    //============================================================
    float reflectanceRate = saturate(reflectance);
    return lerp(baseF0, float3(1.0f, 1.0f, 1.0f), reflectanceRate);
}

float AdvancedPbrDistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalf = saturate(dot(normal, halfVector));
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(kAdvancedPbrPi * denominator * denominator, kAdvancedPbrEpsilon);
}

float AdvancedPbrGeometrySchlickGGX(float normalDotView, float roughness)
{
    float k = roughness + 1.0f;
    k = (k * k) * 0.125f;
    return normalDotView / max(normalDotView * (1.0f - k) + k, kAdvancedPbrEpsilon);
}

float AdvancedPbrGeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    float normalDotView = saturate(dot(normal, viewDirection));
    float normalDotLight = saturate(dot(normal, lightDirection));
    return
        AdvancedPbrGeometrySchlickGGX(normalDotView, roughness) *
        AdvancedPbrGeometrySchlickGGX(normalDotLight, roughness);
}

float3 AdvancedPbrFresnelSchlick(float cosineTheta, float3 baseReflectance)
{
    float fresnelPower = pow(saturate(1.0f - cosineTheta), 5.0f);
    return baseReflectance + (1.0f - baseReflectance) * fresnelPower;
}

float3 AdvancedPbrFresnelSchlickRoughness(float cosineTheta, float3 baseReflectance, float roughness)
{
	float fresnelPower = pow(saturate(1.0f - cosineTheta), 5.0f);
	float3 roughF0 = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), baseReflectance);
	return baseReflectance + (roughF0 - baseReflectance) * fresnelPower;
}

float AdvancedPbrDiffuseBurley(float normalDotView, float normalDotLight, float lightDotHalf, float roughness)
{
	float energyBias = lerp(0.0f, 0.5f, roughness);
	float energyFactor = lerp(1.0f, 1.0f / 1.51f, roughness);
	float fd90 = energyBias + 2.0f * lightDotHalf * lightDotHalf * roughness;
	float lightScatter = 1.0f + (fd90 - 1.0f) * pow(saturate(1.0f - normalDotLight), 5.0f);
	float viewScatter = 1.0f + (fd90 - 1.0f) * pow(saturate(1.0f - normalDotView), 5.0f);
	return lightScatter * viewScatter * energyFactor;
}

float AdvancedPbrDistanceWindow(float distanceToLight, float lightRange)
{
	float safeRange = max(lightRange, 0.01f);
	float distanceRate = saturate(distanceToLight / safeRange);
	float window = 1.0f - pow(distanceRate, 4.0f);
    return window * window;
}

float AdvancedPbrInverseSquareFalloff(float distanceToLight, float lightRange)
{
    float safeDistance = max(distanceToLight, 0.35f);
    float falloff = 1.0f / (safeDistance * safeDistance);
    return falloff * AdvancedPbrDistanceWindow(distanceToLight, lightRange);
}

float3 AdvancedPbrEvaluateDirect(AdvancedPbrMaterial material, AdvancedPbrLighting lighting)
{
    float roughness = AdvancedPbrClampRoughness(material.roughness);
    float metallic = saturate(material.metallic);
    float3 normal = AdvancedPbrSafeNormalize(lighting.normal, float3(0.0f, 1.0f, 0.0f));
    float3 viewDirection = AdvancedPbrSafeNormalize(lighting.viewDirection, float3(0.0f, 0.0f, 1.0f));
    float3 lightDirection = AdvancedPbrSafeNormalize(lighting.lightDirection, float3(0.0f, 1.0f, 0.0f));
    float3 halfVector = AdvancedPbrSafeNormalize(viewDirection + lightDirection, lightDirection);

    float normalDotLight = saturate(dot(normal, lightDirection));
    float normalDotView = saturate(dot(normal, viewDirection));
    float halfDotView = saturate(dot(halfVector, viewDirection));
	float3 f0 = AdvancedPbrBuildF0(material.baseColor, metallic, material.reflectance, material.ior);
	float3 fresnel = AdvancedPbrFresnelSchlick(halfDotView, f0);

	float distribution = AdvancedPbrDistributionGGX(normal, halfVector, roughness);
	float geometry = AdvancedPbrGeometrySmith(normal, viewDirection, lightDirection, roughness);
	float3 specular = distribution * geometry * fresnel / max(4.0f * normalDotView * normalDotLight, 0.0001f);
	float burleyDiffuse = AdvancedPbrDiffuseBurley(normalDotView, normalDotLight, halfDotView, roughness);
	float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * material.baseColor * burleyDiffuse / kAdvancedPbrPi;
    return
        (diffuse + specular) *
        max(lighting.lightColor, 0.0f) *
        max(lighting.lightIntensity, 0.0f) *
        normalDotLight *
        saturate(lighting.shadow);
}

float3 AdvancedPbrEvaluateEnvironment(
    AdvancedPbrMaterial material,
    float3 normal,
    float3 viewDirection,
    float3 skyColor,
    float3 reflectionColor,
    float ambientIntensity,
    float reflectionIntensity)
{
    float roughness = AdvancedPbrClampRoughness(material.roughness);
    float metallic = saturate(material.metallic);
    float3 safeNormal = AdvancedPbrSafeNormalize(normal, float3(0.0f, 1.0f, 0.0f));
    float3 safeViewDirection = AdvancedPbrSafeNormalize(viewDirection, float3(0.0f, 0.0f, 1.0f));
    float normalDotView = saturate(dot(safeNormal, safeViewDirection));
    float3 f0 = AdvancedPbrBuildF0(material.baseColor, metallic, material.reflectance, material.ior);
    float3 fresnel = AdvancedPbrFresnelSchlickRoughness(normalDotView, f0, roughness);
    float3 diffuseFactor = (1.0f - fresnel) * (1.0f - metallic);
    float3 diffuse = material.baseColor * skyColor * max(ambientIntensity, 0.0f) * diffuseFactor;

    float glossyEnergy = lerp(1.25f, 0.08f, roughness);
    float reflectionWeight = lerp(0.35f, 1.0f, saturate(material.reflectance));
    float3 specular = reflectionColor * fresnel * max(reflectionIntensity, 0.0f) * glossyEnergy * reflectionWeight;

    return diffuse + specular;
}

#endif
