Texture2D<float4> gSourceTexture : register(t0);
RWTexture2D<float4> gExposureTexture : register(u0);

cbuffer AutoExposureConstants : register(b0) {
    float adaptationRate;
    float minExposure;
    float maxExposure;
    float deltaTime;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    gExposureTexture.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float3 hdrColor = gSourceTexture[dispatchThreadId.xy].rgb;
    const float luminance = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float targetExposure = clamp(1.0f / max(luminance, 0.001f), minExposure, maxExposure);
    const float blendFactor = saturate(adaptationRate * deltaTime);
    const float currentExposure = gExposureTexture[dispatchThreadId.xy].r;
    const float nextExposure = lerp(currentExposure, targetExposure, blendFactor);

    gExposureTexture[dispatchThreadId.xy] = float4(nextExposure, nextExposure, nextExposure, 1.0f);
}
