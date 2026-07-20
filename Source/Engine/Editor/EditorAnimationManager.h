#pragma once

#include "EditorCommonTypes.h"
#include "EditorScene.h"
#include "Source/Engine/Animation/AnimationGraph.h"
#include "Source/Engine/Animation/PropertyAnimationClip.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorEffectManager;
class EditorScriptManager;

class EditorAnimationManager {
public:
	EditorAnimationManager() = default;
	~EditorAnimationManager() = default;
	EditorAnimationManager(const EditorAnimationManager&) = delete;
	EditorAnimationManager& operator=(const EditorAnimationManager&) = delete;
	EditorAnimationManager(EditorAnimationManager&&) = delete;
	EditorAnimationManager& operator=(EditorAnimationManager&&) = delete;

	void Initialize(
		EditorScene* editorScene,
		EditorEffectManager* effectManager,
		EditorScriptManager* scriptManager,
		std::vector<std::string>* consoleMessages);  // Animation と Effect Event の接続先を受け取る。
	void Start();  // Animation / Animator の実行インスタンスを Play 開始時の Scene から構築する。
	void Update(float deltaTime);  // State Machine、Blend Tree、Event、Root Motion を順番に更新する。
	void Stop();  // Runtime Pose と Parameter を破棄し、Play 開始前の Transform へ戻す。

	bool IsAnimationPlaying(int32_t gameObjectId) const;  // Animation または Animator が動作中なら true を返す。
	float GetAnimationTime(int32_t gameObjectId) const;  // Animator は現在 State、Animation は Clip の再生秒を返す。
	std::string GetAnimatorStateName(int32_t gameObjectId) const;  // Inspector デバッグ表示用の State 名を返す。
	bool GetAnimatorParameters(
		int32_t gameObjectId,
		std::vector<std::pair<std::string, AnimatorParameterValue>>& parameters) const;  // Inspector へ名前順の実行値をコピーする。
	bool GetFloat(int32_t gameObjectId, const std::string& parameterName, float& value) const;  // Float Parameter の現在値を取得する。
	bool GetInt(int32_t gameObjectId, const std::string& parameterName, int32_t& value) const;  // Int Parameter の現在値を取得する。
	bool GetBool(int32_t gameObjectId, const std::string& parameterName, bool& value) const;  // Bool / Trigger Parameter の現在値を取得する。
	bool GetVector2(int32_t gameObjectId, const std::string& parameterName, Vector2& value) const;  // Vector2 Parameter の現在値を取得する。
	bool GetVector3(int32_t gameObjectId, const std::string& parameterName, Vector3& value) const;  // Vector3 Parameter の現在値を取得する。

	bool SetFloat(int32_t gameObjectId, const std::string& parameterName, float value);  // Float Parameter を更新する。
	bool SetInt(int32_t gameObjectId, const std::string& parameterName, int32_t value);  // Int Parameter を更新する。
	bool SetBool(int32_t gameObjectId, const std::string& parameterName, bool value);  // Bool Parameter を更新する。
	bool SetTrigger(int32_t gameObjectId, const std::string& parameterName);  // Trigger を次の State Machine 評価まで有効にする。
	bool ResetTrigger(int32_t gameObjectId, const std::string& parameterName);  // 未消費 Trigger を明示的に解除する。
	bool SetVector2(int32_t gameObjectId, const std::string& parameterName, const Vector2& value);  // Vector2 Parameter を更新する。
	bool SetVector3(int32_t gameObjectId, const std::string& parameterName, const Vector3& value);  // Vector3 Parameter を更新する。
	bool PlayAnimation(int32_t gameObjectId);  // Animation Component を先頭から再生し直す。
	void StopAnimation(int32_t gameObjectId);  // Animation Component の再生を止め、管理していた値を開始前へ戻す。
	bool SetAnimationTime(int32_t gameObjectId, float playbackTime);  // 再生位置を秒単位で変更する。
	bool SetAnimationSpeed(int32_t gameObjectId, float playbackSpeed);  // Animation Component の再生倍率を変更する。
	bool PlayAction(
		int32_t gameObjectId,
		int32_t clipIndex,
		float blendIn,
		float blendOut,
		float playbackSpeed,
		int32_t priority,
		bool loop);  // 攻撃や被弾などの一時 Clip を Base State より上へ重ねる。

private:
	struct BaseTransform {
		Vector3 translate{0.0f, 0.0f, 0.0f};  // Play 開始時の配置。
		Vector3 rotate{0.0f, 0.0f, 0.0f};  // Play 開始時の回転。
		Vector3 scale{1.0f, 1.0f, 1.0f};  // Play 開始時の拡縮。
	};

