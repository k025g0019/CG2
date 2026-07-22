#pragma once

#include "EditorScene.h"

#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Effect Asset
//================================================================

// 複数 GameObject から共有する Particle / VisualEffect 設定を保持する。
// Scene の Component は配置と再生状態を持ち、見た目の共通値は .effect から読み込める。
class EffectAsset {
public:
	bool LoadFromJson(const std::string& filePath);  // .effect JSON を読み込み、全必須値を検証する。
	void ApplyToComponent(EditorComponent& component) const;  // 読み込んだ設定を実行用 Component のコピーへ反映する。

	std::string renderAssetPath;  // Particle 1 個の描画に使う FBX / OBJ。空なら既定モデルを使う。
	float duration = 2.0f;  // Emitter 1 周の秒数。
	float startDelay = 0.0f;  // 再生要求から発生開始までの秒数。
	float emissionRate = 10.0f;  // 1 秒当たりの継続発生数。
	float lifetime = 1.0f;  // Particle 1 個の基本寿命秒。
	float speed = 1.0f;  // 初速度の大きさ。
	float startSize = 0.2f;  // 発生時の一様スケール。
	float endSize = 0.0f;  // 寿命終了時の一様スケール。
	float gravity = 0.0f;  // ワールド下方向へ加える加速度。
	float drag = 0.0f;  // 1 秒当たりの速度減衰係数。
	float rotationSpeed = 0.0f;  // Y 軸回転速度。単位はラジアン/秒。
	float shapeRadius = 0.5f;  // Sphere / Cone の半径。
	float shapeAngleDegrees = 25.0f;  // Cone の広がり角度。
	float speedRandomness = 0.0f;  // 初速度へ加える比率乱数。
	float lifetimeRandomness = 0.0f;  // 寿命へ加える比率乱数。
	float sizeRandomness = 0.0f;  // サイズへ加える比率乱数。
	float startAlpha = 1.0f;  // 発生時の不透明度。
	float endAlpha = 0.0f;  // 寿命終了時の不透明度。
	float emissionStrength = 1.0f;  // Particle Material の放射強度。
	float endSpeedMultiplier = 1.0f;  // 寿命終了時の移動速度倍率。
	float noiseStrength = 0.0f;  // 乱流ノイズが速度へ加える加速度。
	float noiseFrequency = 1.0f;  // 乱流ノイズの時間周波数。
	float collisionBounce = 0.35f;  // Ground 衝突後に残す垂直速度率。
	float collisionFriction = 0.2f;  // Ground 衝突で失う水平速度率。
	float angularSpeed = 45.0f;  // 軌道・渦運動の角速度（度/秒）。
	float radialAcceleration = 0.0f;  // 中心から外向きへ加える加速度。
	float waveAmplitude = 0.0f;  // 波運動の加速度振幅。
	float waveFrequency = 1.0f;  // 波・雲運動の周波数。
	float attractorStrength = 0.0f;  // 中心へ引き寄せる加速度。
	int32_t maxCount = 256;  // 同一 Emitter が保持できる最大 Particle 数。
	int32_t burstCount = 0;  // 周回開始時にまとめて発生する個数。
	int32_t shape = 0;  // 0=Point、1=Sphere、2=Cone、3=Box。
	int32_t simulationSpace = 0;  // 0=World、1=Local。
	int32_t motionType = 0;  // 0=直線、1=軌道、2=渦、3=波、4=吸引、5=雲、6=爆発。
	bool playOnAwake = true;  // Play 開始時に自動再生する。
	bool looping = true;  // Duration 終了後に再び発生する。
	bool collision = false;  // 現在は Ground 簡易衝突を有効にする。
	bool prewarm = false;  // Loop Effect を開始直後から進行済みの分布にする。
	Vector3 startColor{1.0f, 1.0f, 1.0f};  // 発生時の線形 RGB。
	Vector3 endColor{1.0f, 0.35f, 0.05f};  // 寿命終了時の線形 RGB。
	Vector3 direction{0.0f, 1.0f, 0.0f};  // Point 以外でも基準にする初速度方向。
	Vector3 boxSize{1.0f, 1.0f, 1.0f};  // Box Shape の全幅。
	Vector3 motionCenter{0.0f, 0.0f, 0.0f};  // Emitter から見た運動中心。
};

#pragma warning(pop)
