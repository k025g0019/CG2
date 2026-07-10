#pragma warning(disable : 5045)

#include "EditorScriptManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "StringUtility.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {
	EditorScriptManager* gActiveScriptManager = nullptr;  // DLL API の関数ポインタから現在の ScriptManager を逆参照する。

	bool HasRunnableScriptComponent(const EditorGameObject& gameObject) {
		const EditorComponent* scriptComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Script);
		const EditorComponent* monoBehaviourComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MonoBehaviour);

		const bool hasScript = scriptComponent != nullptr && scriptComponent->isActive;
		const bool hasMonoBehaviour = monoBehaviourComponent != nullptr && monoBehaviourComponent->isActive;
		return gameObject.isActive && (hasScript || hasMonoBehaviour);
	}

	EditorScriptVector3 ToScriptVector3(const Vector3& value) {
		EditorScriptVector3 scriptVector{};
		scriptVector.x = value.x;
		scriptVector.y = value.y;
		scriptVector.z = value.z;
		return scriptVector;
	}

	Vector3 ToEditorVector3(const EditorScriptVector3& value) {
		Vector3 editorVector{};
		editorVector.x = value.x;
		editorVector.y = value.y;
		editorVector.z = value.z;
		return editorVector;
	}

	void CopyStringToFixedBuffer(const std::string& sourceText, char* destination, size_t destinationSize) {
		if (destination == nullptr || destinationSize == 0u) {
			return;
		}

		const size_t copySize = (std::min)(sourceText.size(), destinationSize - 1u);
		std::memcpy(destination, sourceText.data(), copySize);
		destination[copySize] = '\0';
	}

	std::string GetRenderableModelAssetPath(const EditorGameObject& gameObject) {
		const EditorComponent* modelRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* meshFilter =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
		if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
			return meshFilter->assetPath;
		}

		return "";
	}

}

void EditorScriptManager::Initialize(
	EditorScene* editorScene,
	EditorInputManager* inputManager,
	EditorAnimationManager* animationManager,
	EditorAIManager* aiManager,
	EditorPhysicsManager* physicsManager,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // RuntimeManager と同じ Scene を参照し、Play 中だけ Script を処理する。
	inputManager_ = inputManager;  // PlayerInput の Action 名を DLL Script から問い合わせる時に使う。
	animationManager_ = animationManager;  // Animation の再生状態と現在時刻を DLL Script から読む時に使う。
	aiManager_ = aiManager;  // AI センサーや音声 / 顔検知の状態を DLL Script から読む時に使う。
	physicsManager_ = physicsManager;  // DLL Script から Jolt の AddForce / SetVelocity を呼ぶための入口。
	consoleMessages_ = consoleMessages;  // DLL ログや読込失敗を Console へ出す先。
	isStarted_ = false;
	lastDeltaTime_ = 0.0f;
	lastFixedDeltaTime_ = 0.0f;
	physicsEvents_.clear();
	scriptBindings_.clear();
	scriptModules_.clear();
	moduleStatusMessages_.clear();
	currentKeyState_.fill(0);
	previousKeyState_.fill(0);
	reloadGeneration_ = 0;
	BuildRuntimeApi();
	gActiveScriptManager = this;
}

void EditorScriptManager::Start() {
	if (editorScene_ == nullptr) {
		return;
	}

	BuildScriptBindings();
	BuildRuntimeApi();

	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		LoadModule(scriptBinding.dllPath);
	}

	for (auto& scriptModulePair : scriptModules_) {
		StartBindingsForModule(scriptModulePair.second);
	}

	isStarted_ = true;
}

void EditorScriptManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	CopyKeyState(keyState);
	HotReloadChangedModules();
	lastDeltaTime_ = deltaTime;  // DLL 側の Update にそのまま渡す秒数。

	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
		if (scriptModule == nullptr || !scriptModule->isLoaded || scriptModule->updateFunction == nullptr) {
			continue;
		}

		scriptModule->updateFunction(scriptBinding.gameObjectId, deltaTime);
	}
}

void EditorScriptManager::FixedUpdate(float fixedDeltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	lastFixedDeltaTime_ = fixedDeltaTime;  // DLL 側の FixedUpdate にそのまま渡す秒数。

	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
		if (scriptModule == nullptr || !scriptModule->isLoaded) {
			continue;
		}

		if (scriptModule->physicsEventFunction != nullptr) {
			for (const EditorJoltPhysicsManager::PhysicsEvent& physicsEvent : physicsEvents_) {
				if (physicsEvent.collision.selfGameObjectId != scriptBinding.gameObjectId) {
					continue;
				}

				const EditorScriptPhysicsEvent scriptPhysicsEvent = ConvertPhysicsEvent(physicsEvent);
				scriptModule->physicsEventFunction(scriptBinding.gameObjectId, &scriptPhysicsEvent);
			}
		}

		if (scriptModule->fixedUpdateFunction != nullptr) {
			scriptModule->fixedUpdateFunction(scriptBinding.gameObjectId, fixedDeltaTime);
		}
	}
}

