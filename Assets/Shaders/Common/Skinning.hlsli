StructuredBuffer<row_major float4x4> gCurrentBoneMatrices : register(t16);
StructuredBuffer<row_major float4x4> gPreviousBoneMatrices : register(t17);

bool HasSkinWeights(float4 boneWeights)
{
    return dot(boneWeights, 1.0f) > 0.000001f;
}

float4 NormalizeSkinWeights(float4 boneWeights)
{
    const float totalWeight = max(dot(boneWeights, 1.0f), 0.000001f);
    return boneWeights / totalWeight;
}

void ApplyCurrentSkinning(
    uint4 boneIndices,
    float4 boneWeights,
    inout float4 localPosition,
    inout float3 localNormal)
{
    if (!HasSkinWeights(boneWeights))
    {
        return;
    }

    const float4 normalizedWeights = NormalizeSkinWeights(boneWeights);
    float4 skinnedPosition = 0.0f;
    float3 skinnedNormal = 0.0f;

    [unroll]
    for (uint influenceIndex = 0u; influenceIndex < 4u; influenceIndex++)
    {
        const float influenceWeight = normalizedWeights[influenceIndex];

        if (influenceWeight <= 0.0f)
        {
            continue;
        }

        const row_major float4x4 boneMatrix =
            gCurrentBoneMatrices[boneIndices[influenceIndex]];
        skinnedPosition += mul(localPosition, boneMatrix) * influenceWeight;
        skinnedNormal += mul(float4(localNormal, 0.0f), boneMatrix).xyz * influenceWeight;
    }

    localPosition = skinnedPosition;
    localNormal = normalize(skinnedNormal);
}

float4 ApplyPreviousSkinning(
    float4 localPosition,
    uint4 boneIndices,
    float4 boneWeights)
{
    if (!HasSkinWeights(boneWeights))
    {
        return localPosition;
    }

    const float4 normalizedWeights = NormalizeSkinWeights(boneWeights);
    float4 skinnedPosition = 0.0f;

    [unroll]
    for (uint influenceIndex = 0u; influenceIndex < 4u; influenceIndex++)
    {
        const float influenceWeight = normalizedWeights[influenceIndex];

        if (influenceWeight <= 0.0f)
        {
            continue;
        }

        skinnedPosition += mul(
            localPosition,
            gPreviousBoneMatrices[boneIndices[influenceIndex]]) * influenceWeight;
    }

    return skinnedPosition;
}
