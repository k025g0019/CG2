#pragma once

#include "EditorAIManager.h"
#include "EditorAnimationManager.h"
#include "EditorInputManager.h"
#include "EditorJoltPhysicsManager.h"
#include "EditorPhysicsManager.h"
#include "EditorRailMovementManager.h"
#include "EditorScene.h"
#include "EditorScriptApi.h"
#include "Source/Engine/Effect/EditorEffectManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorCameraEffectManager;
class EditorActionSequenceManager;
class EditorDamageManager;
class EditorObjectPoolManager;
class EditorRailBranchManager;
class EditorSaveManager;
class EditorTargetingManager;
class EditorWeaponManager;
class EditorWeaponLoadoutManager;
class EditorRuntimePropertyManager;
class EditorWaveSpawnerManager;

struct EditorSceneLoadRequest {
	std::string scenePath;  // 読み込むScene Asset
	bool isAdditive = false;  // trueなら現在Sceneへ追加する
	bool isAsynchronous = false;  // trueならファイル読込をWorker Threadへ移す
};

class EditorScriptManager {
public:
	struct ScriptDebugInfo {
		bool hasBinding = false;  // 対象 GameObject に Script / MonoBehaviour があれば true
		bool sourceDllExists = false;  // Component が参照する DLL が実在すれば true
		bool isLoaded = false;  // Play 中に DLL 初期化まで成功していれば true
		uint64_t reloadGeneration = 0;  // ホットリロード用に付けた世代番号
		std::string sourceDllPath;  // Component に設定された元 DLL パス
		std::string loadedDllPath;  // runtime_cache にコピーした実行中 DLL パス
		std::string lastStatusMessage;  // 最後の読込結果や失敗内容
	};

	//================================================================
	// Script / MonoBehaviour Component の実行入口
	//================================================================

	void Initialize(
		EditorScene* editorScene,
		EditorInputManager* inputManager,
		EditorAnimationManager* animationManager,
		EditorEffectManager* effectManager,
		EditorAIManager* aiManager,
		EditorPhysicsManager* physicsManager,
		std::vector<std::string>* consoleMessages);  // Script 実行対象 Scene、入力、AI、物理、Console を受け取る
	void Start();  // Play 開始時に Script / MonoBehaviour の Start を呼ぶ
	void RegisterRuntimeHierarchy(int32_t rootGameObjectId);  // Pool等がPlay中に複製した階層のScript Bindingを追加する
	void Update(const uint8_t* keyState, float deltaTime);  // 毎フレームの Script 更新を呼ぶ
	void FixedUpdate(float fixedDeltaTime);  // 固定時間更新の Script を呼ぶ
	void SetPhysicsEvents(const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& physicsEvents);  // 衝突イベントを Script 側へ渡す
	void DispatchAnimationEvent(
		int32_t gameObjectId,
		const std::string& eventName,
		float eventTime,
		const std::string& effectAssetPath,
		const Vector3& localOffset);  // Animation Graph Event を対象 GameObject の C++ Script へ即時通知する。
	void Stop();  // Play 停止時に Script の Stop を呼ぶ
	bool IsStarted() const;  // Start 済みかどうかを返す
	ScriptDebugInfo GetDebugInfo(int32_t gameObjectId) const;  // Inspector 用に DLL の現在状態を返す
	bool RefreshExposedFields(EditorComponent& scriptComponent);  // DLL の公開変数定義を Inspector 用データへ同期する
	std::vector<std::string> GetRegisteredActionNames(int32_t gameObjectId);  // 対象ObjectのScriptがBindActionした名前をInspector候補として返す
	void QueueActionEvent(
		int32_t gameObjectId,
		const std::string& functionName,
		int32_t valueType = EditorScriptInputValueTypeButton,
		float buttonValue = 1.0f,
		EditorScriptVector2 vector2Value = {});  // Timelineなどから任意の Script Action を次の Update へ通知する
	void QueueUiEvent(
		int32_t gameObjectId,
		const std::string& functionName,
		int32_t valueType = EditorScriptInputValueTypeButton,
		float buttonValue = 1.0f,
		EditorScriptVector2 vector2Value = {});  // Game View UI の操作を次の Update で Script へ通知する
	bool QueueActionPayload(int32_t gameObjectId, const std::string& functionName, const EditorScriptActionPayload& payload);  // 型付きPayload付きActionをQueueへ追加する。
	bool RequestSceneLoad(const std::string& scenePath);  // 高水準 Component からも Script と同じ検証で Scene 遷移を要求する。
	bool RequestSceneLoadAsync(const std::string& scenePath, bool isAdditive);  // 非同期の置換または追加読込を要求する。
	bool RequestSceneUnload(const std::string& scenePath);  // 追加読込済みSceneの破棄を要求する。
	bool SetGameObjectActive(int32_t gameObjectId, bool isActive);  // 高水準 Component から描画と物理を同じActive状態へ変更する。
	bool ConsumeSceneLoadRequest(EditorSceneLoadRequest& sceneLoadRequest);  // 保留中の読込要求をRuntimeManagerへ1回だけ渡す。
	bool ConsumeSceneUnloadRequest(std::string& scenePath);  // 保留中の破棄要求をRuntimeManagerへ1回だけ渡す。
	void SetRailMovementManager(EditorRailMovementManager* railMovementManager);  // C++ Script の RailFollower API を実行系へ接続する。
	void SetGameplayManagers(
		EditorTargetingManager* targetingManager,
		EditorDamageManager* damageManager,
		EditorObjectPoolManager* objectPoolManager,
		EditorWeaponManager* weaponManager,
		EditorCameraEffectManager* cameraEffectManager,
		EditorRailBranchManager* railBranchManager);  // 汎用照準・射撃・生成・カメラ・分岐APIを各実行系へ接続する
	void SetWorkflowManagers(
		EditorActionSequenceManager* actionSequenceManager,
		EditorSaveManager* saveManager);  // 汎用SequenceとSave/Checkpoint APIを実行系へ接続する。
	void SetReusableGameplayManagers(
		EditorWeaponLoadoutManager* weaponLoadoutManager,
		EditorTargetingManager* targetingManager,
		EditorRuntimePropertyManager* runtimePropertyManager,
		EditorWaveSpawnerManager* waveSpawnerManager);  // Loadout・Target・Property・Encounter APIを実行系へ接続する。
	void SetSceneRuntimeState(
		float loadProgress,
		bool isLoading,
		const std::vector<std::string>& loadedScenePaths);  // Runtimeが管理するScene読込状態をScript APIへ公開する。
	bool IsSceneRuntimeLoading() const;  // Sequenceなどの高水準Componentが非同期完了待ちに使う。
	bool IsSceneRuntimeLoaded(const std::string& scenePath) const;  // 指定SceneがPrimaryまたはAdditiveで有効か返す。

private:
	struct ScriptBinding {
		int32_t gameObjectId = -1;  // この Script を呼ぶ対象 GameObject ID
		size_t componentIndex = 0U;  // 同じ Object に同種・同 DLL を複数付けても Component を区別する配列位置
		EditorComponentType componentType = EditorComponentType::Script;  // Script か MonoBehaviour かの種類
		std::string dllPath;  // Component に設定された元 DLL パス
		void* instance = nullptr;  // Instance API 対応 DLL が Component ごとに生成した専用状態
		size_t synchronizedFieldHash = 0U;  // Inspector と DLL で最後に一致した公開変数値のハッシュ
		bool hasSynchronizedFieldHash = false;  // 初回同期前の hash=0 と実データを区別する
		bool hasStarted = false;  // GameObjectが初めてActiveになりStartを通知済みならtrue
		float updateIntervalRemaining = 0.0f;  // SimulationLODで次のUpdateまで待つ秒数
		float accumulatedUpdateDeltaTime = 0.0f;  // 間引いた時間を次回Updateへまとめて渡す
	};

