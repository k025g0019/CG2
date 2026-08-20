#include "WaterRailShooter0817.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

namespace {
	constexpr int32_t kWeapon20mmSlot = 0;
	constexpr int32_t kWeapon40mmSlot = 1;
	constexpr int32_t kRocketSlot = 2;
	constexpr int32_t kMissileSlot = 3;
	constexpr int32_t kWeapon40mmPrice = 300;
	constexpr int32_t kRocketPrice = 250;
	constexpr int32_t kMissilePrice = 400;
	constexpr float kCheckpoint1Progress = 0.50f;
	constexpr float kStormProgress = 0.52f;
	constexpr float kCheckpoint2Progress = 0.88f;
	constexpr float kBossStartProgress = 0.97f;
	constexpr float kBossStopProgress = 0.99f;

	// Enemy encounter redesign: 22 rail-progress-gated events (E01..E22) replace the old
	// Battle A / Battle B two-wave design. Each fires its EncounterController exactly once
	// when the player's rail progress crosses the given fraction.
	constexpr float kEvent01Progress = 0.03f;
	constexpr float kEvent02Progress = 0.06f;
	constexpr float kEvent03Progress = 0.10f;
	constexpr float kEvent04Progress = 0.15f;
	constexpr float kEvent05Progress = 0.20f;
	constexpr float kEvent06Progress = 0.24f;
	constexpr float kEvent07Progress = 0.28f;
	constexpr float kEvent08Progress = 0.32f;
	constexpr float kEvent09Progress = 0.36f;
	constexpr float kEvent10Progress = 0.40f;
	constexpr float kEvent11Progress = 0.44f;
	constexpr float kEvent12Progress = 0.48f;
	constexpr float kEvent13Progress = 0.53f;
	constexpr float kEvent14Progress = 0.59f;
	constexpr float kEvent15Progress = 0.62f;
	constexpr float kEvent16Progress = 0.66f;
	constexpr float kEvent17Progress = 0.71f;
	constexpr float kEvent18Progress = 0.73f;
	constexpr float kEvent19Progress = 0.77f;
	constexpr float kEvent20Progress = 0.81f;
	constexpr float kEvent21Progress = 0.86f;
	constexpr float kEvent22Progress = 0.93f;
	constexpr const char* kTitleScenePath = "Assets/Scenes/Title.scene";
	constexpr const char* kGameplayScenePath = "Assets/Scenes/WaterRailShooter_0817.scene";
	constexpr const char* kShopScenePath = "Assets/Scenes/Shop.scene";
	constexpr const char* kResultScenePath = "Assets/Scenes/Result.scene";

	// Title / Shop / Result の各Sceneでこの名前のGameObjectへScript Componentを付けると、
	// そのSceneの入口処理としてUpdateが分岐する。
	constexpr const char* kTitleControllerName = "TitleController";
	constexpr const char* kShopControllerName = "ShopController";
	constexpr const char* kResultControllerName = "ResultController";

	// ショップ往復でゲーム進行を持ち越すためのキー。
	// Script DLLはScene遷移でFreeLibraryされる可能性があるため、DLLのグローバル変数ではなく
	// エンジン側が保持するScene永続値(SceneManager)へ必ず退避する。
	constexpr const char* kShopReturnPendingKey = "ShopReturnPending";
	constexpr const char* kShopRailProgressKey = "ShopRailProgress";
	constexpr const char* kShopSalvageKey = "ShopSalvage";
	constexpr const char* kShopCheckpointIndexKey = "ShopCheckpointIndex";
	constexpr const char* kShopEquippedSlotKey = "ShopEquippedSlot";
	constexpr const char* kShopOwned40mmKey = "ShopOwned40mm";
	constexpr const char* kShopOwnedRocketKey = "ShopOwnedRocket";
	constexpr const char* kShopOwnedMissileKey = "ShopOwnedMissile";
	constexpr const char* kShopBattleAStartedKey = "ShopBattleAStarted";
	constexpr const char* kShopBattleACompletedKey = "ShopBattleACompleted";
	constexpr const char* kShopBattleBStartedKey = "ShopBattleBStarted";
	constexpr const char* kShopBattleBCompletedKey = "ShopBattleBCompleted";
	constexpr const char* kShopStormStartedKey = "ShopStormStarted";
	constexpr const char* kShopCheckpoint1OpenedKey = "ShopCheckpoint1Opened";
	constexpr const char* kShopCheckpoint2OpenedKey = "ShopCheckpoint2Opened";
	constexpr const char* kShopSmallBoatCountKey = "ShopSmallBoatCount";
	constexpr const char* kShopMissileBoatCountKey = "ShopMissileBoatCount";

	// Scene間で結果を受け渡すためのキー。SceneManagerの永続値はScene遷移をまたいで残る。
	constexpr const char* kResultIsClearKey = "ResultIsClear";
	constexpr const char* kResultEnemyCountKey = "DestroyedEnemyCount";

	// 死亡・クリアの表示を見せてからResultへ移るまでの秒数。Spaceで即座にスキップできる。
	constexpr float kResultTransitionDelaySeconds = 2.5f;

	// TargetSteering の移動Mode。Component側 targetSteeringMoveMode と同じ並び。
	// 敵1体の行動は「どのModeを、どの順で使うか」だけをScript側で決め、
	// 実際の移動計算はTargetSteering Componentへ任せる。
	namespace EnemyMoveMode {
		constexpr int32_t kDirect = 0;
		constexpr int32_t kArcApproach = 1;
		constexpr int32_t kParallel = 2;
		constexpr int32_t kChase = 3;
		constexpr int32_t kKeepDistance = 4;
		constexpr int32_t kPlayerRelativeMove = 5;
		constexpr int32_t kRetreat = 6;
	}

	// Boss HP段階で増援Waveを呼ぶ閾値。Boss自身はSpawn処理を持たず、
	// 何をどこから何体出すかはWaveSpawner側の設定に任せる。
	constexpr float kBossReinforcement75Threshold = 0.75f;
	constexpr float kBossReinforcement50Threshold = 0.50f;
	constexpr float kBossReinforcement25Threshold = 0.25f;

	const EditorScriptRuntimeApi* runtimeApi = nullptr;
	std::unordered_map<int32_t, std::unique_ptr<WaterRailShooter0817>> scriptStates;