void EditorScriptManager::SetPhysicsEvents(const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& physicsEvents) {
	physicsEvents_ = physicsEvents;  // Jolt の接触イベントを Script 実行前に受け取り、FixedUpdate から参照できるようにする。
}

void EditorScriptManager::Stop() {
	if (isStarted_) {
		for (auto& scriptModulePair : scriptModules_) {
			StopBindingsForModule(scriptModulePair.second);
		}
	}

	isStarted_ = false;
	lastDeltaTime_ = 0.0f;
	lastFixedDeltaTime_ = 0.0f;
	physicsEvents_.clear();
	UnloadAllModules();
	scriptBindings_.clear();
	currentKeyState_.fill(0);
	previousKeyState_.fill(0);
}

bool EditorScriptManager::IsStarted() const {
	return isStarted_;
}

EditorScriptManager::ScriptDebugInfo EditorScriptManager::GetDebugInfo(int32_t gameObjectId) const {
	ScriptDebugInfo debugInfo{};
	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		if (scriptBinding.gameObjectId != gameObjectId) {
			continue;
		}

		debugInfo.hasBinding = true;
		debugInfo.sourceDllPath = scriptBinding.dllPath;
		break;
	}

	if (debugInfo.sourceDllPath.empty() && editorScene_ != nullptr) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject != nullptr) {
			const EditorComponent* scriptComponent =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Script);
			if (scriptComponent != nullptr && !scriptComponent->assetPath.empty()) {
				debugInfo.hasBinding = scriptComponent->isActive;
				debugInfo.sourceDllPath = scriptComponent->assetPath;
			}

			const EditorComponent* monoBehaviourComponent =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::MonoBehaviour);
			if (debugInfo.sourceDllPath.empty() &&
				monoBehaviourComponent != nullptr &&
				!monoBehaviourComponent->assetPath.empty()) {
				debugInfo.hasBinding = monoBehaviourComponent->isActive;
				debugInfo.sourceDllPath = monoBehaviourComponent->assetPath;
			}
		}
	}

	if (!debugInfo.sourceDllPath.empty()) {
		std::error_code fileError;
		debugInfo.sourceDllExists =
			std::filesystem::exists(std::filesystem::path(debugInfo.sourceDllPath), fileError) && !fileError;
	}

	const ScriptModule* scriptModule = nullptr;
	if (!debugInfo.sourceDllPath.empty()) {
		const auto moduleIterator = scriptModules_.find(debugInfo.sourceDllPath);
		if (moduleIterator != scriptModules_.end()) {
			scriptModule = &moduleIterator->second;
		}
	}

	if (scriptModule != nullptr) {
		debugInfo.isLoaded = scriptModule->isLoaded;
		debugInfo.loadedDllPath = scriptModule->loadedDllPath;
		debugInfo.reloadGeneration = reloadGeneration_;
	}

	const auto statusIterator = moduleStatusMessages_.find(debugInfo.sourceDllPath);
	if (statusIterator != moduleStatusMessages_.end()) {
		debugInfo.lastStatusMessage = statusIterator->second;
	}

	return debugInfo;
}

void EditorScriptManager::ScriptLogBridge(const char* message) {
	if (gActiveScriptManager == nullptr || message == nullptr) {
		return;
	}

	gActiveScriptManager->PushConsoleMessage(message);
}

bool EditorScriptManager::ScriptIsKeyDownBridge(int32_t keyCode) {
	if (gActiveScriptManager == nullptr) {
		return false;
	}

	return gActiveScriptManager->IsKeyDownInternal(keyCode);
}

bool EditorScriptManager::ScriptIsKeyPressedBridge(int32_t keyCode) {
	if (gActiveScriptManager == nullptr) {
		return false;
	}

	return gActiveScriptManager->IsKeyPressedInternal(keyCode);
}

EditorScriptVector2 EditorScriptManager::ScriptGetActionVector2Bridge(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) {
	EditorScriptVector2 actionValue{};
	if (gActiveScriptManager == nullptr) {
		return actionValue;
	}

	return gActiveScriptManager->GetActionVector2Internal(gameObjectId, actionMapName, actionName);
}

bool EditorScriptManager::ScriptIsActionPressedBridge(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) {
	if (gActiveScriptManager == nullptr) {
		return false;
	}

	return gActiveScriptManager->IsActionPressedInternal(gameObjectId, actionMapName, actionName);
}

bool EditorScriptManager::ScriptWasActionJustPressedBridge(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) {
	if (gActiveScriptManager == nullptr) {
		return false;
	}

	return gActiveScriptManager->WasActionJustPressedInternal(gameObjectId, actionMapName, actionName);
}

EditorScriptVector2 EditorScriptManager::ScriptGetMousePositionBridge() {
	EditorScriptVector2 mousePosition{};
	if (gActiveScriptManager == nullptr) {
		return mousePosition;
	}

	return gActiveScriptManager->GetMousePositionInternal();
}

