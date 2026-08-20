#pragma once

#include "EditorNativeScript.h"

//================================================================
// WaterRailShooter0817 - 8月17日仕様を接続するゲーム側Component
//================================================================

class WaterRailShooter0817 final : public EditorNativeScript {
public:
	WaterRailShooter0817();

	void Start(int32_t gameObjectId) override;
	void Update(int32_t gameObjectId, float deltaTime) override;
	void Stop(int32_t gameObjectId) override;

private:
	bool isPlayerFireHeld_ = false;
	// GameObject名の検索はScene全体の線形走査なので、このInstanceがTitle/Resultの
	// どちらのControllerかは初回Updateで一度だけ判定して保持する。
	bool hasResolvedSceneRole_ = false;
	bool isTitleController_ = false;
	bool isShopController_ = false;
	bool isResultController_ = false;

	void OnPlayerFire(const EditorScriptInputActionContext& inputContext);
	void OnPlayerReload(const EditorScriptInputActionContext& inputContext);
	void OnNextWeapon(const EditorScriptInputActionContext& inputContext);
	void OnEnemyFire(const EditorScriptInputActionContext& inputContext);
	// TargetSteering が Duration 経過や Chase 到達を通知したときに、敵1体の行動を次段階へ進める。
	void OnEnemyMoveCompleted(const EditorScriptInputActionContext& inputContext);
	void OnSmallBoatDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnMissileBoatDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnBattleACompleted(const EditorScriptInputActionContext& inputContext);
	void OnBattleBCompleted(const EditorScriptInputActionContext& inputContext);
	void OnMidRushCompleted(const EditorScriptInputActionContext& inputContext);
	void OnMaxRushCompleted(const EditorScriptInputActionContext& inputContext);
	void OnBuy40mm(const EditorScriptInputActionContext& inputContext);
	void OnBuyRocket(const EditorScriptInputActionContext& inputContext);
	void OnBuyMissile(const EditorScriptInputActionContext& inputContext);
	void OnEquip20mm(const EditorScriptInputActionContext& inputContext);
	void OnEquip40mm(const EditorScriptInputActionContext& inputContext);
	void OnEquipRocket(const EditorScriptInputActionContext& inputContext);
	void OnEquipMissile(const EditorScriptInputActionContext& inputContext);
	void OnContinue(const EditorScriptInputActionContext& inputContext);
	void OnPlayerDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnBossMainGunDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnBossMissileDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnBossEngineDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnBossPhase1(const EditorScriptInputActionContext& inputContext);
	void OnBossPhase2(const EditorScriptInputActionContext& inputContext);
	void OnBossPhase3(const EditorScriptInputActionContext& inputContext);
	void OnBossDestroyed(const EditorScriptInputActionContext& inputContext);
	void OnResult(const EditorScriptInputActionContext& inputContext);
	void OnRestart(const EditorScriptInputActionContext& inputContext);
	void OnLoadoutChanged(const EditorScriptInputActionContext& inputContext);
	void OnObjectiveChanged(const EditorScriptInputActionContext& inputContext);
};
