struct SSRCB
{
    float4x4 invViewProj;
    float4x4 viewProjMatrix;
    float3 cameraPos;
    float maxDist;
    float2 screenSize;
    float2 invScreenSize;
    float thickness;
    float stride;
    int numSteps;
    float intensity;
};

ConstantBuffer<SSRCB> gSSR : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gDepth : register(t1);
SamplerState gSampler : register(s0);
SamplerState gPointSampler : register(s1);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 UVToWorld(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f), depth, 1.0f);
    float4 worldPos = mul(clipPos, gSSR.invViewProj);
    return worldPos.xyz / max(worldPos.w, 0.0001f);
}

float2 WorldToUV(float3 worldPos)
{
    float4 clipPos = mul(float4(worldPos, 1.0f), gSSR.viewProjMatrix);
    clipPos.xyz /= max(clipPos.w, 0.0001f);
    return clipPos.xy * float2(0.5f, -0.5f) + 0.5f;
}

float3 ComputeNormalFromDepth(float2 uv)
{
    float depthCenter = gDepth.Sample(gPointSampler, uv).r;
    float2 offset = gSSR.invScreenSize;
    float depthRight = gDepth.Sample(gPointSampler, uv + float2(offset.x, 0.0f)).r;
    float depthUp = gDepth.Sample(gPointSampler, uv + float2(0.0f, offset.y)).r;
    float3 worldCenter = UVToWorld(uv, depthCenter);
    float3 worldRight = UVToWorld(uv + float2(offset.x, 0.0f), depthRight);
    float3 worldUp = UVToWorld(uv + float2(0.0f, offset.y), depthUp);
    return normalize(cross(worldRight - worldCenter, worldUp - worldCenter));
}

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float depth = gDepth.Sample(gPointSampler, input.texcoord).r;
    if (depth >= 1.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3 worldPos = UVToWorld(input.texcoord, depth);
    float3 N = ComputeNormalFromDepth(input.texcoord);
    float3 V = normalize(gSSR.cameraPos - worldPos);
    float3 R = normalize(reflect(V, N));

    float3 startPos = worldPos + R * 0.05f;
    float3 endPos = worldPos + R * gSSR.maxDist;
    float2 startUV = WorldToUV(startPos);
    float2 endUV = WorldToUV(endPos);
    float2 rayDir = endUV - startUV;
    float rayLength = length(rayDir);
    if (rayLength < 0.001f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 step = rayDir / max(float(gSSR.numSteps), 1.0f) * gSSR.stride;
    float2 uv = startUV;
    float hit = 0.0f;
    float2 hitUV = 0.0f;

    for (int i = 0; i < gSSR.numSteps; i++)
    {
        uv += step;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) break;

        float sceneDepth = gDepth.Sample(gPointSampler, uv).r;
        float3 scenePos = UVToWorld(uv, sceneDepth);
        float3 toScene = scenePos - worldPos;
        float rayDist = dot(toScene, R);
        float sceneDist = length(toScene - rayDist * R);

        if (rayDist > 0.0f && sceneDist < gSSR.thickness)
        {
            hit = 1.0f;
            hitUV = uv;
            break;
        }
    }

    if (hit > 0.0f)
    {
        float3 reflectionColor = gSceneColor.Sample(gSampler, hitUV).rgb;
        float fresnel = lerp(0.28f, 1.0f, pow(1.0f - max(dot(N, V), 0.0f), 5.0f));
        float2 edgeDist = min(hitUV, 1.0f - hitUV);
        float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 10.0f);
        return float4(reflectionColor * fresnel * edgeFade * gSSR.intensity, hit * edgeFade);
    }

    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