EditorScriptTransform EditorScriptManager::ScriptGetTransformBridge(int32_t gameObjectId) {
	EditorScriptTransform transform{};
	if (gActiveScriptManager == nullptr) {
		return transform;
	}

	return gActiveScriptManager->GetTransformInternal(gameObjectId);
}

void EditorScriptManager::ScriptSetTransformBridge(int32_t gameObjectId, const EditorScriptTransform* transform) {
	if (gActiveScriptManager == nullptr || transform == nullptr) {
		return;
	}

	gActiveScriptManager->SetTransformInternal(gameObjectId, *transform);
}

EditorScriptVector3 EditorScriptManager::ScriptGetVelocityBridge(int32_t gameObjectId) {
	EditorScriptVector3 velocity{};
	if (gActiveScriptManager == nullptr) {
		return velocity;
	}

	return gActiveScriptManager->GetVelocityInternal(gameObjectId);
}

void EditorScriptManager::ScriptSetVelocityBridge(int32_t gameObjectId, const EditorScriptVector3* velocity) {
	if (gActiveScriptManager == nullptr || velocity == nullptr) {
		return;
	}

	gActiveScriptManager->SetVelocityInternal(gameObjectId, *velocity);
}

EditorScriptVector3 EditorScriptManager::ScriptGetAngularVelocityBridge(int32_t gameObjectId) {
	EditorScriptVector3 angularVelocity{};
	if (gActiveScriptManager == nullptr) {
		return angularVelocity;
	}

	return gActiveScriptManager->GetAngularVelocityInternal(gameObjectId);
}

void EditorScriptManager::ScriptSetAngularVelocityBridge(int32_t gameObjectId, const EditorScriptVector3* angularVelocity) {
	if (gActiveScriptManager == nullptr || angularVelocity == nullptr) {
		return;
	}

	gActiveScriptManager->SetAngularVelocityInternal(gameObjectId, *angularVelocity);
}

bool EditorScriptManager::ScriptAddForceBridge(int32_t gameObjectId, const EditorScriptVector3* force) {
	if (gActiveScriptManager == nullptr || force == nullptr) {
		return false;
	}

	return gActiveScriptManager->AddForceInternal(gameObjectId, *force);
}

bool EditorScriptManager::ScriptAddImpulseBridge(int32_t gameObjectId, const EditorScriptVector3* impulse) {
	if (gActiveScriptManager == nullptr || impulse == nullptr) {
		return false;
	}

	return gActiveScriptManager->AddImpulseInternal(gameObjectId, *impulse);
}

bool EditorScriptManager::ScriptAddTorqueBridge(int32_t gameObjectId, const EditorScriptVector3* torque) {
	if (gActiveScriptManager == nullptr || torque == nullptr) {
		return false;
	}

	return gActiveScriptManager->AddTorqueInternal(gameObjectId, *torque);
}

EditorScriptAiSensorState EditorScriptManager::ScriptGetAiSensorStateBridge(int32_t gameObjectId, int32_t sensorKind) {
	EditorScriptAiSensorState sensorState{};
	if (gActiveScriptManager == nullptr) {
		return sensorState;
	}

	return gActiveScriptManager->GetAiSensorStateInternal(gameObjectId, sensorKind);
}

EditorScriptMaterialState EditorScriptManager::ScriptGetMaterialStateBridge(int32_t gameObjectId) {
	EditorScriptMaterialState materialState{};
	if (gActiveScriptManager == nullptr) {
		return materialState;
	}

	return gActiveScriptManager->GetMaterialStateInternal(gameObjectId);
}

EditorScriptAnimationState EditorScriptManager::ScriptGetAnimationStateBridge(int32_t gameObjectId) {
	EditorScriptAnimationState animationState{};
	if (gActiveScriptManager == nullptr) {
		return animationState;
	}

	return gActiveScriptManager->GetAnimationStateInternal(gameObjectId);
}

void EditorScriptManager::BuildScriptBindings() {
	scriptBindings_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!HasRunnableScriptComponent(gameObject)) {
			continue;
		}

		for (const EditorComponent& component : gameObject.components) {
			const bool isScriptComponent =
				component.type == EditorComponentType::Script ||
				component.type == EditorComponentType::MonoBehaviour;

			if (!isScriptComponent || !component.isActive || component.assetPath.empty()) {
				continue;
			}

			const bool isDllPath =
				component.assetPath.size() >= 4 &&
				component.assetPath.substr(component.assetPath.size() - 4) == ".dll";

			if (!isDllPath) {
				continue;
			}

			ScriptBinding scriptBinding{};
			scriptBinding.gameObjectId = gameObject.id;
			scriptBinding.componentType = component.type;
			scriptBinding.dllPath = component.assetPath;
			scriptBindings_.push_back(scriptBinding);
		}
	}
}