	struct SharedGameState {
		bool isInitialized = false;
		bool isCheckpoint1Opened = false;
		bool isCheckpoint2Opened = false;
		bool isBattleACompleted = false;
		bool isBattleAStarted = false;
		bool isBattleBStarted = false;
		bool isBattleBCompleted = false;
		bool isStormStarted = false;
		bool isBossStarted = false;
		bool isBossRailSlowed = false;
		bool isPlayerDestroyed = false;
		bool isMissionClear = false;
		bool isBossMainGunDestroyed = false;
		bool isBossMissileDestroyed = false;
		bool isBossEngineDestroyed = false;
		// Enemy encounter redesign: one-shot flags for E01..E22.
		bool isEvent01Started = false;
		bool isEvent02Started = false;
		bool isEvent03Started = false;
		bool isEvent04Started = false;
		bool isEvent05Started = false;
		bool isEvent06Started = false;
		bool isEvent07Started = false;
		bool isEvent08Started = false;
		bool isEvent09Started = false;
		bool isEvent10Started = false;
		bool isEvent11Started = false;
		bool isEvent12Started = false;
		bool isEvent13Started = false;
		bool isEvent14Started = false;
		bool isEvent15Started = false;
		bool isEvent16Started = false;
		bool isEvent17Started = false;
		bool isEvent18Started = false;
		bool isEvent19Started = false;
		bool isEvent20Started = false;
		bool isEvent21Started = false;
		bool isEvent22Started = false;
		// E12 (MidRushEncounter) / E21 (MaxRushEncounter) completion flags gate Checkpoint 1 / 2.
		bool isMidRushCompleted = false;
		bool isMaxRushCompleted = false;
		int32_t checkpointIndex = 0;
		int32_t equippedWeaponSlot = kWeapon20mmSlot;
		int32_t smallBoatDestroyedCount = 0;
		int32_t missileBoatDestroyedCount = 0;
		int32_t battleBDestroyedCount = 0;
		// 死亡・クリア後、Result Sceneへ移るまでの待ち状態。
		bool isResultTransitionPending = false;
		float resultTransitionRemainingSeconds = 0.0f;
		// Boss HP段階の増援。同じ閾値で二重に発火しないよう一度だけtrueにする。
		bool isBossReinforcement75Started = false;
		bool isBossReinforcement50Started = false;
		bool isBossReinforcement25Started = false;
		std::array<bool, 4> ownedWeapons{true, false, false, false};
	};

	SharedGameState sharedGameState;

	WaterRailShooter0817& GetState(int32_t gameObjectId) {
		std::unique_ptr<WaterRailShooter0817>& scriptState = scriptStates[gameObjectId];

		if (scriptState == nullptr) {
			scriptState = std::make_unique<WaterRailShooter0817>();
		}

		return *scriptState;
	}

	WaterRailShooter0817& GetMetadataState() {
		static WaterRailShooter0817 metadataState;
		return metadataState;
	}

	bool IsPerformed(const EditorScriptInputActionContext& inputContext) {
		return inputContext.phase == EditorScriptInputPhasePerformed;
	}

	void Log(const std::string& message) {
		if (runtimeApi != nullptr && runtimeApi->Log != nullptr) {
			runtimeApi->Log(message.c_str());
		}
	}

	GameObject Find(const char* gameObjectName) {
		return GameObject::Find(gameObjectName);
	}

	void SetActive(const char* gameObjectName, bool isActive) {
		const GameObject gameObject = Find(gameObjectName);

		if (gameObject.HasReference()) {
			gameObject.SetActive(isActive);
		}
	}

	void SetState(const std::string& stateName) {
		const GameObject stageController = Find("StageController");

		if (stageController.HasReference()) {
			GenericStateMachine{stageController}.ChangeState(stateName);
		}
	}

	//================================================================
	// Scene transition
	//================================================================

	float ReadSceneFloat(const char* key, float defaultValue = 0.0f) {
		float value = defaultValue;
		SceneManager::GetFloat(key, value);
		return value;
	}

	// ショップへ行く直前に、ゲーム側の進行状況をScene永続値へ退避する。
	// Scene遷移でDLLのグローバル(sharedGameState)が消えても復元できるようにする。
	void SaveProgressForShop(float railProgress) {
		float salvage = 0.0f;
		GenericCounter{Find("SALVAGE Counter")}.Get(salvage);

		SceneManager::SetFloat(kShopRailProgressKey, railProgress);
		SceneManager::SetFloat(kShopSalvageKey, salvage);
		SceneManager::SetFloat(kShopCheckpointIndexKey, static_cast<float>(sharedGameState.checkpointIndex));
		SceneManager::SetFloat(kShopEquippedSlotKey, static_cast<float>(sharedGameState.equippedWeaponSlot));
		SceneManager::SetFloat(kShopOwned40mmKey, sharedGameState.ownedWeapons[kWeapon40mmSlot] ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopOwnedRocketKey, sharedGameState.ownedWeapons[kRocketSlot] ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopOwnedMissileKey, sharedGameState.ownedWeapons[kMissileSlot] ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopBattleAStartedKey, sharedGameState.isBattleAStarted ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopBattleACompletedKey, sharedGameState.isBattleACompleted ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopBattleBStartedKey, sharedGameState.isBattleBStarted ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopBattleBCompletedKey, sharedGameState.isBattleBCompleted ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopStormStartedKey, sharedGameState.isStormStarted ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopCheckpoint1OpenedKey, sharedGameState.isCheckpoint1Opened ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopCheckpoint2OpenedKey, sharedGameState.isCheckpoint2Opened ? 1.0f : 0.0f);
		SceneManager::SetFloat(kShopSmallBoatCountKey, static_cast<float>(sharedGameState.smallBoatDestroyedCount));
		SceneManager::SetFloat(kShopMissileBoatCountKey, static_cast<float>(sharedGameState.missileBoatDestroyedCount));
	}

	// ショップから戻ってきたゲーム側で、退避した進行状況を sharedGameState へ書き戻す。
	// レール位置とSALVAGE Counterの復元は呼び出し側が続けて行う。
	void RestoreProgressFromShop() {
		sharedGameState.checkpointIndex = static_cast<int32_t>(ReadSceneFloat(kShopCheckpointIndexKey));
		sharedGameState.equippedWeaponSlot = static_cast<int32_t>(ReadSceneFloat(kShopEquippedSlotKey));
		sharedGameState.ownedWeapons[kWeapon20mmSlot] = true;
		sharedGameState.ownedWeapons[kWeapon40mmSlot] = ReadSceneFloat(kShopOwned40mmKey) > 0.5f;
		sharedGameState.ownedWeapons[kRocketSlot] = ReadSceneFloat(kShopOwnedRocketKey) > 0.5f;
		sharedGameState.ownedWeapons[kMissileSlot] = ReadSceneFloat(kShopOwnedMissileKey) > 0.5f;
		sharedGameState.isBattleAStarted = ReadSceneFloat(kShopBattleAStartedKey) > 0.5f;
		sharedGameState.isBattleACompleted = ReadSceneFloat(kShopBattleACompletedKey) > 0.5f;
		sharedGameState.isBattleBStarted = ReadSceneFloat(kShopBattleBStartedKey) > 0.5f;
		sharedGameState.isBattleBCompleted = ReadSceneFloat(kShopBattleBCompletedKey) > 0.5f;
		sharedGameState.isStormStarted = ReadSceneFloat(kShopStormStartedKey) > 0.5f;
		sharedGameState.isCheckpoint1Opened = ReadSceneFloat(kShopCheckpoint1OpenedKey) > 0.5f;
		sharedGameState.isCheckpoint2Opened = ReadSceneFloat(kShopCheckpoint2OpenedKey) > 0.5f;
		sharedGameState.smallBoatDestroyedCount = static_cast<int32_t>(ReadSceneFloat(kShopSmallBoatCountKey));
		sharedGameState.missileBoatDestroyedCount = static_cast<int32_t>(ReadSceneFloat(kShopMissileBoatCountKey));
	}

