#ifndef CAMERA_HLSLI
#define CAMERA_HLSLI

struct CameraCB
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 viewProjMatrix;
    float4x4 invViewProjMatrix;
    float3 cameraPos;
    float nearZ;
    float3 cameraDir;
    float farZ;
    float2 screenSize;
    float2 invScreenSize;
    float fov;
    float aspectRatio;
};

float3 WorldToUVW(float3 worldPos, CameraCB camera)
{
    float4 clipPos = mul(float4(worldPos, 1.0f), camera.viewProjMatrix);
    clipPos.xyz /= clipPos.w;
    return float3(clipPos.xy * 0.5f + 0.5f, clipPos.z);
}

float3 UVWToWorld(float3 uvw, CameraCB camera)
{
    float4 clipPos = float4(uvw.xy * 2.0f - 1.0f, uvw.z, 1.0f);
    float4 worldPos = mul(clipPos, camera.invViewProjMatrix);
    return worldPos.xyz / worldPos.w;
}

float3 GetViewDirection(float2 uv, CameraCB camera)
{
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, -(uv.y * 2.0f - 1.0f), 1.0f, 1.0f);
    float4 viewDir = mul(clipPos, camera.invViewProjMatrix);
    viewDir.xyz /= viewDir.w;
    return normalize(viewDir.xyz - camera.cameraPos);
}

float LinearizeDepth(float depth, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ + depth * (nearZ - farZ));
}

float LinearizeDepthReverse(float depth, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - depth * (farZ - nearZ));
}

#endif