void EditorScriptManager::BuildRuntimeApi() {
	runtimeApi_.apiVersion = kEditorScriptApiVersion;
	runtimeApi_.Log = ScriptLogBridge;
	runtimeApi_.IsKeyDown = ScriptIsKeyDownBridge;
	runtimeApi_.IsKeyPressed = ScriptIsKeyPressedBridge;
	runtimeApi_.GetActionVector2 = ScriptGetActionVector2Bridge;
	runtimeApi_.IsActionPressed = ScriptIsActionPressedBridge;
	runtimeApi_.WasActionJustPressed = ScriptWasActionJustPressedBridge;
	runtimeApi_.GetMousePosition = ScriptGetMousePositionBridge;
	runtimeApi_.GetTransform = ScriptGetTransformBridge;
	runtimeApi_.SetTransform = ScriptSetTransformBridge;
	runtimeApi_.GetVelocity = ScriptGetVelocityBridge;
	runtimeApi_.SetVelocity = ScriptSetVelocityBridge;
	runtimeApi_.GetAngularVelocity = ScriptGetAngularVelocityBridge;
	runtimeApi_.SetAngularVelocity = ScriptSetAngularVelocityBridge;
	runtimeApi_.AddForce = ScriptAddForceBridge;
	runtimeApi_.AddImpulse = ScriptAddImpulseBridge;
	runtimeApi_.AddTorque = ScriptAddTorqueBridge;
	runtimeApi_.GetAiSensorState = ScriptGetAiSensorStateBridge;
	runtimeApi_.GetMaterialState = ScriptGetMaterialStateBridge;
	runtimeApi_.GetAnimationState = ScriptGetAnimationStateBridge;
}

void EditorScriptManager::StartBindingsForModule(ScriptModule& scriptModule) {
	if (!scriptModule.isLoaded || scriptModule.startFunction == nullptr) {
		return;
	}

	for (const int32_t gameObjectId : scriptModule.attachedGameObjectIds) {
		scriptModule.startFunction(gameObjectId);
	}
}

void EditorScriptManager::StopBindingsForModule(ScriptModule& scriptModule) {
	if (!scriptModule.isLoaded || scriptModule.stopFunction == nullptr) {
		return;
	}

	for (const int32_t gameObjectId : scriptModule.attachedGameObjectIds) {
		scriptModule.stopFunction(gameObjectId);
	}
}

void EditorScriptManager::HotReloadChangedModules() {
	for (auto& scriptModulePair : scriptModules_) {
		ScriptModule& scriptModule = scriptModulePair.second;
		const std::filesystem::path sourcePath = std::filesystem::path(scriptModule.sourceDllPath);
		std::error_code fileError;

		if (!std::filesystem::exists(sourcePath, fileError)) {
			continue;
		}

		const std::filesystem::file_time_type currentWriteTime = std::filesystem::last_write_time(sourcePath, fileError);
		if (fileError || currentWriteTime == scriptModule.lastWriteTime) {
			continue;
		}

		StopBindingsForModule(scriptModule);
		UnloadModule(scriptModule);
		if (LoadModule(scriptModule.sourceDllPath)) {
			ScriptModule* reloadedModule = FindModule(scriptModule.sourceDllPath);
			if (reloadedModule != nullptr) {
				StartBindingsForModule(*reloadedModule);
				PushConsoleMessage("DLL 再読み込み: " + scriptModule.sourceDllPath);
			}
		}
	}
}

void EditorScriptManager::UnloadAllModules() {
	for (auto& scriptModulePair : scriptModules_) {
		UnloadModule(scriptModulePair.second);
	}

	scriptModules_.clear();
}

bool EditorScriptManager::LoadModule(const std::string& dllPath) {
	const std::filesystem::path sourcePath = std::filesystem::absolute(std::filesystem::path(dllPath));
	std::error_code fileError;

	if (!std::filesystem::exists(sourcePath, fileError)) {
		moduleStatusMessages_[dllPath] = "DLL 読み込み失敗: ファイルが見つからない";
		PushConsoleMessage("DLL 読み込み失敗: " + dllPath);
		return false;
	}

	ScriptModule& scriptModule = scriptModules_[dllPath];
	scriptModule.sourceDllPath = dllPath;
	scriptModule.attachedGameObjectIds.clear();

	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		if (scriptBinding.dllPath == dllPath) {
			scriptModule.attachedGameObjectIds.push_back(scriptBinding.gameObjectId);
		}
	}

	scriptModule.lastWriteTime = std::filesystem::last_write_time(sourcePath, fileError);
	if (fileError) {
		moduleStatusMessages_[dllPath] = "DLL 更新日時取得失敗";
		PushConsoleMessage("DLL 更新日時取得失敗: " + dllPath);
		return false;
	}

	const std::filesystem::path cacheDirectory = std::filesystem::path("runtime_cache") / "scripts";
	std::filesystem::create_directories(cacheDirectory, fileError);
	if (fileError) {
		moduleStatusMessages_[dllPath] = "DLL キャッシュフォルダ作成失敗";
		PushConsoleMessage("DLL キャッシュフォルダ作成失敗: " + cacheDirectory.generic_string());
		return false;
	}

	reloadGeneration_++;
	const std::string copiedFileName =
		sourcePath.stem().generic_string() + "_" + std::to_string(reloadGeneration_) + ".dll";
	const std::filesystem::path copiedDllPath = cacheDirectory / copiedFileName;

	std::filesystem::copy_file(
		sourcePath,
		copiedDllPath,
		std::filesystem::copy_options::overwrite_existing,
		fileError);
	if (fileError) {
		moduleStatusMessages_[dllPath] = "DLL コピー失敗";
		PushConsoleMessage("DLL コピー失敗: " + sourcePath.generic_string());
		return false;
	}

	const std::wstring copiedDllPathWide = ConvertString(copiedDllPath.generic_string());
	HMODULE moduleHandle = LoadLibraryW(copiedDllPathWide.c_str());
	if (moduleHandle == nullptr) {
		moduleStatusMessages_[dllPath] = "LoadLibrary 失敗";
		PushConsoleMessage("LoadLibrary 失敗: " + dllPath);
		return false;
	}

	scriptModule.loadedDllPath = copiedDllPath.generic_string();
	scriptModule.moduleHandle = moduleHandle;

