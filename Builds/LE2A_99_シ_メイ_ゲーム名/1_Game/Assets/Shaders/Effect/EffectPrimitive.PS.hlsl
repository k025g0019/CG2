// Stage1 VFXシステム専用の最小Pixel Shader。
// Textureに頂点Color(Tint / Alpha / Flipbook前後で変化するAlphaも含む)を掛けるだけの
// Unlit合成。Blend Mode(Alpha/Additive)はPSO側で切り替える。

Texture2D<float4> gTexture : register(t0);
// Soft Particle用: 既存のOpaque Depth Copy(Object3d.PS.hlslのgWaterSceneDepthと同じ資源)を読む。
// Water Passが不透明描画直後にCopyResourceで作るR24_UNORM_X8_TYPELESS SRVで、Device Depth(0..1, 非Reversed)を保持する。
Texture2D<float> gSceneDepth : register(t1);
SamplerState gSampler : register(s0);

// OceanSurface.PS.hlsl / Object3d.PS.hlsl の gWaterView と同じ構成(逆ViewProjection + Viewport UV変換)。
// Scene ViewとGame Viewが同じRender Targetを共有するため、Depth CopyのUVをそのViewport範囲へ変換する。
cbuffer SceneDepthView : register(b1) {
	float4x4 gInverseViewProjection;
	float2 gViewportUvScale;
	float2 gViewportUvOffset;
};

// EffectNodeDefinition::useSoftParticle / softParticleFadeDistanceをBatch単位でそのまま渡す。
cbuffer SoftParticleParams : register(b2) {
	float gUseSoftParticle;          // 0.0f = Hard Particle(従来通り) / 1.0f以上 = Depth Fadeを有効化。
	float gSoftParticleFadeDistance; // Sceneとの距離差(World単位)がこれ未満でAlphaをFadeする。
};

struct PSInput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
	float4 color : COLOR0;
	float3 worldPosition : TEXCOORD1;
};

// OceanSurface.PS.hlsl(Object3d.PS.hlsl内)のReconstructWaterWorldPositionと同じ手法。
// Device DepthをNDCへ戻し、逆ViewProjectionでWorld座標を復元する。
float3 ReconstructSceneWorldPosition(float2 screenUv, float deviceDepth) {
	const float2 viewportUv = saturate((screenUv - gViewportUvOffset) / max(gViewportUvScale, float2(0.00001f, 0.00001f)));
	const float2 ndc = float2(
		viewportUv.x * 2.0f - 1.0f,
		1.0f - viewportUv.y * 2.0f);
	const float4 worldPosition = mul(float4(ndc, deviceDepth, 1.0f), gInverseViewProjection);
	return worldPosition.xyz / max(abs(worldPosition.w), 0.00001f);
}

float4 main(PSInput input) : SV_TARGET {
	float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
	float4 finalColor = textureColor * input.color;

	// useSoftParticleがOFFのNode/Batchでは従来通り何もせず、性能・見た目とも回帰なしにする。
	if (gUseSoftParticle > 0.5f) {
		uint sceneWidth = 1u;
		uint sceneHeight = 1u;
		gSceneDepth.GetDimensions(sceneWidth, sceneHeight);
		const float2 sceneSize = max(float2(sceneWidth, sceneHeight), float2(1.0f, 1.0f));
		const float2 screenUv = saturate(input.position.xy / sceneSize);
		const float opaqueDeviceDepth = gSceneDepth.SampleLevel(gSampler, screenUv, 0.0f);

		// 同じ画面Pixel(ほぼ同じView Ray)上のOpaque面とParticleのWorld距離を、線形Depth差の近似として使う。
		// (Object3d.PS.hlslのSampleOceanSceneがviewPathLengthに使っているのと同じ近似手法)
		const float3 opaqueWorldPosition = ReconstructSceneWorldPosition(screenUv, opaqueDeviceDepth);
		const float fadeDistance = length(opaqueWorldPosition - input.worldPosition);
		const float fadeFactor = saturate(fadeDistance / max(gSoftParticleFadeDistance, 0.001f));
		finalColor.a *= fadeFactor;
	}

	if (finalColor.a <= 0.003f) {
		discard;
	}

	return finalColor;
}