	// 死亡・クリアのどちらでもここを通し、結果をScene間永続値へ記録してから
	// Result Sceneへの遷移待ちを始める。実際の遷移はUpdateが待ち時間を数えて行う。
	void BeginResultTransition(bool isClear) {
		if (sharedGameState.isResultTransitionPending) {
			return;
		}

		SceneManager::SetFloat(kResultIsClearKey, isClear ? 1.0f : 0.0f);
		SceneManager::SetFloat(
			kResultEnemyCountKey,
			static_cast<float>(sharedGameState.smallBoatDestroyedCount + sharedGameState.missileBoatDestroyedCount));
		sharedGameState.isResultTransitionPending = true;
		sharedGameState.resultTransitionRemainingSeconds = kResultTransitionDelaySeconds;
		Log(isClear ? "RESULT: MISSION CLEAR -> Result Scene" : "RESULT: MISSION FAILED -> Result Scene");
	}

	//================================================================
	// Enemy movement / Wave control
	//================================================================

	// 敵1体の移動Modeを切り替える。移動計算そのものはTargetSteering Componentが持ち、
	// Scriptは「次にどのModeへ移るか」だけを決める。
	bool SetEnemyMoveMode(const GameObject& enemy, int32_t moveMode) {
		return enemy.HasReference() &&
			RuntimeProperty::SetInt(enemy, "TargetSteering", "MoveMode", moveMode);
	}

	int32_t GetEnemyMoveMode(const GameObject& enemy) {
		int32_t moveMode = EnemyMoveMode::kDirect;

		if (enemy.HasReference()) {
			RuntimeProperty::GetInt(enemy, "TargetSteering", "MoveMode", moveMode);
		}

		return moveMode;
	}

	// TargetSteering の完了通知を受けて、敵1体の行動を次の段階へ進める。
	// Scene側の「完了後のMode」だけで足りる単純な敵はここへ来ても何もしないが、
	// 攻撃を挟む敵など、Scriptでしか決められない遷移をここで扱う。
	void AdvanceEnemyBehaviour(const GameObject& enemy) {
		if (!enemy.HasReference()) {
			return;
		}

		switch (GetEnemyMoveMode(enemy)) {
			case EnemyMoveMode::kChase:
				// 追跡で目標距離まで詰めたら並走へ移る。
				SetEnemyMoveMode(enemy, EnemyMoveMode::kParallel);
				break;
			case EnemyMoveMode::kArcApproach:
				// 旋回接近を終えたら1回攻撃してから離脱する。
				Weapon{enemy}.FireProjectile();
				SetEnemyMoveMode(enemy, EnemyMoveMode::kRetreat);
				break;
			case EnemyMoveMode::kParallel:
				// 並走を終えたら離脱する。
				SetEnemyMoveMode(enemy, EnemyMoveMode::kRetreat);
				break;
			default:
				// Direct / KeepDistance / PlayerRelativeMove / Retreat は
				// Scene側の「完了後のMode」設定だけで完結させる。
				break;
		}
	}

	// Boss自身にSpawn処理を書かず、HP段階に応じてWaveSpawnerへ開始要求だけを出す。
	// 何をどこから何体出すかはWave側の設定が持つ。
	void StartReinforcementWave(const char* waveGameObjectName) {
		const GameObject wave = Find(waveGameObjectName);

		if (wave.HasReference()) {
			WaveSpawner{wave}.Start();
			Log(std::string("WAVE: ") + waveGameObjectName + " 開始");
		}
	}

	// Boss HP割合を読み、閾値を跨いだ段階の増援Waveを一度だけ開始する。
	void UpdateBossReinforcements() {
		const GameObject boss = Find("BossShip");

		if (!boss.HasReference()) {
			return;
		}

		float currentHealth = 0.0f;
		float maximumHealth = 0.0f;

		if (!RuntimeProperty::GetFloat(boss, "Health", "Current", currentHealth) ||
			!RuntimeProperty::GetFloat(boss, "Health", "Maximum", maximumHealth) ||
			maximumHealth <= 0.0f) {
			return;
		}

		const float healthRatio = currentHealth / maximumHealth;

		if (!sharedGameState.isBossReinforcement75Started && healthRatio <= kBossReinforcement75Threshold) {
			sharedGameState.isBossReinforcement75Started = true;
			StartReinforcementWave("Boss75 Wave");
		}

		if (!sharedGameState.isBossReinforcement50Started && healthRatio <= kBossReinforcement50Threshold) {
			sharedGameState.isBossReinforcement50Started = true;
			StartReinforcementWave("Boss50 Wave");
		}

		if (!sharedGameState.isBossReinforcement25Started && healthRatio <= kBossReinforcement25Threshold) {
			sharedGameState.isBossReinforcement25Started = true;
			StartReinforcementWave("Boss25 Wave");
		}
	}

	// マウスが狙っている洋上/敵の着弾点へ "PlayerAimTarget" を毎フレーム移動する。
	// Weapon 20mm の ProjectileEmitter を AimMode=Ballistic にし、BallisticPrediction の
	// Target GameObject をこの Marker に向けておくと、既存の未改造 BallisticPrediction が
	// 重力込みの発射角度・初速を自動計算し、クリック地点へ時間差で着弾する曲射弾になる。
	// BallisticPrediction(InitialSpeed=650, MaximumTime=4)の到達範囲(理論上限2600)を
	// 超えないよう、狙う座標の探索距離もある程度に抑える。水平線付近を狙うと際限なく
	// 遠い交点になり、届かず発射に失敗する(BallisticValid=false)ため。
	constexpr float kAimTargetMaximumDistance = 1200.0f;
	constexpr float kAimTargetFallbackDistance = 400.0f;

	void UpdateAimTarget(const GameObject& playerShip) {
		const GameObject aimTarget = Find("PlayerAimTarget");

		if (!aimTarget.HasReference()) {
			return;
		}

		EditorScriptRay cameraAimRay{};

		if (!Physics::GetAimRay(playerShip, cameraAimRay)) {
			return;
		}

		// 旧実装はGetAimRayの始点(カメラ位置)を自機の位置へすり替えていた
		// (Physics::Raycastが自機を除外できず、カメラ位置からのレイをそのまま
		// 使うと自機の船体に当たってしまっていたための応急処置)。
		// しかし始点だけ下げて方向はカメラのままだと、画面上でマウスが指している
		// 延長線とは別の直線になり、狙った場所より手前・下に着弾する原因になっていた。
		// RaycastIgnoringHierarchyで自機を確実に除外できるようになったので、
		// カメラの本当のレイ(cameraAimRay)をそのまま使う。
		const EditorScriptRay& aimRay = cameraAimRay;

		EditorScriptPhysicsHit physicsHit{};
		const bool hasPhysicsHit = Physics::RaycastIgnoringHierarchy(
			aimRay,
			kAimTargetMaximumDistance,
			playerShip,
			physicsHit);

		EditorScriptOceanSegmentHit oceanHit{};
		const bool hasOceanHit = Ocean::Raycast(playerShip, aimRay, kAimTargetMaximumDistance, oceanHit);

		EditorScriptVector3 impactPoint{};

		if (hasPhysicsHit && (!hasOceanHit || physicsHit.distance <= oceanHit.distance)) {
			impactPoint = physicsHit.point;
		}
		else if (hasOceanHit) {
			impactPoint = oceanHit.point;
		}
		else {
			// 水平線付近を狙うとOcean::Raycastが探索距離内で波面に届かないことがある。
			// その場合はレイとY=0平面(海面の近似)との交点を使い、空中に外れないようにする。
			float fallbackDistance = kAimTargetFallbackDistance;

			if (std::fabs(aimRay.direction.y) > 0.0001f) {
				const float planeDistance = -aimRay.origin.y / aimRay.direction.y;

				if (planeDistance > 0.0f) {
					fallbackDistance = (std::min)(planeDistance, kAimTargetMaximumDistance);
				}
			}

			impactPoint = {
				aimRay.origin.x + aimRay.direction.x * fallbackDistance,
				aimRay.origin.y + aimRay.direction.y * fallbackDistance,
				aimRay.origin.z + aimRay.direction.z * fallbackDistance};
		}

		EditorScriptTransform transform = aimTarget.GetTransform();
		transform.position = impactPoint;
		aimTarget.SetTransform(transform);
	}