#pragma warning(push)
#pragma warning(disable : 4191)
	scriptModule.loadFunction =
		reinterpret_cast<EditorScriptLoadFn>(GetProcAddress(moduleHandle, "EditorScript_Load"));
	scriptModule.unloadFunction =
		reinterpret_cast<EditorScriptUnloadFn>(GetProcAddress(moduleHandle, "EditorScript_Unload"));
	scriptModule.startFunction =
		reinterpret_cast<EditorScriptStartFn>(GetProcAddress(moduleHandle, "EditorScript_Start"));
	scriptModule.updateFunction =
		reinterpret_cast<EditorScriptUpdateFn>(GetProcAddress(moduleHandle, "EditorScript_Update"));
	scriptModule.fixedUpdateFunction =
		reinterpret_cast<EditorScriptFixedUpdateFn>(GetProcAddress(moduleHandle, "EditorScript_FixedUpdate"));
	scriptModule.physicsEventFunction =
		reinterpret_cast<EditorScriptPhysicsEventFn>(GetProcAddress(moduleHandle, "EditorScript_OnPhysicsEvent"));
	scriptModule.stopFunction =
		reinterpret_cast<EditorScriptStopFn>(GetProcAddress(moduleHandle, "EditorScript_Stop"));
#pragma warning(pop)

	if (scriptModule.loadFunction == nullptr || !scriptModule.loadFunction(kEditorScriptApiVersion, &runtimeApi_)) {
		moduleStatusMessages_[dllPath] = "DLL 初期化失敗";
		PushConsoleMessage("DLL 初期化失敗: " + dllPath);
		UnloadModule(scriptModule);
		return false;
	}

	scriptModule.isLoaded = true;
	moduleStatusMessages_[dllPath] = "DLL 読み込み成功";
	PushConsoleMessage("DLL 読み込み: " + dllPath);
	return true;
}

void EditorScriptManager::UnloadModule(ScriptModule& scriptModule) {
	if (scriptModule.unloadFunction != nullptr) {
		scriptModule.unloadFunction();
	}

	if (scriptModule.moduleHandle != nullptr) {
		FreeLibrary(reinterpret_cast<HMODULE>(scriptModule.moduleHandle));
	}

	if (!scriptModule.loadedDllPath.empty()) {
		std::error_code fileError;
		std::filesystem::remove(std::filesystem::path(scriptModule.loadedDllPath), fileError);
	}

	scriptModule.moduleHandle = nullptr;
	scriptModule.loadFunction = nullptr;
	scriptModule.unloadFunction = nullptr;
	scriptModule.startFunction = nullptr;
	scriptModule.updateFunction = nullptr;
	scriptModule.fixedUpdateFunction = nullptr;
	scriptModule.physicsEventFunction = nullptr;
	scriptModule.stopFunction = nullptr;
	scriptModule.isLoaded = false;
}

EditorScriptManager::ScriptModule* EditorScriptManager::FindModule(const std::string& dllPath) {
	const auto scriptModuleIt = scriptModules_.find(dllPath);
	if (scriptModuleIt == scriptModules_.end()) {
		return nullptr;
	}

	return &scriptModuleIt->second;
}

void EditorScriptManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ == nullptr) {
		return;
	}

	consoleMessages_->push_back(message);
}

void EditorScriptManager::CopyKeyState(const uint8_t* keyState) {
	previousKeyState_ = currentKeyState_;

	if (keyState == nullptr) {
		currentKeyState_.fill(0);
		return;
	}

	for (size_t keyIndex = 0; keyIndex < currentKeyState_.size(); keyIndex++) {
		currentKeyState_[keyIndex] = keyState[keyIndex];
	}
}

