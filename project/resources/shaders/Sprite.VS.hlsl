#include "Sprite.hlsli"

cbuffer SpriteTransform : register(b0) {
    row_major float4x4 WVP;
};

struct SpriteVertexInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

SpriteVertexOutput main(SpriteVertexInput input) {
    SpriteVertexOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    return output;
}