	struct ScriptModule {
		std::string sourceDllPath;  // ユーザーがビルドした元 DLL のパス
		std::string loadedDllPath;  // LoadLibrary 用にコピーした作業 DLL のパス
		std::filesystem::file_time_type lastWriteTime{};  // ホットリロード検出に使う更新日時
		void* moduleHandle = nullptr;  // LoadLibraryW が返す HMODULE を void* で保持する
		EditorScriptLoadFn loadFunction = nullptr;  // DLL 読込時の初期化関数
		EditorScriptUnloadFn unloadFunction = nullptr;  // DLL 解放前の終了関数
		EditorScriptStartFn startFunction = nullptr;  // Play 開始時の開始関数
		EditorScriptUpdateFn updateFunction = nullptr;  // 毎フレーム更新関数
		EditorScriptFixedUpdateFn fixedUpdateFunction = nullptr;  // 固定更新関数
		EditorScriptPhysicsEventFn physicsEventFunction = nullptr;  // 接触イベント通知関数
		EditorScriptAnimationEventFn animationEventFunction = nullptr;  // Animation Graph の任意イベント通知関数
		EditorScriptStopFn stopFunction = nullptr;  // Play 停止時の終了関数
		EditorScriptGetFieldCountFn getFieldCountFunction = nullptr;  // DLL が公開する Inspector 変数数を返す
		EditorScriptGetFieldDescriptorFn getFieldDescriptorFunction = nullptr;  // 公開変数の型と表示名を返す
		EditorScriptGetFieldValueFn getFieldValueFunction = nullptr;  // Script インスタンスの現在値を返す
		EditorScriptSetFieldValueFn setFieldValueFunction = nullptr;  // Inspector 保存値を Script インスタンスへ設定する
		EditorScriptInvokeActionFn invokeActionFunction = nullptr;  // PlayerInput の関数名を C++ メソッドへ通知する
		EditorScriptGetActionCountFn getActionCountFunction = nullptr;  // Script が公開する Action 候補数を返す
		EditorScriptGetActionNameFn getActionNameFunction = nullptr;  // Script が公開する Action 名を返す
		EditorScriptCreateInstanceFn createInstanceFunction = nullptr;  // Component 固有の Script 実体を生成する
		EditorScriptDestroyInstanceFn destroyInstanceFunction = nullptr;  // Component 固有の Script 実体を破棄する
		EditorScriptStartInstanceFn startInstanceFunction = nullptr;  // Component 固有実体へ Start を通知する
		EditorScriptUpdateInstanceFn updateInstanceFunction = nullptr;  // Component 固有実体へ Update を通知する
		EditorScriptFixedUpdateInstanceFn fixedUpdateInstanceFunction = nullptr;  // Component 固有実体へ FixedUpdate を通知する
		EditorScriptPhysicsEventInstanceFn physicsEventInstanceFunction = nullptr;  // Component 固有実体へ物理 Event を通知する
		EditorScriptAnimationEventInstanceFn animationEventInstanceFunction = nullptr;  // Component 固有実体へ Animation Event を通知する
		EditorScriptStopInstanceFn stopInstanceFunction = nullptr;  // Component 固有実体へ Stop を通知する
		EditorScriptGetFieldValueInstanceFn getFieldValueInstanceFunction = nullptr;  // Component 固有実体の公開値を読む
		EditorScriptSetFieldValueInstanceFn setFieldValueInstanceFunction = nullptr;  // Component 固有実体へ Inspector 値を設定する
		EditorScriptInvokeActionInstanceFn invokeActionInstanceFunction = nullptr;  // Component 固有実体の Action を呼ぶ
		bool isLoaded = false;  // 関数取得と初期化まで成功していれば true
		std::vector<int32_t> attachedGameObjectIds;  // この DLL を使っている GameObject 一覧
	};

	struct ScriptMetadata {
		std::filesystem::file_time_type lastWriteTime{};  // 同じ DLL を毎フレーム読み直さないための更新日時
		std::vector<EditorScriptFieldDescriptor> fieldDescriptors;  // Inspector へ表示する公開変数定義
		std::vector<std::string> actionNames;  // BindAction された候補を Play 前の Inspector へ渡す
		bool isValid = false;  // メタデータ取得に成功していれば true
	};

	struct QueuedUiEvent {
		int32_t gameObjectId = -1;  // クリックを受けた GameObject ID。
		std::string functionName;  // 呼び出す C++ Script 関数名。
		int32_t valueType = EditorScriptInputValueTypeButton;  // Button か Vector2 かを Script 側へ伝える。
		float buttonValue = 1.0f;  // Button / Toggle の値。
		EditorScriptVector2 vector2Value{};  // Slider など 1 軸値は x に入れる。
		bool isUiEvent = false;  // trueなら InputContext の発生元を UI として通知する。
		EditorScriptActionPayload payload{};  // 任意の型付きAction値。
	};

