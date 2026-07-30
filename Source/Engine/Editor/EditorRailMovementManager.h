#pragma once

#include "EditorScene.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorRailRuntimeSample {
	Vector3 position{0.0f, 0.0f, 0.0f};  // Scene 上で評価したワールド座標。
	float distance = 0.0f;  // レール始点からこのサンプルまでの累積距離。
};

struct EditorRailFollowerRuntimeState {
	std::vector<EditorRailRuntimeSample> samples;  // 形状が変わるまで再利用するレールサンプル。
	float traveledDistance = 0.0f;  // 現在のレール上距離。
	float totalDistance = 0.0f;  // キャッシュ済みレール全長。
	float currentSpeed = 0.0f;  // 加減速を適用した現在速度。
	float pendingNormalized = 0.0f;  // サンプル構築前に要求された移動先。
	size_t pathSignature = 0u;  // 制御点の位置と有効状態から作る変更検出値。
	int32_t cachedPathGameObjectId = -1;  // samples を作った Rail Path。
	bool cachedLoop = false;  // samples を作った時のループ設定。
	bool cachedSmoothCurve = true;  // samples を作った時の補間方式。
	bool isDistanceInitialized = false;  // 開始位置を距離へ変換済みなら true。
	bool isPaused = false;  // 外部命令または終端到達で停止中なら true。
	bool isReversed = false;  // Inspector の速度符号とは別に進行方向を反転する。
	bool endReached = false;  // 終端通知が未消費なら true。
	bool hasPendingNormalized = false;  // pendingNormalized を次回の形状構築時に適用する。
};

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
	bool SetPaused(int32_t gameObjectId, bool isPaused);  // ゲームルールから停止と再開を指示する。
	bool IsPaused(int32_t gameObjectId) const;  // 現在の外部停止状態を返す。
	bool SetSpeed(int32_t gameObjectId, float speed);  // Play 中の目標速度を変更する。
	bool SetReverse(int32_t gameObjectId, bool isReversed);  // 速度値を壊さず進行方向だけを切り替える。
	bool SetNormalizedProgress(int32_t gameObjectId, float normalizedProgress);  // レール上の任意位置へ移動する。
	bool SetRailPath(int32_t gameObjectId, int32_t railPathGameObjectId, bool preservesProgress);  // 接続先レールを切り替える。
	bool GetNormalizedProgress(int32_t gameObjectId, float& normalizedProgress) const;  // 現在の進行率を返す。
	bool GetRailLength(int32_t gameObjectId, float& railLength) const;  // 現在参照しているレール全長を返す。
	bool GetRailPosition(int32_t gameObjectId, float normalizedProgress, Vector3& position) const;  // 指定進行率の位置を返す。
	bool GetRailDirection(int32_t gameObjectId, float normalizedProgress, Vector3& direction) const;  // 指定進行率の接線方向を返す。
	bool ConsumeEndReached(int32_t gameObjectId);  // 終端通知を一度だけ取り出す。

private:
	EditorScene* editorScene_ = nullptr;  // Play 中の Scene。
	std::unordered_map<int32_t, EditorRailFollowerRuntimeState> runtimeStates_;  // 追従対象ごとの独立した実行状態。
	bool isStarted_ = false;  // Play 中だけ true。
};

#pragma warning(pop)
