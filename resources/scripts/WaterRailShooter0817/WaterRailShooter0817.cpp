#include "WaterRailShooter0817.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {
	constexpr int32_t kWeapon20mmSlot = 0;
	constexpr int32_t kWeapon40mmSlot = 1;
	constexpr int32_t kRocketSlot = 2;
	constexpr int32_t kMissileSlot = 3;
	constexpr float kStormProgress = 0.52f;
	// ボスは廃止。Railが終端付近へ到達した時点でMission Clearにする。
	// RailFollowerの補間や終端停止で1.0fぴったりまで進まない場合があるため、
	// 画面上ほぼ到達している0.985fをClear判定に使う。
	constexpr float kRailCompleteProgress = 0.985f;

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
	constexpr const char* kTitleScenePath = "Assets/Scenes/WaterRailShooter/Title.scene";
	constexpr const char* kGameplayScenePath = "Assets/Scenes/WaterRailShooter/WaterRailShooter_0817.scene";
	constexpr const char* kClearResultScenePath = "Assets/Scenes/WaterRailShooter/WaterRailShooter_Clear.scene";
	constexpr const char* kGameOverResultScenePath = "Assets/Scenes/WaterRailShooter/WaterRailShooter_GameOver.scene";

	// Title / Result の各Sceneでこの名前のGameObjectへScript Componentを付けると、
	// そのSceneの入口処理としてUpdateが分岐する。
	constexpr const char* kTitleControllerName = "TitleController";
	constexpr const char* kResultControllerName = "ResultController";

	// Scene間で結果を受け渡すためのキー。SceneManagerの永続値はScene遷移をまたいで残る。
	constexpr const char* kResultIsClearKey = "ResultIsClear";
	constexpr const char* kResultEnemyCountKey = "DestroyedEnemyCount";

	// Result SceneのLoad要求が一時的に拒否された場合の再試行間隔。
	// Gameplay側には結果UIを出さず、Result Sceneだけを結果表示の場所にする。
	constexpr float kResultRetrySeconds = 0.25f;

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


	struct SharedGameState {
		bool isInitialized = false;
		bool isBattleACompleted = false;
		bool isBattleAStarted = false;
		bool isBattleBStarted = false;
		bool isBattleBCompleted = false;
		bool isStormStarted = false;
		bool isPlayerDestroyed = false;
		bool isMissionClear = false;
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
		// Checkpointは廃止済み。MidRush/MaxRush完了フラグ自体は他の用途に使う可能性があるため残す。
		bool isMidRushCompleted = false;
		bool isMaxRushCompleted = false;
		int32_t equippedWeaponSlot = kWeapon20mmSlot;
		int32_t smallBoatDestroyedCount = 0;
		int32_t missileBoatDestroyedCount = 0;
		int32_t battleBDestroyedCount = 0;
		// 死亡・クリア後、Result Sceneへ移るまでの待ち状態。
		bool isResultTransitionPending = false;
		float resultTransitionRemainingSeconds = 0.0f;
		// Shop廃止に伴い、購入の概念自体を削除。最終的にどの武器を積むかは未定のため、
		// 全スロットを既定で所持済みにしてEキーでの切替だけで全武器を試せるようにしておく。
		std::array<bool, 4> ownedWeapons{true, true, true, true};
		// 敵撃破エフェクトはScene上に実在するParticleSystem Component(FX Explosion Small/Large)を
		// 使う。外部.effectdefファイルの文字列呼び出しはしない。既定でGameObjectはisActive=falseに
		// してあるので、撃破位置へ移動させてから一定時間だけSetActive(true)にし、時間が来たら
		// SetActive(false)へ戻す。
		GameObject playerAimTarget;
		// 撃破イベント内で毎回名前検索を行うと、Find/文字列処理が一時Allocになるため、
		// StageControllerと再生対象はPlay開始時に一度だけ解決して使い回す。
		GameObject stageController;
		GameObject explosionSmall;
		GameObject explosionLarge;
		GameObject sfxExplosionSmall;
		GameObject sfxExplosionLarge;
		GameObject salvageCounter;
		float explosionSmallRemainingSeconds = 0.0f;
		float explosionLargeRemainingSeconds = 0.0f;
	};

	SharedGameState sharedGameState;

	constexpr float kExplosionEffectActiveSeconds = 0.5f;

	// Find()は本体をこのファイルの後方(228行付近)で定義しているため、それより前にある
	// PlayEnemyDestroyedEffect/StopEnemyDestroyedEffectから呼べるよう前方宣言する。
	GameObject Find(const char* gameObjectName);

	// Scene上に実在するParticleSystem Component(FX Explosion Small/Large、EFFECTS配下)を
	// 撃破位置へ移動してから一定時間だけ有効化する。外部.effectdefファイルへの文字列参照は
	// 使わない(Inspectorから見える・触れる通常のComponentのみで完結させる)。
	void PlayEnemyDestroyedEffect(const EditorScriptInputActionContext& inputContext, bool isLargeExplosion) {
		const int32_t destroyedGameObjectId = inputContext.payloadType == EditorScriptActionPayloadTypeGameObject
			? inputContext.payloadGameObjectId
			: -1;

		if (destroyedGameObjectId < 0) {
			return;
		}

		GameObject& cachedExplosionEffect = isLargeExplosion
			? sharedGameState.explosionLarge
			: sharedGameState.explosionSmall;
		// 通常はStartで解決済み。旧Sceneや入口順序が異なる場合だけ一度フォールバックする。
		if (!cachedExplosionEffect.HasReference()) {
			cachedExplosionEffect = Find(isLargeExplosion ? "FX Explosion Large" : "FX Explosion Small");
		}

		const GameObject& explosionEffect = cachedExplosionEffect;

		if (!explosionEffect.HasReference()) {
			return;
		}

		const EditorScriptTransform destroyedTransform = GameObject{destroyedGameObjectId}.GetTransform();
		explosionEffect.SetTransform(destroyedTransform);
		explosionEffect.SetActive(true);

		// SetActive(true)だけではParticleは1つも出ない。EffectManagerはEmitterごとに
		// isPlayingを持ち、Play開始時のPlayOnAwake判定かPlayEffect()でしかtrueにならないため、
		// ここで明示的に再生を開始する(これが無いと「重いのに何も見えない」状態になる)。
		if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr && EditorNativeScriptRuntime::GetRuntimeApi()->PlayEffect != nullptr) {
			EditorNativeScriptRuntime::GetRuntimeApi()->PlayEffect(explosionEffect.GetInstanceId());
		}

		(isLargeExplosion
			? sharedGameState.explosionLargeRemainingSeconds
			: sharedGameState.explosionSmallRemainingSeconds) = kExplosionEffectActiveSeconds;
	}

	// 撃破効果音等と違いUpdateで毎フレーム呼ぶ必要があるため、StageController側のUpdateから
	// 一度だけ呼ぶ。時間切れになったExplosion GameObjectをSetActive(false)へ戻すだけの処理。
	void StopEnemyDestroyedEffect(const GameObject& explosionEffect) {

		if (!explosionEffect.HasReference()) {
			return;
		}

		// Emitterの発生だけ止める。既に出ているParticleはGPU側で寿命分だけ残って消えるため、
		// 爆発が途中でぶつ切りにならない。
		if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr && EditorNativeScriptRuntime::GetRuntimeApi()->StopEffect != nullptr) {
			EditorNativeScriptRuntime::GetRuntimeApi()->StopEffect(explosionEffect.GetInstanceId());
		}

		explosionEffect.SetActive(false);
	}

	void UpdateEnemyDestroyedEffects(float deltaTime) {
		if (sharedGameState.explosionSmallRemainingSeconds > 0.0f) {
			sharedGameState.explosionSmallRemainingSeconds -= deltaTime;

			if (sharedGameState.explosionSmallRemainingSeconds <= 0.0f) {
				StopEnemyDestroyedEffect(sharedGameState.explosionSmall);
			}
		}

		if (sharedGameState.explosionLargeRemainingSeconds > 0.0f) {
			sharedGameState.explosionLargeRemainingSeconds -= deltaTime;

			if (sharedGameState.explosionLargeRemainingSeconds <= 0.0f) {
				StopEnemyDestroyedEffect(sharedGameState.explosionLarge);
			}
		}
	}


	bool IsPerformed(const EditorScriptInputActionContext& inputContext) {
		return inputContext.phase == EditorScriptInputPhasePerformed;
	}

	void Log(const std::string& message) {
		if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr && EditorNativeScriptRuntime::GetRuntimeApi()->Log != nullptr) {
			EditorNativeScriptRuntime::GetRuntimeApi()->Log(message.c_str());
		}
	}

	GameObject Find(const char* gameObjectName) {
		return GameObject::Find(gameObjectName);
	}

	bool SetActive(const char* gameObjectName, bool isActive) {
		const GameObject gameObject = Find(gameObjectName);

		if (!gameObject.HasReference()) {
			return false;
		}

		return gameObject.SetActive(isActive);
	}

	bool SetResultTextVisible(const char* gameObjectName, bool isVisible) {
		const GameObject gameObject = Find(gameObjectName);

		if (!gameObject.HasReference()) {
			return false;
		}

		// Text GameObjectだけONにしても、Text Component側が無効だと描画されない。
		// Result Sceneでは表示対象のText Componentも必ず復旧してからGameObjectを表示する。
		// Scene Builderで作ったUIはTextMeshProUGUIを使う場合があるため、旧Textと両方を復旧する。
		if (isVisible) {
			gameObject.SetComponentActive("Text", true);
			gameObject.SetComponentActive("TextMeshProUGUI", true);
		}

		return gameObject.SetActive(isVisible);
	}

	void SetComponentActiveForAll(const char* componentTypeName, bool isActive) {
		for (const GameObject& gameObject : GameObject::FindAllWithComponent(componentTypeName)) {
			gameObject.SetComponentActive(componentTypeName, isActive);
		}
	}

	void ReleaseOrDeactivateAllWithComponent(const char* componentTypeName) {
		for (const GameObject& gameObject : GameObject::FindAllWithComponent(componentTypeName)) {
			if (!ObjectPool::Release(gameObject)) {
				gameObject.SetActive(false);
			}
		}
	}

	void StopGameplayForResultTransition() {
		// Gameplay Scene内の仮Result UIは使わない。
		// 結果表示はResult.sceneのResultControllerだけに集約する。
		SetActive("MISSION CLEAR Text", false);
		SetActive("RESULT Button", false);
		SetActive("MISSION FAILED Text", false);
		SetActive("RESTART Button", false);

		// Result遷移待ち中に生成・発射が進むと、遷移失敗時に敵や弾だけが増え続ける。
		// ここではWaterRailShooter専用の戦闘系だけを止め、Engineの汎用挙動は変えない。
		SetComponentActiveForAll("WaveSpawner", false);
		SetComponentActiveForAll("EncounterController", false);
		SetComponentActiveForAll("ProjectileEmitter", false);
		SetComponentActiveForAll("WeaponGroup", false);

		ReleaseOrDeactivateAllWithComponent("ProjectileDetonator");
		ReleaseOrDeactivateAllWithComponent("TargetSteering");
	}

	GameObject GetActionTargetGameObject(const EditorScriptInputActionContext& inputContext) {
		if (inputContext.payloadType != EditorScriptActionPayloadTypeGameObject ||
			inputContext.payloadGameObjectId < 0) {
			return GameObject{};
		}

		return GameObject{inputContext.payloadGameObjectId};
	}

	void RetireDestroyedEnemy(const EditorScriptInputActionContext& inputContext) {
		const GameObject enemy = GetActionTargetGameObject(inputContext);

		if (!enemy.HasReference()) {
			return;
		}

		// WaveSpawnerはPool返却を「撃破済み」として扱う。
		// 撃破後にActiveのまま残すと、描画・物理・ターゲット候補に残り続け、
		// 終盤の重さや射線詰まりの原因になる。
		if (!ObjectPool::Release(enemy)) {
			enemy.SetActive(false);
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

	const char* GetResultScenePath(bool isClear) {
		return isClear ? kClearResultScenePath : kGameOverResultScenePath;
	}

	bool TryLoadResultScene(bool isClear) {
		// 同期LoadSceneは失敗時復帰のために現在のGameplay Sceneを丸ごとコピーする。
		// 終盤はPool生成物が多く、このコピーがResult遷移の体感待ち時間になるため、
		// Resultだけは非同期読込要求にして重いコピーを避ける。
		// CLEARとGAME OVERはScene自体を分け、Result Scene内のText Active切替へ依存しない。
		const char* resultScenePath = GetResultScenePath(isClear);
		const bool loadOk = SceneManager::LoadSceneAsync(std::string{resultScenePath});
		Log(std::string("DEBUG_SCENE_ROLE: LoadSceneAsync(") + resultScenePath + ") result=" + (loadOk ? "true" : "false"));
		return loadOk;
	}

	bool ApplyResultSceneUi(int32_t& destroyedEnemyCount) {
		float isClearValue = 0.0f;
		float destroyedEnemyCountValue = 0.0f;
		SceneManager::GetFloat(kResultIsClearKey, isClearValue);
		SceneManager::GetFloat(kResultEnemyCountKey, destroyedEnemyCountValue);
		const bool isClear = isClearValue > 0.5f;
		destroyedEnemyCount = static_cast<int32_t>(destroyedEnemyCountValue);

		// CLEAR / GAME OVER はScene自体を分けて初期Activeを固定する。
		// ここでは親Canvasだけ復旧し、Textの出し分けはSceneデータへ任せる。
		SetActive("Canvas", true);
		return isClear;
	}

	// 死亡・クリアのどちらでもここを通し、結果をScene間永続値へ記録してから
	// Result Sceneへ即時遷移する。失敗時だけUpdateで短い間隔の再試行を続ける。
	void BeginResultTransition(bool isClear) {
		if (sharedGameState.isResultTransitionPending) {
			return;
		}

		SceneManager::SetFloat(kResultIsClearKey, isClear ? 1.0f : 0.0f);
		SceneManager::SetFloat(
			kResultEnemyCountKey,
			static_cast<float>(sharedGameState.smallBoatDestroyedCount + sharedGameState.missileBoatDestroyedCount));
		StopGameplayForResultTransition();
		sharedGameState.isResultTransitionPending = true;
		sharedGameState.resultTransitionRemainingSeconds = kResultRetrySeconds;
		Log(isClear ? "RESULT: MISSION CLEAR -> Result Scene" : "RESULT: MISSION FAILED -> Result Scene");

		if (TryLoadResultScene(isClear)) {
			sharedGameState.isResultTransitionPending = false;
		}
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
		const GameObject& aimTarget = sharedGameState.playerAimTarget;

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

	// AUDIO 配下の AudioSource を名前で鳴らす。assetPath が空のスロットは
	// EditorAudioManager 側で何もせず戻るため、音声ファイル未設定でも安全に呼べる。
	void PlaySfx(const GameObject& audioObject) {
		if (audioObject.HasReference()) {
			Audio{audioObject}.Play();
		}
	}

	void PlaySfx(const char* audioObjectName) {
		PlaySfx(Find(audioObjectName));
	}

	void PlayCachedSfx(GameObject& cachedAudioObject, const char* audioObjectName) {
		if (!cachedAudioObject.HasReference()) {
			cachedAudioObject = Find(audioObjectName);
		}

		PlaySfx(cachedAudioObject);
	}

	// 装備中の武器に対応する発砲音を鳴らす。
	void PlayEquippedWeaponFireSfx() {
		switch (sharedGameState.equippedWeaponSlot) {
		case kWeapon40mmSlot: PlaySfx("SFX Fire 40mm"); break;
		case kRocketSlot:     PlaySfx("SFX Fire Rocket"); break;
		case kMissileSlot:    PlaySfx("SFX Fire Missile"); break;
		default:              PlaySfx("SFX Fire 20mm"); break;
		}
	}

	void AddSalvage(float salvageAmount) {
		GameObject salvageCounter = sharedGameState.salvageCounter;

		if (!salvageCounter.HasReference()) {
			salvageCounter = Find("SALVAGE Counter");
			sharedGameState.salvageCounter = salvageCounter;
		}

		if (!salvageCounter.HasReference()) {
			return;
		}

		GenericCounter counter{salvageCounter};
		counter.Add(salvageAmount);
		float currentSalvage = 0.0f;
		counter.Get(currentSalvage);
		Log("SALVAGE: " + std::to_string(static_cast<int32_t>(currentSalvage)));
	}

	void EquipWeapon(int32_t slotIndex) {
		if (slotIndex < 0 || slotIndex >= static_cast<int32_t>(sharedGameState.ownedWeapons.size()) ||
			!sharedGameState.ownedWeapons[static_cast<size_t>(slotIndex)]) {
			Log("LOADOUT: 未購入の武器です");
			return;
		}

		const GameObject playerShip = Find("PlayerShip");

		// PlayerShipが見つからない場合は選択だけを記録する(通常のゲームプレイでは起きない)。
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

	// ボスは廃止。Railが終端へ到達した時点でMission Clearにする(OnRailCompleted参照)。
	void OnRailCompleted() {
		if (sharedGameState.isMissionClear) {
			return;
		}

		sharedGameState.isMissionClear = true;
		PlayCachedSfx(sharedGameState.sfxExplosionLarge, "SFX Explosion Large");
		const GameObject mainBgm = Find("BGM Main");

		if (mainBgm.HasReference()) {
			Audio{mainBgm}.Stop();
		}

		RailFollower{Find("PlayerShip")}.Pause();
		AddSalvage(500.0f);
		PlaySfx("SFX Mission Clear");
		SetState("Clear");
		ObjectiveTracker{Find("StageController")}.Set("Clear", ObjectiveState::Completed, 1.0f);
		Log("MISSION CLEAR (RAIL COMPLETE)");
		BeginResultTransition(true);
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
	BindAction("OnGunBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnGunBoatDestroyed(context); });
	BindAction("OnHighSpeedBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnHighSpeedBoatDestroyed(context); });
	BindAction("OnBattleACompleted", [this](const EditorScriptInputActionContext& context) { OnBattleACompleted(context); });
	BindAction("OnBattleBCompleted", [this](const EditorScriptInputActionContext& context) { OnBattleBCompleted(context); });
	BindAction("OnMidRushCompleted", [this](const EditorScriptInputActionContext& context) { OnMidRushCompleted(context); });
	BindAction("OnMaxRushCompleted", [this](const EditorScriptInputActionContext& context) { OnMaxRushCompleted(context); });
	BindAction("OnPlayerDestroyed", [this](const EditorScriptInputActionContext& context) { OnPlayerDestroyed(context); });
	BindAction("OnResult", [this](const EditorScriptInputActionContext& context) { OnResult(context); });
	BindAction("OnRestart", [this](const EditorScriptInputActionContext& context) { OnRestart(context); });
	BindAction("OnLoadoutChanged", [this](const EditorScriptInputActionContext& context) { OnLoadoutChanged(context); });
	BindAction("OnObjectiveChanged", [this](const EditorScriptInputActionContext& context) { OnObjectiveChanged(context); });
}

void WaterRailShooter0817::Start(int32_t gameObjectId) {
	const GameObject titleController = Find(kTitleControllerName);

	if (titleController.HasReference() && gameObjectId == titleController.GetInstanceId()) {
		hasResolvedSceneRole_ = true;
		isTitleController_ = true;
		isResultController_ = false;
		// 次のプレイのために、前回の結果待ち状態を必ず捨てる。
		sharedGameState = SharedGameState();
		Log("TITLE: PRESS SPACE TO START");
		return;
	}

	const GameObject resultController = Find(kResultControllerName);

	if (resultController.HasReference() && gameObjectId == resultController.GetInstanceId()) {
		hasResolvedSceneRole_ = true;
		isTitleController_ = false;
		isResultController_ = true;
		int32_t destroyedEnemyCount = 0;
		const bool isClear = ApplyResultSceneUi(destroyedEnemyCount);
		Log(std::string("RESULT: ") + (isClear ? "MISSION CLEAR" : "MISSION FAILED") +
			" / 撃破数 " + std::to_string(destroyedEnemyCount) +
			" / PRESS SPACE TO RETURN TO TITLE");
		return;
	}

	const GameObject stageController = Find("StageController");
	cachedStageController_ = stageController;
	cachedPlayerShip_ = Find("PlayerShip");

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId()) {
		hasResolvedSceneRole_ = true;
		isTitleController_ = false;
		isResultController_ = false;
		return;
	}

	sharedGameState = SharedGameState();
	sharedGameState.isInitialized = true;
	sharedGameState.stageController = stageController;
	sharedGameState.playerAimTarget = Find("PlayerAimTarget");
	sharedGameState.explosionSmall = Find("FX Explosion Small");
	sharedGameState.explosionLarge = Find("FX Explosion Large");
	sharedGameState.sfxExplosionSmall = Find("SFX Explosion Small");
	sharedGameState.sfxExplosionLarge = Find("SFX Explosion Large");
	sharedGameState.salvageCounter = Find("SALVAGE Counter");

	SetActive("MISSION CLEAR Text", false);
	SetActive("RESULT Button", false);
	SetActive("MISSION FAILED Text", false);
	SetActive("RESTART Button", false);

	EquipWeapon(kWeapon20mmSlot);
	SetState("Approach");
	Log("WaterRailShooter0817: START");
}

void WaterRailShooter0817::Update(int32_t gameObjectId, float deltaTime) {
	// 非同期Scene遷移後に古い役割判定が残ると、ResultControllerなのに通常Gameplayとして扱われ、
	// CLEAR / GAME OVER のTextをONにできない。現在SceneにResultControllerがいる場合は毎フレーム補正する。
	const GameObject currentResultController = Find(kResultControllerName);
	if (currentResultController.HasReference() && gameObjectId == currentResultController.GetInstanceId()) {
		hasResolvedSceneRole_ = true;
		isTitleController_ = false;
		isResultController_ = true;
	}

	// StartでScene役割を解決済み。旧SceneでStartが呼ばれなかった場合だけ、従来の
	// 初回判定を残して安全に復旧する。
	if (!hasResolvedSceneRole_) {
		hasResolvedSceneRole_ = true;
		const GameObject titleController = Find(kTitleControllerName);
		isTitleController_ = titleController.HasReference() && gameObjectId == titleController.GetInstanceId();
		const GameObject resultController = Find(kResultControllerName);
		isResultController_ = resultController.HasReference() && gameObjectId == resultController.GetInstanceId();
		// DEBUG_SCENE_ROLE: 2回目以降のPlayでSceneRoleが正しく再判定されているかを確認するための一時ログ。
		Log("DEBUG_SCENE_ROLE: gameObjectId=" + std::to_string(gameObjectId) +
			" isTitleController=" + std::to_string(isTitleController_) +
			" isResultController=" + std::to_string(isResultController_) +
			" titleFound=" + std::to_string(titleController.HasReference()) +
			" resultFound=" + std::to_string(resultController.HasReference()));
	}

	// Title Scene: Spaceでゲーム本編へ。
	if (isTitleController_) {
		if (Input::GetKeyDown(KeyCode::Space)) {
			Log("TITLE: START GAME");
			const bool loadOk = SceneManager::LoadScene(kGameplayScenePath);
			// DEBUG_SCENE_ROLE: LoadSceneの戻り値そのものを見て、要求が拒否されているのか
			// それとも別の理由(Sceneは切り替わったが表示/入力側が更新されない等)かを切り分ける。
			Log(std::string("DEBUG_SCENE_ROLE: LoadScene(Gameplay) result=") + (loadOk ? "true" : "false"));
		}

		return;
	}

	// Result Scene: Spaceでタイトルへ戻る。
	if (isResultController_) {
		int32_t unusedDestroyedEnemyCount = 0;
		ApplyResultSceneUi(unusedDestroyedEnemyCount);

		if (Input::GetKeyDown(KeyCode::Space)) {
			Log("RESULT: BACK TO TITLE");
			const bool loadOk = SceneManager::LoadScene(kTitleScenePath);
			Log(std::string("DEBUG_SCENE_ROLE: LoadScene(Title) result=") + (loadOk ? "true" : "false"));
		}

		return;
	}

	const GameObject& playerShip = cachedPlayerShip_;
	const GameObject& stageController = cachedStageController_;

	if (playerShip.HasReference() && gameObjectId == playerShip.GetInstanceId()) {
		if (!sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
			UpdateAimTarget(playerShip);
		}

		if (isPlayerFireHeld_ && !sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
			// Fire() は連射入力中は毎フレーム呼ばれ、実際に発砲したかは戻り値から判別できない。
			// 装弾数が減った瞬間だけを「1発出た」と見なすことで、武器ごとの発射間隔に
			// そのまま同期した発砲音になる(20mmの0.09s連射でも二重に鳴らない)。
			int32_t ammoBeforeFire = 0;
			int32_t reserveBeforeFire = 0;
			const bool hasAmmoBeforeFire =
				WeaponLoadout{playerShip}.GetAmmo(ammoBeforeFire, reserveBeforeFire);
			WeaponLoadout{playerShip}.Fire();
			int32_t ammoAfterFire = 0;
			int32_t reserveAfterFire = 0;

			if (hasAmmoBeforeFire &&
				WeaponLoadout{playerShip}.GetAmmo(ammoAfterFire, reserveAfterFire) &&
				ammoAfterFire < ammoBeforeFire) {
				PlayEquippedWeaponFireSfx();
			}
		}

		return;
	}

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId() ||
		!sharedGameState.isInitialized) {
		return;
	}

	UpdateEnemyDestroyedEffects(deltaTime);

	// 死亡・クリア後はGameplay内UIを出さず、Result SceneへのLoadだけを再試行する。
	if (sharedGameState.isResultTransitionPending) {
		sharedGameState.resultTransitionRemainingSeconds -= deltaTime;

		if (sharedGameState.resultTransitionRemainingSeconds <= 0.0f) {
			if (TryLoadResultScene(sharedGameState.isMissionClear)) {
				sharedGameState.isResultTransitionPending = false;
			}
			else {
				// Clear/Failed後はPlayer入力を止めるため、ここで一度遷移に失敗すると
				// 「Clear判定後に撃てないがResultへ行かない」状態になる。
				// SceneManager側が一時的にLoadSceneを受け付けない場合に備え、短い間隔で再試行する。
				sharedGameState.resultTransitionRemainingSeconds = kResultRetrySeconds;
			}
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

	// Checkpointは廃止(Shop往復のための仕組みだった)。Railは各Encounterのクリアを待たず、
	// 進行率だけで最後まで流れ続ける。MidRush/MaxRushEncounter完了フラグ自体は
	// (今後何かに使う可能性があるため)sharedGameStateへ残すが、Railを止める用途では使わない。
	if (!sharedGameState.isStormStarted && railProgress >= kStormProgress) {
		sharedGameState.isStormStarted = true;
		SetState("Storm");
		Log("STORM SECTION");
	}

	// E13..E22
	if (!sharedGameState.isEvent13Started && railProgress >= kEvent13Progress) {
		sharedGameState.isEvent13Started = true;
		EncounterController{Find("E13 Encounter")}.Start();
		Log("EVENT 13: START");
	}

	if (!sharedGameState.isEvent14Started && railProgress >= kEvent14Progress) {
		sharedGameState.isEvent14Started = true;
		EncounterController{Find("E14 Encounter")}.Start();
		Log("EVENT 14: START");
	}

	if (!sharedGameState.isEvent15Started && railProgress >= kEvent15Progress) {
		sharedGameState.isEvent15Started = true;
		EncounterController{Find("E15 Encounter")}.Start();
		Log("EVENT 15: START");
	}

	if (!sharedGameState.isEvent16Started && railProgress >= kEvent16Progress) {
		sharedGameState.isEvent16Started = true;
		EncounterController{Find("E16 Encounter")}.Start();
		Log("EVENT 16: START");
	}

	if (!sharedGameState.isEvent17Started && railProgress >= kEvent17Progress) {
		sharedGameState.isEvent17Started = true;
		EncounterController{Find("E17 Encounter")}.Start();
		Log("EVENT 17: START");
	}

	if (!sharedGameState.isEvent18Started && railProgress >= kEvent18Progress) {
		sharedGameState.isEvent18Started = true;
		EncounterController{Find("E18 Encounter")}.Start();
		Log("EVENT 18: START");
	}

	if (!sharedGameState.isEvent19Started && railProgress >= kEvent19Progress) {
		sharedGameState.isEvent19Started = true;
		EncounterController{Find("E19 Encounter")}.Start();
		Log("EVENT 19: START");
	}

	if (!sharedGameState.isEvent20Started && railProgress >= kEvent20Progress) {
		sharedGameState.isEvent20Started = true;
		EncounterController{Find("E20 Encounter")}.Start();
		Log("EVENT 20: START");
	}

	if (!sharedGameState.isEvent21Started && railProgress >= kEvent21Progress) {
		sharedGameState.isEvent21Started = true;
		SetState("BattleB");
		ObjectiveTracker{stageController}.Set("BattleB", ObjectiveState::Active, 0.0f);
		EncounterController{Find("E21 Encounter")}.Start();
		Log("EVENT 21: START (MaxRushEncounter)");
	}

	if (!sharedGameState.isEvent22Started && railProgress >= kEvent22Progress) {
		sharedGameState.isEvent22Started = true;
		EncounterController{Find("E22 Encounter")}.Start();
		Log("EVENT 22: START");
	}

	// ボスは廃止。Railが終端(進行率1.0)へ到達したらMission Clearにする。
	if (!sharedGameState.isMissionClear && railProgress >= kRailCompleteProgress) {
		OnRailCompleted();
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
		if (WeaponLoadout{GameObject{inputContext.gameObjectId}}.Reload()) {
			PlaySfx("SFX Reload");
		}
	}
}

void WaterRailShooter0817::OnNextWeapon(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		EquipNextOwnedWeapon();
		PlaySfx("SFX Weapon Switch");
	}
}

void WaterRailShooter0817::OnEnemyFire(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext) && !sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
		Log("DEBUG_FIRE: OnEnemyFire received for gameObjectId=" + std::to_string(inputContext.gameObjectId));

		// 実際に発射できた時だけ鳴らす(Cooldown中や対象不在では鳴らさない)。
		if (Weapon{GameObject{inputContext.gameObjectId}}.FireProjectile()) {
			PlaySfx("SFX Enemy Fire");
		}
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
	PlayEnemyDestroyedEffect(inputContext, false);
	RetireDestroyedEnemy(inputContext);
	sharedGameState.smallBoatDestroyedCount++;
	PlayCachedSfx(sharedGameState.sfxExplosionSmall, "SFX Explosion Small");
	AddSalvage(20.0f);

	if (sharedGameState.isBattleBStarted) {
		sharedGameState.battleBDestroyedCount++;
	}

	ObjectiveTracker{sharedGameState.stageController}.Set(
		sharedGameState.isBattleBStarted ? "BattleB" : "BattleA",
		ObjectiveState::Active,
		static_cast<float>(sharedGameState.isBattleBStarted
			? sharedGameState.battleBDestroyedCount
			: sharedGameState.smallBoatDestroyedCount));
}

void WaterRailShooter0817::OnMissileBoatDestroyed(const EditorScriptInputActionContext& inputContext) {
	PlayEnemyDestroyedEffect(inputContext, true);
	RetireDestroyedEnemy(inputContext);
	sharedGameState.missileBoatDestroyedCount++;
	sharedGameState.battleBDestroyedCount++;
	PlayCachedSfx(sharedGameState.sfxExplosionSmall, "SFX Explosion Small");
	AddSalvage(40.0f);
	ObjectiveTracker{sharedGameState.stageController}.Set(
		"BattleB",
		ObjectiveState::Active,
		static_cast<float>(sharedGameState.battleBDestroyedCount));
}

// 機関砲艇 / 高速艇はテンプレート側で死亡Action名を設定済みだが、
// これまでハンドラが無く撃破しても何も起きなかった。Salvageと撃破音をここで処理する。
void WaterRailShooter0817::OnGunBoatDestroyed(const EditorScriptInputActionContext& inputContext) {
	PlayEnemyDestroyedEffect(inputContext, false);
	RetireDestroyedEnemy(inputContext);
	sharedGameState.smallBoatDestroyedCount++;
	PlayCachedSfx(sharedGameState.sfxExplosionSmall, "SFX Explosion Small");
	AddSalvage(35.0f);
}

void WaterRailShooter0817::OnHighSpeedBoatDestroyed(const EditorScriptInputActionContext& inputContext) {
	PlayEnemyDestroyedEffect(inputContext, false);
	RetireDestroyedEnemy(inputContext);
	sharedGameState.smallBoatDestroyedCount++;
	PlayCachedSfx(sharedGameState.sfxExplosionSmall, "SFX Explosion Small");
	AddSalvage(15.0f);
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
// Loadout
//================================================================

//================================================================
// Player death / Boss phases / Result
//================================================================

void WaterRailShooter0817::OnPlayerDestroyed(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
	sharedGameState.isPlayerDestroyed = true;
	PlayCachedSfx(sharedGameState.sfxExplosionLarge, "SFX Explosion Large");
	RailFollower{Find("PlayerShip")}.Pause();
	PlaySfx("SFX Mission Failed");
	SetState("Failed");
	Log("PLAYER: DESTROYED");
	BeginResultTransition(false);
}

void WaterRailShooter0817::OnResult(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		SceneManager::LoadSceneAsync(std::string{GetResultScenePath(sharedGameState.isMissionClear)});
	}
}

void WaterRailShooter0817::OnRestart(const EditorScriptInputActionContext& inputContext) {
	if (IsPerformed(inputContext)) {
		SceneManager::LoadSceneAsync(std::string{kGameplayScenePath});
	}
}

void WaterRailShooter0817::OnLoadoutChanged(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
}

void WaterRailShooter0817::OnObjectiveChanged(const EditorScriptInputActionContext& inputContext) {
	(void)inputContext;
}
