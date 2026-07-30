#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRailMovementManager {
public:
	EditorRailMovementManager() = default;  // RuntimeManager が 1 つだけ保持する。
	~EditorRailMovementManager() = default;  // Scene は外部所有なので破棄しない。
	EditorRailMovementManager(const EditorRailMovementManager&) = delete;
	EditorRailMovementManager& operator=(const EditorRailMovementManager&) = delete;
	EditorRailMovementManager(EditorRailMovementManager&&) = delete;
	EditorRailMovementManager& operator=(EditorRailMovementManager&&) = delete;

	void Initialize(EditorScene* editorScene);  // Rail と追従対象を検索する Scene を受け取る。
	void Start();  // Play 開始時に走行距離を初期化する。
	void Update(float deltaTime);  // 子ウェイポイントから曲線を作り、追従対象を進める。
	void Draw();  // Rail の専用デバッグ描画はまだ行わない。
	void Stop();  // Play 終了時に実行状態を破棄する。
	bool GetNormalizedProgress(int32_t gameObjectId, float& normalizedProgress) const;  // 敵 Wave が参照する現在の進行率を返す。

private:
	EditorScene* editorScene_ = nullptr;  // Play 中の Scene。
	std::unordered_map<int32_t, float> traveledDistanceByGameObjectId_;  // 追従対象ごとの現在距離。
	std::unordered_map<int32_t, float> totalDistanceByGameObjectId_;  // 進行率へ変換するレール全長。
	bool isStarted_ = false;  // Play 中だけ true。
};

#pragma warning(pop)
