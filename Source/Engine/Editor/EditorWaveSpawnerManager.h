#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorRailMovementManager;
class EditorScriptManager;

class EditorWaveSpawnerManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorPhysicsManager* physicsManager,
		EditorScriptManager* scriptManager);  // 汎用WaveのScene、開始条件、任意Script通知を接続する。
	void Start();  // 子GameObjectを待機状態へ移す。
	void Update(float deltaTime);  // 条件成立後、子GameObjectだけを順次有効化する。
	void Stop();  // Play中の進行状態を破棄する。

private:
	struct WaveRuntime {
		std::vector<int32_t> childGameObjectIds;
		int32_t nextChildIndex = 0;
		float spawnTimer = 0.0f;
		bool hasTriggered = false;
		bool hasCompleted = false;
	};

	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	std::unordered_map<int32_t, WaveRuntime> waveRuntimes_;
	std::unordered_map<int32_t, bool> originalActiveStates_;  // 待機前の子階層Active状態をSpawn時に復元する
	bool isStarted_ = false;

	bool IsTriggerSatisfied(const EditorComponent& component) const;  // Play開始またはRail進行率だけを判定する。
	void SetRuntimeActiveRecursive(int32_t gameObjectId, bool isActive);  // 子階層の描画とPhysicsを同じ状態へ切り替える。
	int32_t SpawnNextChild(WaveRuntime& waveRuntime);  // 次の子を1体だけ有効化し、対象IDを返す。
	void QueueAction(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		const std::string& actionName,
		float value) const;  // Wave固有ルールを持たず、登録されたScript Actionだけを通知する。
	void CompleteWaveIfNeeded(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		WaveRuntime& waveRuntime);  // 全子生成後の完了Actionを一度だけ通知する。
};

#pragma warning(pop)