	EditorScene* editorScene_ = nullptr;  // Script Component を探す対象 Scene
	EditorInputManager* inputManager_ = nullptr;  // PlayerInput Action を読む入力 API
	EditorAnimationManager* animationManager_ = nullptr;  // Animation の再生状態と現在時刻を読む API
	EditorEffectManager* effectManager_ = nullptr;  // Effect の再生・停止・生存数 API
	EditorAIManager* aiManager_ = nullptr;  // AI センサー状態を読む AI API
	EditorPhysicsManager* physicsManager_ = nullptr;  // AddForce / SetVelocity へ接続する物理 API
	EditorRailMovementManager* railMovementManager_ = nullptr;  // RailFollower の停止・移動・問い合わせ API
	EditorTargetingManager* targetingManager_ = nullptr;  // Game View座標からWorld Rayを生成するAPI
	EditorDamageManager* damageManager_ = nullptr;  // DamageReceiverとHealthを操作するAPI
	EditorObjectPoolManager* objectPoolManager_ = nullptr;  // Pool貸出・返却とSpawnerを操作するAPI
	EditorWeaponManager* weaponManager_ = nullptr;  // HitscanとProjectileを発射するAPI
	EditorCameraEffectManager* cameraEffectManager_ = nullptr;  // Camera BlendとShakeを再生するAPI
	EditorRailBranchManager* railBranchManager_ = nullptr;  // RailBranchを手動実行するAPI
	EditorActionSequenceManager* actionSequenceManager_ = nullptr;  // 汎用ActionSequenceを制御するAPI
	EditorSaveManager* saveManager_ = nullptr;  // Save SlotとCheckpointを制御するAPI
	EditorWeaponLoadoutManager* weaponLoadoutManager_ = nullptr;  // 可変Weapon SlotとReloadを操作するAPI
	EditorRuntimePropertyManager* runtimePropertyManager_ = nullptr;  // 型付きProperty、Tween、Relayを操作するAPI
	EditorWaveSpawnerManager* waveSpawnerManager_ = nullptr;  // Wave、Encounter、Spawn地点を操作するAPI
	std::vector<std::string>* consoleMessages_ = nullptr;  // Script のログやエラーを出す Console
	bool isStarted_ = false;  // Start が呼ばれていれば true
	float lastDeltaTime_ = 0.0f;  // 最後に Update へ渡した秒数
	float lastFixedDeltaTime_ = 0.0f;  // 最後に FixedUpdate へ渡した秒数
	std::vector<EditorJoltPhysicsManager::PhysicsEvent> physicsEvents_;  // OnCollision / OnTrigger 相当の元データ
	std::vector<ScriptBinding> scriptBindings_;  // Scene 内 Script Component から作った実行対象一覧
	std::unordered_map<int32_t, std::vector<size_t>> scriptBindingIndicesByGameObjectId_;  // Input/UI 通知を対象 Script へ直接渡す索引
	std::unordered_map<std::string, ScriptModule> scriptModules_;  // DLL パス単位で 1 度だけロードしたモジュール一覧
	std::unordered_map<std::string, std::string> moduleStatusMessages_;  // DLL ごとの最新状態メッセージ
	std::unordered_map<std::string, ScriptMetadata> scriptMetadataCache_;  // Play 前の Inspector 用 DLL メタデータキャッシュ
	std::unordered_map<std::string, bool> inputActionActiveStates_;  // Vector2 Action の started / canceled 判定用状態
	std::unordered_set<std::string> missingActionWarnings_;  // 未登録関数の警告を同じPlay中に一度だけ出す
	std::vector<QueuedUiEvent> queuedUiEvents_;  // Game View UI から来たイベントを Update まで保持する
	EditorSceneLoadRequest requestedSceneLoad_;  // Update終了後に安全に処理するScene読込要求。
	std::string requestedSceneUnloadPath_;  // Update終了後に安全に処理するAdditive Scene破棄要求。
	float sceneLoadProgress_ = 0.0f;  // Runtimeから公開された非同期Scene読込進捗
	bool isSceneLoading_ = false;  // RuntimeがScene読込中ならtrue
	std::vector<std::string> loadedScenePaths_;  // PrimaryとAdditiveを含む読込済みScene
	std::unordered_map<std::string, float> sceneFloatValues_;  // Scene切替後も保持する一時floatデータ
	std::unordered_map<std::string, std::string> sceneStringValues_;  // Scene切替後も保持する一時文字列データ
	std::array<uint8_t, 256> currentKeyState_{};  // DLL Script から参照する最新キー状態
	std::array<uint8_t, 256> previousKeyState_{};  // 押した瞬間判定用の 1 フレーム前キー状態
	int32_t hotReloadCheckFrameTimer_ = 0;  // DLL 更新日時の確認を毎フレーム実行しないための残りフレーム数
	int32_t fieldSynchronizationFrameTimer_ = 0;  // 公開変数の DLL 往復を毎フレーム行わないための残りフレーム数
	uint64_t reloadGeneration_ = 0;  // 作業 DLL コピー名を毎回変えるための通し番号
	EditorScriptRuntimeApi runtimeApi_{};  // DLL へ渡す関数ポインタ群

	void BuildScriptBindings();  // Scene 内の Script / MonoBehaviour から DLL 実行対象一覧を作る
	void BuildRuntimeApi();  // DLL へ公開する関数ポインタを設定する
	void StartBindingsForModule(ScriptModule& scriptModule);  // DLLを使うActiveなGameObjectへ未通知のStartを送る
	void StartBindingIfNeeded(ScriptBinding& scriptBinding, ScriptModule& scriptModule);  // 初回Active時だけ公開値同期とStartを行う
	void StopBindingsForModule(ScriptModule& scriptModule);  // Start済みGameObjectだけへStopを送る
	bool UsesInstanceApi(const ScriptModule& scriptModule) const;  // Create / Destroy が揃った新 ABI かを返す
	bool InvokeBindingAction(const ScriptBinding& scriptBinding, ScriptModule& scriptModule, const char* functionName, const EditorScriptInputActionContext& inputContext);  // 新旧 ABI を吸収して対象 Component の Action を呼ぶ
	void DispatchQueuedUiEvents();  // Button などの UI イベントを C++ Script 関数へ通知する
	void DispatchInputActions();  // PlayerInput の Action を同じ GameObject の C++ 関数へ通知する
	void ApplyComponentFieldsToInstance(const ScriptBinding& scriptBinding, ScriptModule& scriptModule);  // 保存済み Inspector 値を Script インスタンスへ戻す
	void ReadInstanceFieldsToComponent(const ScriptBinding& scriptBinding, ScriptModule& scriptModule);  // Script が更新した公開変数を Inspector と Scene 保存値へ戻す
	void SynchronizeComponentProperties(EditorComponent& scriptComponent, const std::vector<EditorScriptFieldDescriptor>& fieldDescriptors);  // DLL 定義と保存値を名前で統合する
	bool ReadMetadataFromDll(const std::string& dllPath, ScriptMetadata& scriptMetadata);  // Play 前でも DLL から公開変数定義を取得する
	EditorComponent* FindScriptComponent(const ScriptBinding& scriptBinding);  // Binding が指す Script / MonoBehaviour Component を返す
	bool IsScriptBindingActive(const ScriptBinding& scriptBinding) const;  // GameObjectとScript Componentが実行可能ならtrue
	void HotReloadChangedModules();  // 元 DLL の更新日時を見て差し替える
	void UnloadAllModules();  // Play 停止時にすべての DLL を解放する
	bool LoadModule(const std::string& dllPath);  // DLL を読み込み、必要な関数を取得する
	void UnloadModule(ScriptModule& scriptModule);  // 1 つの DLL を解放する
	ScriptModule* FindModule(const std::string& dllPath);  // パスから既存モジュールを探す
	void PushConsoleMessage(const std::string& message);  // Console に 1 行追加する
	void CopyKeyState(const uint8_t* keyState);  // Runtime から受けたキー配列を保持する
	EditorScriptPhysicsEvent ConvertPhysicsEvent(const EditorJoltPhysicsManager::PhysicsEvent& physicsEvent) const;  // Jolt の接触イベントを DLL 用構造体へ変換する

