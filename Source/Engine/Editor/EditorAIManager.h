#pragma once

#include "EditorPhysicsManager.h"
#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorAiSensorResult {
	bool isDetected = false;  // 直近フレームで対象を検出したか。
	bool hasDetails = false;  // label / text / motion などの詳細値が有効なら true。
	int32_t connectedGameObjectId = -1;  // Sensor が Inspector で参照している対象。
	int32_t detectedGameObjectId = -1;  // 実際に検出した GameObject。未検出または外部検出は -1。
	int32_t commandId = -1;  // 音声コマンドなどを数値で分岐したい時の ID。
	float range = 0.0f;  // Sensor の判定距離。
	float angleDegrees = 0.0f;  // 視界センサーの角度。
	float confidence = 0.0f;  // 画像 / 音声 / Python 検出の信頼度。
	float distance = 0.0f;  // 検出対象までの水平距離。
	Vector3 direction = {0.0f, 0.0f, 0.0f};  // 検出対象への正規化方向。
	float screenX = 0.0f;  // 画像系センサーの画面 X。Python 出力で設定する。
	float screenY = 0.0f;  // 画像系センサーの画面 Y。Python 出力で設定する。
	float boundsX = 0.0f;  // 検出枠の左上 X。Python 出力で設定する。
	float boundsY = 0.0f;  // 検出枠の左上 Y。Python 出力で設定する。
	float boundsWidth = 0.0f;  // 検出枠の幅。Python 出力で設定する。
	float boundsHeight = 0.0f;  // 検出枠の高さ。Python 出力で設定する。
	float motionX = 0.0f;  // 動き検出の X 方向。Scene 内ではワールド X 差分。
	float motionY = 0.0f;  // 動き検出の Y 方向。Scene 内ではワールド Z 差分。
	float motionMagnitude = 0.0f;  // 動き量。
	std::string label;  // 物体名、色名、検出ラベル。
	std::string text;  // Whisper などの認識文字列。
	std::string command;  // 音声コマンド名。
};

class EditorAIManager {
public:
	EditorAIManager() = default;  // RuntimeManager が 1 つだけ保持する。
	~EditorAIManager() = default;  // Scene / Physics は外部所有なのでここでは破棄しない。
	EditorAIManager(const EditorAIManager&) = delete;  // AI の実行状態を二重に持たない。
	EditorAIManager& operator=(const EditorAIManager&) = delete;  // AI の実行状態を二重に持たない。
	EditorAIManager(EditorAIManager&&) = delete;  // Runtime 中の参照先を動かさない。
	EditorAIManager& operator=(EditorAIManager&&) = delete;  // Runtime 中の参照先を動かさない。

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, std::vector<std::string>* consoleMessages);  // AI が読む Scene と物理 API を受け取る。
	void Start();  // Play 開始時に AI の速度と巡回基準位置を初期化する。
	void Update(float deltaTime);  // Play 中に AI Component を実行する。
	void Draw();  // 将来の AI Debug 表示用。現時点では描画処理を持たない。
	void Stop();  // Play 停止時に AI の実行状態を捨てる。
	bool IsSensorDetected(int32_t gameObjectId) const;  // 直近フレームでその GameObject の AI センサーが検知中なら true を返す。
	bool TryGetSensorResult(int32_t gameObjectId, EditorComponentType sensorComponentType, EditorAiSensorResult& sensorResult) const;  // Script API へ渡す詳細センサー結果を返す。

private:
	EditorScene* editorScene_ = nullptr;  // AI Component を検索する Scene。
	EditorPhysicsManager* physicsManager_ = nullptr;  // Rigidbody 付き AI へ速度を渡す物理 API。
	std::vector<std::string>* consoleMessages_ = nullptr;  // AI 状態を Console へ出す。
	std::unordered_map<int32_t, Vector3> agentVelocities_;  // AI ごとの水平移動速度。
	std::unordered_map<int32_t, Vector3> patrolOrigins_;  // 巡回行動の中心位置。
	std::unordered_map<int32_t, float> agentTimers_;  // 巡回やステート更新に使う経過時間。
	std::unordered_map<int32_t, float> pythonTimers_;  // Python AI を毎フレーム呼ばないための待ち時間。
	std::unordered_map<int32_t, bool> pythonWarningLogged_;  // Python 実行失敗ログを同じ Component で連打しないための印。
	std::unordered_map<int32_t, int32_t> agentStates_;  // StateMachine / HTN が現在選んでいる状態番号。
	std::unordered_map<int32_t, bool> visibleTargets_;  // 視界センサーが前フレーム対象を見ていたか。
	std::unordered_map<int64_t, EditorAiSensorResult> sensorResults_;  // Sensor 種類ごとの直近結果。
	std::unordered_map<int64_t, Vector3> sensorPreviousPositions_;  // 動き検出で前回位置を比較するための記録。
	bool isStarted_ = false;  // Play 中の AI が開始済みなら true。

	void UpdateAgent(EditorGameObject& gameObject, EditorComponent& aiComponent, float deltaTime);  // 行動系 AI を 1 つ更新する。
	void UpdateVisionSensor(const EditorGameObject& gameObject, EditorComponent& sensorComponent, float deltaTime);  // 視界センサーを 1 つ更新する。
	Vector3 MakeDesiredDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime);  // 行動モードから移動方向を作る。
	Vector3 MakeBehaviorTreeDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime);  // 条件評価で行動を選ぶ。
	Vector3 MakeStateMachineDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime);  // 待機 / 追跡 / 攻撃の状態を切り替える。
	Vector3 MakeGoapDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject);  // GOAP で次に必要な行動を選ぶ。
	Vector3 MakeHtnDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject);  // 大きな目的を移動タスクへ分解する。
	Vector3 MakePathfindingDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject);  // MicroPather で障害物を避ける経路方向を作る。
	Vector3 MakeSteeringDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime);  // Seek / Flee / Arrive などの操舵方向を作る。
	Vector3 MakeFlockDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent) const;  // 近くの AI と分離・整列・結合する方向を作る。
	Vector3 MakeBehaviorModeDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime);  // Inspector の動作モードから方向を作る。
	bool TryRunPythonDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent, const EditorGameObject* targetGameObject, float deltaTime, Vector3& direction);  // .py AI があれば方向を上書きする。
	bool TryRunPythonSensor(const EditorGameObject& gameObject, const EditorComponent& sensorComponent, const EditorGameObject* targetGameObject, float deltaTime, EditorAiSensorResult& sensorResult);  // .py Sensor があれば詳細検知結果を受け取る。
	void MoveAgent(EditorGameObject& gameObject, EditorComponent& aiComponent, const Vector3& desiredDirection, float deltaTime);  // Rigidbody または Transform へ AI 速度を反映する。
	Vector3 AvoidPhysicsObstacle(int32_t gameObjectId, const Vector3& currentPosition, const Vector3& desiredVelocity, float radius, float deltaTime) const;  // 物理 Collider を見て簡易回避する。
	void PushConsoleMessage(const std::string& message) const;  // Console 出力先がある時だけログを追加する。
};

#pragma warning(pop)