EditorScriptPhysicsEvent EditorScriptManager::ConvertPhysicsEvent(
	const EditorJoltPhysicsManager::PhysicsEvent& physicsEvent) const {
	EditorScriptPhysicsEvent scriptPhysicsEvent{};
	scriptPhysicsEvent.selfGameObjectId = physicsEvent.collision.selfGameObjectId;
	scriptPhysicsEvent.otherGameObjectId = physicsEvent.collision.otherGameObjectId;
	scriptPhysicsEvent.point = ToScriptVector3(physicsEvent.collision.point);
	scriptPhysicsEvent.normal = ToScriptVector3(physicsEvent.collision.normal);
	scriptPhysicsEvent.relativeVelocity = ToScriptVector3(physicsEvent.collision.relativeVelocity);
	scriptPhysicsEvent.separation = physicsEvent.collision.separation;
	scriptPhysicsEvent.isTrigger = physicsEvent.collision.isTrigger;

	switch (physicsEvent.type) {
	case EditorJoltPhysicsManager::PhysicsEventType::CollisionEnter:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeCollisionEnter;
		break;
	case EditorJoltPhysicsManager::PhysicsEventType::CollisionStay:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeCollisionStay;
		break;
	case EditorJoltPhysicsManager::PhysicsEventType::CollisionExit:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeCollisionExit;
		break;
	case EditorJoltPhysicsManager::PhysicsEventType::TriggerEnter:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeTriggerEnter;
		break;
	case EditorJoltPhysicsManager::PhysicsEventType::TriggerStay:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeTriggerStay;
		break;
	case EditorJoltPhysicsManager::PhysicsEventType::TriggerExit:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeTriggerExit;
		break;
	default:
		scriptPhysicsEvent.type = EditorScriptPhysicsEventTypeCollisionEnter;
		break;
	}

	return scriptPhysicsEvent;
}

bool EditorScriptManager::IsKeyDownInternal(int32_t keyCode) const {
	if (keyCode < 0 || keyCode >= static_cast<int32_t>(currentKeyState_.size())) {
		return false;
	}

	return currentKeyState_[static_cast<size_t>(keyCode)] != 0;
}

bool EditorScriptManager::IsKeyPressedInternal(int32_t keyCode) const {
	if (keyCode < 0 || keyCode >= static_cast<int32_t>(currentKeyState_.size())) {
		return false;
	}

	const size_t keyIndex = static_cast<size_t>(keyCode);
	return currentKeyState_[keyIndex] != 0 && previousKeyState_[keyIndex] == 0;
}

EditorScriptVector2 EditorScriptManager::GetActionVector2Internal(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) const {
	EditorScriptVector2 actionValue{};
	if (inputManager_ == nullptr || actionMapName == nullptr || actionName == nullptr) {
		return actionValue;
	}

	float x = 0.0f;
	float y = 0.0f;
	if (inputManager_->TryGetActionVector2(gameObjectId, actionMapName, actionName, x, y)) {
		actionValue.x = x;
		actionValue.y = y;
	}

	return actionValue;
}

bool EditorScriptManager::IsActionPressedInternal(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) const {
	if (inputManager_ == nullptr || actionMapName == nullptr || actionName == nullptr) {
		return false;
	}

	return inputManager_->IsActionPressed(gameObjectId, actionMapName, actionName);
}

bool EditorScriptManager::WasActionJustPressedInternal(
	int32_t gameObjectId,
	const char* actionMapName,
	const char* actionName) const {
	if (inputManager_ == nullptr || actionMapName == nullptr || actionName == nullptr) {
		return false;
	}

	return inputManager_->WasActionJustPressed(gameObjectId, actionMapName, actionName);
}

EditorScriptVector2 EditorScriptManager::GetMousePositionInternal() const {
	EditorScriptVector2 mousePosition{};
	POINT cursorPoint{};
	if (!GetCursorPos(&cursorPoint)) {
		return mousePosition;
	}

	HWND windowHandle = GetForegroundWindow();
	if (windowHandle != nullptr) {
		ScreenToClient(windowHandle, &cursorPoint);
	}

	mousePosition.x = static_cast<float>(cursorPoint.x);
	mousePosition.y = static_cast<float>(cursorPoint.y);
	return mousePosition;
}

EditorScriptTransform EditorScriptManager::GetTransformInternal(int32_t gameObjectId) const {
	EditorScriptTransform transform{};

	if (editorScene_ == nullptr) {
		return transform;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return transform;
	}

	transform.position = ToScriptVector3(gameObject->translate);
	transform.rotation = ToScriptVector3(gameObject->rotate);
	transform.scale = ToScriptVector3(gameObject->scale);
	return transform;
}

void EditorScriptManager::SetTransformInternal(int32_t gameObjectId, const EditorScriptTransform& transform) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	gameObject->translate = ToEditorVector3(transform.position);
	gameObject->rotate = ToEditorVector3(transform.rotation);
	gameObject->scale = ToEditorVector3(transform.scale);
}

EditorScriptVector3 EditorScriptManager::GetVelocityInternal(int32_t gameObjectId) const {
	EditorScriptVector3 velocity{};

	if (editorScene_ == nullptr) {
		return velocity;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return velocity;
	}

	const EditorComponent* rigidBodyComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
	if (rigidBodyComponent == nullptr) {
		return velocity;
	}

	return ToScriptVector3(rigidBodyComponent->velocity);
}