	bool IsKeyDownInternal(int32_t keyCode) const;  // DLL API 用のキー押下判定
	bool IsKeyPressedInternal(int32_t keyCode) const;  // DLL API 用の押した瞬間判定
	EditorScriptVector2 GetActionVector2Internal(int32_t gameObjectId, const char* actionMapName, const char* actionName) const;  // DLL API 用に PlayerInput の Vector2 Action を返す
	bool IsActionPressedInternal(int32_t gameObjectId, const char* actionMapName, const char* actionName) const;  // DLL API 用に Button Action の押下中判定を返す
	bool WasActionJustPressedInternal(int32_t gameObjectId, const char* actionMapName, const char* actionName) const;  // DLL API 用に Button Action の押した瞬間判定を返す
	EditorScriptVector2 GetMousePositionInternal() const;  // DLL API 用にクライアント座標のマウス位置を返す
	EditorScriptTransform GetTransformInternal(int32_t gameObjectId) const;  // DLL API 用に GameObject Transform を返す
	void SetTransformInternal(int32_t gameObjectId, const EditorScriptTransform& transform);  // DLL API 用に GameObject Transform を上書きする
	EditorScriptVector3 GetVelocityInternal(int32_t gameObjectId) const;  // DLL API 用に Rigidbody 速度を返す
	void SetVelocityInternal(int32_t gameObjectId, const EditorScriptVector3& velocity);  // DLL API 用に Rigidbody 速度を設定する
	EditorScriptVector3 GetAngularVelocityInternal(int32_t gameObjectId) const;  // DLL API 用に Rigidbody 角速度を返す
	void SetAngularVelocityInternal(int32_t gameObjectId, const EditorScriptVector3& angularVelocity);  // DLL API 用に Rigidbody 角速度を設定する
	bool AddForceInternal(int32_t gameObjectId, const EditorScriptVector3& force);  // DLL API 用に継続力を加える
	bool AddForceAtPositionInternal(int32_t gameObjectId, const EditorScriptVector3& force, const EditorScriptVector3& worldPosition);  // DLL API 用に作用点付き継続力を加える
	bool AddImpulseInternal(int32_t gameObjectId, const EditorScriptVector3& impulse);  // DLL API 用に瞬間力を加える
	bool AddTorqueInternal(int32_t gameObjectId, const EditorScriptVector3& torque);  // DLL API 用に回転トルクを加える
	int32_t AddExplosionImpulseInternal(const EditorScriptVector3& center, float radius, float impulseStrength, float upwardModifier);  // DLL API 用に範囲爆発を発生させる
	bool AttachRopeInternal(int32_t ownerGameObjectId, int32_t targetGameObjectId, const EditorScriptVector3& ownerLocalAnchor, const EditorScriptVector3& targetAnchor, float maximumLength);  // DLL API 用にロープを接続する
	bool DetachRopeInternal(int32_t ownerGameObjectId);  // DLL API 用にロープを解除する
	bool SetRopeLengthInternal(int32_t ownerGameObjectId, float maximumLength);  // DLL API 用にロープ長を変更する
	bool RepairRopeInternal(int32_t ownerGameObjectId);  // DLL API 用にロープ破断を修復する
	EditorScriptRopeState GetRopeStateInternal(int32_t ownerGameObjectId) const;  // DLL API 用にロープ状態を返す
	EditorScriptAiSensorState GetAiSensorStateInternal(int32_t gameObjectId, int32_t sensorKind) const;  // DLL API 用に AI センサー状態を返す
	EditorScriptMaterialState GetMaterialStateInternal(int32_t gameObjectId) const;  // DLL API 用に Material 情報を返す
	EditorScriptAnimationState GetAnimationStateInternal(int32_t gameObjectId) const;  // DLL API 用に Animation 情報を返す
	bool SetAnimatorFloatInternal(int32_t gameObjectId, const char* parameterName, float value);  // DLL API 用に Animator Float を設定する
	bool SetAnimatorIntInternal(int32_t gameObjectId, const char* parameterName, int32_t value);  // DLL API 用に Animator Int を設定する
	bool SetAnimatorBoolInternal(int32_t gameObjectId, const char* parameterName, bool value);  // DLL API 用に Animator Bool を設定する
	bool SetAnimatorTriggerInternal(int32_t gameObjectId, const char* parameterName);  // DLL API 用に Animator Trigger を発火する
	bool SetAnimatorVector2Internal(int32_t gameObjectId, const char* parameterName, const EditorScriptVector2& value);  // DLL API 用に Animator Vector2 を設定する
	bool SetAnimatorVector3Internal(int32_t gameObjectId, const char* parameterName, const EditorScriptVector3& value);  // DLL API 用に Animator Vector3 を設定する
	bool GetAnimatorFloatInternal(int32_t gameObjectId, const char* parameterName, float& value) const;  // DLL API 用に Animator Float を取得する
	bool GetAnimatorIntInternal(int32_t gameObjectId, const char* parameterName, int32_t& value) const;  // DLL API 用に Animator Int を取得する
	bool GetAnimatorBoolInternal(int32_t gameObjectId, const char* parameterName, bool& value) const;  // DLL API 用に Animator Bool / Trigger を取得する
	bool GetAnimatorVector2Internal(int32_t gameObjectId, const char* parameterName, EditorScriptVector2& value) const;  // DLL API 用に Animator Vector2 を取得する
	bool GetAnimatorVector3Internal(int32_t gameObjectId, const char* parameterName, EditorScriptVector3& value) const;  // DLL API 用に Animator Vector3 を取得する
	bool ResetAnimatorTriggerInternal(int32_t gameObjectId, const char* parameterName);  // DLL API 用に Trigger を解除する
	int32_t FindGameObjectByNameInternal(const char* gameObjectName) const;  // DLL API 用に名前から GameObject ID を探す
	bool SetGameObjectActiveInternal(int32_t gameObjectId, bool isActive);  // DLL API 用に GameObject の有効状態を変更する
	bool IsGameObjectActiveInternal(int32_t gameObjectId) const;  // DLL API 用に GameObject の有効状態を取得する
	bool SetComponentActiveInternal(int32_t gameObjectId, const char* componentTypeName, bool isActive);  // DLL API 用に任意 Component の有効状態を変更する
	bool IsComponentActiveInternal(int32_t gameObjectId, const char* componentTypeName) const;  // DLL API 用に任意 Component の有効状態を取得する
	bool HasComponentInternal(int32_t gameObjectId, const char* componentTypeName) const;  // DLL API 用にComponentの有無を取得する
	bool InvokeScriptActionInternal(int32_t gameObjectId, const char* functionName);  // DLL API 用に別Scriptの登録済みActionをキューへ積む
	bool RequestSceneLoadInternal(const std::string& scenePath, bool isAdditive = false, bool isAsynchronous = false);  // Scene読込要求を検証して保留する。
	bool RequestSceneLoadByBuildIndexInternal(int32_t sceneIndex);  // Build Settings の順番から Scene 遷移を要求する。

