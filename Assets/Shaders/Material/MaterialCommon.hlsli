#ifndef MATERIALCOMMON_HLSLI
#define MATERIALCOMMON_HLSLI

struct MaterialSurface
{
    float3 albedo;
    float alpha;
    float metallic;
    float roughness;
    float reflectance;
    float ior;
    float emissionStrength;
};

float3 MaterialFresnel(float3 f0, float cosineTheta)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosineTheta), 5.0f);
}

#endif
