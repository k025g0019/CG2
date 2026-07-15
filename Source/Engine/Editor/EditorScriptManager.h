#pragma once

#include "EditorAIManager.h"
#include "EditorAnimationManager.h"
#include "EditorInputManager.h"
#include "EditorJoltPhysicsManager.h"
#include "EditorPhysicsManager.h"
#include "EditorScene.h"
#include "EditorScriptApi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

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
		EditorAIManager* aiManager,
		EditorPhysicsManager* physicsManager,
		std::vector<std::string>* consoleMessages);  // Script 実行対象 Scene、入力、AI、物理、Console を受け取る
	void Start();  // Play 開始時に Script / MonoBehaviour の Start を呼ぶ
	void Update(const uint8_t* keyState, float deltaTime);  // 毎フレームの Script 更新を呼ぶ
	void FixedUpdate(float fixedDeltaTime);  // 固定時間更新の Script を呼ぶ
	void SetPhysicsEvents(const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& physicsEvents);  // 衝突イベントを Script 側へ渡す
	void Stop();  // Play 停止時に Script の Stop を呼ぶ
	bool IsStarted() const;  // Start 済みかどうかを返す
	ScriptDebugInfo GetDebugInfo(int32_t gameObjectId) const;  // Inspector 用に DLL の現在状態を返す
	bool RefreshExposedFields(EditorComponent& scriptComponent);  // DLL の公開変数定義を Inspector 用データへ同期する