void EditorScriptManager::SetVelocityInternal(int32_t gameObjectId, const EditorScriptVector3& velocity) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	EditorComponent* rigidBodyComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
	if (rigidBodyComponent != nullptr) {
		rigidBodyComponent->velocity = ToEditorVector3(velocity);
	}

	if (physicsManager_ != nullptr) {
		physicsManager_->SetVelocity(gameObjectId, ToEditorVector3(velocity));
	}
}

EditorScriptVector3 EditorScriptManager::GetAngularVelocityInternal(int32_t gameObjectId) const {
	EditorScriptVector3 angularVelocity{};

	if (editorScene_ == nullptr) {
		return angularVelocity;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return angularVelocity;
	}

	const EditorComponent* rigidBodyComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
	if (rigidBodyComponent == nullptr) {
		return angularVelocity;
	}

	return ToScriptVector3(rigidBodyComponent->angularVelocity);
}

void EditorScriptManager::SetAngularVelocityInternal(int32_t gameObjectId, const EditorScriptVector3& angularVelocity) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	EditorComponent* rigidBodyComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
	if (rigidBodyComponent != nullptr) {
		rigidBodyComponent->angularVelocity = ToEditorVector3(angularVelocity);
	}

	if (physicsManager_ != nullptr) {
		physicsManager_->SetAngularVelocity(gameObjectId, ToEditorVector3(angularVelocity));
	}
}

bool EditorScriptManager::AddForceInternal(int32_t gameObjectId, const EditorScriptVector3& force) {
	if (physicsManager_ == nullptr) {
		return false;
	}

	return physicsManager_->AddForce(gameObjectId, ToEditorVector3(force));
}

bool EditorScriptManager::AddImpulseInternal(int32_t gameObjectId, const EditorScriptVector3& impulse) {
	if (physicsManager_ == nullptr) {
		return false;
	}

	return physicsManager_->AddImpulse(gameObjectId, ToEditorVector3(impulse));
}

bool EditorScriptManager::AddTorqueInternal(int32_t gameObjectId, const EditorScriptVector3& torque) {
	if (physicsManager_ == nullptr) {
		return false;
	}

	return physicsManager_->AddTorque(gameObjectId, ToEditorVector3(torque));
}

EditorScriptAiSensorState EditorScriptManager::GetAiSensorStateInternal(int32_t gameObjectId, int32_t sensorKind) const {
	EditorScriptAiSensorState sensorState{};
	sensorState.connectedGameObjectId = -1;
	sensorState.detectedGameObjectId = -1;
	sensorState.commandId = -1;
	if (editorScene_ == nullptr) {
		return sensorState;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return sensorState;
	}

	EditorComponentType sensorComponentType = EditorComponentType::AIVisionSensor;
	if (sensorKind == EditorScriptAiSensorKindObjectDetection) {
		sensorComponentType = EditorComponentType::AIOpenCvObjectDetector;
	}
	else if (sensorKind == EditorScriptAiSensorKindColorTracking) {
		sensorComponentType = EditorComponentType::AIOpenCvColorTracker;
	}
	else if (sensorKind == EditorScriptAiSensorKindMotionDetection) {
		sensorComponentType = EditorComponentType::AIMotionSensor;
	}
	else if (sensorKind == EditorScriptAiSensorKindWhisperSpeech) {
		sensorComponentType = EditorComponentType::AIWhisperSpeechRecognizer;
	}
	else if (sensorKind == EditorScriptAiSensorKindVoiceCommand) {
		sensorComponentType = EditorComponentType::AIVoiceCommand;
	}

	const EditorComponent* sensorComponent = EditorComponentUtility::FindComponent(*gameObject, sensorComponentType);
	if (sensorComponent == nullptr) {
		return sensorState;
	}

	sensorState.hasComponent = true;
	sensorState.isActive = sensorComponent->isActive;
	sensorState.connectedGameObjectId = sensorComponent->connectedGameObjectId;
	sensorState.range = sensorComponent->colliderRadius;
	sensorState.angleDegrees = sensorComponent->colliderSize.x;
	if (sensorComponent->isActive && aiManager_ != nullptr) {
		EditorAiSensorResult sensorResult{};
		if (aiManager_->TryGetSensorResult(gameObjectId, sensorComponentType, sensorResult)) {
			sensorState.isDetected = sensorResult.isDetected;
			sensorState.hasDetails = sensorResult.hasDetails;
			sensorState.connectedGameObjectId = sensorResult.connectedGameObjectId;
			sensorState.detectedGameObjectId = sensorResult.detectedGameObjectId;
			sensorState.commandId = sensorResult.commandId;
			sensorState.range = sensorResult.range;
			sensorState.angleDegrees = sensorResult.angleDegrees;
			sensorState.confidence = sensorResult.confidence;
			sensorState.distance = sensorResult.distance;
			sensorState.direction = ToScriptVector3(sensorResult.direction);
			sensorState.screenPosition = {sensorResult.screenX, sensorResult.screenY};
			sensorState.boundsPosition = {sensorResult.boundsX, sensorResult.boundsY};
			sensorState.boundsSize = {sensorResult.boundsWidth, sensorResult.boundsHeight};
			sensorState.motion = {sensorResult.motionX, sensorResult.motionY};
			sensorState.motionMagnitude = sensorResult.motionMagnitude;
			CopyStringToFixedBuffer(sensorResult.label, sensorState.label, sizeof(sensorState.label));
			CopyStringToFixedBuffer(sensorResult.text, sensorState.text, sizeof(sensorState.text));
			CopyStringToFixedBuffer(sensorResult.command, sensorState.command, sizeof(sensorState.command));
		}
	}

	return sensorState;
}

