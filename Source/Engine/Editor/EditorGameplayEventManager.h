#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRailMovementManager;
class EditorScriptManager;

class EditorGameplayEventManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorScriptManager* scriptManager);  // 汎用イベントの値SourceとScript通知先を接続する。
	void Start();  // TimelineとThresholdの実行状態を初期化する。
	void Update(float deltaTime);  // 条件だけを評価し、任意名のScript Actionを通知する。
	void Stop();  // Play中のイベント状態を破棄する。

private:
	struct TimelineRuntime {
		float elapsedTime = 0.0f;
		bool wasConditionMet = false;
		bool hasTriggered = false;
	};

	struct ThresholdRuntime {
		int32_t currentState = -1;
	};

	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	std::unordered_map<int32_t, TimelineRuntime> timelineRuntimes_;
	std::unordered_map<int32_t, ThresholdRuntime> thresholdRuntimes_;
	bool isStarted_ = false;

	void UpdateTimelineEvents(float deltaTime);  // 時間またはRail進行率の立ち上がりを通知する。
	void UpdateThresholdStates();  // Health比率またはRail進行率のState変更を通知する。
	bool ReadThresholdValue(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		float& value,
		bool& isDescending) const;  // Source値と境界方向を取得する。
	void QueueAction(
		const EditorGameObject& ownerGameObject,
		int32_t targetGameObjectId,
		const std::string& actionName,
		float value) const;  // ゲーム固有処理を行わず名前付きActionだけを送る。
};

#pragma warning(pop)
