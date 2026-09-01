#ifndef DIRECTXTK_ENVIRONMENT_MAP_EFFECT_BRIDGE_HLSLI
#define DIRECTXTK_ENVIRONMENT_MAP_EFFECT_BRIDGE_HLSLI

//================================================================
// DirectXTK EnvironmentMapEffect 移植部
//================================================================
// 元実装:
// ThirdParty/DirectXTK-may2026/Src/Shaders/EnvironmentMapEffect.fx
//
// D3D12 の RootSignature とこのエンジンの Material / Light 定数に合わせるため、
// Texture / cbuffer / entry point は持ち込まず、反射計算だけを同じ形で使う。

struct DirectXTKEnvironmentMapInput
{
    float3 litColor;                // ライト計算後の色。
    float alpha;                    // テクスチャと材質を掛けた最終アルファ。
    float3 eyeVector;               // DirectXTK の EyePosition - PositionWS と同じ向き。
    float3 worldNormal;             // ワールド空間法線。
    float environmentMapAmount;     // EnvironmentMapAmount 相当。
    float fresnelFactor;            // FresnelFactor 相当。
    float3 environmentMapSpecular;  // EnvironmentMapSpecular 相当。
    bool useFresnel;                // DirectXTK の Fresnel / 非 Fresnel バリアント切替。
    float3 environmentCoordinate;   // Box Projection 適用後のキューブマップ参照方向。
    bool useEnvironmentCoordinate;  // true なら上の参照方向をそのまま使う。
};

float DirectXTKComputeEnvironmentFresnel(
    float3 eyeVector,
    float3 worldNormal,
    float fresnelFactor,
    float environmentMapAmount)
{
    // DirectXTK: pow(max(1 - abs(dot(eyeVector, worldNormal)), 0), FresnelFactor) * EnvironmentMapAmount
    float viewAngle = dot(normalize(eyeVector), normalize(worldNormal));
    return pow(max(1.0f - abs(viewAngle), 0.0f), max(fresnelFactor, 0.001f)) * environmentMapAmount;
}

float3 DirectXTKComputeEnvironmentCoordinate(float3 eyeVector, float3 worldNormal)
{
    // DirectXTK: reflect(-eyeVector, worldNormal)
    return reflect(-normalize(eyeVector), normalize(worldNormal));
}

float3 DirectXTKApplyEnvironmentMap(
    DirectXTKEnvironmentMapInput input,
    TextureCube<float4> environmentMap,
    SamplerState environmentSampler,
    float environmentTextureEnabled)
{
    if (environmentTextureEnabled < 0.5f)
    {
        return input.litColor;
    }

    float3 environmentCoordinate = input.useEnvironmentCoordinate
        ? normalize(input.environmentCoordinate)
        : DirectXTKComputeEnvironmentCoordinate(input.eyeVector, input.worldNormal);

    float4 sampledEnvironmentMap = environmentMap.Sample(
        environmentSampler,
        environmentCoordinate) * input.alpha;

    float reflectionAmount = input.useFresnel
        ? DirectXTKComputeEnvironmentFresnel(
            input.eyeVector,
            input.worldNormal,
            input.fresnelFactor,
            input.environmentMapAmount)
        : input.environmentMapAmount;

    // DirectXTK: color.rgb = lerp(color.rgb, envmap.rgb, amount.rgb)
    float3 color = lerp(input.litColor, sampledEnvironmentMap.rgb, saturate(reflectionAmount));

    // DirectXTK: color.rgb += EnvironmentMapSpecular * envmap.a
    color += input.environmentMapSpecular * sampledEnvironmentMap.a;
    return color;
}

#endif
