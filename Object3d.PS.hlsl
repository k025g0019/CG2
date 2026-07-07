struct Material {
    float4 color;
    int enableLighting;
    int useTexture;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
    float padding0;
    float padding1;
    float padding2;
    float4x4 uvTransform;
};

struct DirectionalLight {
    float4 color;
    float3 direction;
    float intensity;
    float3 skyUpperColor;
    float skyIntensity;
    float3 skyLowerColor;
    float skyEmission;
    float ambientIntensity;
    float horizonSharpness;
    float reflectionIntensity;
    float padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
Texture2D<float4> gTexture : register(t0);
Texture2D<float> gShadowTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerState gShadowSampler : register(s1);

struct PixelShaderInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input) {
    PixelShaderOutput output;
    float4 transformedTexcoord = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedTexcoord.xy);
    float4 baseColor = gMaterial.useTexture != 0 ? textureColor : float4(1.0f, 1.0f, 1.0f, 1.0f);
    output.color = gMaterial.color * baseColor;

    if (gMaterial.enableLighting != 0) {
        float3 normalizedNormal = normalize(input.normal);
        float3 normalizedLightDirection = normalize(gDirectionalLight.direction);
        float3 viewDirection = normalize(float3(0.0f, 2.5f, -8.0f) - input.worldPosition);
        float3 halfVector = normalize(viewDirection - normalizedLightDirection);
        float3 reflectionVector = reflect(-viewDirection, normalizedNormal);

        float ndotl = saturate(dot(normalizedNormal, -normalizedLightDirection));
        float ndotv = saturate(dot(normalizedNormal, viewDirection));
        float ndoth = saturate(dot(normalizedNormal, halfVector));
        float vdotH = saturate(dot(viewDirection, halfVector));

        float roughness = saturate(gMaterial.roughness);
        float metallic = saturate(gMaterial.metallic);
        float reflectance = saturate(gMaterial.reflectance);
        float ior = max(gMaterial.ior, 1.0f);
        float emissionStrength = max(gMaterial.emissionStrength, 0.0f);
        float skyIntensity = max(gDirectionalLight.skyIntensity, 0.0f);
        float skyEmission = max(gDirectionalLight.skyEmission, 0.0f);
        float ambientIntensity = max(gDirectionalLight.ambientIntensity, 0.0f);
        float reflectionIntensity = max(gDirectionalLight.reflectionIntensity, 0.0f);
        float horizonSharpness = max(gDirectionalLight.horizonSharpness, 0.01f);

        float dielectricF0Scalar = pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
        dielectricF0Scalar = max(dielectricF0Scalar, 0.04f + reflectance * 0.25f);
        float3 dielectricF0 = float3(dielectricF0Scalar, dielectricF0Scalar, dielectricF0Scalar);
        float3 metalF0 = max(output.color.rgb, float3(0.02f, 0.02f, 0.02f));
        float3 f0 = lerp(dielectricF0, metalF0, metallic);

        float3 fresnel = f0 + (1.0f - f0) * pow(1.0f - vdotH, 5.0f);
        float alpha = max(roughness * roughness, 0.045f);
        float alphaSquared = alpha * alpha;
        float ndothSquared = ndoth * ndoth;
        float ndfDenominator = ndothSquared * (alphaSquared - 1.0f) + 1.0f;
        float normalDistribution = alphaSquared / max(3.14159265f * ndfDenominator * ndfDenominator, 0.0001f);
        float k = (roughness + 1.0f) * (roughness + 1.0f) * 0.125f;
        float geometryV = ndotv / max(ndotv * (1.0f - k) + k, 0.0001f);
        float geometryL = ndotl / max(ndotl * (1.0f - k) + k, 0.0001f);
        float geometry = geometryV * geometryL;
        float3 specularBrdf = normalDistribution * geometry * fresnel / max(4.0f * ndotv * ndotl, 0.0001f);

        float3 diffuseColor = output.color.rgb * (1.0f - metallic);
        float3 diffuse = diffuseColor * gDirectionalLight.color.rgb * ndotl * gDirectionalLight.intensity;
        float3 specular = specularBrdf * gDirectionalLight.color.rgb * ndotl * gDirectionalLight.intensity;

        float skyBlend = pow(saturate(normalizedNormal.y * 0.5f + 0.5f), horizonSharpness);
        float reflectionBlend = pow(saturate(reflectionVector.y * 0.5f + 0.5f), horizonSharpness);
        float3 ambientSkyColor = lerp(gDirectionalLight.skyLowerColor, gDirectionalLight.skyUpperColor, skyBlend);
        float3 reflectionSkyColor = lerp(gDirectionalLight.skyLowerColor, gDirectionalLight.skyUpperColor, reflectionBlend);
        float3 ambient = output.color.rgb * ambientSkyColor * ambientIntensity * skyIntensity;
        float3 emission = output.color.rgb * emissionStrength;
        float3 skyGlow = ambientSkyColor * skyEmission;
        float3 environmentReflection =
            reflectionSkyColor *
            lerp(fresnel, output.color.rgb, metallic) *
            reflectionIntensity *
            (0.25f + reflectance) *
            (0.35f + (1.0f - roughness) * 0.90f) *
            ndotv;

        float shadow = 1.0f;
        float3 shadowNdc = input.shadowPosition.xyz / input.shadowPosition.w;
        float2 shadowUv = float2(shadowNdc.x * 0.5f + 0.5f, -shadowNdc.y * 0.5f + 0.5f);
        bool isInsideShadowMap =
            shadowUv.x >= 0.0f && shadowUv.x <= 1.0f &&
            shadowUv.y >= 0.0f && shadowUv.y <= 1.0f &&
            shadowNdc.z >= 0.0f && shadowNdc.z <= 1.0f;
        if (isInsideShadowMap) {
            float shadowTexel = 1.0f / 2048.0f;
            float shadowBias = max(0.0008f * (1.0f - ndotl), 0.00025f);
            float shadowSum = 0.0f;
            [unroll]
            for (int y = -1; y <= 1; y++) {
                [unroll]
                for (int x = -1; x <= 1; x++) {
                    float closestDepth = gShadowTexture.Sample(gShadowSampler, shadowUv + float2(x, y) * shadowTexel);
                    shadowSum += (shadowNdc.z - shadowBias <= closestDepth) ? 1.0f : 0.0f;
                }
            }
            shadow = lerp(0.35f, 1.0f, shadowSum / 9.0f);
        }

        output.color.rgb = (diffuse + specular) * shadow + ambient + environmentReflection + emission + skyGlow;
    }

    output.color.a = saturate(output.color.a);
    output.color.rgb = pow(abs(output.color.rgb), 1.0f / 2.2f);
    return output;
}
