#pragma once

#include "EditorInputManager.h"
#include "EditorPhysicsManager.h"
#include "EditorScene.h"
#include "EditorScriptManager.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRuntimeManager {
public:
	EditorRuntimeManager() = default;  // 共有状態として 1 つだけ保持する
	~EditorRuntimeManager() = default;  // 所有 Manager の破棄に任せる
	EditorRuntimeManager(const EditorRuntimeManager&) = delete;  // Play 状態と Jolt World を二重所有しないためコピー禁止
	EditorRuntimeManager& operator=(const EditorRuntimeManager&) = delete;  // Play 状態と Jolt World を二重所有しないためコピー代入禁止
	EditorRuntimeManager(EditorRuntimeManager&&) = delete;  // 共有状態の所有先を動かさない
	EditorRuntimeManager& operator=(EditorRuntimeManager&&) = delete;  // 共有状態の所有先を動かさない

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // Play 実行時に操作する Scene と Console 出力先を受け取る
	void Update(const uint8_t* keyState, float deltaTime);  // Play 中だけ Input と Physics を進める
	void Draw();  // Play 中のデバッグ描画を呼ぶ
	void TogglePlay();  // Play / Stop を切り替える
	bool IsPlaying() const;  // 現在 Play 中かを返す

private:
	EditorScene* editorScene_ = nullptr;  // Play 実行対象の Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // Play 中の物理 / Script ログを出す Console
	EditorScene sceneBackup_;  // Stop 時に編集前状態へ戻すための Scene バックアップ
	EditorScriptManager scriptManager_;  // Script / MonoBehaviour Component の実行入口
	EditorInputManager inputManager_;  // Input Component の実行担当
	EditorPhysicsManager physicsManager_;  // RigidBody / Collider の実行担当
	bool isPlaying_ = false;  // Play 中なら true
	bool hasSceneBackup_ = false;  // sceneBackup_ が有効なら true
};

#pragma warning(pop)
