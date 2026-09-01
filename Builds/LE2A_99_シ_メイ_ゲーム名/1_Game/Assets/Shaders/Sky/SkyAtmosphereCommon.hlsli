#ifndef CG2_SKY_ATMOSPHERE_COMMON_HLSLI
#define CG2_SKY_ATMOSPHERE_COMMON_HLSLI

static const float3 kRayleighScattering = float3(5.802f, 13.558f, 33.100f) * 0.001f;
static const float3 kMieScattering = float3(3.996f, 3.996f, 3.996f) * 0.001f;
static const float kPlanetRadius = 6360.0f;
static const float kAtmosphereRadius = 6460.0f;

float ComputeAtmosphereHeight(float3 worldPosition) {
    return saturate((length(worldPosition) - kPlanetRadius) / max(kAtmosphereRadius - kPlanetRadius, 0.0001f));
}

#endif
