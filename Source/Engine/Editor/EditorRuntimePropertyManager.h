#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorScriptManager;
class EditorTargetingManager;
class EditorWeaponManager;
class EditorDamageManager;
class EditorInputManager;
class EditorAudioManager;
class EditorEffectManager;

class EditorRuntimePropertyManager {
public:
	void Initialize(EditorScene* editorScene, EditorScriptManager* scriptManager, EditorTargetingManager* targetingManager, EditorWeaponManager* weaponManager, EditorDamageManager* damageManager, EditorInputManager* inputManager, EditorAudioManager* audioManager, EditorEffectManager* effectManager);  // PropertyとActionの実行対象を接続する。
	void Start();  // Tween自動再生とRelay自動実行を開始する。
	float UpdateTimeScale(float unscaledDeltaTime);  // 実時間でTimeScaleを更新し、今フレームのゲーム時間倍率を返す。
	void Update(float deltaTime);  // 再生中Tweenを進める。
	void Stop();  // Runtime状態を破棄する。
	bool SetFloat(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, float value);  // 登録済みFloat Propertyを書き換える。
	bool GetFloat(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, float& value) const;  // 登録済みFloat Propertyを読む。
	bool SetInt(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, int32_t value);  // 登録済みInt Propertyを書き換える。
	bool GetInt(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, int32_t& value) const;  // 登録済みInt Propertyを読む。
	bool SetBool(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, bool value);  // 登録済みbool Propertyを書き換える。
	bool GetBool(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, bool& value) const;  // 登録済みbool Propertyを読む。
	bool SetVector2(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, const EditorScriptVector2& value);  // 登録済みVector2 Propertyを書き換える。
	bool GetVector2(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, EditorScriptVector2& value) const;  // 登録済みVector2 Propertyを読む。
	bool SetVector3(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, const Vector3& value);  // 登録済みVector3 Propertyを書き換える。
	bool GetVector3(int32_t gameObjectId, const std::string& componentName, const std::string& propertyName, Vector3& value) const;  // 登録済みVector3 Propertyを読む。
	bool PlayTween(int32_t tweenGameObjectId);  // PropertyTweenを先頭から再生する。
	bool StopTween(int32_t tweenGameObjectId);  // PropertyTweenを停止する。
	bool IsTweenPlaying(int32_t tweenGameObjectId) const;  // Tween再生中か返す。
	bool Relay(int32_t relayGameObjectId);  // 子ActionRelayTargetへActionを分配する。
	bool StartTimer(int32_t gameObjectId);  // Timerを先頭から開始する。
	bool PauseTimer(int32_t gameObjectId, bool isPaused);  // Timerを停止・再開する。
	bool GetTimerRemaining(int32_t gameObjectId, float& remainingSeconds) const;  // 残り秒数を返す。
	bool ChangeState(int32_t gameObjectId, const std::string& stateName);  // GenericStateMachineを任意Stateへ遷移する。
	bool GetState(int32_t gameObjectId, std::string& stateName) const;  // 現在Stateを返す。
	bool SetAttribute(int32_t gameObjectId, float value);  // Attribute現在値をClampして設定する。
	bool GetAttribute(int32_t gameObjectId, float& current, float& maximum) const;  // Attribute値を返す。
	bool GetTargetLockState(int32_t gameObjectId, float& progress, bool& isLocked, int32_t& targetGameObjectId) const;  // Lock状態を返す。
	bool SetNamedAttribute(int32_t gameObjectId, const std::string& attributeName, float value);
	bool GetNamedAttribute(int32_t gameObjectId, const std::string& attributeName, float& current, float& maximum) const;
	bool SetCounter(int32_t gameObjectId, float value);
	bool AddCounter(int32_t gameObjectId, float deltaValue);
	bool GetCounter(int32_t gameObjectId, float& value) const;
	bool EvaluateCondition(int32_t gameObjectId, bool& result);
	bool GetMultiTargetLockCount(int32_t gameObjectId, int32_t& targetCount) const;
	bool GetMultiTargetLockTarget(int32_t gameObjectId, int32_t targetIndex, int32_t& targetGameObjectId, float& progress, bool& isLocked) const;
	bool GetGameplayDataValue(int32_t gameObjectId, const std::string& key, int32_t& valueType, std::string& value) const;
	bool StartCooldown(int32_t gameObjectId, const std::string& cooldownName, float durationOverride = -1.0f);
	bool ResetCooldown(int32_t gameObjectId, const std::string& cooldownName);
	bool GetCooldown(int32_t gameObjectId, const std::string& cooldownName, float& remainingSeconds, bool& isReady) const;
	bool GetThreatCount(int32_t gameObjectId, int32_t& threatCount) const;
	bool GetThreat(int32_t gameObjectId, int32_t threatIndex, EditorThreatRuntimeEntry& threat) const;
	bool PlayTimeScale(int32_t gameObjectId, float scaleOverride = -1.0f, float durationOverride = -1.0f);  // TimeScaleまたはHitStopを実時間で開始する。
	float GetTimeScale() const;  // 現在ゲーム更新へ適用している倍率を返す。
	bool SetObjective(int32_t gameObjectId, const std::string& objectiveId, int32_t state, float currentValue);
	bool GetObjective(int32_t gameObjectId, const std::string& objectiveId, int32_t& state, float& currentValue, float& targetValue) const;
	bool ApplyDifficulty(int32_t gameObjectId, int32_t difficultyIndex);
	bool GetDamageDirection(int32_t gameObjectId, EditorScriptVector2& direction, float& alpha, int32_t& sourceGameObjectId) const;
	bool SetGamePaused(int32_t gameObjectId, bool isPaused);  // GamePause設定に従って時間・物理・音声・入力Mapを切り替える
	bool IsGamePaused() const;  // 永続Pause状態を返す
	bool IsPhysicsPaused() const;  // 現在のPauseがPhysics停止を要求しているか返す
	bool GetSurfaceWakeState(int32_t gameObjectId, float& speed, float& intensity) const;  // 航跡の現在船速と0～1強度を返す
	bool GetWaterSurfaceState(int32_t gameObjectId, int32_t& state, float& signedDistance, int32_t& oceanGameObjectId, Vector3& position, Vector3& normal, Vector3& velocity) const;  // 入出水状態と水面情報を返す
	bool GetWaterSurfaceFoam(int32_t gameObjectId, float& foam) const;  // 現在Sampleの砕波・圧縮泡率を返す
	bool GetOceanProbeSample(int32_t gameObjectId, int32_t probeIndex, EditorOceanProbeEntry& probeEntry) const;  // 距離別Ocean Sampleを返す
	bool GetOceanProbeFoam(int32_t gameObjectId, int32_t probeIndex, float& foam) const;  // 距離別Sampleの泡率を返す
	bool ApplyStatusEffect(int32_t gameObjectId, const std::string& effectId, int32_t sourceGameObjectId);  // 定義済みEffectをStack規則に従って追加する
	bool RemoveStatusEffect(int32_t gameObjectId, const std::string& effectId);  // 指定IDのRuntime Effectを終了する
	bool ClearStatusEffects(int32_t gameObjectId);  // 所有者の全Effectを終了する
	bool HasStatusEffect(int32_t gameObjectId, const std::string& effectId) const;
	bool GetStatusEffectCount(int32_t gameObjectId, int32_t& effectCount) const;
	bool GetStatusEffectEntry(int32_t gameObjectId, int32_t effectIndex, EditorStatusEffectRuntimeEntry& effectEntry) const;
	void ResetRuntimeState(int32_t gameObjectId, const EditorComponent* resetConfiguration);  // Pool再利用時に汎用Runtime Componentを初期値へ戻す

private:
	struct TweenRuntime {
		float elapsedSeconds = 0.0f;  // 現在の再生時間。
		bool isPlaying = false;  // Update対象ならtrue。
	};
	struct TargetLockRuntime {
		float elapsedSeconds = 0.0f;
		float lostSeconds = 0.0f;
	};
	struct TimeScaleRuntime {
		int32_t ownerGameObjectId = -1;
		float startScale = 1.0f;
		float targetScale = 1.0f;
		float currentScale = 1.0f;
		float elapsedSeconds = 0.0f;
		float durationSeconds = 0.0f;
		float blendSeconds = 0.0f;
		bool isPlaying = false;
	};

