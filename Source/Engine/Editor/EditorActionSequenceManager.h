#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRailMovementManager;
class EditorScriptManager;

class EditorActionSequenceManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorScriptManager* scriptManager);  // Sceneと汎用Action通知先を接続する
	void Start();  // ActionSequenceごとの実行状態を構築する
	void Update(float deltaTime);  // 順次・並列Stepを進める
	void Stop();  // Scene再構築に備えて実行状態を保持したまま停止する
	void ResetSessionState();  // 主Scene切替またはPlay終了時に全実行状態を破棄する

	bool Play(int32_t sequenceGameObjectId);  // 指定Sequenceを先頭から再生する
	bool Pause(int32_t sequenceGameObjectId, bool isPaused);  // 再生状態を保持したまま停止・再開する
	bool StopSequence(int32_t sequenceGameObjectId);  // 指定Sequenceだけ停止する
	bool Signal(int32_t sequenceGameObjectId, const std::string& signalName);  // Signal待機Stepを解除する
	bool IsPlaying(int32_t sequenceGameObjectId) const;  // 指定Sequenceが実行中か返す

private:
	struct ActiveStep {
		int32_t stepIndex = -1;  // Sequence直下の子配列番号
		float remainingSeconds = 0.0f;  // 待機Stepの残り秒数
		bool isWaitingForSignal = false;  // Signal待機中ならtrue
		std::string signalName;  // 解除に必要なSignal名
		bool isWaitingForScene = false;  // 非同期Scene読込の完了待ちならtrue
		std::string scenePath;  // 完了確認するScene Asset
	};

	struct SequenceRuntime {
		std::vector<int32_t> stepGameObjectIds;  // Hierarchy順のStep所有Object
		std::vector<ActiveStep> activeSteps;  // 現在の単独Stepまたは並列Group
		std::unordered_set<std::string> receivedSignals;  // 未消費の外部Signal
		int32_t nextStepIndex = 0;  // 次に開始するStep番号
		bool isPlaying = false;  // 再生中ならtrue
		bool isPaused = false;  // 一時停止中ならtrue
	};

	EditorScene* editorScene_ = nullptr;  // 実行対象Scene
	EditorRailMovementManager* railMovementManager_ = nullptr;  // Rail進行率条件の取得先
	EditorScriptManager* scriptManager_ = nullptr;  // Action通知とScene読込要求の送信先
	std::unordered_map<int32_t, SequenceRuntime> sequenceRuntimes_;  // Sequence所有Object IDごとの状態

	bool StartNextBatch(int32_t sequenceGameObjectId, SequenceRuntime& sequenceRuntime);  // 次の単独Stepまたは並列Groupを開始する
	bool StartStep(
		int32_t sequenceGameObjectId,
		SequenceRuntime& sequenceRuntime,
		int32_t stepIndex,
		ActiveStep& activeStep);  // Step開始時の即時処理と待機状態を設定する
	bool UpdateActiveStep(SequenceRuntime& sequenceRuntime, ActiveStep& activeStep, float deltaTime);  // trueならStep完了
	bool EvaluateCondition(const EditorComponent& stepComponent, int32_t ownerGameObjectId) const;  // 条件分岐値を取得して比較する
	const EditorComponent* FindSequenceComponent(int32_t gameObjectId) const;  // Sequence設定を取得する
	const EditorComponent* FindStepComponent(int32_t gameObjectId) const;  // Step設定を取得する
};

#pragma warning(pop)