	void AddSalvage(float salvageAmount) {
		const GameObject salvageCounter = Find("SALVAGE Counter");

		if (!salvageCounter.HasReference()) {
			return;
		}

		GenericCounter counter{salvageCounter};
		counter.Add(salvageAmount);
		float currentSalvage = 0.0f;
		counter.Get(currentSalvage);
		Log("SALVAGE: " + std::to_string(static_cast<int32_t>(currentSalvage)));
	}

	bool SpendSalvage(int32_t price) {
		const GameObject salvageCounter = Find("SALVAGE Counter");

		if (!salvageCounter.HasReference()) {
			return false;
		}

		GenericCounter counter{salvageCounter};
		float currentSalvage = 0.0f;

		if (!counter.Get(currentSalvage) || currentSalvage < static_cast<float>(price)) {
			Log("SHOP: SALVAGEが不足しています");
			return false;
		}

		return counter.Set(currentSalvage - static_cast<float>(price));
	}

	void SetOwnedIndicators(bool isShopVisible) {
		SetActive("OWNED 40mm", isShopVisible && sharedGameState.ownedWeapons[kWeapon40mmSlot]);
		SetActive("OWNED Rocket", isShopVisible && sharedGameState.ownedWeapons[kRocketSlot]);
		SetActive("OWNED Missile", isShopVisible && sharedGameState.ownedWeapons[kMissileSlot]);
	}

	void SetShopVisible(bool isVisible) {
		const std::array<const char*, 10> shopObjectNames = {
			"SHOP UI",
			"SHOP Title",
			"SHOP Buy 40mm",
			"SHOP Buy Rocket",
			"SHOP Buy Missile",
			"LOADOUT Equip 20mm",
			"LOADOUT Equip 40mm",
			"LOADOUT Equip Rocket",
			"LOADOUT Equip Missile",
			"SHOP Continue",
		};

		for (const char* shopObjectName : shopObjectNames) {
			SetActive(shopObjectName, isVisible);
		}

		SetOwnedIndicators(isVisible);
	}

	void EquipWeapon(int32_t slotIndex) {
		if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(sharedGameState.ownedWeapons.size()) ||
			!sharedGameState.ownedWeapons[static_cast<size_t>(slotIndex)]) {
			Log("LOADOUT: 未購入の武器です");
			return;
		}

		const GameObject playerShip = Find("PlayerShip");

		// Shop SceneにはPlayerShipが存在しない。その場合は選択だけを記録し、
		// ゲームへ戻ったときの復元処理が実際のWeaponLoadoutへ反映する。
		if (!playerShip.HasReference()) {
			sharedGameState.equippedWeaponSlot = slotIndex;
			Log("LOADOUT: slot " + std::to_string(slotIndex) + " を選択(ゲーム復帰時に装備)");
			return;
		}

		if (!WeaponLoadout{playerShip}.Select(slotIndex)) {
			Log("LOADOUT: 装備変更に失敗しました");
			return;
		}

		sharedGameState.equippedWeaponSlot = slotIndex;
		Log("LOADOUT: slot " + std::to_string(slotIndex) + " を装備");
	}

	void EquipNextOwnedWeapon() {
		for (int32_t offset = 1; offset <= static_cast<int32_t>(sharedGameState.ownedWeapons.size()); offset++) {
			const int32_t slotIndex =
				(sharedGameState.equippedWeaponSlot + offset) %
				static_cast<int32_t>(sharedGameState.ownedWeapons.size());

			if (sharedGameState.ownedWeapons[static_cast<size_t>(slotIndex)]) {
				EquipWeapon(slotIndex);
				return;
			}
		}
	}

	void BuyWeapon(int32_t slotIndex, int32_t price, const char* weaponName) {
		if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(sharedGameState.ownedWeapons.size())) {
			return;
		}

		if (sharedGameState.ownedWeapons[static_cast<size_t>(slotIndex)]) {
			Log(std::string("SHOP: ") + weaponName + " は購入済みです");
			return;
		}

		if (!SpendSalvage(price)) {
			return;
		}

		sharedGameState.ownedWeapons[static_cast<size_t>(slotIndex)] = true;
		SetOwnedIndicators(true);
		Log(std::string("SHOP: ") + weaponName + " を購入しました");
	}

	void SaveCheckpointState() {
		float salvage = 0.0f;
		GenericCounter{Find("SALVAGE Counter")}.Get(salvage);
		SaveSystem::SetFloat("SALVAGE", salvage);
		SaveSystem::SetFloat("Owned40mm", sharedGameState.ownedWeapons[kWeapon40mmSlot] ? 1.0f : 0.0f);
		SaveSystem::SetFloat("OwnedRocket", sharedGameState.ownedWeapons[kRocketSlot] ? 1.0f : 0.0f);
		SaveSystem::SetFloat("OwnedMissile", sharedGameState.ownedWeapons[kMissileSlot] ? 1.0f : 0.0f);
		SaveSystem::SetFloat("EquippedSlot", static_cast<float>(sharedGameState.equippedWeaponSlot));
		SaveSystem::SetFloat("CheckpointIndex", static_cast<float>(sharedGameState.checkpointIndex));
		SaveSystem::Save("water_rail_shooter_0817_progress");
	}

	void OpenCheckpoint(int32_t checkpointIndex) {
		sharedGameState.checkpointIndex = checkpointIndex;

		const GameObject playerShip = Find("PlayerShip");
		RailFollower{playerShip}.Pause();
		SetState(checkpointIndex == 1 ? "Checkpoint1" : "Checkpoint2");

		const GameObject checkpoint = Find(checkpointIndex == 1 ? "Checkpoint 1" : "Checkpoint 2");

		if (checkpoint.HasReference()) {
			Checkpoint{checkpoint}.Save();
		}

		SaveCheckpointState();
		Log(checkpointIndex == 1 ? "CHECKPOINT 1" : "CHECKPOINT 2");

		// ショップはゲーム内UIではなく独立したShop Sceneへ行く。
		// 戻ってきたときにこの位置から再開できるよう、進行状況を退避してから遷移する。
		float railProgress = 0.0f;
		RailFollower{playerShip}.GetNormalizedProgress(railProgress);
		SaveProgressForShop(railProgress);
		SceneManager::SetFloat(kShopReturnPendingKey, 1.0f);
		Log("SHOP: Shop Sceneへ移動");
		SceneManager::LoadScene(kShopScenePath);
	}

	void ActivateBoss() {
		sharedGameState.isBossStarted = true;
		SetActive("BossShip", true);
		SetActive("Boss Main Gun", true);
		SetActive("Boss Missile Launcher", true);
		SetActive("Boss Engine", true);
		SetActive("HUD Boss HP", true);
		SetState("Boss");
		ObjectiveTracker{Find("StageController")}.Set("Boss", ObjectiveState::Active, 0.0f);
		Log("BOSS: 大型艦が出現");
	}

	void HideBoss() {
		SetActive("Boss Main Gun", false);
		SetActive("Boss Missile Launcher", false);
		SetActive("Boss Engine", false);
		SetActive("BossShip", false);
		SetActive("HUD Boss HP", false);
	}
}

