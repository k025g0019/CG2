#pragma once

#include "EditorPhysicsManager.h"
#include "EditorScene.h"

#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorLocalMoveManager {
public:
	EditorLocalMoveManager() = default;  // RuntimeManager が 1 つだけ保持する通常コンストラクタ。
	~EditorLocalMoveManager() = default;  // Scene と Physics は外部所有なのでここでは破棄しない。
	EditorLocalMoveManager(const EditorLocalMoveManager&) = delete;  // 実行状態を二重に持たないためコピー禁止。
	EditorLocalMoveManager& operator=(const EditorLocalMoveManager&) = delete;  // 実行状態を二重に持たないためコピー代入禁止。
	EditorLocalMoveManager(EditorLocalMoveManager&&) = delete;  // Manager の参照先を動かさない。
	EditorLocalMoveManager& operator=(EditorLocalMoveManager&&) = delete;  // Manager の参照先を動かさない。

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager);  // 更新対象 Scene と Rigidbody 反映先を受け取る。
	void Start();  // Play 開始時の入口。現状は開始フラグだけを更新する。
	void Update(float deltaTime);  // ローカル移動 Component を持つ GameObject を進める。
	void Draw();  // デバッグ描画はまだない。
	void Stop();  // Play 停止時の入口。現状は開始フラグだけを戻す。

private:
	EditorScene* editorScene_ = nullptr;  // ローカル移動 Component を検索する対象 Scene。
	EditorPhysicsManager* physicsManager_ = nullptr;  // Dynamic Rigidbody の速度反映先。
	bool isStarted_ = false;  // Play 中に更新してよい状態なら true。
};

#pragma warning(pop)
