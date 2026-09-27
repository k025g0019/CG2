#ifndef ANALYTIC_ATMOSPHERE_HLSLI
#define ANALYTIC_ATMOSPHERE_HLSLI

static const float kAtmospherePi = 3.14159265359f;

float AtmosphereRayleighPhase(float cosineTheta)
{
    return 3.0f * (1.0f + cosineTheta * cosineTheta) /
        (16.0f * kAtmospherePi);
}

float AtmosphereMiePhase(float cosineTheta)
{
    const float asymmetry = 0.76f;
    const float asymmetrySquared = asymmetry * asymmetry;
    const float denominator = max(
        1.0f + asymmetrySquared - 2.0f * asymmetry * cosineTheta,
        0.001f);

    return (1.0f - asymmetrySquared) /
        (4.0f * kAtmospherePi * pow(denominator, 1.5f));
}

float3 EvaluateAtmosphereBase(
    float3 viewDirection,
    float3 topColor,
    float3 bottomColor,
    float intensity)
{
    const float3 safeViewDirection = normalize(viewDirection);
    const float upperHemisphere = saturate(safeViewDirection.y);
    const float lowerHemisphere = saturate(-safeViewDirection.y);
    const float horizonDensity = exp2(-abs(safeViewDirection.y) * 7.0f);
    const float verticalBlend = pow(upperHemisphere, 0.38f);
    const float3 safeTopColor = max(topColor, 0.0f);
    const float3 safeBottomColor = max(bottomColor, 0.0f);
    const float3 gradientColor = lerp(
        safeBottomColor,
        safeTopColor,
        verticalBlend);
    const float3 rayleighTint = float3(0.38f, 0.64f, 1.0f) *
        horizonDensity * 0.12f;
    const float groundFade = 1.0f - lowerHemisphere * 0.42f;

    return max(gradientColor + rayleighTint, 0.0f) *
        max(intensity, 0.0f) * groundFade;
}

float3 EvaluateAtmosphereSolarScattering(
    float3 viewDirection,
    float3 sunDirection,
    float sunIntensity)
{
    const float3 safeViewDirection = normalize(viewDirection);
    const float3 safeSunDirection = normalize(sunDirection);
    const float cosineTheta = clamp(
        dot(safeViewDirection, safeSunDirection),
        -1.0f,
        1.0f);
    const float horizonDensity = exp2(-abs(safeViewDirection.y) * 5.5f);
    const float sunElevation = saturate(safeSunDirection.y * 3.0f + 0.15f);
    const float3 sunTint = lerp(
        float3(1.0f, 0.34f, 0.08f),
        float3(1.0f, 0.91f, 0.72f),
        sunElevation);
    const float rayleighScattering = AtmosphereRayleighPhase(cosineTheta) *
        (0.10f + horizonDensity * 0.16f);
    const float mieScattering = AtmosphereMiePhase(cosineTheta) *
        (0.035f + horizonDensity * 0.055f);
    const float sunDisc = pow(saturate(cosineTheta), 2048.0f) * 9.0f;

    return sunTint *
        (rayleighScattering + mieScattering + sunDisc) *
        max(sunIntensity, 0.0f);
}

//============================================================
// 反射方向ごとの空情報
//============================================================

float3 EvaluateAtmosphereDirectionalDetail(
    float3 viewDirection,
    float3 sunDirection,
    float3 topColor,
    float3 bottomColor,
    float intensity,
    float sunIntensity)
{
    const float3 safeViewDirection = normalize(viewDirection);
    const float3 safeSunDirection = normalize(sunDirection);
    const float upwardDirection = saturate(safeViewDirection.y);
    const float horizonBand = exp2(-abs(safeViewDirection.y) * 11.0f);
    const float middleBand = smoothstep(0.02f, 0.42f, upwardDirection) *
        (1.0f - smoothstep(0.48f, 0.92f, upwardDirection));
    const float3 horizonColor = lerp(
        max(bottomColor, 0.0f),
        float3(0.52f, 0.66f, 0.76f),
        0.26f);
    const float3 middleColor = lerp(
        max(bottomColor, 0.0f),
        max(topColor, 0.0f),
        0.54f);
    float3 directionalDetail =
        horizonColor * horizonBand * 0.18f +
        middleColor * middleBand * 0.055f;

    // Texture追加なしの連続した低周波雲。floor/hashを使わず、水面反射で斑点化しない。
    const float cloudWaveA = sin(
        safeViewDirection.x * 19.0f + safeViewDirection.z * 13.0f);
    const float cloudWaveB = sin(
        safeViewDirection.x * -31.0f + safeViewDirection.z * 23.0f + 1.7f);
    const float cloudCoverage = smoothstep(
        0.42f,
        0.88f,
        cloudWaveA * 0.62f + cloudWaveB * 0.38f) *
        smoothstep(-0.06f, 0.12f, safeViewDirection.y) *
        (1.0f - smoothstep(0.48f, 0.82f, safeViewDirection.y));
    const float sunAlignment = saturate(dot(
        safeViewDirection,
        safeSunDirection));
    const float warmSunHalo = pow(sunAlignment, 36.0f) *
        smoothstep(-0.08f, 0.22f, safeSunDirection.y);
    const float3 warmHaloColor = float3(1.0f, 0.70f, 0.40f);
    directionalDetail +=
        float3(0.72f, 0.78f, 0.82f) * cloudCoverage * 0.032f +
        warmHaloColor * warmSunHalo * max(sunIntensity, 0.0f) * 0.018f;

    return directionalDetail * max(intensity, 0.0f);
}

float3 EvaluateAnalyticAtmosphere(
    float3 viewDirection,
    float3 sunDirection,
    float3 topColor,
    float3 bottomColor,
    float intensity,
    float sunIntensity)
{
    return EvaluateAtmosphereBase(
        viewDirection,
        topColor,
        bottomColor,
        intensity) +
        EvaluateAtmosphereSolarScattering(
            viewDirection,
            sunDirection,
            sunIntensity) +
        EvaluateAtmosphereDirectionalDetail(
            viewDirection,
            sunDirection,
            topColor,
            bottomColor,
            intensity,
            sunIntensity);
}

float3 ApplyAerialPerspective(
    float3 surfaceColor,
    float3 worldPosition,
    float3 cameraPosition,
    float3 sunDirection,
    float3 topColor,
    float3 bottomColor,
    float skyIntensity,
    float sunIntensity,
    float density)
{
    const float3 cameraToSurface = worldPosition - cameraPosition;
    const float viewDistance = length(cameraToSurface);

    if (viewDistance <= 0.001f || density <= 0.0f)
    {
        return surfaceColor;
    }

    const float3 viewDirection = cameraToSurface / viewDistance;
    const float heightDensity = exp2(
        -max((worldPosition.y + cameraPosition.y) * 0.5f, 0.0f) * 0.0012f);
    const float transmittance = exp(
        -viewDistance * density * heightDensity);
    const float3 inscattering = EvaluateAnalyticAtmosphere(
        viewDirection,
        sunDirection,
        topColor,
        bottomColor,
        skyIntensity,
        sunIntensity * 0.18f);

    return lerp(inscattering, surfaceColor, saturate(transmittance));
}

#endif
