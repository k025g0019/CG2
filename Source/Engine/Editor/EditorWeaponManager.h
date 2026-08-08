#pragma once

#include "EditorScene.h"
#include "EditorJoltPhysicsManager.h"
#include "EditorTargetingManager.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorDamageManager;
class EditorEffectManager;
class EditorAudioManager;
class EditorCameraEffectManager;
class EditorInputManager;
class EditorObjectPoolManager;
class EditorPhysicsManager;
class EditorScriptManager;

class EditorWeaponManager {
public:
	struct ThreatInfo {
		int32_t projectileGameObjectId = -1;
		int32_t sourceGameObjectId = -1;
		float distance = 0.0f;
		float closingSpeed = 0.0f;
		float estimatedArrivalSeconds = 0.0f;
	};

	void Initialize(
		EditorScene* editorScene,
		EditorInputManager* inputManager,
		EditorTargetingManager* targetingManager,
		EditorPhysicsManager* physicsManager,
		EditorDamageManager* damageManager,
		EditorObjectPoolManager* objectPoolManager,
		EditorScriptManager* scriptManager,
		EditorEffectManager* effectManager,
		EditorAudioManager* audioManager,
		EditorCameraEffectManager* cameraEffectManager);  // 武器が利用する独立Systemを接続する
	void Start();  // Cooldownと飛翔中Projectileを初期化する
	void Update(float deltaTime);  // Fire入力、Hitscan、Projectile移動を更新する
	void Stop();  // 飛翔中ProjectileをPoolへ戻してRuntime状態を破棄する
	bool FireHitscan(int32_t weaponGameObjectId);  // ScriptやEventからHitscanWeaponを1回発射する
	bool FireProjectile(int32_t emitterGameObjectId);  // ScriptやEventからProjectileEmitterを1回発射する
	bool FireWeaponGroup(int32_t groupGameObjectId);  // 複数Weaponを設定Modeでまとめて発射する
	bool IsWeaponGroupFiring(int32_t groupGameObjectId) const;  // Sequential列が残っているか返す
	bool GetAccuracySpread(int32_t weaponGameObjectId, float& spreadDegrees) const;  // 現在のBase＋蓄積Spreadを返す
	bool DetonateProjectile(int32_t projectileGameObjectId);  // ProjectileDetonatorを手動起爆してPoolへ返す
	void GetIncomingThreats(
		int32_t targetGameObjectId,
		float maximumDistance,
		float minimumClosingSpeed,
		float maximumMissDistance,
		int32_t maximumCount,
		std::vector<ThreatInfo>& threats) const;  // 接近軌道にあるProjectileを到達予測順で返す
	void ResetRuntimeState(int32_t gameObjectId);  // Pool再利用時に武器Cooldownを初期化する

private:
	struct ActiveProjectile {
		int32_t gameObjectId = -1;  // ObjectPoolから借りている表示Object
		int32_t ownerGameObjectId = -1;  // 発射元。Damage送信元と通知に使う
		int32_t instigatorGameObjectId = -1;  // Friendly Fireと発射者階層判定の責任主体
		int32_t actionTargetGameObjectId = -1;  // 命中Actionを受けるScript所有者
		std::string hitActionName;  // 命中時の任意Action
		Vector3 direction{0.0f, 0.0f, 1.0f};  // World移動方向
		float speed = 0.0f;  // 毎秒移動距離
		Vector3 velocity{0.0f, 0.0f, 0.0f};  // 発射母体速度を含むWorld速度の正本
		Vector3 gravity{0.0f, 0.0f, 0.0f};  // BallisticPrediction接続時のWorld重力
		float drag = 0.0f;  // BallisticPredictionと同じ線形Drag
		float radius = 0.0f;  // 連続SphereCast半径
		float damage = 0.0f;  // 命中時の基礎ダメージ
		int32_t damageTagId = 0;  // 固定Enumではない文字列Damage Tagの安定Hash
		float remainingLifetime = 0.0f;  // Poolへ戻るまでの残り秒数
		bool oceanCollision = true;  // FFT水面とのフレーム間交差を判定する
		float traveledDistance = 0.0f;  // Arming Distance判定用の累積移動距離
		float armingDistance = 0.0f;  // この距離まではPhysics/Ocean命中を無効化する
		std::vector<int32_t> ignoredGameObjectIds;  // 発射者階層、Team、明示除外、貫通済みObject
		float penetrationEnergy = 0.0f;
		float penetrationLoss = 1.0f;
		int32_t maximumPenetrations = 0;
		int32_t penetrationCount = 0;
		float ricochetAngleDegrees = 75.0f;
		float energyRetention = 0.65f;
		float damageRetention = 0.75f;
		int32_t maximumRicochets = 0;
		int32_t ricochetCount = 0;
		std::vector<EditorProjectileSurfaceModifierEntry> surfaceModifiers;
	};

