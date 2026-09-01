//============================================================
// Ocean GPU Tessellation Hull Shader
//============================================================

struct TransformationMatrix
{
    row_major float4x4 WVP;
    row_major float4x4 World;
    row_major float4x4 lightWVP;
    float4 reflectionClipPlane;
    float4 reflectionClipParams;
    float4 oceanParams0;
    float4 oceanParams1;
    float4 oceanParams2;
    float4 oceanParams3;
    float4 oceanParams4;
    float4 oceanParams5;
    float4 oceanWaveData0[16];
    float4 oceanWaveData1[16];
    float4 surfaceParams0;
    float4 surfaceParams1;
    float4 oceanRenderParams;
    float4 temporalParams;
    row_major float4x4 previousWVP;
};

struct OceanControlPoint
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct OceanPatchConstants
{
    float edgeFactors[3] : SV_TessFactor;
    float insideFactor : SV_InsideTessFactor;
};

ConstantBuffer<TransformationMatrix> gOceanTransformation : register(b4);

float ResolveOceanEdgeFactor(float4 localStart, float4 localEnd)
{
    localStart.xz += gOceanTransformation.oceanParams5.zw;
    localEnd.xz += gOceanTransformation.oceanParams5.zw;
    const float4 clipStart = mul(localStart, gOceanTransformation.WVP);
    const float4 clipEnd = mul(localEnd, gOceanTransformation.WVP);

    if (clipStart.w <= 0.0001f && clipEnd.w <= 0.0001f)
    {
        return 1.0f;
    }

    const float2 ndcStart = clipStart.xy / max(abs(clipStart.w), 0.0001f);
    const float2 ndcEnd = clipEnd.xy / max(abs(clipEnd.w), 0.0001f);
    const float viewportHeight = max(gOceanTransformation.oceanRenderParams.w, 1.0f);
    const float targetPixels = max(gOceanTransformation.oceanRenderParams.y, 4.0f);
    const float edgePixels = length(ndcEnd - ndcStart) * viewportHeight * 0.5f;
    const float maximumFactor = clamp(
        gOceanTransformation.oceanRenderParams.z,
        1.0f,
        8.0f);
    return clamp(edgePixels / targetPixels, 1.0f, maximumFactor);
}

OceanPatchConstants ResolveOceanPatchConstants(
    InputPatch<OceanControlPoint, 3> patch,
    uint patchId : SV_PrimitiveID)
{
    OceanPatchConstants output;
    output.edgeFactors[0] = ResolveOceanEdgeFactor(
        patch[1].position,
        patch[2].position);
    output.edgeFactors[1] = ResolveOceanEdgeFactor(
        patch[2].position,
        patch[0].position);
    output.edgeFactors[2] = ResolveOceanEdgeFactor(
        patch[0].position,
        patch[1].position);
    output.insideFactor = max(
        output.edgeFactors[0],
        max(output.edgeFactors[1], output.edgeFactors[2]));
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ResolveOceanPatchConstants")]
OceanControlPoint main(
    InputPatch<OceanControlPoint, 3> patch,
    uint controlPointId : SV_OutputControlPointID,
    uint patchId : SV_PrimitiveID)
{
    return patch[controlPointId];
}