EditorScriptMaterialState EditorScriptManager::GetMaterialStateInternal(int32_t gameObjectId) const {
	EditorScriptMaterialState materialState{};
	materialState.intensity = 1.0f;
	materialState.roughness = 0.5f;
	materialState.ior = 1.0f;
	materialState.alpha = 1.0f;
	materialState.color = {1.0f, 1.0f, 1.0f};

	if (editorScene_ == nullptr) {
		return materialState;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return materialState;
	}

	const EditorComponent* rendererComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::ModelRenderer);
	if (rendererComponent == nullptr) {
		rendererComponent =
			EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::SkinnedMeshRenderer);
	}

	if (rendererComponent == nullptr) {
		return materialState;
	}

	materialState.hasComponent = true;
	materialState.useLighting = true;
	materialState.intensity = rendererComponent->intensity;
	materialState.metallic = rendererComponent->metallic;
	materialState.roughness = rendererComponent->roughness;
	materialState.ior = rendererComponent->ior;
	materialState.alpha = rendererComponent->alpha;
	materialState.reflectionStrength = rendererComponent->reflectionStrength;
	materialState.color = ToScriptVector3(rendererComponent->color);

	std::string rendererAssetPath = rendererComponent->assetPath;
	if (rendererAssetPath.empty()) {
		rendererAssetPath = GetRenderableModelAssetPath(*gameObject);
	}

	CopyStringToFixedBuffer(rendererAssetPath, materialState.rendererAssetPath, sizeof(materialState.rendererAssetPath));

	ModelData modelData{};
	if (rendererAssetPath.empty() || !EditorAssetUtility::LoadModelAsset(rendererAssetPath, modelData)) {
		return materialState;
	}

	const MaterialData& materialData = modelData.material;
	materialState.hasTexture = !materialData.textureFilePath.empty();
	materialState.hasUvLayoutTexture = !materialData.uvLayoutTextureFilePath.empty();
	CopyStringToFixedBuffer(materialData.name, materialState.materialName, sizeof(materialState.materialName));
	CopyStringToFixedBuffer(materialData.textureFilePath, materialState.texturePath, sizeof(materialState.texturePath));
	CopyStringToFixedBuffer(
		materialData.uvLayoutTextureFilePath,
		materialState.uvLayoutTexturePath,
		sizeof(materialState.uvLayoutTexturePath));
	return materialState;
}

EditorScriptAnimationState EditorScriptManager::GetAnimationStateInternal(int32_t gameObjectId) const {
	EditorScriptAnimationState animationState{};
	if (editorScene_ == nullptr) {
		return animationState;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return animationState;
	}

	const EditorComponent* animationComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Animation);
	if (animationComponent == nullptr) {
		return animationState;
	}

	animationState.hasComponent = true;
	animationState.isLoop = animationComponent->animationLoop;
	animationState.playOnAwake = animationComponent->animationPlayOnAwake;
	animationState.animationType = animationComponent->animationType;
	animationState.animationSpeed = animationComponent->animationSpeed;
	animationState.animationAmplitude = animationComponent->animationAmplitude;
	animationState.isPlaying = false;
	animationState.currentTime = 0.0f;

	if (animationManager_ != nullptr) {
		animationState.isPlaying = animationManager_->IsAnimationPlaying(gameObjectId);
		animationState.currentTime = animationManager_->GetAnimationTime(gameObjectId);
	}
	else {
		animationState.isPlaying = isStarted_ && animationComponent->isActive;
	}

	std::string animationAssetPath = animationComponent->assetPath;
	if (animationAssetPath.empty()) {
		animationAssetPath = GetRenderableModelAssetPath(*gameObject);
	}

	CopyStringToFixedBuffer(animationAssetPath, animationState.assetPath, sizeof(animationState.assetPath));

	ModelData modelData{};
	if (animationAssetPath.empty() || !EditorAssetUtility::LoadModelAsset(animationAssetPath, modelData)) {
		return animationState;
	}

	animationState.clipCount = static_cast<int32_t>(modelData.animationClips.size());
	if (!modelData.animationClips.empty()) {
		animationState.currentClipDuration = modelData.animationClips.front().durationSeconds;
		CopyStringToFixedBuffer(
			modelData.animationClips.front().name,
			animationState.currentClipName,
			sizeof(animationState.currentClipName));
	}

	return animationState;
}