//================================================================
// Component registration
//================================================================

WaterRailShooter0817::WaterRailShooter0817() {
	BindAction("OnPlayerFire", [this](const EditorScriptInputActionContext& context) { OnPlayerFire(context); });
	BindAction("OnPlayerReload", [this](const EditorScriptInputActionContext& context) { OnPlayerReload(context); });
	BindAction("OnNextWeapon", [this](const EditorScriptInputActionContext& context) { OnNextWeapon(context); });
	BindAction("OnEnemyFire", [this](const EditorScriptInputActionContext& context) { OnEnemyFire(context); });
	BindAction("OnEnemyMoveCompleted", [this](const EditorScriptInputActionContext& context) { OnEnemyMoveCompleted(context); });
	BindAction("OnSmallBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnSmallBoatDestroyed(context); });
	BindAction("OnMissileBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnMissileBoatDestroyed(context); });
	BindAction("OnBattleACompleted", [this](const EditorScriptInputActionContext& context) { OnBattleACompleted(context); });
	BindAction("OnBattleBCompleted", [this](const EditorScriptInputActionContext& context) { OnBattleBCompleted(context); });
	BindAction("OnMidRushCompleted", [this](const EditorScriptInputActionContext& context) { OnMidRushCompleted(context); });
	BindAction("OnMaxRushCompleted", [this](const EditorScriptInputActionContext& context) { OnMaxRushCompleted(context); });
	BindAction("OnBuy40mm", [this](const EditorScriptInputActionContext& context) { OnBuy40mm(context); });
	BindAction("OnBuyRocket", [this](const EditorScriptInputActionContext& context) { OnBuyRocket(context); });
	BindAction("OnBuyMissile", [this](const EditorScriptInputActionContext& context) { OnBuyMissile(context); });
	BindAction("OnEquip20mm", [this](const EditorScriptInputActionContext& context) { OnEquip20mm(context); });
	BindAction("OnEquip40mm", [this](const EditorScriptInputActionContext& context) { OnEquip40mm(context); });
	BindAction("OnEquipRocket", [this](const EditorScriptInputActionContext& context) { OnEquipRocket(context); });
	BindAction("OnEquipMissile", [this](const EditorScriptInputActionContext& context) { OnEquipMissile(context); });
	BindAction("OnContinue", [this](const EditorScriptInputActionContext& context) { OnContinue(context); });
	BindAction("OnPlayerDestroyed", [this](const EditorScriptInputActionContext& context) { OnPlayerDestroyed(context); });
	BindAction("OnBossMainGunDestroyed", [this](const EditorScriptInputActionContext& context) { OnBossMainGunDestroyed(context); });
	BindAction("OnBossMissileDestroyed", [this](const EditorScriptInputActionContext& context) { OnBossMissileDestroyed(context); });
	BindAction("OnBossEngineDestroyed", [this](const EditorScriptInputActionContext& context) { OnBossEngineDestroyed(context); });
	BindAction("OnBossPhase1", [this](const EditorScriptInputActionContext& context) { OnBossPhase1(context); });
	BindAction("OnBossPhase2", [this](const EditorScriptInputActionContext& context) { OnBossPhase2(context); });
	BindAction("OnBossPhase3", [this](const EditorScriptInputActionContext& context) { OnBossPhase3(context); });
	BindAction("OnBossDestroyed", [this](const EditorScriptInputActionContext& context) { OnBossDestroyed(context); });
	BindAction("OnResult", [this](const EditorScriptInputActionContext& context) { OnResult(context); });
	BindAction("OnRestart", [this](const EditorScriptInputActionContext& context) { OnRestart(context); });
	BindAction("OnLoadoutChanged", [this](const EditorScriptInputActionContext& context) { OnLoadoutChanged(context); });
	BindAction("OnObjectiveChanged", [this](const EditorScriptInputActionContext& context) { OnObjectiveChanged(context); });
}

void WaterRailShooter0817::Start(int32_t gameObjectId) {
	const GameObject titleController = Find(kTitleControllerName);

	if (titleController.HasReference() && gameObjectId == titleController.GetInstanceId()) {
		// 次のプレイのために、前回の結果待ち状態を必ず捨てる。
		sharedGameState = {};
		Log("TITLE: PRESS SPACE TO START");
		return;
	}

	const GameObject shopController = Find(kShopControllerName);

	if (shopController.HasReference() && gameObjectId == shopController.GetInstanceId()) {
		// ゲーム側から退避した所持状況を復元し、この Scene 内で購入・装備できる状態にする。
		RestoreProgressFromShop();
		GenericCounter{Find("SALVAGE Counter")}.Set(ReadSceneFloat(kShopSalvageKey));
		SetShopVisible(true);
		Log("SHOP: 開店 / SALVAGE " +
			std::to_string(static_cast<int32_t>(ReadSceneFloat(kShopSalvageKey))));
		return;
	}

	const GameObject resultController = Find(kResultControllerName);

	if (resultController.HasReference() && gameObjectId == resultController.GetInstanceId()) {
		float isClearValue = 0.0f;
		float destroyedEnemyCount = 0.0f;
		SceneManager::GetFloat(kResultIsClearKey, isClearValue);
		SceneManager::GetFloat(kResultEnemyCountKey, destroyedEnemyCount);
		const bool isClear = isClearValue > 0.5f;

		// Scene側に該当Objectがあれば結果に応じて出し分ける。無ければ何もしない。
		SetActive("MISSION CLEAR Text", isClear);
		SetActive("MISSION FAILED Text", !isClear);
		Log(std::string("RESULT: ") + (isClear ? "MISSION CLEAR" : "MISSION FAILED") +
			" / 撃破数 " + std::to_string(static_cast<int32_t>(destroyedEnemyCount)) +
			" / PRESS SPACE TO RETURN TO TITLE");
		return;
	}

	const GameObject stageController = Find("StageController");

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId()) {
		return;
	}

	// ショップから戻ってきた場合は、初期化ではなく退避しておいた進行状況を復元する。
	const bool isReturningFromShop = ReadSceneFloat(kShopReturnPendingKey) > 0.5f;

	sharedGameState = {};
	sharedGameState.isInitialized = true;
	sharedGameState.ownedWeapons[kWeapon20mmSlot] = true;

	if (isReturningFromShop) {
		RestoreProgressFromShop();
		sharedGameState.isInitialized = true;
	}

	SetShopVisible(false);
	SetActive("MISSION CLEAR Text", false);
	SetActive("RESULT Button", false);
	SetActive("MISSION FAILED Text", false);
	SetActive("RESTART Button", false);
	SetActive("HUD Boss HP", false);

	if (isReturningFromShop) {
		// フラグは一度で使い切る。以後の通常起動を巻き込まないようにする。
		SceneManager::SetFloat(kShopReturnPendingKey, 0.0f);

		GenericCounter{Find("SALVAGE Counter")}.Set(ReadSceneFloat(kShopSalvageKey));
		EquipWeapon(sharedGameState.equippedWeaponSlot);

		const GameObject playerShip = Find("PlayerShip");
		const float railProgress = ReadSceneFloat(kShopRailProgressKey);
		RailFollower{playerShip}.JumpTo(railProgress);
		RailFollower{playerShip}.Resume();
		SetState(sharedGameState.checkpointIndex == 1 ? "BattleB" : "BossApproach");
		Log("SHOP: ゲームへ復帰 / 進行率 " + std::to_string(railProgress));
		return;
	}

	EquipWeapon(kWeapon20mmSlot);
	SetState("Approach");
	Log("WaterRailShooter0817: START");
}

void WaterRailShooter0817::Update(int32_t gameObjectId, float deltaTime) {
	// 名前検索はScene全体の線形走査になるため、Title/Resultの判定は初回だけ行って保持する。
	// ゲーム本編Sceneでは両方falseになり、以降このFindは走らない。
	if (!hasResolvedSceneRole_) {
		hasResolvedSceneRole_ = true;
		const GameObject titleController = Find(kTitleControllerName);
		isTitleController_ = titleController.HasReference() && gameObjectId == titleController.GetInstanceId();
		const GameObject shopController = Find(kShopControllerName);
		isShopController_ = shopController.HasReference() && gameObjectId == shopController.GetInstanceId();
		const GameObject resultController = Find(kResultControllerName);
		isResultController_ = resultController.HasReference() && gameObjectId == resultController.GetInstanceId();
	}

	// Title Scene: Spaceでゲーム本編へ。
	if (isTitleController_) {
		if (Input::GetKeyDown(KeyCode::Space)) {
			Log("TITLE: START GAME");
			SceneManager::LoadScene(kGameplayScenePath);
		}

		return;
	}

	// Shop Scene: 操作は全てボタン(OnBuy* / OnEquip* / OnContinue)なので毎Frameの処理は無い。
	if (isShopController_) {
		return;
	}

	// Result Scene: Spaceでタイトルへ戻る。
	if (isResultController_) {
		if (Input::GetKeyDown(KeyCode::Space)) {
			Log("RESULT: BACK TO TITLE");
			SceneManager::LoadScene(kTitleScenePath);
		}

		return;
	}

	const GameObject playerShip = Find("PlayerShip");
	const GameObject stageController = Find("StageController");

	if (playerShip.HasReference() && gameObjectId == playerShip.GetInstanceId()) {
		if (!sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
			UpdateAimTarget(playerShip);
		}

		if (isPlayerFireHeld_ && !sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
			WeaponLoadout{playerShip}.Fire();
		}

		return;
	}

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId() ||
		!sharedGameState.isInitialized) {
		return;
	}

	// 死亡・クリア後は表示を少し見せてからResult Sceneへ移る。Spaceで即スキップできる。
	if (sharedGameState.isResultTransitionPending) {
		sharedGameState.resultTransitionRemainingSeconds -= deltaTime;

		if (sharedGameState.resultTransitionRemainingSeconds <= 0.0f || Input::GetKeyDown(KeyCode::Space)) {
			sharedGameState.isResultTransitionPending = false;
			SceneManager::LoadScene(kResultScenePath);
		}

		return;
	}

	if (sharedGameState.isPlayerDestroyed || sharedGameState.isMissionClear) {
		return;
	}

	float railProgress = 0.0f;

	if (!playerShip.HasReference() || !RailFollower{playerShip}.GetNormalizedProgress(railProgress)) {
		return;
	}

	// Enemy encounter redesign: E01..E12 fire before Checkpoint 1.
	if (!sharedGameState.isEvent01Started && railProgress >= kEvent01Progress) {
		sharedGameState.isEvent01Started = true;
		EncounterController{Find("E01 Encounter")}.Start();
		Log("EVENT 01: START");
	}

	if (!sharedGameState.isEvent02Started && railProgress >= kEvent02Progress) {
		sharedGameState.isEvent02Started = true;
		EncounterController{Find("E02 Encounter")}.Start();
		Log("EVENT 02: START");
	}

	if (!sharedGameState.isEvent03Started && railProgress >= kEvent03Progress) {
		sharedGameState.isEvent03Started = true;
		EncounterController{Find("E03 Encounter")}.Start();
		Log("EVENT 03: START");
	}

	if (!sharedGameState.isEvent04Started && railProgress >= kEvent04Progress) {
		sharedGameState.isEvent04Started = true;
		EncounterController{Find("E04 Encounter")}.Start();
		Log("EVENT 04: START");
	}

	if (!sharedGameState.isEvent05Started && railProgress >= kEvent05Progress) {
		sharedGameState.isEvent05Started = true;
		EncounterController{Find("E05 Encounter")}.Start();
		Log("EVENT 05: START");
	}

	if (!sharedGameState.isEvent06Started && railProgress >= kEvent06Progress) {
		sharedGameState.isEvent06Started = true;
		EncounterController{Find("E06 Encounter")}.Start();
		Log("EVENT 06: START");
	}

	if (!sharedGameState.isEvent07Started && railProgress >= kEvent07Progress) {
		sharedGameState.isEvent07Started = true;
		EncounterController{Find("E07 Encounter")}.Start();
		Log("EVENT 07: START");
	}

	if (!sharedGameState.isEvent08Started && railProgress >= kEvent08Progress) {
		sharedGameState.isEvent08Started = true;
		EncounterController{Find("E08 Encounter")}.Start();
		Log("EVENT 08: START");
	}

	if (!sharedGameState.isEvent09Started && railProgress >= kEvent09Progress) {
		sharedGameState.isEvent09Started = true;
		EncounterController{Find("E09 Encounter")}.Start();
		Log("EVENT 09: START");
	}

	if (!sharedGameState.isEvent10Started && railProgress >= kEvent10Progress) {
		sharedGameState.isEvent10Started = true;
		EncounterController{Find("E10 Encounter")}.Start();
		Log("EVENT 10: START");
	}

	if (!sharedGameState.isEvent11Started && railProgress >= kEvent11Progress) {
		sharedGameState.isEvent11Started = true;
		EncounterController{Find("E11 Encounter")}.Start();
		Log("EVENT 11: START");
	}

	if (!sharedGameState.isEvent12Started && railProgress >= kEvent12Progress) {
		sharedGameState.isEvent12Started = true;
		SetState("BattleA");
		ObjectiveTracker{stageController}.Set("BattleA", ObjectiveState::Active, 0.0f);
		EncounterController{Find("E12 Encounter")}.Start();
		Log("EVENT 12: START (MidRushEncounter)");
	}

	// Checkpoint 1 now waits for MidRushEncounter (E12) to be fully cleared.
	if (!sharedGameState.isCheckpoint1Opened && railProgress >= kCheckpoint1Progress) {
		if (!sharedGameState.isMidRushCompleted) {
			RailFollower{playerShip}.Pause();
			return;
		}

		sharedGameState.isCheckpoint1Opened = true;
		OpenCheckpoint(1);
		return;
	}

	if (!sharedGameState.isStormStarted && railProgress >= kStormProgress) {
		sharedGameState.isStormStarted = true;
		SetState("Storm");
		RuntimeProperty::SetFloat(Find("Ocean"), "Ocean", "WaveHeight", 2.25f);
		RuntimeProperty::SetFloat(Find("Ocean"), "Ocean", "WindSpeed", 26.0f);
		Log("STORM SECTION");
	}

	// E13..E21 fire before Checkpoint 2.
	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent13Started && railProgress >= kEvent13Progress) {
		sharedGameState.isEvent13Started = true;
		EncounterController{Find("E13 Encounter")}.Start();
		Log("EVENT 13: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent14Started && railProgress >= kEvent14Progress) {
		sharedGameState.isEvent14Started = true;
		EncounterController{Find("E14 Encounter")}.Start();
		Log("EVENT 14: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent15Started && railProgress >= kEvent15Progress) {
		sharedGameState.isEvent15Started = true;
		EncounterController{Find("E15 Encounter")}.Start();
		Log("EVENT 15: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent16Started && railProgress >= kEvent16Progress) {
		sharedGameState.isEvent16Started = true;
		EncounterController{Find("E16 Encounter")}.Start();
		Log("EVENT 16: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent17Started && railProgress >= kEvent17Progress) {
		sharedGameState.isEvent17Started = true;
		EncounterController{Find("E17 Encounter")}.Start();
		Log("EVENT 17: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent18Started && railProgress >= kEvent18Progress) {
		sharedGameState.isEvent18Started = true;
		EncounterController{Find("E18 Encounter")}.Start();
		Log("EVENT 18: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent19Started && railProgress >= kEvent19Progress) {
		sharedGameState.isEvent19Started = true;
		EncounterController{Find("E19 Encounter")}.Start();
		Log("EVENT 19: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent20Started && railProgress >= kEvent20Progress) {
		sharedGameState.isEvent20Started = true;
		EncounterController{Find("E20 Encounter")}.Start();
		Log("EVENT 20: START");
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isEvent21Started && railProgress >= kEvent21Progress) {
		sharedGameState.isEvent21Started = true;
		SetState("BattleB");
		ObjectiveTracker{stageController}.Set("BattleB", ObjectiveState::Active, 0.0f);
		EncounterController{Find("E21 Encounter")}.Start();
		Log("EVENT 21: START (MaxRushEncounter)");
	}

	// Checkpoint 2 now waits for MaxRushEncounter (E21) to be fully cleared.
	if (sharedGameState.checkpointIndex == 1 && !sharedGameState.isCheckpoint2Opened &&
		railProgress >= kCheckpoint2Progress) {
		if (!sharedGameState.isMaxRushCompleted) {
			RailFollower{playerShip}.Pause();
			return;
		}

		sharedGameState.isCheckpoint2Opened = true;
		OpenCheckpoint(2);
		return;
	}

	// E22 (boss escort wave) fires after Checkpoint 2, before Boss activation.
	if (sharedGameState.checkpointIndex >= 2 && !sharedGameState.isEvent22Started && railProgress >= kEvent22Progress) {
		sharedGameState.isEvent22Started = true;
		EncounterController{Find("E22 Encounter")}.Start();
		Log("EVENT 22: START");
	}

	if (sharedGameState.checkpointIndex >= 2 && !sharedGameState.isBossStarted &&
		railProgress >= kBossStartProgress) {
		ActivateBoss();
	}

	if (sharedGameState.isBossStarted && !sharedGameState.isBossRailSlowed &&
		railProgress >= kBossStopProgress) {
		sharedGameState.isBossRailSlowed = true;
		RailFollower{playerShip}.SetSpeed(18.0f);
		Log("BOSS: SIDE-BY-SIDE COMBAT");
	}

	// Boss戦中はHP段階を監視し、閾値を跨いだところで増援Waveへ開始要求を出す。
	if (sharedGameState.isBossStarted) {
		UpdateBossReinforcements();
	}
}

void WaterRailShooter0817::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	isPlayerFireHeld_ = false;
}

//================================================================
// Player / Enemy combat
//================================================================

void WaterRailShooter0817::OnPlayerFire(const EditorScriptInputActionContext& inputContext) {
	isPlayerFireHeld_ = inputContext.phase != EditorScriptInputPhaseCanceled;
}

void WaterRailShooter0817::OnPlayerReload(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		WeaponLoadout{GameObject{inputContext.gameObjectId}}.Reload();
	}
}

void WaterRailShooter0817::OnNextWeapon(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipNextOwnedWeapon();
	}
}

void WaterRailShooter0817::OnEnemyFire(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext) && !sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
		Log("DEBUG_FIRE: OnEnemyFire received for gameObjectId=" + std::to_string(inputContext.gameObjectId));
		Weapon{GameObject{inputContext.gameObjectId}}.FireProjectile();
	}
}

void WaterRailShooter0817::OnEnemyMoveCompleted(const EditorScriptInputActionContext& inputContext) {
	if (sharedGameState.isPlayerDestroyed || sharedGameState.isMissionClear) {
		return;
	}

	// TargetSteering の完了Actionは所有者(敵自身)へ通知される設定を既定にしている。
	AdvanceEnemyBehaviour(GameObject{inputContext.gameObjectId});
}

void WaterRailShooter0817::OnSmallBoatDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.smallBoatDestroyedCount++;
	AddSalvage(20.0f);

	if (sharedGameState.isBattleBStarted) {
		sharedGameState.battleBDestroyedCount++;
	}

	ObjectiveTracker{Find("StageController")}.Set(
		sharedGameState.isBattleBStarted ? "BattleB" : "BattleA",
		ObjectiveState::Active,
		static_cast<float>(sharedGameState.isBattleBStarted
			? sharedGameState.battleBDestroyedCount
			: sharedGameState.smallBoatDestroyedCount));
}

