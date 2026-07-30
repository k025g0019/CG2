#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAnimationManager;
class EditorEffectManager;
class EditorPhysicsManager;
class EditorRailMovementManager;

class EditorRailShooterEnemyManager {
public:
	EditorRailShooterEnemyManager() = default;
	~EditorRailShooterEnemyManager() = default;
	EditorRailShooterEnemyManager(const EditorRailShooterEnemyManager&) = delete;
	EditorRailShooterEnemyManager& operator=(const EditorRailShooterEnemyManager&) = delete;
	EditorRailShooterEnemyManager(EditorRailShooterEnemyManager&&) = delete;
	EditorRailShooterEnemyManager& operator=(EditorRailShooterEnemyManager&&) = delete;

	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorPhysicsManager* physicsManager,
		EditorEffectManager* effectManager,
		EditorAnimationManager* animationManager);  // 敵 Wave と既存実行系を接続する。
	void Start();  // 敵を出現待ちへ移し、Health を初期化する。
	void Update(float deltaTime);  // レール進行率による出現と敵攻撃を更新する。
	void Draw();  // 専用デバッグ描画はまだ行わない。
	void Stop();  // 実行時の敵状態を破棄する。

private:
	struct EnemyRuntime {
		float attackTimer = 0.0f;  // 次の攻撃までの残り秒数。
		bool hasSpawned = false;  // Wave 条件を満たして出現済みなら true。
	};
	struct ProjectileRuntime {
		int32_t gameObjectId = -1;  // Play 開始時に複製した表示用 Object。
		int32_t targetGameObjectId = -1;  // 発射時に固定した攻撃対象。
		Vector3 velocity = {0.0f, 0.0f, 0.0f};  // World 空間の移動速度。
		float remainingLifetime = 0.0f;  // 0 以下ならプールへ戻す。
		float damage = 0.0f;  // 命中時に対象へ与えるダメージ。
		float hitRadius = 0.5f;  // 高速移動区間と対象中心を判定する半径。
		bool isFlying = false;  // 発射中なら true。
	};

	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	EditorEffectManager* effectManager_ = nullptr;
	EditorAnimationManager* animationManager_ = nullptr;
	std::unordered_map<int32_t, EnemyRuntime> enemyRuntimes_;
	std::unordered_map<int32_t, std::vector<ProjectileRuntime>> projectilePoolsByEnemyId_;
	bool isStarted_ = false;

	void SetGameObjectRuntimeActive(EditorGameObject& gameObject, bool isActive);  // 描画とJolt Bodyを同時に切り替える。
	void BuildProjectilePools();  // 敵ごとに表示用弾 Object を事前複製する。
	void UpdateProjectiles(float deltaTime);  // 発射中の弾だけを移動し、対象への命中を判定する。
	bool LaunchProjectile(EditorGameObject& enemyGameObject, const EditorComponent& enemyComponent);  // 空いている弾を対象へ発射する。
	void DeactivateProjectile(ProjectileRuntime& projectileRuntime);  // 弾を非表示へ戻して再利用可能にする。
	void ApplyAttack(EditorGameObject& enemyGameObject, EditorComponent& enemyComponent);  // Effect、Animation、敵弾を一度発生させる。
	void ApplyDamageToTarget(int32_t targetGameObjectId, float damage);  // Health を減らし、0 なら対象を停止する。
};

#pragma warning(pop)
