#include "Sprite.hlsli"

cbuffer SpriteMaterial : register(b0) {
    float4 color;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(SpriteVertexOutput input) : SV_TARGET0 {
    return gTexture.Sample(gSampler, input.texcoord) * color;
}
