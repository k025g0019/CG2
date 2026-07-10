Texture2D<float4> gInputTexture : register(t0);
RWStructuredBuffer<uint> gHistogram : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const float3 color = gInputTexture.Load(int3(dispatchThreadId.xy, 0)).rgb;
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    const uint histogramIndex = min((uint)(saturate(luminance) * 255.0f), 255u);
    InterlockedAdd(gHistogram[histogramIndex], 1u);
}
