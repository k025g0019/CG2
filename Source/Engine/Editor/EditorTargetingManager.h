#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorInputManager;
class EditorPhysicsManager;
class EditorScriptManager;

class EditorTargetingManager {
public:
	struct AimRay {
		Vector3 origin{0.0f, 0.0f, 0.0f};  // Game View Cameraから復元したWorld始点
		Vector3 direction{0.0f, 0.0f, 1.0f};  // 正規化済みWorld方向
	};

	void Initialize(
		EditorScene* editorScene,
		EditorInputManager* inputManager,
		EditorPhysicsManager* physicsManager,
		EditorScriptManager* scriptManager);  // 照準・Target選択・追従に必要な実行系を接続する
	void Start();  // Scene保存値をPlay開始時の照準位置として使う
	void Update(float deltaTime);  // マウスまたはVector2 Actionから照準位置を更新する
	void Stop();  // Runtime参照を停止状態へ戻す
	bool GetAimRay(int32_t screenAimGameObjectId, AimRay& aimRay) const;  // ScreenAimまたは中央位置からWorld Rayを作る
	bool ViewportPointToRay(const EditorScriptVector2& normalizedPosition, AimRay& aimRay) const;  // 0～1のGame View座標をWorld Rayへ変換する
	bool GetNormalizedPosition(int32_t screenAimGameObjectId, EditorScriptVector2& normalizedPosition) const;  // UIやScriptから現在照準位置を読む
	bool GetCurrentTarget(int32_t targetSelectorGameObjectId, int32_t& targetGameObjectId) const;  // Selectorが選択した現在Targetを返す
	bool GetCandidateTargets(
		int32_t targetSelectorGameObjectId,
		int32_t maximumCount,
		std::vector<int32_t>& targetGameObjectIds) const;  // Selector条件を満たす候補を優先順で返す
	bool SetExplicitTarget(int32_t targetSelectorGameObjectId, int32_t targetGameObjectId);  // ScriptからSelectorのTargetを明示設定する
	bool SolveIntercept(const Vector3& origin, int32_t targetGameObjectId, float projectileSpeed, float maximumTime, Vector3& position, float& time) const;
	bool GetInterceptPrediction(int32_t ownerGameObjectId, Vector3& position, float& time) const;
	bool GetBallisticPrediction(int32_t ownerGameObjectId, Vector3& launchDirection, Vector3& impactPosition, float& flightTime) const;  // 重力・抗力込みの発射方向と着弾予測を返す
	bool GetBallisticTrajectoryPoint(int32_t ownerGameObjectId, int32_t pointIndex, Vector3& point) const;  // 描画やScriptから軌道点を読む

private:
	EditorScene* editorScene_ = nullptr;  // ScreenAimとReticle UIを検索するScene
	EditorInputManager* inputManager_ = nullptr;  // Gamepad等のVector2 Actionを読む入力Manager
	EditorPhysicsManager* physicsManager_ = nullptr;  // 遮蔽RaycastとRigidbody追従へ使う
	EditorScriptManager* scriptManager_ = nullptr;  // Target変更Actionを通知する
	bool isStarted_ = false;  // Play中だけ入力を反映する
	std::unordered_map<int32_t, float> steeringElapsedSeconds_;  // TargetSteeringごとの開始Delay経過時間
	std::unordered_map<int32_t, float> steeringSpeeds_;  // Transform追従時の現在速度
	std::unordered_map<int32_t, int32_t> explicitTargets_;  // Scriptが明示したSelector Target
	std::unordered_map<int32_t, float> screenAimInputStrengths_;  // AimAssistが生入力を奪わないための0～1入力強度
	std::unordered_map<int32_t, float> targetSelectorUpdateRemainingSeconds_;  // AI Selectorの次回探索までの秒数

	void UpdateReticleUi(const EditorComponent& screenAimComponent);  // 参照先RectTransformを照準位置へ移動する
	void UpdateTargetSelectors(float deltaTime, bool forceUpdate);  // 距離・角度・遮蔽・選択方式から現在Targetを更新する
	void UpdateAimAssist(float deltaTime);  // 生入力後のScreenAimへ範囲付きTarget補正を合成する
	void UpdateInterceptPredictions();  // 非誘導弾の解析迎撃位置をComponent Runtime値へ更新する
	void UpdateBallisticPredictions();  // 重力・抗力・Target加速度を含む弾道を固定刻みで予測する
	void UpdateTargetSteering(float deltaTime);  // Targetへ旋回しながらTransformまたはRigidbodyを前進させる
};

#pragma warning(pop)