	struct SampledTransform {
		Vector3 translation{0.0f, 0.0f, 0.0f};  // Clip からサンプリングしたローカル移動。
		Vector3 rotation{0.0f, 0.0f, 0.0f};  // Clip からサンプリングしたローカル回転。
		Vector3 scale{1.0f, 1.0f, 1.0f};  // Clip からサンプリングしたローカル拡縮。
		bool isValid = false;  // Clip / Keyframe が存在する場合だけ true。
	};

	struct ActiveAnimationAction {
		int32_t clipIndex = -1;  // 一時再生する Clip 番号。
		float playbackTime = 0.0f;  // Action 内の現在秒。
		float blendIn = 0.1f;  // 開始時に Base Pose から混ぜる秒数。
		float blendOut = 0.1f;  // 終了時に Base Pose へ戻す秒数。
		float playbackSpeed = 1.0f;  // Action Clip の再生倍率。
		int32_t priority = 0;  // 大きい Action を優先する。
		bool loop = false;  // true なら明示停止までループする。
		bool isActive = false;  // 現在 Base Pose へ合成するか。
	};

	struct AnimatorRuntimeInstance {
		AnimationGraph graph;  // GameObject が使用する共有可能な Graph 定義。
		std::vector<ModelAnimationClipData> clips;  // Model から読み込んだ Clip 一覧。
		std::unordered_map<std::string, AnimatorParameterValue> parameters;  // Instance 固有 Parameter。
		int32_t currentState = 0;  // 現在評価する State 番号。
		int32_t previousState = -1;  // 遷移中だけ保持する遷移元 State 番号。
		float stateTime = 0.0f;  // 現在 State の経過秒。
		float previousStateTime = 0.0f;  // 遷移元 State の経過秒。
		float transitionTime = 0.0f;  // 現在遷移の経過秒。
		float transitionDuration = 0.0f;  // 現在遷移で Pose を混ぜる秒数。
		float previousEventTime = 0.0f;  // Event 通過判定に使う前フレーム秒。
		SampledTransform previousRootPose{};  // Root Motion の差分抽出元。
		ActiveAnimationAction action{};  // State Machine を複雑化させない一時 Action。
		bool hasPreviousRootPose = false;  // 初回に大きな Root Motion が出ることを防ぐ。
	};

	struct PropertyAnimationRuntime {
		PropertyAnimationClip clip;  // Transform / Light / Material の共有可能なカーブ定義。
		std::unordered_map<int32_t, float> baseValues;  // Play 開始時の値。Additive / Multiply と Stop 復元に使う。
		float previousEventTime = 0.0f;  // 前フレームから Event 時刻を通過したか判定する基準秒。
	};

	EditorScene* editorScene_ = nullptr;  // Animation を反映する Scene。
	EditorEffectManager* effectManager_ = nullptr;  // Animation Event から Effect を再生する接続先。
	EditorScriptManager* scriptManager_ = nullptr;  // Animation Event をユーザー C++ DLL へ通知する接続先。
	std::vector<std::string>* consoleMessages_ = nullptr;  // Graph 読み込み失敗や Event を表示する Console。
	std::unordered_map<int32_t, float> animationTimes_;  // 旧 Animation Component の再生秒。
	std::unordered_map<int32_t, BaseTransform> baseTransforms_;  // Stop と In-Place Pose の基準。
	std::unordered_map<int32_t, ModelAnimationClipData> animationClips_;  // 旧 Animation が使う単一 Clip。
	std::unordered_map<int32_t, PropertyAnimationRuntime> propertyAnimationRuntimes_;  // .animclip のカーブと基準値。
	std::unordered_map<int32_t, AnimatorRuntimeInstance> animatorRuntimes_;  // Animator Component ごとの実行状態。
	bool isStarted_ = false;  // Play 中だけ Update を許可する。

