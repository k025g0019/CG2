#pragma once

#include "EditorPhysicsManager.h"
#include "EditorScene.h"

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRollingMoveManager {
public:
	EditorRollingMoveManager() = default;  // RuntimeManager が 1 つだけ保持する通常コンストラクタ。
	~EditorRollingMoveManager() = default;  // Scene と Physics は外部所有なのでここでは破棄しない。
	EditorRollingMoveManager(const EditorRollingMoveManager&) = delete;  // 実行状態を二重に持たないためコピー禁止。
	EditorRollingMoveManager& operator=(const EditorRollingMoveManager&) = delete;  // 実行状態を二重に持たないためコピー代入禁止。
	EditorRollingMoveManager(EditorRollingMoveManager&&) = delete;  // Manager の参照先を動かさない。
	EditorRollingMoveManager& operator=(EditorRollingMoveManager&&) = delete;  // Manager の参照先を動かさない。

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager);  // 更新対象 Scene と Rigidbody 反映先を受け取る。
	void Start();  // Play 開始時に更新有効へ切り替える。
	void Update(float deltaTime);  // ローリング移動 Component を持つ GameObject を進める。
	void Draw();  // デバッグ描画はまだない。
	void Stop();  // Play 停止時に更新を止める。

private:
	EditorScene* editorScene_ = nullptr;  // ローリング移動 Component を検索する対象 Scene。
	EditorPhysicsManager* physicsManager_ = nullptr;  // Dynamic Rigidbody の速度反映先。
	bool isStarted_ = false;  // Play 中に更新してよい状態なら true。
};

#pragma warning(pop)