void WaterRailShooter0817::OnMissileBoatDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.missileBoatDestroyedCount++;
	sharedGameState.battleBDestroyedCount++;
	AddSalvage(40.0f);
	ObjectiveTracker{Find("StageController")}.Set(
		"BattleB",
		ObjectiveState::Active,
		static_cast<float>(sharedGameState.battleBDestroyedCount));
}

void WaterRailShooter0817::OnBattleACompleted(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isBattleACompleted = true;
	ObjectiveTracker{Find("StageController")}.Set("BattleA", ObjectiveState::Completed, 15.0f);
	Log("BATTLE A: COMPLETE");
}

void WaterRailShooter0817::OnBattleBCompleted(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isBattleBCompleted = true;
	ObjectiveTracker{Find("StageController")}.Set("BattleB", ObjectiveState::Completed, 10.0f);
	Log("BATTLE B: COMPLETE");
}

void WaterRailShooter0817::OnMidRushCompleted(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isMidRushCompleted = true;
	Log("MID RUSH: COMPLETE");
}

void WaterRailShooter0817::OnMaxRushCompleted(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isMaxRushCompleted = true;
	Log("MAX RUSH: COMPLETE");
}

//================================================================
// Shop / Loadout / Checkpoint
//================================================================

void WaterRailShooter0817::OnBuy40mm(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		BuyWeapon(kWeapon40mmSlot, kWeapon40mmPrice, "40mm Autocannon");
	}
}