	EditorScene* editorScene_ = nullptr;  // Property所有GameObjectを検索するScene。
	EditorScriptManager* scriptManager_ = nullptr;  // RelayとTween完了Actionの通知先。
	EditorTargetingManager* targetingManager_ = nullptr;  // MultiTargetLockの候補取得元。
	EditorWeaponManager* weaponManager_ = nullptr;  // ThreatTrackerの飛翔中Projectile取得元。
	EditorDamageManager* damageManager_ = nullptr;
	EditorInputManager* inputManager_ = nullptr;  // Pause時のGameplay/UI Map切替先
	EditorAudioManager* audioManager_ = nullptr;  // Pause時に再生位置を保持してVoiceを止める
	EditorEffectManager* effectManager_ = nullptr;  // SurfaceWakeのEmitter再生先
	std::unordered_map<int32_t, TweenRuntime> tweenRuntimes_;  // Tween所有者ごとの再生状態。
	std::unordered_map<int32_t, TargetLockRuntime> targetLockRuntimes_;  // Lock経過と喪失猶予。
	std::unordered_map<int32_t, float> previousAttributeValues_;  // 変更Action判定用。
	std::unordered_map<int32_t, float> initialAttributeValues_;  // Pool再利用時に編集値へ戻す。
	std::unordered_map<int32_t, std::unordered_map<std::string, float>> previousNamedAttributeValues_;
	std::unordered_map<int32_t, std::unordered_map<std::string, float>> initialNamedAttributeValues_;
	std::unordered_map<int32_t, std::unordered_map<int32_t, float>> multiTargetLostSeconds_;
	std::unordered_map<int32_t, std::unordered_set<int32_t>> previousThreatGameObjectIds_;
	std::unordered_map<int32_t, uint64_t> damageIndicatorSequences_;
	std::unordered_map<int32_t, Vector3> surfaceWakePreviousPositions_;  // 所有者のWorld移動量から船速を求める
	std::unordered_map<int32_t, bool> surfaceWakeActiveStates_;  // EffectのPlay/Stopを状態変化時だけ呼ぶ
	std::unordered_map<int32_t, bool> waterSurfaceUnderwaterStates_;  // 前Frameの水面内外状態
	TimeScaleRuntime timeScaleRuntime_{};  // scale=0でも復帰できるよう非スケール時間で進める単一Global状態。
	int32_t gamePauseOwnerGameObjectId_ = -1;  // 現在Pauseを所有するGameObject

	EditorComponent* FindComponent(int32_t gameObjectId, const std::string& componentName) const;  // 名前から対象Componentを返す。
	EditorComponent* FindTypedComponent(int32_t gameObjectId, EditorComponentType componentType) const;  // 型から対象Componentを返す。
	EditorComponent* FindTweenComponent(int32_t tweenGameObjectId) const;  // PropertyTween設定を返す。
	float EvaluateCurve(int32_t curveType, float normalizedTime) const;  // Tween Curveを0～1で評価する。
	bool EvaluateConditionComponent(int32_t gameObjectId, EditorComponent& condition, bool notifyAction);
	void UpdateSurfaceWakes(float deltaTime);  // Ocean Sampleへ追従する泡・飛沫Emitterを船速から更新する
	void UpdateOceanGameplayQueries();  // 入出水状態と前方Probe配列を同じOcean Sampleから更新する
};

#pragma warning(pop)