	static void ScriptLogBridge(const char* message);  // DLL からのログを現在の ScriptManager へ流す
	static bool ScriptIsKeyDownBridge(int32_t keyCode);  // DLL からのキー押下判定を現在の ScriptManager へ流す
	static bool ScriptIsKeyPressedBridge(int32_t keyCode);  // DLL からの押した瞬間判定を現在の ScriptManager へ流す
	static EditorScriptVector2 ScriptGetActionVector2Bridge(int32_t gameObjectId, const char* actionMapName, const char* actionName);  // DLL からの Vector2 Action 取得を現在の ScriptManager へ流す
	static bool ScriptIsActionPressedBridge(int32_t gameObjectId, const char* actionMapName, const char* actionName);  // DLL からの Button Action 押下判定を現在の ScriptManager へ流す
	static bool ScriptWasActionJustPressedBridge(int32_t gameObjectId, const char* actionMapName, const char* actionName);  // DLL からの Button Action 押した瞬間判定を現在の ScriptManager へ流す
	static EditorScriptVector2 ScriptGetMousePositionBridge();  // DLL からのマウス座標取得を現在の ScriptManager へ流す
	static EditorScriptTransform ScriptGetTransformBridge(int32_t gameObjectId);  // DLL からの Transform 取得を現在の ScriptManager へ流す
	static void ScriptSetTransformBridge(int32_t gameObjectId, const EditorScriptTransform* transform);  // DLL からの Transform 設定を現在の ScriptManager へ流す
	static EditorScriptVector3 ScriptGetVelocityBridge(int32_t gameObjectId);  // DLL からの Rigidbody 速度取得を現在の ScriptManager へ流す
	static void ScriptSetVelocityBridge(int32_t gameObjectId, const EditorScriptVector3* velocity);  // DLL からの Rigidbody 速度設定を現在の ScriptManager へ流す
	static EditorScriptVector3 ScriptGetAngularVelocityBridge(int32_t gameObjectId);  // DLL からの Rigidbody 角速度取得を現在の ScriptManager へ流す
	static void ScriptSetAngularVelocityBridge(int32_t gameObjectId, const EditorScriptVector3* angularVelocity);  // DLL からの Rigidbody 角速度設定を現在の ScriptManager へ流す
	static bool ScriptAddForceBridge(int32_t gameObjectId, const EditorScriptVector3* force);  // DLL からの継続力要求を現在の ScriptManager へ流す
	static bool ScriptAddForceAtPositionBridge(int32_t gameObjectId, const EditorScriptVector3* force, const EditorScriptVector3* worldPosition);  // DLL からの作用点付き継続力要求を流す
	static bool ScriptAddImpulseBridge(int32_t gameObjectId, const EditorScriptVector3* impulse);  // DLL からの瞬間力要求を現在の ScriptManager へ流す
	static bool ScriptAddTorqueBridge(int32_t gameObjectId, const EditorScriptVector3* torque);  // DLL からの回転トルク要求を現在の ScriptManager へ流す
	static int32_t ScriptAddExplosionImpulseBridge(const EditorScriptVector3* center, float radius, float impulseStrength, float upwardModifier);  // DLL からの範囲爆発要求を流す
	static bool ScriptAttachRopeBridge(int32_t ownerGameObjectId, int32_t targetGameObjectId, const EditorScriptVector3* ownerLocalAnchor, const EditorScriptVector3* targetAnchor, float maximumLength);  // DLL からのロープ接続要求を流す
	static bool ScriptDetachRopeBridge(int32_t ownerGameObjectId);  // DLL からのロープ解除要求を流す
	static bool ScriptSetRopeLengthBridge(int32_t ownerGameObjectId, float maximumLength);  // DLL からのロープ長変更要求を流す
	static bool ScriptRepairRopeBridge(int32_t ownerGameObjectId);  // DLL からのロープ修復要求を流す
	static EditorScriptRopeState ScriptGetRopeStateBridge(int32_t ownerGameObjectId);  // DLL へロープ状態を返す
	static EditorScriptAiSensorState ScriptGetAiSensorStateBridge(int32_t gameObjectId, int32_t sensorKind);  // DLL からの AI センサー取得を現在の ScriptManager へ流す
	static EditorScriptMaterialState ScriptGetMaterialStateBridge(int32_t gameObjectId);  // DLL からの Material 取得を現在の ScriptManager へ流す
	static EditorScriptAnimationState ScriptGetAnimationStateBridge(int32_t gameObjectId);  // DLL からの Animation 取得を現在の ScriptManager へ流す
	static bool ScriptSetAnimatorFloatBridge(int32_t gameObjectId, const char* parameterName, float value);
	static bool ScriptSetAnimatorIntBridge(int32_t gameObjectId, const char* parameterName, int32_t value);
	static bool ScriptSetAnimatorBoolBridge(int32_t gameObjectId, const char* parameterName, bool value);
	static bool ScriptSetAnimatorTriggerBridge(int32_t gameObjectId, const char* parameterName);
	static bool ScriptSetAnimatorVector2Bridge(int32_t gameObjectId, const char* parameterName, const EditorScriptVector2* value);
	static bool ScriptSetAnimatorVector3Bridge(int32_t gameObjectId, const char* parameterName, const EditorScriptVector3* value);
	static bool ScriptPlayAnimationActionBridge(int32_t gameObjectId, int32_t clipIndex, float blendIn, float blendOut, float playbackSpeed, int32_t priority, bool loop);
	static bool ScriptPlayEffectBridge(int32_t gameObjectId);
	static bool ScriptPlayEffectAtBridge(int32_t gameObjectId, const char* effectAssetPath, const EditorScriptVector3* localOffset);
	static void ScriptStopEffectBridge(int32_t gameObjectId);
	static int32_t ScriptGetAliveParticleCountBridge(int32_t gameObjectId);
	static bool ScriptGetAnimatorFloatBridge(int32_t gameObjectId, const char* parameterName, float* value);
	static bool ScriptGetAnimatorIntBridge(int32_t gameObjectId, const char* parameterName, int32_t* value);
	static bool ScriptGetAnimatorBoolBridge(int32_t gameObjectId, const char* parameterName, bool* value);
	static bool ScriptGetAnimatorVector2Bridge(int32_t gameObjectId, const char* parameterName, EditorScriptVector2* value);
	static bool ScriptGetAnimatorVector3Bridge(int32_t gameObjectId, const char* parameterName, EditorScriptVector3* value);
	static bool ScriptResetAnimatorTriggerBridge(int32_t gameObjectId, const char* parameterName);
	static bool ScriptPlayAnimationBridge(int32_t gameObjectId);
	static bool ScriptStopAnimationBridge(int32_t gameObjectId);
	static bool ScriptIsAnimationPlayingBridge(int32_t gameObjectId);
	static float ScriptGetAnimationTimeBridge(int32_t gameObjectId);
	static bool ScriptSetAnimationTimeBridge(int32_t gameObjectId, float playbackTime);
	static bool ScriptSetAnimationSpeedBridge(int32_t gameObjectId, float playbackSpeed);
	static bool ScriptGetAnimatorStateNameBridge(int32_t gameObjectId, char* stateName, int32_t stateNameCapacity);
	static bool ScriptIsEffectPlayingBridge(int32_t gameObjectId);
	static int32_t ScriptFindGameObjectByNameBridge(const char* gameObjectName);
	static bool ScriptSetGameObjectActiveBridge(int32_t gameObjectId, bool isActive);
	static bool ScriptIsGameObjectActiveBridge(int32_t gameObjectId);
	static bool ScriptSetComponentActiveBridge(int32_t gameObjectId, const char* componentTypeName, bool isActive);
	static bool ScriptIsComponentActiveBridge(int32_t gameObjectId, const char* componentTypeName);
	static bool ScriptLoadSceneBridge(const char* scenePath);
	static bool ScriptLoadSceneByBuildIndexBridge(int32_t sceneIndex);
	static bool ScriptSetRailPausedBridge(int32_t gameObjectId, bool isPaused);
	static bool ScriptIsRailPausedBridge(int32_t gameObjectId);
	static bool ScriptSetRailSpeedBridge(int32_t gameObjectId, float speed);
	static bool ScriptSetRailReverseBridge(int32_t gameObjectId, bool isReversed);
	static bool ScriptSetRailNormalizedProgressBridge(int32_t gameObjectId, float normalizedProgress);
	static bool ScriptSetRailPathBridge(int32_t gameObjectId, int32_t railPathGameObjectId, bool preservesProgress);
	static bool ScriptSetRailMoveInputBridge(int32_t gameObjectId, const EditorScriptVector2* moveInput);
	static bool ScriptSetRailOffsetBridge(int32_t gameObjectId, const EditorScriptVector2* offset);
	static bool ScriptGetRailOffsetBridge(int32_t gameObjectId, EditorScriptVector2* offset);
	static bool ScriptGetRailNormalizedProgressBridge(int32_t gameObjectId, float* normalizedProgress);
	static bool ScriptGetRailLengthBridge(int32_t gameObjectId, float* railLength);
	static bool ScriptGetRailPositionBridge(int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* position);
	static bool ScriptGetRailDirectionBridge(int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* direction);
	static bool ScriptConsumeRailEndReachedBridge(int32_t gameObjectId);
	static bool ScriptViewportPointToRayBridge(const EditorScriptVector2* normalizedPosition, EditorScriptRay* ray);
	static bool ScriptGetAimRayBridge(int32_t screenAimGameObjectId, EditorScriptRay* ray);
	static bool ScriptPhysicsRaycastBridge(const EditorScriptRay* ray, float distance, EditorScriptPhysicsHit* hit);
	static bool ScriptPhysicsSphereCastBridge(const EditorScriptRay* ray, float radius, float distance, EditorScriptPhysicsHit* hit);
	static bool ScriptPhysicsCapsuleCastBridge(const EditorScriptRay* ray, float radius, float height, float distance, EditorScriptPhysicsHit* hit);
	static bool ScriptSampleOceanSurfaceBridge(
		int32_t queryGameObjectId,
		const EditorScriptVector3* worldPosition,
		EditorScriptOceanSurfaceHit* hit);
	static bool ScriptApplyDamageBridge(int32_t targetGameObjectId, float damage, int32_t sourceGameObjectId);
	static bool ScriptApplyDamageContextBridge(EditorScriptDamageContext* damageContext);
	static bool ScriptGetLastDamageContextBridge(int32_t targetGameObjectId, EditorScriptDamageContext* damageContext);
	static bool ScriptGetHealthBridge(int32_t gameObjectId, float* currentHealth, float* maximumHealth);
	static bool ScriptSetHealthBridge(int32_t gameObjectId, float currentHealth);
	static int32_t ScriptSpawnFromPoolBridge(int32_t poolGameObjectId, const EditorScriptVector3* position, const EditorScriptVector3* rotation);
	static int32_t ScriptSpawnFromSpawnerBridge(int32_t spawnerGameObjectId);
	static bool ScriptReleaseToPoolBridge(int32_t gameObjectId);
	static bool ScriptFireHitscanBridge(int32_t weaponGameObjectId);
	static bool ScriptFireProjectileBridge(int32_t emitterGameObjectId);
	static bool ScriptGetWeaponAccuracySpreadBridge(int32_t gameObjectId, float* spreadDegrees);
	static bool ScriptPlayTimeScaleBridge(int32_t gameObjectId, float scaleOverride, float durationOverride);
	static float ScriptGetTimeScaleBridge();
	static bool ScriptPlayCameraBlendBridge(int32_t componentOwnerGameObjectId);
	static bool ScriptPlayCameraShakeBridge(int32_t componentOwnerGameObjectId);
	static bool ScriptTriggerRailBranchBridge(int32_t componentOwnerGameObjectId);
	static bool ScriptLoadSceneAsyncBridge(const char* scenePath, bool isAdditive);
	static bool ScriptUnloadSceneBridge(const char* scenePath);
	static float ScriptGetSceneLoadProgressBridge();
	static bool ScriptIsSceneLoadingBridge();
	static bool ScriptIsSceneLoadedBridge(const char* scenePath);
	static void ScriptSetSceneFloatBridge(const char* key, float value);
	static bool ScriptGetSceneFloatBridge(const char* key, float* value);
	static void ScriptSetSceneStringBridge(const char* key, const char* value);
	static bool ScriptGetSceneStringBridge(const char* key, char* value, int32_t valueCapacity);
	static bool ScriptPlayActionSequenceBridge(int32_t sequenceGameObjectId);
	static bool ScriptPauseActionSequenceBridge(int32_t sequenceGameObjectId, bool isPaused);
	static bool ScriptStopActionSequenceBridge(int32_t sequenceGameObjectId);
	static bool ScriptSignalActionSequenceBridge(int32_t sequenceGameObjectId, const char* signalName);
	static bool ScriptIsActionSequencePlayingBridge(int32_t sequenceGameObjectId);
	static bool ScriptSaveSlotBridge(const char* slotName);
	static bool ScriptLoadSlotBridge(const char* slotName);
	static bool ScriptDeleteSlotBridge(const char* slotName);
	static bool ScriptHasSlotBridge(const char* slotName);
	static bool ScriptActivateCheckpointBridge(int32_t checkpointGameObjectId, bool shouldLoad);
	static void ScriptSetSaveFloatBridge(const char* key, float value);
	static bool ScriptGetSaveFloatBridge(const char* key, float* value);
	static void ScriptSetSaveStringBridge(const char* key, const char* value);
	static bool ScriptGetSaveStringBridge(const char* key, char* value, int32_t valueCapacity);
	static bool ScriptLoadoutSelectSlotBridge(int32_t gameObjectId, int32_t slotIndex);
	static bool ScriptLoadoutSelectNextBridge(int32_t gameObjectId);
	static bool ScriptLoadoutSelectPreviousBridge(int32_t gameObjectId);
	static bool ScriptLoadoutFireBridge(int32_t gameObjectId);
	static bool ScriptLoadoutReloadBridge(int32_t gameObjectId);
	static bool ScriptLoadoutGetAmmoBridge(int32_t gameObjectId, int32_t* currentAmmo, int32_t* reserveAmmo);
	static bool ScriptLoadoutGetAmmoAtSlotBridge(int32_t gameObjectId, int32_t slotIndex, int32_t* currentAmmo, int32_t* reserveAmmo, int32_t* maximumAmmo);
	static bool ScriptLoadoutAddMagazineAmmoBridge(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	static bool ScriptLoadoutAddReserveAmmoBridge(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	static bool ScriptLoadoutSetMagazineAmmoBridge(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	static bool ScriptLoadoutSetReserveAmmoBridge(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	static bool ScriptLoadoutSetMaximumAmmoBridge(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	static bool ScriptLoadoutRefillMagazineBridge(int32_t gameObjectId, int32_t slotIndex);
	static bool ScriptFireWeaponGroupBridge(int32_t gameObjectId);
	static bool ScriptIsWeaponGroupFiringBridge(int32_t gameObjectId);
	static bool ScriptGetTurretAimStateBridge(int32_t gameObjectId, EditorScriptTurretAimState* state);
	static bool ScriptGetFireLineStateBridge(int32_t gameObjectId, EditorScriptFireLineState* state);
	static bool ScriptApplyStatusEffectBridge(int32_t gameObjectId, const char* effectId, int32_t sourceGameObjectId);
	static bool ScriptRemoveStatusEffectBridge(int32_t gameObjectId, const char* effectId);
	static bool ScriptClearStatusEffectsBridge(int32_t gameObjectId);
	static bool ScriptHasStatusEffectBridge(int32_t gameObjectId, const char* effectId);
	static bool ScriptGetStatusEffectCountBridge(int32_t gameObjectId, int32_t* effectCount);
	static bool ScriptGetStatusEffectEntryBridge(int32_t gameObjectId, int32_t effectIndex, EditorScriptStatusEffectEntry* effectEntry);
	static bool ScriptSampleOceanSurfaceDetailedBridge(int32_t queryGameObjectId, const EditorScriptVector3* worldPosition, EditorScriptOceanSurfaceHit* hit, float* foam);
	static bool ScriptGetWaterSurfaceFoamBridge(int32_t gameObjectId, float* foam);
	static bool ScriptGetOceanProbeFoamBridge(int32_t gameObjectId, int32_t probeIndex, float* foam);
	static bool ScriptGetCurrentTargetBridge(int32_t gameObjectId, int32_t* targetGameObjectId);
	static bool ScriptSetExplicitTargetBridge(int32_t gameObjectId, int32_t targetGameObjectId);
	static bool ScriptSetRuntimeFloatBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, float value);
	static bool ScriptGetRuntimeFloatBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, float* value);
	static bool ScriptSetRuntimeIntBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t value);
	static bool ScriptGetRuntimeIntBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t* value);
	static bool ScriptSetRuntimeBoolBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, bool value);
	static bool ScriptGetRuntimeBoolBridge(int32_t gameObjectId, const char* componentName, const char* propertyName, bool* value);
	static bool ScriptSetRuntimeVector2Bridge(int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector2* value);
	static bool ScriptGetRuntimeVector2Bridge(int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector2* value);
	static bool ScriptSetRuntimeVector3Bridge(int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector3* value);
	static bool ScriptGetRuntimeVector3Bridge(int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector3* value);
	static bool ScriptPlayPropertyTweenBridge(int32_t gameObjectId);
	static bool ScriptStopPropertyTweenBridge(int32_t gameObjectId);
	static bool ScriptIsPropertyTweenPlayingBridge(int32_t gameObjectId);
	static bool ScriptRelayActionBridge(int32_t gameObjectId);
	static bool ScriptHasComponentBridge(int32_t gameObjectId, const char* componentTypeName);
	static bool ScriptInvokeScriptActionBridge(int32_t gameObjectId, const char* functionName);
	static bool ScriptInvokeScriptActionPayloadBridge(int32_t gameObjectId, const char* functionName, const EditorScriptActionPayload* payload);
	static bool ScriptStartTimerBridge(int32_t gameObjectId);
	static bool ScriptPauseTimerBridge(int32_t gameObjectId, bool isPaused);
	static bool ScriptGetTimerRemainingBridge(int32_t gameObjectId, float* remainingSeconds);
	static bool ScriptChangeGenericStateBridge(int32_t gameObjectId, const char* stateName);
	static bool ScriptGetGenericStateBridge(int32_t gameObjectId, char* stateName, int32_t stateNameCapacity);
	static bool ScriptSetAttributeValueBridge(int32_t gameObjectId, float value);
	static bool ScriptGetAttributeValueBridge(int32_t gameObjectId, float* current, float* maximum);
	static bool ScriptGetTargetLockStateBridge(int32_t gameObjectId, float* progress, bool* isLocked, int32_t* targetGameObjectId);
	static bool ScriptSetNamedAttributeValueBridge(int32_t gameObjectId, const char* attributeName, float value);
	static bool ScriptGetNamedAttributeValueBridge(int32_t gameObjectId, const char* attributeName, float* current, float* maximum);
	static bool ScriptSetCounterValueBridge(int32_t gameObjectId, float value);
	static bool ScriptAddCounterValueBridge(int32_t gameObjectId, float deltaValue);
	static bool ScriptGetCounterValueBridge(int32_t gameObjectId, float* value);
	static bool ScriptEvaluateGenericConditionBridge(int32_t gameObjectId, bool* result);
	static bool ScriptGetMultiTargetLockCountBridge(int32_t gameObjectId, int32_t* targetCount);
	static bool ScriptGetMultiTargetLockTargetBridge(int32_t gameObjectId, int32_t targetIndex, int32_t* targetGameObjectId, float* progress, bool* isLocked);
	static bool ScriptGetGameplayDataValueBridge(int32_t gameObjectId, const char* key, int32_t* valueType, char* value, int32_t valueCapacity);
	static int32_t ScriptHashDamageTagBridge(const char* damageTag);
	static int32_t ScriptApplyAreaDamageBridge(int32_t areaDamageGameObjectId, int32_t instigatorGameObjectId);
	static bool ScriptDetonateProjectileBridge(int32_t projectileGameObjectId);
	static bool ScriptGetThreatTrackerCountBridge(int32_t gameObjectId, int32_t* threatCount);
	static bool ScriptGetThreatTrackerEntryBridge(int32_t gameObjectId, int32_t threatIndex, EditorScriptThreatInfo* threatInfo);
	static bool ScriptStartNamedCooldownBridge(int32_t gameObjectId, const char* cooldownName, float durationOverride);
	static bool ScriptResetNamedCooldownBridge(int32_t gameObjectId, const char* cooldownName);
	static bool ScriptGetNamedCooldownBridge(int32_t gameObjectId, const char* cooldownName, float* remainingSeconds, bool* isReady);
	static bool ScriptResetRuntimeStateBridge(int32_t gameObjectId);
	static bool ScriptGetRailStateBridge(int32_t gameObjectId, EditorScriptRailState* state);
	static bool ScriptSetRailDistanceBridge(int32_t gameObjectId, float distance);
	static bool ScriptGetRailClosestProgressBridge(int32_t gameObjectId, const EditorScriptVector3* worldPosition, float* normalizedProgress);
	static bool ScriptGetRailFrameBridge(int32_t gameObjectId, float normalizedProgress, EditorScriptRailFrame* frame);
	static bool ScriptSetRailSpeedProfileEnabledBridge(int32_t gameObjectId, bool isEnabled);
	static bool ScriptGetRailSpeedMultiplierBridge(int32_t gameObjectId, float* speedMultiplier);
	static bool ScriptGetRailActiveZoneBridge(int32_t gameObjectId, char* zoneId, int32_t zoneIdCapacity);
	static bool ScriptRearmRailEventMarkersBridge(int32_t gameObjectId, const char* markerId);
	static bool ScriptGetSimulationLodLevelBridge(int32_t gameObjectId, int32_t* lodLevel);
	static bool ScriptStartWaveSpawnerBridge(int32_t gameObjectId);
	static bool ScriptIsWaveSpawnerCompleteBridge(int32_t gameObjectId, bool waitsForAllDefeated);
	static bool ScriptGetInterceptPredictionBridge(int32_t gameObjectId, EditorScriptVector3* position, float* timeSeconds);
	static bool ScriptSetObjectiveBridge(int32_t gameObjectId, const char* objectiveId, int32_t state, float currentValue);
	static bool ScriptGetObjectiveBridge(int32_t gameObjectId, const char* objectiveId, int32_t* state, float* currentValue, float* targetValue);
	static bool ScriptStartEncounterBridge(int32_t gameObjectId);
	static bool ScriptResolveSpawnPointBridge(int32_t gameObjectId, EditorScriptVector3* position, EditorScriptVector3* rotation);
	static bool ScriptApplyDifficultyBridge(int32_t gameObjectId, int32_t difficultyIndex);
	static bool ScriptGetDamageDirectionBridge(int32_t gameObjectId, EditorScriptVector2* direction, float* alpha, int32_t* sourceGameObjectId);
	static bool ScriptGetBallisticPredictionBridge(int32_t gameObjectId, EditorScriptBallisticPrediction* prediction);
	static bool ScriptGetBallisticTrajectoryPointBridge(int32_t gameObjectId, int32_t pointIndex, EditorScriptVector3* point);
	static bool ScriptGetDamageEventBufferCountBridge(int32_t gameObjectId, int32_t* eventCount);
	static bool ScriptGetDamageEventBufferEntryBridge(int32_t gameObjectId, int32_t eventIndex, EditorScriptDamageEvent* damageEvent);
	static bool ScriptSetGamePausedBridge(int32_t gameObjectId, bool isPaused);
	static bool ScriptIsGamePausedBridge();
	static bool ScriptGetSurfaceWakeStateBridge(int32_t gameObjectId, float* speed, float* intensity);
	static bool ScriptOceanSegmentCastBridge(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanSegmentHit* hit);
	static bool ScriptOceanRaycastBridge(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptRay* ray, float maximumDistance, float clearance, EditorScriptOceanSegmentHit* hit);
	static bool ScriptQueryOceanOcclusionBridge(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanOcclusion* occlusion);
	static bool ScriptGetWaterSurfaceStateBridge(int32_t gameObjectId, EditorScriptWaterSurfaceState* state);
	static bool ScriptGetOceanProbeSampleBridge(int32_t gameObjectId, int32_t probeIndex, EditorScriptOceanProbeSample* sample);
};

#pragma warning(pop)
