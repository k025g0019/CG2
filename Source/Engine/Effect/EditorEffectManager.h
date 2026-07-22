#pragma once

#include "EffectAsset.h"
#include "EditorScene.h"

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorEffectManager {
public:
	struct GpuParticleSpawn {
		Vector3 position{0.0f, 0.0f, 0.0f};  // GPU Particle の発生位置。
		float lifetime = 1.0f;  // Particle が消えるまでの秒数。
		Vector3 velocity{0.0f, 0.0f, 0.0f};  // GPU Compute Shader が積分する初速度。
		float startSize = 1.0f;  // 発生時サイズ。
		Vector3 startColor{1.0f, 1.0f, 1.0f};  // 発生時色。
		float startAlpha = 1.0f;  // 発生時不透明度。
		Vector3 endColor{1.0f, 1.0f, 1.0f};  // 寿命終了時色。
		float endAlpha = 0.0f;  // 寿命終了時不透明度。
		float endSize = 0.0f;  // 寿命終了時サイズ。
		float gravity = 0.0f;  // Y マイナス方向の加速度。
		float drag = 0.0f;  // 速度減衰。
		float noiseStrength = 0.0f;  // GPU 側で足す簡易乱流の強さ。
		float noiseFrequency = 1.0f;  // 乱流の時間周波数。
		int32_t motionType = 0;  // 0=直線、1=軌道、2=渦、3=波、4=吸引、5=雲、6=爆発。
		Vector3 motionCenter{0.0f, 0.0f, 0.0f};  // 軌道・渦・吸引運動のワールド中心。
		float angularSpeed = 0.0f;  // 軌道・渦運動の角速度（ラジアン/秒）。
		float radialAcceleration = 0.0f;  // 中心から外向きへ加える加速度。
		float waveAmplitude = 0.0f;  // 波運動の加速度振幅。
		float waveFrequency = 1.0f;  // 波・雲運動の時間周波数。
		float attractorStrength = 0.0f;  // 中心へ引く加速度。
		float rotation = 0.0f;  // 描画モデルの現在回転角（ラジアン）。
		float rotationSpeed = 0.0f;  // 描画モデルの回転速度（ラジアン/秒）。
		float emissionStrength = 0.0f;  // HDR色へ加える放射強度。
		std::string renderAssetPath;  // 空なら板、FBX / OBJ ならその Mesh を GPU インスタンシングする。
	};

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // Effect の生成先 Scene とログ出力先を受け取る。
	void Start();  // 自動再生 Emitter を初期化する。
	void Update(float deltaTime);  // Emitter の発生判定と Particle の移動・消滅を処理する。
	void Stop();  // Play 中に生成した一時 GameObject を全て破棄する。
	void Draw();  // 描画は SceneSynchronizer が担当するため、Effect 固有の直接描画は行わない。

	bool PlayEffect(int32_t gameObjectId);  // 指定 GameObject に付いている全 Effect を先頭から再生する。
	bool PlayEffectAt(
		int32_t gameObjectId,
		const std::string& effectAssetPath,
		const Vector3& localOffset);  // Animation Event 用に指定位置へ 1 回だけ Effect を発生させる。
	void StopEffect(int32_t gameObjectId);  // 指定 Emitter の発生を止め、既存 Particle は寿命まで残す。
	bool IsEffectPlaying(int32_t gameObjectId) const;  // Emitter が発生中、または所有 Particle が生存中なら true を返す。
	int32_t GetAliveParticleCount(int32_t gameObjectId) const;  // Inspector / C++ Script 用に生存数を返す。
	const std::vector<GpuParticleSpawn>& GetPendingGpuParticleSpawns() const;  // Renderer がこのフレームに GPU へ積む発生要求。
	void ClearPendingGpuParticleSpawns();  // Renderer が GPU へ転送した Spawn 要求を消す。
	float GetLastDeltaTime() const;  // Render 側 Compute Shader に渡す直近 Update 秒。

private:
	struct EmitterRuntime {
		float elapsedTime = 0.0f;  // 再生開始からの経過秒数。
		float spawnAccumulator = 0.0f;  // 発生レートの小数端数を次フレームへ持ち越す。
		bool isPlaying = false;  // false の Emitter は新しい Particle を生成しない。
		bool isBurstEmitted = false;  // 同じ Loop 内で Burst を二重発生させない。
	};

	struct ParticleRuntime {
		uint64_t emitterKey = 0u;  // 所有 Emitter を Component 種類まで含めて識別するキー。
		int32_t ownerGameObjectId = -1;  // API から生存数を数える元 GameObject ID。
		int32_t particleGameObjectId = -1;  // Scene に生成した描画用 GameObject ID。
		float age = 0.0f;  // 発生からの経過秒数。
		float lifetime = 1.0f;  // 消滅までの秒数。
		float startSize = 1.0f;  // 発生時サイズ。
		float endSize = 0.0f;  // 消滅時サイズ。
		float rotationSpeed = 0.0f;  // Y 軸の回転速度（ラジアン/秒）。
		float gravity = 0.0f;  // 下方向加速度。
		float drag = 0.0f;  // 速度減衰係数。
		float startAlpha = 1.0f;  // 発生時の不透明度。
		float endAlpha = 0.0f;  // 寿命終了時の不透明度。
		float emissionStrength = 1.0f;  // Material の放射強度。
		float endSpeedMultiplier = 1.0f;  // 寿命終了時の移動速度倍率。
		float noiseStrength = 0.0f;  // 乱流ノイズの加速度。
		float noiseFrequency = 1.0f;  // 乱流ノイズの時間周波数。
		float collisionBounce = 0.35f;  // Ground 衝突で残す垂直速度率。
		float collisionFriction = 0.2f;  // Ground 衝突で失う水平速度率。
		bool useCollision = false;  // Ground 簡易衝突を有効にする。
		bool useLocalSpace = false;  // Emitter 移動へ追従する場合は true。
		Vector3 localPosition{0.0f, 0.0f, 0.0f};  // Local Space 時の Emitter 相対位置。
		Vector3 velocity{0.0f, 0.0f, 0.0f};  // 1 秒当たりの移動量。
		Vector3 noisePhase{0.0f, 0.0f, 0.0f};  // 各 Particle の乱流を同期させない位相。
		Vector3 startColor{1.0f, 1.0f, 1.0f};  // 発生時色。
		Vector3 endColor{1.0f, 1.0f, 1.0f};  // 消滅時色。
	};

	struct EmitterSnapshot {
		uint64_t key = 0u;
		int32_t ownerGameObjectId = -1;
		Vector3 ownerPosition{0.0f, 0.0f, 0.0f};
		EditorComponent component{};
	};

	EditorScene* editorScene_ = nullptr;  // Effect GameObject の生成・更新先。
	std::vector<std::string>* consoleMessages_ = nullptr;  // 再生失敗などを表示する Console。
	std::unordered_map<uint64_t, EmitterRuntime> emitterRuntimes_;  // Emitter ごとの再生状態。
	std::unordered_map<std::string, EffectAsset> effectAssetCache_;  // Play 中に読み込んだ共有 .effect を再利用する。
	std::vector<ParticleRuntime> particles_;  // 現在生存している Particle。
	std::vector<GpuParticleSpawn> pendingGpuParticleSpawns_;  // GPU StructuredBuffer へ追加する新規 Particle。
	std::mt19937 randomEngine_{0x434732u};  // Play ごとに再現可能な乱数系列。
	uint32_t particleSerial_ = 0u;  // 一時 GameObject 名を重複させない連番。
	float lastDeltaTime_ = 0.0f;  // Render 側で GPU Particle を同じ秒数だけ進めるための値。
	bool isStarted_ = false;  // Play 中だけ Update を有効にする。

	static uint64_t MakeEmitterKey(int32_t gameObjectId, EditorComponentType componentType);  // GameObject と Component 種類を 1 つのキーへまとめる。
	std::vector<EmitterSnapshot> CollectEmitterSnapshots();  // Scene 変更前に Emitter 設定をコピーし、共有 .effect を解決する。
	bool ApplyEffectAsset(EditorComponent& component);  // assetPath が .effect の場合に共有設定を実行用コピーへ反映する。
	void UpdateEmitters(const std::vector<EmitterSnapshot>& emitters, float deltaTime);  // 発生レート・Duration・Burst を処理する。
	void UpdateParticles(float deltaTime);  // 生存 Particle の Transform と Material を更新する。
	void PrewarmEmitter(const EmitterSnapshot& emitter);  // Loop Effect を寿命分だけ進めた初期分布を作る。
	void SpawnParticles(const EmitterSnapshot& emitter, int32_t spawnCount);  // 設定に従って複数 Particle を生成する。
	bool SpawnParticle(const EmitterSnapshot& emitter);  // 描画用 GameObject を 1 つ作る。
	Vector3 MakeSpawnOffset(const EditorComponent& component);  // Shape 内のランダムな発生位置を返す。
	Vector3 MakeSpawnDirection(const EditorComponent& component);  // Shape と基準方向から初速度方向を返す。
	float RandomRange(float minimumValue, float maximumValue);  // 一様乱数を float で返す。
	void PushConsoleMessage(const std::string& message);  // Console が接続済みの場合だけログを追加する。
};

#pragma warning(pop)
