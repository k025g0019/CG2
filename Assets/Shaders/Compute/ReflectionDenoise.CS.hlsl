Texture2D<float4> gInputReflection : register(t0);
RWTexture2D<float4> gOutputReflection : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 center = int2(dispatchThreadId.xy);
    float4 color = 0.0f;
    float weight = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            color += gInputReflection.Load(int3(center + int2(x, y), 0));
            weight += 1.0f;
        }
    }

    gOutputReflection[dispatchThreadId.xy] = color / max(weight, 1.0f);
}
