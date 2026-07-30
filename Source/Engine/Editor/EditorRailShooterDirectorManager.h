#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAnimationManager;
class EditorEffectManager;
class EditorEffekseerManager;
class EditorRailMovementManager;
class EditorScriptManager;

class EditorRailShooterDirectorManager {
public:
	EditorRailShooterDirectorManager() = default;
	~EditorRailShooterDirectorManager() = default;
	EditorRailShooterDirectorManager(const EditorRailShooterDirectorManager&) = delete;
	EditorRailShooterDirectorManager& operator=(const EditorRailShooterDirectorManager&) = delete;
	EditorRailShooterDirectorManager(EditorRailShooterDirectorManager&&) = delete;
	EditorRailShooterDirectorManager& operator=(EditorRailShooterDirectorManager&&) = delete;

	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorAnimationManager* animationManager,
		EditorEffectManager* effectManager,
		EditorEffekseerManager* effekseerManager,
		EditorScriptManager* scriptManager);  // 制作支援 Component と既存 Runtime を接続する。
	void Start();  // 船演出・敵軌道・ステージ進行を Play 開始状態へそろえる。
	void Update(float deltaTime);  // Rail / AI 更新後に演出、複合移動、ゴールを更新する。
	void Draw();  // 専用描画は行わない。
	void Stop();  // 実行時状態を破棄する。

private:
	struct ShipRuntime {
		Vector3 previousPosition = {0.0f, 0.0f, 0.0f};  // Transform から速度を求める前フレーム位置。
		float wakeBaseRate = 0.0f;  // 航跡 Particle の編集時発生数。
		float windBaseRate = 0.0f;  // 風切り Particle の編集時発生数。
		bool effectsPlaying = false;  // 航跡と風切りを再生中なら true。
	};

	struct EnemyMotionRuntime {
		Vector3 basePosition = {0.0f, 0.0f, 0.0f};  // Rail を使わないパターンの中心位置。
		float elapsedTime = 0.0f;  // パターン開始からの秒数。
		bool wasActive = false;  // Wave 出現直後に中心を取り直すための状態。
	};

	enum class StagePhase {
		WaitingStart,
		Running,
		WaitingTransition,
		Complete,
	};

	struct StageRuntime {
		StagePhase phase = StagePhase::WaitingStart;
		float remainingTime = 0.0f;  // Start / Goal 演出の残り秒数。
		bool railWasActive = true;  // 開始待機後に戻す Rail Movement の有効状態。
	};

	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorAnimationManager* animationManager_ = nullptr;
	EditorEffectManager* effectManager_ = nullptr;
	EditorEffekseerManager* effekseerManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	std::unordered_map<int32_t, ShipRuntime> shipRuntimes_;
	std::unordered_map<int32_t, EnemyMotionRuntime> enemyMotionRuntimes_;
	std::unordered_map<int32_t, StageRuntime> stageRuntimes_;
	bool isStarted_ = false;

	void UpdateShips(float deltaTime);  // 船速から帆速度と Effect 発生量を更新する。
	void UpdateEnemyMotions(float deltaTime);  // 選択した敵移動パターンを適用する。
	void UpdateStages(float deltaTime);  // Start、Goal、Scene 遷移を進める。
	void BeginStage(const EditorComponent& stageComponent, StageRuntime& stageRuntime);  // 開始演出後に Rail を有効化する。
	void ReachStageGoal(const EditorComponent& stageComponent, StageRuntime& stageRuntime);  // Rail を止めて Goal 演出を始める。
	bool SetRailMovementActive(int32_t gameObjectId, bool isActive);  // Rail Movement Component の有効状態を切り替える。
	void PlayCombinedEffect(int32_t gameObjectId);  // 内蔵 Particle と Effekseer をまとめて再生する。
	void StopCombinedEffect(int32_t gameObjectId);  // 内蔵 Particle と Effekseer をまとめて停止する。
	float GetEffectBaseRate(int32_t gameObjectId) const;  // 速度連動前の Particle 発生数を返す。
	void SetEffectRate(int32_t gameObjectId, float emissionRate);  // Particle / VisualEffect の発生数を変更する。
};

#pragma warning(pop)
