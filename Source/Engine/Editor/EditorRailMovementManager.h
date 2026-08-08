#pragma once

#include "EditorScene.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorInputManager;
class EditorScriptManager;

struct EditorRailRuntimeSample {
	Vector3 position{0.0f, 0.0f, 0.0f};  // Scene 上で評価したワールド座標。
	float distance = 0.0f;  // レール始点からこのサンプルまでの累積距離。
};

struct EditorRailFollowerRuntimeState {
	std::vector<EditorRailRuntimeSample> samples;  // 形状が変わるまで再利用するレールサンプル。
	float traveledDistance = 0.0f;  // 現在のレール上距離。
	float totalDistance = 0.0f;  // キャッシュ済みレール全長。
	float currentSpeed = 0.0f;  // 加減速を適用した現在速度。
	float targetSpeedMultiplier = 1.0f;  // Speed ProfileとZoneを合成した現在倍率。
	float pendingNormalized = 0.0f;  // サンプル構築前に要求された移動先。
	EditorScriptVector2 currentOffset{0.0f, 0.0f};  // レール右・上方向の現在オフセット。
	EditorScriptVector2 moveInput{0.0f, 0.0f};  // Script または PlayerInput から受けた今フレームの移動入力。
	EditorScriptVector2 modifierInputOffset{0.0f, 0.0f};  // MovementModifierの入力から作る右・上Offset。
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

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, EditorInputManager* inputManager, EditorScriptManager* scriptManager);  // Rail 対象 Scene と物理・入力・Zone Action通知先を受け取る。
	void Start();  // Play 開始時に走行距離を初期化する。
	void Update(float deltaTime);  // 子ウェイポイントから曲線を作り、追従対象を進める。
	void FixedUpdate(float fixedDeltaTime);  // Dynamic Rigidbody の物理追従力を Jolt 更新直前に加える。
	void Draw();  // Rail の専用デバッグ描画はまだ行わない。
	void Stop();  // Runtime の一時停止。Additive Scene 再構築に備えて走行状態は保持する。
	void ResetSessionState();  // Play 開始・終了または通常 Scene 遷移で走行状態を破棄する。
	bool SetPaused(int32_t gameObjectId, bool isPaused);  // ゲームルールから停止と再開を指示する。
	bool IsPaused(int32_t gameObjectId) const;  // 現在の外部停止状態を返す。
	bool SetSpeed(int32_t gameObjectId, float speed);  // Play 中の目標速度を変更する。
	bool SetReverse(int32_t gameObjectId, bool isReversed);  // 速度値を壊さず進行方向だけを切り替える。
	bool SetNormalizedProgress(int32_t gameObjectId, float normalizedProgress);  // レール上の任意位置へ移動する。
	bool SetRailPath(int32_t gameObjectId, int32_t railPathGameObjectId, bool preservesProgress);  // 接続先レールを切り替える。
	bool SetMoveInput(int32_t gameObjectId, const EditorScriptVector2& moveInput);  // 左右・上下入力を次の Update へ渡す。
	bool SetOffset(int32_t gameObjectId, const EditorScriptVector2& offset);  // レール中心からの右・上オフセットを直接指定する。
	bool GetOffset(int32_t gameObjectId, EditorScriptVector2& offset) const;  // 現在の右・上オフセットを返す。
	bool GetNormalizedProgress(int32_t gameObjectId, float& normalizedProgress) const;  // 現在の進行率を返す。
	bool GetRailLength(int32_t gameObjectId, float& railLength) const;  // 現在参照しているレール全長を返す。
	bool GetRailPosition(int32_t gameObjectId, float normalizedProgress, Vector3& position) const;  // 指定進行率の位置を返す。
	bool GetRailDirection(int32_t gameObjectId, float normalizedProgress, Vector3& direction) const;  // 指定進行率の接線方向を返す。
	bool SetDistance(int32_t gameObjectId, float distance);  // レール始点からの実距離で現在位置を指定する。
	bool GetState(int32_t gameObjectId, EditorScriptRailState& state) const;  // Scriptが分岐せず扱える走行状態をまとめて返す。
	bool GetSpeedMultiplier(int32_t gameObjectId, float& speedMultiplier) const;  // ProfileとZoneを合成した現在倍率を返す。
	bool RearmEventMarkers(int32_t gameObjectId, const std::string& markerId);  // 空IDなら全Marker、指定IDなら一致Markerを再通知可能にする。
	bool GetClosestNormalizedProgress(int32_t gameObjectId, const Vector3& worldPosition, float& normalizedProgress) const;  // World位置に最も近いレール進行率を返す。
	bool GetRailFrame(int32_t gameObjectId, float normalizedProgress, EditorScriptRailFrame& frame) const;  // 指定位置のPositionと直交基底をまとめて返す。
	bool ConsumeEndReached(int32_t gameObjectId);  // 終端通知を一度だけ取り出す。

private:
	EditorScene* editorScene_ = nullptr;  // Play 中の Scene。
	EditorPhysicsManager* physicsManager_ = nullptr;  // 物理追従モードで力とトルクを加える入口。
	EditorInputManager* inputManager_ = nullptr;  // PlayerInput の Vector2 Action を読む入口。
	EditorScriptManager* scriptManager_ = nullptr;  // RailZoneの進入・退出Actionを送る入口。
	std::unordered_map<int32_t, EditorRailFollowerRuntimeState> runtimeStates_;  // 追従対象ごとの独立した実行状態。
	bool isStarted_ = false;  // Play 中だけ true。
};

#pragma warning(pop)
