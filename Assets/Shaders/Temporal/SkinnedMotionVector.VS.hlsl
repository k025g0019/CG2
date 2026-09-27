struct SkinnedMotionTransform
{
    row_major float4x4 currentViewProjection;
    row_major float4x4 previousViewProjection;
};

ConstantBuffer<SkinnedMotionTransform> gMotionTransform : register(b0);
StructuredBuffer<row_major float4x4> gCurrentBoneMatrices : register(t0);
StructuredBuffer<row_major float4x4> gPreviousBoneMatrices : register(t1);

struct VSInput
{
    float4 position : POSITION0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 currentClip : TEXCOORD0;
    float4 previousClip : TEXCOORD1;
};

float4 SkinPosition(
    float4 position,
    uint4 boneIndices,
    float4 boneWeights,
    bool usePreviousBones)
{
    const float weightSum = max(dot(boneWeights, 1.0f), 0.00001f);
    const float4 normalizedWeights = boneWeights / weightSum;
    float4 skinnedPosition = 0.0f;

    [unroll]
    for (uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
    {
        const row_major float4x4 boneMatrix = usePreviousBones
            ? gPreviousBoneMatrices[boneIndices[influenceIndex]]
            : gCurrentBoneMatrices[boneIndices[influenceIndex]];
        skinnedPosition += mul(position, boneMatrix) * normalizedWeights[influenceIndex];
    }

    return skinnedPosition;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 currentPosition = SkinPosition(
        input.position, input.boneIndices, input.boneWeights, false);
    const float4 previousPosition = SkinPosition(
        input.position, input.boneIndices, input.boneWeights, true);
    output.currentClip = mul(currentPosition, gMotionTransform.currentViewProjection);
    output.previousClip = mul(previousPosition, gMotionTransform.previousViewProjection);
    output.position = output.currentClip;
    return output;
}