void WaterRailShooter0817::OnBuyRocket(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		BuyWeapon(kRocketSlot, kRocketPrice, "Rocket Pod");
	}
}

void WaterRailShooter0817::OnBuyMissile(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		BuyWeapon(kMissileSlot, kMissilePrice, "Anti-Ship Missile");
	}
}

void WaterRailShooter0817::OnEquip20mm(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipWeapon(kWeapon20mmSlot);
	}
}

void WaterRailShooter0817::OnEquip40mm(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipWeapon(kWeapon40mmSlot);
	}
}

void WaterRailShooter0817::OnEquipRocket(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipWeapon(kRocketSlot);
	}
}

void WaterRailShooter0817::OnEquipMissile(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipWeapon(kMissileSlot);
	}
}

void WaterRailShooter0817::OnContinue(const EditorScriptInputActionContext& inputContext) {
	if (!IsPerformed(inputContext)) {
		return;
	}

	// Shop Sceneの「Continue」。購入・装備の結果をScene永続値へ書き戻してからゲームへ戻る。
	float salvage = 0.0f;
	GenericCounter{Find("SALVAGE Counter")}.Get(salvage);
	SceneManager::SetFloat(kShopSalvageKey, salvage);
	SceneManager::SetFloat(kShopEquippedSlotKey, static_cast<float>(sharedGameState.equippedWeaponSlot));
	SceneManager::SetFloat(kShopOwned40mmKey, sharedGameState.ownedWeapons[kWeapon40mmSlot] ? 1.0f : 0.0f);
	SceneManager::SetFloat(kShopOwnedRocketKey, sharedGameState.ownedWeapons[kRocketSlot] ? 1.0f : 0.0f);
	SceneManager::SetFloat(kShopOwnedMissileKey, sharedGameState.ownedWeapons[kMissileSlot] ? 1.0f : 0.0f);
	SaveCheckpointState();
	Log("SHOP: CONTINUE -> ゲームへ戻る");
	SceneManager::LoadScene(kGameplayScenePath);
}

