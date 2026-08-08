#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRailMovementManager;
class EditorScriptManager;

class EditorRailBranchManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorScriptManager* scriptManager);  // 分岐設定をレール実行系と任意Script Actionへ接続する
	void Start();  // 進行率の監視状態を初期化する
	void Update();  // 進行率しきい値を横切った分岐を実行する
	void Stop();  // Play停止時に監視状態を破棄する
	bool Trigger(int32_t railBranchGameObjectId);  // ScriptやTimelineから手動分岐を実行する

private:
	struct BranchRuntime {
		float previousProgress = 0.0f;  // 前フレームの進行率
		bool hasPreviousProgress = false;  // 初回監視済みならtrue
		bool hasTriggered = false;  // 一度だけ設定の実行済み状態
	};

	EditorScene* editorScene_ = nullptr;  // RailBranch Componentを検索するScene
	EditorRailMovementManager* railMovementManager_ = nullptr;  // 進行率取得と接続先変更を行うManager
	EditorScriptManager* scriptManager_ = nullptr;  // 分岐後の任意Actionを通知するManager
	std::unordered_map<int32_t, BranchRuntime> branchRuntimes_;  // RailBranch所有GameObjectごとの監視状態
	bool isStarted_ = false;  // Play中だけ自動分岐を評価する

	bool ExecuteBranch(const EditorGameObject& gameObject, const EditorComponent& component);  // 1つの設定を検証して切り替える
	void QueueAction(const EditorGameObject& gameObject, const EditorComponent& component) const;  // 分岐成功を任意Scriptへ通知する
};

#pragma warning(pop)