	struct PendingShot {
		int32_t weaponGameObjectId = -1;
		int32_t targetGameObjectId = -1;
		int32_t spawnPointGameObjectId = -1;
		float remainingDelay = 0.0f;
		float patternYawDegrees = 0.0f;
		int32_t completionType = 0;  // 0=なし、1=FirePattern、2=TargetAssignment
		bool isProjectile = false;
		bool isLast = false;
	};

	struct VisualRecoilRuntime {
		int32_t gameObjectId = -1;
		Vector3 positionOffset{};
		Vector3 rotationOffset{};
	};

	struct PendingWeaponGroupShot {
		int32_t groupGameObjectId = -1;
		int32_t weaponGameObjectId = -1;
		float remainingDelay = 0.0f;
		bool isLast = false;
	};

	EditorScene* editorScene_ = nullptr;  // Weapon、SpawnPoint、Projectile表示Objectを検索するScene
	EditorInputManager* inputManager_ = nullptr;  // Fire Actionを読む入力Manager
	EditorTargetingManager* targetingManager_ = nullptr;  // 画面照準からWorld Rayを取得するManager
	EditorPhysicsManager* physicsManager_ = nullptr;  // RaycastとSphereCastを実行するManager
	EditorDamageManager* damageManager_ = nullptr;  // 命中対象へDamageを送るManager
	EditorObjectPoolManager* objectPoolManager_ = nullptr;  // Projectile表示Objectを貸し借りするManager
	EditorScriptManager* scriptManager_ = nullptr;  // 発射・命中Action通知先
	EditorEffectManager* effectManager_ = nullptr;  // Surface別Effectを命中位置へ再生する
	EditorAudioManager* audioManager_ = nullptr;  // Surface別AudioSourceを再生する
	EditorCameraEffectManager* cameraEffectManager_ = nullptr;  // RecoilとImpactのCamera Shakeを再生する
	std::unordered_map<int32_t, float> hitscanCooldowns_;  // HitscanWeapon所有者ごとの残り発射間隔
	std::unordered_map<int32_t, float> projectileCooldowns_;  // ProjectileEmitter所有者ごとの残り発射間隔
	std::vector<ActiveProjectile> activeProjectiles_;  // 飛翔中Projectileの連続判定状態
	std::vector<PendingShot> pendingShots_;  // Burst・Salvo・Spread・Sequence・Chargeの未発射列
	std::vector<PendingWeaponGroupShot> pendingWeaponGroupShots_;  // Group Sequentialの未発射Weapon列
	std::unordered_map<int32_t, VisualRecoilRuntime> visualRecoilRuntimes_;  // Weapon所有者ごとの表示反動差分
	uint32_t accuracyRandomState_ = 0x434732u;  // Playごとに再現可能なSpread乱数
	bool isStarted_ = false;  // Play中だけ入力と飛翔を更新する