//================================================================
// Player death / Boss phases / Result
//================================================================

void WaterRailShooter0817::OnPlayerDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isPlayerDestroyed = true;
	RailFollower{Find("PlayerShip")}.Pause();
	SetShopVisible(false);
	SetActive("MISSION FAILED Text", true);
	SetActive("RESTART Button", true);
	SetState("Failed");
	Log("PLAYER: DESTROYED");
	BeginResultTransition(false);
}

void WaterRailShooter0817::OnBossMainGunDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;

	if (!sharedGameState.isBossMainGunDestroyed) {
		sharedGameState.isBossMainGunDestroyed = true;
		SetActive("Boss Main Gun", false);
		AddSalvage(80.0f);
		Log("BOSS PART: MAIN GUN DESTROYED");
	}
}

void WaterRailShooter0817::OnBossMissileDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;

	if (!sharedGameState.isBossMissileDestroyed) {
		sharedGameState.isBossMissileDestroyed = true;
		SetActive("Boss Missile Launcher", false);
		AddSalvage(80.0f);
		Log("BOSS PART: MISSILE LAUNCHER DESTROYED");
	}
}

void WaterRailShooter0817::OnBossEngineDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;

	if (!sharedGameState.isBossEngineDestroyed) {
		sharedGameState.isBossEngineDestroyed = true;
		SetActive("Boss Engine", false);
		RailFollower{Find("PlayerShip")}.SetSpeed(14.0f);
		AddSalvage(120.0f);
		Log("BOSS PART: ENGINE DESTROYED / FINAL PHASE");
	}
}

void WaterRailShooter0817::OnBossPhase1(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	RailFollower{Find("PlayerShip")}.SetSpeed(24.0f);
	Log("BOSS PHASE 1: APPROACH");
}

void WaterRailShooter0817::OnBossPhase2(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	RailFollower{Find("PlayerShip")}.SetSpeed(20.0f);
	Log("BOSS PHASE 2: BROADSIDE");
}

void WaterRailShooter0817::OnBossPhase3(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	RailFollower{Find("PlayerShip")}.SetSpeed(16.0f);
	Log("BOSS PHASE 3: STERN / ENGINE");
}

void WaterRailShooter0817::OnBossDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;

	if (sharedGameState.isMissionClear) {
		return;
	}

	sharedGameState.isMissionClear = true;
	RailFollower{Find("PlayerShip")}.Pause();
	HideBoss();
	AddSalvage(500.0f);
	SetActive("MISSION CLEAR Text", true);
	SetActive("RESULT Button", true);
	SetState("Clear");
	ObjectiveTracker{Find("StageController")}.Set("Boss", ObjectiveState::Completed, 1.0f);
	Log("MISSION CLEAR");
	BeginResultTransition(true);
}

void WaterRailShooter0817::OnResult(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		SceneManager::LoadScene(kResultScenePath);
	}
}

void WaterRailShooter0817::OnRestart(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		SceneManager::LoadScene(kGameplayScenePath);
	}
}

void WaterRailShooter0817::OnLoadoutChanged(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
}

void WaterRailShooter0817::OnObjectiveChanged(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
}

//================================================================
// Editor DLL ABI
//================================================================

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;
	EditorNativeScriptRuntime::SetRuntimeApi(api);
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	scriptStates.clear();
	sharedGameState = {};
	runtimeApi = nullptr;
	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);
}

extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	GetState(gameObjectId).Start(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {
	GetState(gameObjectId).Update(gameObjectId, deltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	GetState(gameObjectId).FixedUpdate(gameObjectId, fixedDeltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(
	int32_t gameObjectId,
	const EditorScriptPhysicsEvent* physicsEvent) {
	if (physicsEvent != nullptr) {
		GetState(gameObjectId).DispatchPhysicsEvent(*physicsEvent);
	}
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	const auto scriptStateIterator = scriptStates.find(gameObjectId);

	if (scriptStateIterator != scriptStates.end()) {
		scriptStateIterator->second->Stop(gameObjectId);
		scriptStates.erase(scriptStateIterator);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {
	return fieldDescriptor != nullptr &&
		GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr &&
		GetState(gameObjectId).GetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_SetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	const EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr &&
		GetState(gameObjectId).SetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_InvokeAction(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {
	return inputContext != nullptr &&
		GetState(gameObjectId).InvokeAction(functionName, *inputContext);
}
