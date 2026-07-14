#include "../Temporal/TemporalCommon.hlsli"

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gDenoisedReflection : register(t1);
Texture2D<float4> gMaterialMask : register(t2);
Texture2D<float> gDisocclusionMask : register(t3);
RWTexture2D<float4> gCompositeColor : register(u0);

//================================================================
// SSR モードの材質だけへ画面空間反射を合成する
//================================================================

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gRenderSize))
    {
        return;
    }

    const uint2 pixelPosition = dispatchThreadId.xy;
    const float4 sceneColor = gSceneColor.Load(int3(pixelPosition, 0));
    const float4 reflectionColor = gDenoisedReflection.Load(int3(pixelPosition, 0));
    const float4 materialMask = gMaterialMask.Load(int3(pixelPosition, 0));
    const bool isSsrMaterial = materialMask.b < 0.5f;
    const float reflectionWeight = isSsrMaterial
        ? saturate(materialMask.r * materialMask.g * reflectionColor.a)
        : 0.0f;
    const float disocclusion = gDisocclusionMask.Load(int3(pixelPosition, 0));
    const float stableReflectionWeight = reflectionWeight * (1.0f - disocclusion * 0.5f);
    gCompositeColor[pixelPosition] = float4(
        lerp(sceneColor.rgb, reflectionColor.rgb, stableReflectionWeight),
        sceneColor.a);
}