private:
	struct ScriptBinding {
		int32_t gameObjectId = -1;  // この Script を呼ぶ対象 GameObject ID
		EditorComponentType componentType = EditorComponentType::Script;  // Script か MonoBehaviour かの種類
		std::string dllPath;  // Component に設定された元 DLL パス
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
		EditorScriptStopFn stopFunction = nullptr;  // Play 停止時の終了関数
		EditorScriptGetFieldCountFn getFieldCountFunction = nullptr;  // DLL が公開する Inspector 変数数を返す
		EditorScriptGetFieldDescriptorFn getFieldDescriptorFunction = nullptr;  // 公開変数の型と表示名を返す
		EditorScriptGetFieldValueFn getFieldValueFunction = nullptr;  // Script インスタンスの現在値を返す
		EditorScriptSetFieldValueFn setFieldValueFunction = nullptr;  // Inspector 保存値を Script インスタンスへ設定する
		EditorScriptInvokeActionFn invokeActionFunction = nullptr;  // PlayerInput の関数名を C++ メソッドへ通知する
		bool isLoaded = false;  // 関数取得と初期化まで成功していれば true
		std::vector<int32_t> attachedGameObjectIds;  // この DLL を使っている GameObject 一覧
	};

	struct ScriptMetadata {
		std::filesystem::file_time_type lastWriteTime{};  // 同じ DLL を毎フレーム読み直さないための更新日時
		std::vector<EditorScriptFieldDescriptor> fieldDescriptors;  // Inspector へ表示する公開変数定義
		bool isValid = false;  // メタデータ取得に成功していれば true
	};

	EditorScene* editorScene_ = nullptr;  // Script Component を探す対象 Scene
	EditorInputManager* inputManager_ = nullptr;  // PlayerInput Action を読む入力 API
	EditorAnimationManager* animationManager_ = nullptr;  // Animation の再生状態と現在時刻を読む API
	EditorAIManager* aiManager_ = nullptr;  // AI センサー状態を読む AI API
	EditorPhysicsManager* physicsManager_ = nullptr;  // AddForce / SetVelocity へ接続する物理 API
	std::vector<std::string>* consoleMessages_ = nullptr;  // Script のログやエラーを出す Console
	bool isStarted_ = false;  // Start が呼ばれていれば true
	float lastDeltaTime_ = 0.0f;  // 最後に Update へ渡した秒数
	float lastFixedDeltaTime_ = 0.0f;  // 最後に FixedUpdate へ渡した秒数
	std::vector<EditorJoltPhysicsManager::PhysicsEvent> physicsEvents_;  // OnCollision / OnTrigger 相当の元データ
	std::vector<ScriptBinding> scriptBindings_;  // Scene 内 Script Component から作った実行対象一覧
	std::unordered_map<std::string, ScriptModule> scriptModules_;  // DLL パス単位で 1 度だけロードしたモジュール一覧
	std::unordered_map<std::string, std::string> moduleStatusMessages_;  // DLL ごとの最新状態メッセージ
	std::unordered_map<std::string, ScriptMetadata> scriptMetadataCache_;  // Play 前の Inspector 用 DLL メタデータキャッシュ
	std::unordered_map<std::string, bool> inputActionActiveStates_;  // Vector2 Action の started / canceled 判定用状態
	std::unordered_set<std::string> missingActionWarnings_;  // 未登録関数の警告を同じPlay中に一度だけ出す
	std::array<uint8_t, 256> currentKeyState_{};  // DLL Script から参照する最新キー状態
	std::array<uint8_t, 256> previousKeyState_{};  // 押した瞬間判定用の 1 フレーム前キー状態
	uint64_t reloadGeneration_ = 0;  // 作業 DLL コピー名を毎回変えるための通し番号
	EditorScriptRuntimeApi runtimeApi_{};  // DLL へ渡す関数ポインタ群

	void BuildScriptBindings();  // Scene 内の Script / MonoBehaviour から DLL 実行対象一覧を作る
	void BuildRuntimeApi();  // DLL へ公開する関数ポインタを設定する
	void StartBindingsForModule(ScriptModule& scriptModule);  // DLL を使う全 GameObject に Start を送る
	void StopBindingsForModule(ScriptModule& scriptModule);  // DLL を使う全 GameObject に Stop を送る
	void DispatchInputActions();  // PlayerInput の Action を同じ GameObject の C++ 関数へ通知する
	void ApplyComponentFieldsToInstance(const ScriptBinding& scriptBinding, ScriptModule& scriptModule);  // 保存済み Inspector 値を Script インスタンスへ戻す
	void ReadInstanceFieldsToComponent(const ScriptBinding& scriptBinding, ScriptModule& scriptModule);  // Script が更新した公開変数を Inspector と Scene 保存値へ戻す
	void SynchronizeComponentProperties(EditorComponent& scriptComponent, const std::vector<EditorScriptFieldDescriptor>& fieldDescriptors);  // DLL 定義と保存値を名前で統合する
	bool ReadMetadataFromDll(const std::string& dllPath, ScriptMetadata& scriptMetadata);  // Play 前でも DLL から公開変数定義を取得する
	EditorComponent* FindScriptComponent(const ScriptBinding& scriptBinding);  // Binding が指す Script / MonoBehaviour Component を返す
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
	bool AddImpulseInternal(int32_t gameObjectId, const EditorScriptVector3& impulse);  // DLL API 用に瞬間力を加える
	bool AddTorqueInternal(int32_t gameObjectId, const EditorScriptVector3& torque);  // DLL API 用に回転トルクを加える
	EditorScriptAiSensorState GetAiSensorStateInternal(int32_t gameObjectId, int32_t sensorKind) const;  // DLL API 用に AI センサー状態を返す
	EditorScriptMaterialState GetMaterialStateInternal(int32_t gameObjectId) const;  // DLL API 用に Material 情報を返す
	EditorScriptAnimationState GetAnimationStateInternal(int32_t gameObjectId) const;  // DLL API 用に Animation 情報を返す

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
	static bool ScriptAddImpulseBridge(int32_t gameObjectId, const EditorScriptVector3* impulse);  // DLL からの瞬間力要求を現在の ScriptManager へ流す
	static bool ScriptAddTorqueBridge(int32_t gameObjectId, const EditorScriptVector3* torque);  // DLL からの回転トルク要求を現在の ScriptManager へ流す
	static EditorScriptAiSensorState ScriptGetAiSensorStateBridge(int32_t gameObjectId, int32_t sensorKind);  // DLL からの AI センサー取得を現在の ScriptManager へ流す
	static EditorScriptMaterialState ScriptGetMaterialStateBridge(int32_t gameObjectId);  // DLL からの Material 取得を現在の ScriptManager へ流す
	static EditorScriptAnimationState ScriptGetAnimationStateBridge(int32_t gameObjectId);  // DLL からの Animation 取得を現在の ScriptManager へ流す
};

#pragma warning(pop)
