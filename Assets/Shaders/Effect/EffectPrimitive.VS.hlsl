// Stage1 VFXシステム専用の最小Vertex Shader。
// CPU側でBillboard/Flipbook/Ribbon/RingのTriangleをWorld座標として計算済みのため、
// ここではViewProjectionを掛けるだけで良い。

cbuffer ViewProjection : register(b0) {
	float4x4 gViewProjection;
};

struct VSInput {
	float3 position : POSITION;
	float2 texcoord : TEXCOORD0;
	float4 color : COLOR0;
};

struct VSOutput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
	float4 color : COLOR0;
	// Soft Particle用: CPUで計算済みのWorld座標をそのままPixel Shaderへ渡す(Depth Fadeの距離計算に使う)。
	float3 worldPosition : TEXCOORD1;
};

VSOutput main(VSInput input) {
	VSOutput output;
	output.position = mul(float4(input.position, 1.0f), gViewProjection);
	output.texcoord = input.texcoord;
	output.color = input.color;
	output.worldPosition = input.position;
	return output;
}