	bool ShouldFire(
		const EditorGameObject& gameObject,
		int32_t inputGameObjectId,
		const std::string& actionMapName,
		const std::string& actionName,
		bool isAutomatic) const;  // Button Actionを単発または押下中として判定する
	bool BuildAimRay(int32_t screenAimGameObjectId, EditorTargetingManager::AimRay& aimRay) const;  // ScreenAim未設定時は中央Rayを返す
	Vector3 BuildProjectileDirection(
		const Vector3& spawnPosition,
		const EditorTargetingManager::AimRay& aimRay) const;  // Camera Ray上の遠点へ向く発射方向を作る
	bool QueueFireRequest(int32_t weaponGameObjectId, bool isProjectile);  // PatternまたはTarget割当へ1回の発射要求を展開する
	bool QueueTargetAssignment(int32_t emitterGameObjectId, const EditorComponent& assignment);  // MultiTargetLockからTarget別Shotを作る
	void QueueFirePattern(int32_t weaponGameObjectId, bool isProjectile, const EditorComponent* pattern);  // Mode設定からShot列を作る
	void UpdatePendingShots(float deltaTime);  // Delay到達Shotを実弾1発へ変換する
	void UpdateWeaponGroups(float deltaTime);  // Groupの順次発射と完了通知を進める
	bool IsWeaponReady(int32_t weaponGameObjectId) const;  // Weapon種別とCooldownから即時発射可能か調べる
	bool FireWeaponObject(int32_t weaponGameObjectId);  // Hitscan/Projectileの種別を自動判別して発射する
	bool ExecuteHitscanShot(int32_t weaponGameObjectId, float patternYawDegrees);  // Cooldownを再判定せずHitscanを1発処理する
	int32_t ExecuteProjectileShot(int32_t emitterGameObjectId, int32_t targetGameObjectId, int32_t spawnPointGameObjectId, float patternYawDegrees);  // Projectileを1発生成してTargetを割り当てる
	Vector3 ApplyAccuracy(int32_t weaponGameObjectId, const Vector3& direction, float patternYawDegrees);  // Pattern角とAccuracy Coneを合成する
	void RecoverAccuracy(float deltaTime);  // 発射していない時間もSpreadを毎秒回復する
	void ApplyRecoil(int32_t weaponGameObjectId);  // 物理・表示・Camera・Actionへ反動を渡す
	void UpdateVisualRecoil(float deltaTime);  // 表示反動を元Transformへ戻す
	void ExecuteImpactResponse(int32_t weaponGameObjectId, int32_t hitGameObjectId, const Vector3& hitPosition, const Vector3& hitNormal, int32_t damageTagId);  // Surface別応答を実行する
	std::string ResolveSurfaceTag(int32_t hitGameObjectId) const;  // Colliderから親方向へSurfaceTypeを検索する
	int32_t ResolveTeamId(int32_t gameObjectId) const;  // Objectから親方向へTeamを検索する
	bool IsInHierarchy(int32_t gameObjectId, int32_t hierarchyRootGameObjectId) const;  // Objectが指定Root自身または子孫か調べる
	void BuildAttackIgnoredGameObjects(
		int32_t weaponGameObjectId,
		int32_t projectileGameObjectId,
		int32_t& instigatorGameObjectId,
		float& armingDistance,
		std::vector<int32_t>& ignoredGameObjectIds) const;  // Filter設定からJolt Body除外一覧を構築する
	bool CastAttackPhysics(
		const Vector3& origin,
		float radius,
		const Vector3& direction,
		float distance,
		const std::vector<int32_t>& ignoredGameObjectIds,
		EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Ray/Sphere Castへ同じ除外一覧を適用する
	EditorComponent* FindFireLineComponent(int32_t weaponGameObjectId) const;  // Weapon自身から親方向の発射前検査を探す
	bool EvaluateFireLine(int32_t weaponGameObjectId, bool writesRuntimeState) const;  // 自艦を除外せず砲口前方の安全性を判定する
	void UpdateFireLineChecks();  // Inspector Runtime表示を入力がないFrameも更新する
	const EditorProjectileSurfaceModifierEntry* FindProjectileSurfaceModifier(
		const ActiveProjectile& projectile,
		const std::string& surfaceTag) const;  // Surface別Energy補正を検索する
	void QueueCompletionAction(int32_t weaponGameObjectId, int32_t completionType);  // PatternまたはTarget斉射完了を通知する
	void UpdateProjectiles(float deltaTime);  // SphereCastでフレーム間のすり抜けを防いで移動する
	void ExecuteDetonation(ActiveProjectile& activeProjectile, const Vector3& position, int32_t hitGameObjectId);  // AreaDamageとActionを共通実行する
	void QueueAction(
		int32_t ownerGameObjectId,
		int32_t actionTargetGameObjectId,
		const std::string& actionName,
		float value) const;  // 武器通知を任意Script Actionへ渡す
};

#pragma warning(pop)
