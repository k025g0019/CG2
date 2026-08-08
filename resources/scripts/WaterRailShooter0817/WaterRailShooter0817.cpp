#include "WaterRailShooter0817.h"

#include <array>
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
	constexpr float kBattleAProgress = 0.08f;
	constexpr float kCheckpoint1Progress = 0.30f;
	constexpr float kBattleBProgress = 0.37f;
	constexpr float kStormProgress = 0.52f;
	constexpr float kCheckpoint2Progress = 0.64f;
	constexpr float kBossStartProgress = 0.73f;
	constexpr float kBossStopProgress = 0.80f;
	constexpr const char* kGameplayScenePath = "Assets/Scenes/WaterRailShooter_0817.scene";
	constexpr const char* kResultScenePath = "Assets/Scenes/Result.scene";

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
		int32_t checkpointIndex = 0;
		int32_t equippedWeaponSlot = kWeapon20mmSlot;
		int32_t smallBoatDestroyedCount = 0;
		int32_t missileBoatDestroyedCount = 0;
		int32_t battleBDestroyedCount = 0;
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

		if (!playerShip.HasReference() || !WeaponLoadout{playerShip}.Select(slotIndex)) {
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
		RailFollower{Find("PlayerShip")}.Pause();
		SetShopVisible(true);
		SetState(checkpointIndex == 1 ? "Checkpoint1" : "Checkpoint2");

		const GameObject checkpoint = Find(checkpointIndex == 1 ? "Checkpoint 1" : "Checkpoint 2");

		if (checkpoint.HasReference()) {
			Checkpoint{checkpoint}.Save();
		}

		SaveCheckpointState();
		Log(checkpointIndex == 1 ? "CHECKPOINT 1" : "CHECKPOINT 2");
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
	BindAction("OnSmallBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnSmallBoatDestroyed(context); });
	BindAction("OnMissileBoatDestroyed", [this](const EditorScriptInputActionContext& context) { OnMissileBoatDestroyed(context); });
	BindAction("OnBattleACompleted", [this](const EditorScriptInputActionContext& context) { OnBattleACompleted(context); });
	BindAction("OnBattleBCompleted", [this](const EditorScriptInputActionContext& context) { OnBattleBCompleted(context); });
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
	const GameObject stageController = Find("StageController");

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId()) {
		return;
	}

	sharedGameState = {};
	sharedGameState.isInitialized = true;
	sharedGameState.ownedWeapons[kWeapon20mmSlot] = true;
	SetShopVisible(false);
	SetActive("MISSION CLEAR Text", false);
	SetActive("RESULT Button", false);
	SetActive("MISSION FAILED Text", false);
	SetActive("RESTART Button", false);
	SetActive("HUD Boss HP", false);
	EquipWeapon(kWeapon20mmSlot);
	SetState("Approach");
	Log("WaterRailShooter0817: START");
}

void WaterRailShooter0817::Update(int32_t gameObjectId, float deltaTime) {
	(void)deltaTime;

	const GameObject playerShip = Find("PlayerShip");
	const GameObject stageController = Find("StageController");

	if (playerShip.HasReference() && gameObjectId == playerShip.GetInstanceId()) {
		if (isPlayerFireHeld_ && !sharedGameState.isPlayerDestroyed && !sharedGameState.isMissionClear) {
			WeaponLoadout{playerShip}.Fire();
		}

		return;
	}

	if (!stageController.HasReference() || gameObjectId != stageController.GetInstanceId() ||
		!sharedGameState.isInitialized || sharedGameState.isPlayerDestroyed ||
		sharedGameState.isMissionClear) {
		return;
	}

	float railProgress = 0.0f;

	if (!playerShip.HasReference() || !RailFollower{playerShip}.GetNormalizedProgress(railProgress)) {
		return;
	}

	if (!sharedGameState.isBattleAStarted && railProgress >= kBattleAProgress) {
		sharedGameState.isBattleAStarted = true;
		EncounterController{Find("Battle A Encounter")}.Start();
		SetState("BattleA");
		ObjectiveTracker{stageController}.Set("BattleA", ObjectiveState::Active, 0.0f);
		Log("BATTLE A: START");
	}

	if (!sharedGameState.isCheckpoint1Opened && railProgress >= kCheckpoint1Progress) {
		if (!sharedGameState.isBattleACompleted) {
			RailFollower{playerShip}.Pause();
			return;
		}

		sharedGameState.isCheckpoint1Opened = true;
		OpenCheckpoint(1);
		return;
	}

	if (sharedGameState.checkpointIndex >= 1 && !sharedGameState.isBattleBStarted &&
		railProgress >= kBattleBProgress) {
		sharedGameState.isBattleBStarted = true;
		EncounterController{Find("Battle B Encounter")}.Start();
		SetState("BattleB");
		ObjectiveTracker{stageController}.Set("BattleB", ObjectiveState::Active, 0.0f);
		Log("BATTLE B: START");
	}

	if (!sharedGameState.isStormStarted && railProgress >= kStormProgress) {
		sharedGameState.isStormStarted = true;
		SetState("Storm");
		RuntimeProperty::SetFloat(Find("Ocean"), "Ocean", "WaveHeight", 2.25f);
		RuntimeProperty::SetFloat(Find("Ocean"), "Ocean", "WindSpeed", 26.0f);
		Log("STORM SECTION");
	}

	if (sharedGameState.checkpointIndex == 1 && !sharedGameState.isCheckpoint2Opened &&
		railProgress >= kCheckpoint2Progress) {
		if (!sharedGameState.isBattleBCompleted) {
			RailFollower{playerShip}.Pause();
			return;
		}

		sharedGameState.isCheckpoint2Opened = true;
		OpenCheckpoint(2);
		return;
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
		Weapon{GameObject{inputContext.gameObjectId}}.FireProjectile();
	}
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

	SaveCheckpointState();
	SetShopVisible(false);
	RailFollower{Find("PlayerShip")}.Resume();
	SetState(sharedGameState.checkpointIndex == 1 ? "BattleB" : "BossApproach");
	Log("CHECKPOINT: CONTINUE");
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
	SceneManager::SetFloat(
		"DestroyedEnemyCount",
		static_cast<float>(sharedGameState.smallBoatDestroyedCount + sharedGameState.missileBoatDestroyedCount));
	Log("MISSION CLEAR");
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
