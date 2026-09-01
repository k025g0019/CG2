Texture2D<float4> gInputTexture : register(t0);
RWTexture2D<float4> gOutputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 basePixel = dispatchThreadId.xy * 2u;
    const float4 a = gInputTexture.Load(int3(basePixel, 0));
    const float4 b = gInputTexture.Load(int3(basePixel + uint2(1u, 0u), 0));
    const float4 c = gInputTexture.Load(int3(basePixel + uint2(0u, 1u), 0));
    const float4 d = gInputTexture.Load(int3(basePixel + uint2(1u, 1u), 0));
    gOutputTexture[dispatchThreadId.xy] = (a + b + c + d) * 0.25f;
}
