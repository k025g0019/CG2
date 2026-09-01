Texture2D<float> gInputAo : register(t0);
RWTexture2D<float> gOutputAo : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 center = int2(dispatchThreadId.xy);
    float ao = 0.0f;
    float weight = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float sampleAo = gInputAo.Load(int3(center + int2(x, y), 0));
            ao += sampleAo;
            weight += 1.0f;
        }
    }

    gOutputAo[dispatchThreadId.xy] = ao / max(weight, 1.0f);
}