	void CacheAnimationClip(const EditorGameObject& gameObject, const EditorComponent& component);  // 旧 Animation 用 Clip を読む。
	void CachePropertyAnimationClip(const EditorGameObject& gameObject, const EditorComponent& component);  // .animclip と対象 Property の基準値を読む。
	void StartAnimator(const EditorGameObject& gameObject, const EditorComponent& component);  // Graph と全 Clip を Instance へ構築する。
	void UpdateAnimation(EditorGameObject& gameObject, EditorComponent& component, float& playbackTime);  // 旧 Animation を更新する。
	void UpdateFbxAnimation(EditorGameObject& gameObject, EditorComponent& component, float& playbackTime);  // FBX Transform Clip を更新する。
	void UpdatePropertyAnimation(EditorGameObject& gameObject, EditorComponent& component, float playbackTime);  // .animclip の全 Track と Event を更新する。
	void UpdateAnimator(EditorGameObject& gameObject, EditorComponent& component, float deltaTime);  // Animator Graph の 1 フレームを評価する。
	void UpdateAutomaticParameters(EditorGameObject& gameObject, EditorComponent& component, AnimatorRuntimeInstance& runtime);  // 速度を Local MoveX / MoveY へ変換する。
	void EvaluateStateMachine(AnimatorRuntimeInstance& runtime, const EditorComponent& component);  // 条件を満たした Transition を開始する。
	SampledTransform SampleState(const AnimatorRuntimeInstance& runtime, int32_t stateIndex, float playbackTime) const;  // State の Clip / Blend Tree を評価する。
	SampledTransform SampleClip(const AnimatorRuntimeInstance& runtime, int32_t clipIndex, float playbackTime, bool loop) const;  // Keyframe 間を補間する。
	SampledTransform SampleBlend1D(const AnimatorRuntimeInstance& runtime, const AnimationGraphState& state, float playbackTime) const;  // 速度など 1 軸で 2 Sample を混ぜる。
	SampledTransform SampleDirectionalBlend(const AnimatorRuntimeInstance& runtime, const AnimationGraphState& state, float playbackTime) const;  // 方向と大きさから周囲 Sample を混ぜる。
	SampledTransform SampleCartesianBlend(const AnimatorRuntimeInstance& runtime, const AnimationGraphState& state, float playbackTime) const;  // 任意 2D Sample の近傍を混ぜる。
	SampledTransform SampleDirectBlend(const AnimatorRuntimeInstance& runtime, const AnimationGraphState& state, float playbackTime) const;  // Sample ごとの Parameter を直接ウェイトとして使う。
	void ApplyAnimatorPose(EditorGameObject& gameObject, EditorComponent& component, AnimatorRuntimeInstance& runtime, const SampledTransform& pose);  // In-Place / Root Motion を分けて Transform へ反映する。
	void UpdateAction(AnimatorRuntimeInstance& runtime, float deltaTime, SampledTransform& basePose) const;  // 優先 Action を Base Pose へ合成する。
	void DispatchAnimationEvents(EditorGameObject& gameObject, AnimatorRuntimeInstance& runtime, int32_t clipIndex, float previousTime, float currentTime, bool loop);  // 通過した Event を EffectSystemへ送る。
	bool GetAnimatedProperty(const EditorGameObject& gameObject, AnimationPropertyTarget target, float& value) const;  // Component の現在値を Track と同じ float へ変換する。
	bool SetAnimatedProperty(EditorGameObject& gameObject, AnimationPropertyTarget target, float value) const;  // Track の float を対象 Component へ書き戻す。
	AnimatorParameterValue* FindParameter(int32_t gameObjectId, const std::string& parameterName);  // GameObject と名前から Runtime Parameter を探す。
	void PushConsoleMessage(const std::string& message);  // Console が有効な時だけログを追加する。
};

#pragma warning(pop)
