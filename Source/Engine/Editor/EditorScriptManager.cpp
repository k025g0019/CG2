#pragma warning(disable : 5045)

#include "EditorScriptManager.h"

#include "EditorAssetUtility.h"
#include "EditorActionSequenceManager.h"
#include "EditorCameraEffectManager.h"
#include "EditorComponentUtility.h"
#include "EditorDamageManager.h"
#include "EditorObjectPoolManager.h"
#include "EditorOceanSystem.h"
#include "EditorProfilerManager.h"
#include "EditorRailBranchManager.h"
#include "EditorRuntimePropertyManager.h"
#include "EditorSaveManager.h"
#include "EditorSharedState.h"
#include "EditorTargetingManager.h"
#include "EditorWeaponManager.h"
#include "EditorWeaponLoadoutManager.h"
#include "EditorWaveSpawnerManager.h"
#include "StringUtility.h"
#include "Source/Engine/Effect/EditorVfxManager.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <sstream>

namespace {
	EditorScriptManager* gActiveScriptManager = nullptr;  // DLL API の関数ポインタから現在の ScriptManager を逆参照する。
	constexpr int32_t kHotReloadCheckFrameInterval = 30;  // Play 中の DLL 更新確認を 30 フレーム間隔へ抑える。
	constexpr int32_t kFieldSynchronizationFrameInterval = 5;  // Inspector 公開変数の DLL 往復は最大 6 フレームに 1 回へ抑える。

	bool TryResolveComponentType(const char* componentTypeName, EditorComponentType& componentType) {
		if (componentTypeName == nullptr || componentTypeName[0] == '\0') {
			return false;
		}

		for (int32_t componentIndex = 0;
			componentIndex < static_cast<int32_t>(EditorComponentType::Count);
			componentIndex++) {
			const EditorComponentType candidateType = ComponentTypeFromIndex(componentIndex);

			if (ToString(candidateType) == componentTypeName) {
				componentType = candidateType;
				return true;
			}
		}

		return false;
	}

	bool IsColliderComponentType(EditorComponentType componentType) {
		return componentType == EditorComponentType::BoxCollider ||
			componentType == EditorComponentType::SphereCollider ||
			componentType == EditorComponentType::CapsuleCollider ||
			componentType == EditorComponentType::MeshCollider ||
			componentType == EditorComponentType::TerrainCollider ||
			componentType == EditorComponentType::WheelCollider ||
			componentType == EditorComponentType::AutoConvexCollision ||
			componentType == EditorComponentType::CharacterController;
	}

	void CombineHash(size_t& currentHash, size_t valueHash) {
		currentHash ^= valueHash + 0x9E3779B9U + (currentHash << 6U) + (currentHash >> 2U);
	}

	size_t HashScriptProperties(const std::vector<EditorScriptProperty>& scriptProperties) {
		size_t propertyHash = scriptProperties.size();

		for (const EditorScriptProperty& scriptProperty : scriptProperties) {
			CombineHash(propertyHash, std::hash<std::string>{}(scriptProperty.name));
			CombineHash(propertyHash, std::hash<int32_t>{}(scriptProperty.type));

			switch (scriptProperty.type) {
			case EditorScriptFieldTypeBool:
				CombineHash(propertyHash, std::hash<bool>{}(scriptProperty.boolValue));
				break;
			case EditorScriptFieldTypeInt32:
			case EditorScriptFieldTypeGameObject:
				CombineHash(propertyHash, std::hash<int32_t>{}(scriptProperty.intValue));
				break;
			case EditorScriptFieldTypeFloat:
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.floatValue));
				break;
			case EditorScriptFieldTypeVector2:
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.vector2Value.x));
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.vector2Value.y));
				break;
			case EditorScriptFieldTypeVector3:
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.vector3Value.x));
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.vector3Value.y));
				CombineHash(propertyHash, std::hash<float>{}(scriptProperty.vector3Value.z));
				break;
			case EditorScriptFieldTypeString:
			case EditorScriptFieldTypeSceneAsset:
				CombineHash(propertyHash, std::hash<std::string>{}(scriptProperty.stringValue));
				break;
			default:
				break;
			}
		}

		return propertyHash;
	}

	bool HasRunnableScriptComponent(const EditorGameObject& gameObject) {
		const EditorComponent* scriptComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Script);
		const EditorComponent* monoBehaviourComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MonoBehaviour);

		const bool hasScript = scriptComponent != nullptr && scriptComponent->isActive;
		const bool hasMonoBehaviour = monoBehaviourComponent != nullptr && monoBehaviourComponent->isActive;
		return hasScript || hasMonoBehaviour;
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

	EditorScriptWireState ToScriptWireState(
		const EditorPhysicsManager::RuntimeWireState& wireState) {
		EditorScriptWireState scriptState{};
		scriptState.handle = wireState.handle;
		scriptState.firstGameObjectId = wireState.desc.firstGameObjectId;
		scriptState.secondGameObjectId = wireState.desc.secondGameObjectId;
		scriptState.ownerGameObjectId = wireState.desc.ownerGameObjectId;
		scriptState.isActive = wireState.isActive;
		scriptState.isBroken = wireState.isBroken;
		scriptState.firstWorldAnchor = ToScriptVector3(wireState.firstWorldAnchor);
		scriptState.secondWorldAnchor = ToScriptVector3(wireState.secondWorldAnchor);
		scriptState.maximumLength = wireState.desc.maximumLength;
		scriptState.minimumLength = wireState.desc.minimumLength;
		scriptState.currentLength = wireState.currentLength;
		scriptState.currentTension = wireState.currentTension;
		return scriptState;
	}

	EditorJoltPhysicsManager::RuntimeJointType ToRuntimeJointType(EditorScriptJointType jointType) {
		return static_cast<EditorJoltPhysicsManager::RuntimeJointType>(
			static_cast<int32_t>(jointType));
	}

	EditorJoltPhysicsManager::RuntimeJointSettings ToRuntimeJointSettings(
		const EditorScriptJointDesc& jointDesc) {
		EditorJoltPhysicsManager::RuntimeJointSettings jointSettings{};
		jointSettings.ownerAnchor = ToEditorVector3(jointDesc.ownerAnchor);
		jointSettings.connectedAnchor = ToEditorVector3(jointDesc.connectedAnchor);
		jointSettings.axis = ToEditorVector3(jointDesc.axis);
		jointSettings.minDistance = jointDesc.minDistance;
		jointSettings.maxDistance = jointDesc.maxDistance;
		jointSettings.minAngle = jointDesc.minAngle;
		jointSettings.maxAngle = jointDesc.maxAngle;
		jointSettings.frequency = jointDesc.frequency;
		jointSettings.damping = jointDesc.damping;
		jointSettings.freezePositionX = jointDesc.freezePositionX;
		jointSettings.freezePositionY = jointDesc.freezePositionY;
		jointSettings.freezePositionZ = jointDesc.freezePositionZ;
		jointSettings.freezeRotationX = jointDesc.freezeRotationX;
		jointSettings.freezeRotationY = jointDesc.freezeRotationY;
		jointSettings.freezeRotationZ = jointDesc.freezeRotationZ;
		return jointSettings;
	}

	EditorScriptOceanSegmentHit ToScriptOceanSegmentHit(const EditorOceanSegmentHit& sourceHit) {
		EditorScriptOceanSegmentHit scriptHit{};
		scriptHit.oceanGameObjectId = sourceHit.oceanGameObjectId;
		scriptHit.point = ToScriptVector3(sourceHit.position);
		scriptHit.normal = ToScriptVector3(sourceHit.normal);
		scriptHit.surfaceVelocity = ToScriptVector3(sourceHit.surfaceVelocity);
		scriptHit.distance = sourceHit.distance;
		scriptHit.normalizedDistance = sourceHit.normalizedDistance;
		return scriptHit;
	}

	void CopyStringToFixedBuffer(const std::string& sourceText, char* destination, size_t destinationSize) {
		if (destination == nullptr || destinationSize == 0u) {
			return;
		}

		const size_t copySize = (std::min)(sourceText.size(), destinationSize - 1u);
		std::memcpy(destination, sourceText.data(), copySize);
		destination[copySize] = '\0';
	}

	void ReadRegisteredActionNames(
		EditorScriptGetActionCountFn getActionCountFunction,
		EditorScriptGetActionNameFn getActionNameFunction,
		std::vector<std::string>& actionNames) {
		if (getActionCountFunction == nullptr || getActionNameFunction == nullptr) {
			return;
		}

		const int32_t actionCount = (std::clamp)(getActionCountFunction(), 0, 512);

		for (int32_t actionIndex = 0; actionIndex < actionCount; actionIndex++) {
			char actionName[128]{};

			if (!getActionNameFunction(actionIndex, actionName, static_cast<int32_t>(sizeof(actionName))) ||
			actionName[0] == '\0') {
				continue;
			}

			if (std::find(actionNames.begin(), actionNames.end(), actionName) == actionNames.end()) {
				actionNames.emplace_back(actionName);
			}
		}
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

	EditorScriptProperty MakeScriptProperty(const EditorScriptFieldDescriptor& fieldDescriptor) {
		EditorScriptProperty scriptProperty{};
		scriptProperty.name = fieldDescriptor.name;
		scriptProperty.displayName = fieldDescriptor.displayName;
		scriptProperty.type = fieldDescriptor.defaultValue.type;
		scriptProperty.boolValue = fieldDescriptor.defaultValue.boolValue;
		scriptProperty.intValue = fieldDescriptor.defaultValue.intValue;
		scriptProperty.floatValue = fieldDescriptor.defaultValue.floatValue;
		scriptProperty.vector2Value = {
			fieldDescriptor.defaultValue.vector2Value.x,
			fieldDescriptor.defaultValue.vector2Value.y};
		scriptProperty.vector3Value = ToEditorVector3(fieldDescriptor.defaultValue.vector3Value);
		scriptProperty.stringValue = fieldDescriptor.defaultValue.stringValue;
		scriptProperty.minValue = fieldDescriptor.minValue;
		scriptProperty.maxValue = fieldDescriptor.maxValue;
		scriptProperty.step = fieldDescriptor.step;
		scriptProperty.hasRange = fieldDescriptor.hasRange;
		return scriptProperty;
	}

	EditorScriptFieldValue MakeScriptFieldValue(const EditorScriptProperty& scriptProperty) {
		EditorScriptFieldValue fieldValue{};
		fieldValue.type = scriptProperty.type;
		fieldValue.boolValue = scriptProperty.boolValue;
		fieldValue.intValue = scriptProperty.intValue;
		fieldValue.floatValue = scriptProperty.floatValue;
		fieldValue.vector2Value = {
			scriptProperty.vector2Value.x,
			scriptProperty.vector2Value.y};
		fieldValue.vector3Value = ToScriptVector3(scriptProperty.vector3Value);
		CopyStringToFixedBuffer(scriptProperty.stringValue, fieldValue.stringValue, sizeof(fieldValue.stringValue));
		return fieldValue;
	}

	void ApplyScriptFieldValue(
		EditorScriptProperty& scriptProperty,
		const EditorScriptFieldValue& fieldValue) {
		if (scriptProperty.type != fieldValue.type) {
			return;
		}

		switch (fieldValue.type) {
		case EditorScriptFieldTypeBool:
			scriptProperty.boolValue = fieldValue.boolValue;
			break;
		case EditorScriptFieldTypeInt32:
		case EditorScriptFieldTypeGameObject:
			scriptProperty.intValue = fieldValue.intValue;
			break;
		case EditorScriptFieldTypeFloat:
			scriptProperty.floatValue = fieldValue.floatValue;
			break;
		case EditorScriptFieldTypeVector2:
			scriptProperty.vector2Value = {
				fieldValue.vector2Value.x,
				fieldValue.vector2Value.y};
			break;
		case EditorScriptFieldTypeVector3:
			scriptProperty.vector3Value = ToEditorVector3(fieldValue.vector3Value);
			break;
		case EditorScriptFieldTypeString:
		case EditorScriptFieldTypeSceneAsset:
			scriptProperty.stringValue = fieldValue.stringValue;
			break;
		default:
			break;
		}
	}

	std::string MakeInputActionStateKey(
		int32_t gameObjectId,
		const std::string& actionMapName,
		const std::string& actionName) {
		return std::to_string(gameObjectId) + "|" + actionMapName + "|" + actionName;
	}

}

void EditorScriptManager::Initialize(
	EditorScene* editorScene,
	EditorInputManager* inputManager,
	EditorAnimationManager* animationManager,
	EditorEffectManager* effectManager,
	EditorAudioManager* audioManager,
	EditorAIManager* aiManager,
	EditorPhysicsManager* physicsManager,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // RuntimeManager と同じ Scene を参照し、Play 中だけ Script を処理する。
	inputManager_ = inputManager;  // PlayerInput の Action 名を DLL Script から問い合わせる時に使う。
	animationManager_ = animationManager;  // Animation の再生状態と現在時刻を DLL Script から読む時に使う。
	effectManager_ = effectManager;  // ParticleSystem / VisualEffect を DLL Script から再生する時に使う。
	audioManager_ = audioManager;  // AudioSource を DLL Script から任意のタイミングで鳴らす時に使う。
	aiManager_ = aiManager;  // AI センサーや音声 / 顔検知の状態を DLL Script から読む時に使う。
	physicsManager_ = physicsManager;  // DLL Script から Jolt の AddForce / SetVelocity を呼ぶための入口。
	consoleMessages_ = consoleMessages;  // DLL ログや読込失敗を Console へ出す先。
	isStarted_ = false;
	lastDeltaTime_ = 0.0f;
	lastFixedDeltaTime_ = 0.0f;
	physicsEvents_.clear();
	scriptBindings_.clear();
	scriptBindingIndicesByGameObjectId_.clear();
	scriptModules_.clear();
	moduleStatusMessages_.clear();
	scriptMetadataCache_.clear();
	inputActionActiveStates_.clear();
	missingActionWarnings_.clear();
	queuedUiEvents_.clear();
	requestedSceneLoad_ = {};
	requestedSceneUnloadPath_.clear();
	currentKeyState_.fill(0);
	previousKeyState_.fill(0);
	EditorSharedState::ApplyRuntimeCursorLock(false);
	EditorSharedState::ApplyRuntimeCursorVisibility(true);
	hotReloadCheckFrameTimer_ = 0;
	fieldSynchronizationFrameTimer_ = 0;
	reloadGeneration_ = 0;
	BuildRuntimeApi();
	gActiveScriptManager = this;
}

void EditorScriptManager::SetRailMovementManager(
	EditorRailMovementManager* railMovementManager) {
	railMovementManager_ = railMovementManager;
}

void EditorScriptManager::SetEffekseerManager(
	EditorEffekseerManager* effekseerManager) {
	effekseerManager_ = effekseerManager;
}

void EditorScriptManager::SetVfxManager(EditorVfxManager* vfxManager) {
	vfxManager_ = vfxManager;
}

void EditorScriptManager::SetProfilerManager(EditorProfilerManager* profilerManager) {
	profilerManager_ = profilerManager;
}

void EditorScriptManager::SetGameplayManagers(
	EditorTargetingManager* targetingManager,
	EditorDamageManager* damageManager,
	EditorObjectPoolManager* objectPoolManager,
	EditorWeaponManager* weaponManager,
	EditorCameraEffectManager* cameraEffectManager,
	EditorRailBranchManager* railBranchManager) {
	targetingManager_ = targetingManager;
	damageManager_ = damageManager;
	objectPoolManager_ = objectPoolManager;
	weaponManager_ = weaponManager;
	cameraEffectManager_ = cameraEffectManager;
	railBranchManager_ = railBranchManager;
}

void EditorScriptManager::SetWorkflowManagers(
	EditorActionSequenceManager* actionSequenceManager,
	EditorSaveManager* saveManager) {
	actionSequenceManager_ = actionSequenceManager;
	saveManager_ = saveManager;
}

void EditorScriptManager::SetReusableGameplayManagers(
	EditorWeaponLoadoutManager* weaponLoadoutManager,
	EditorTargetingManager* targetingManager,
	EditorRuntimePropertyManager* runtimePropertyManager,
	EditorWaveSpawnerManager* waveSpawnerManager) {
	weaponLoadoutManager_ = weaponLoadoutManager;
	targetingManager_ = targetingManager;
	runtimePropertyManager_ = runtimePropertyManager;
	waveSpawnerManager_ = waveSpawnerManager;
}

void EditorScriptManager::SetSceneRuntimeState(
	float loadProgress,
	bool isLoading,
	const std::vector<std::string>& loadedScenePaths) {
	sceneLoadProgress_ = (std::clamp)(loadProgress, 0.0f, 1.0f);
	isSceneLoading_ = isLoading;
	loadedScenePaths_.clear();
	loadedScenePaths_.reserve(loadedScenePaths.size());

	for (const std::string& scenePath : loadedScenePaths) {
		loadedScenePaths_.push_back(
			std::filesystem::path(scenePath).lexically_normal().generic_string());
	}
}

bool EditorScriptManager::IsSceneRuntimeLoading() const {
	return isSceneLoading_;
}

bool EditorScriptManager::IsSceneRuntimeLoaded(const std::string& scenePath) const {
	const std::string normalizedScenePath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();
	return std::find(
		loadedScenePaths_.begin(),
		loadedScenePaths_.end(),
		normalizedScenePath) != loadedScenePaths_.end();
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

void EditorScriptManager::RegisterRuntimeHierarchy(int32_t rootGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	std::vector<int32_t> pendingGameObjectIds{rootGameObjectId};

	while (!pendingGameObjectIds.empty()) {
		const int32_t gameObjectId = pendingGameObjectIds.back();
		pendingGameObjectIds.pop_back();
		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

		if (gameObject == nullptr) {
			continue;
		}

		for (size_t componentIndex = 0U; componentIndex < gameObject->components.size(); componentIndex++) {
			const EditorComponent& component = gameObject->components[componentIndex];
			const bool isScriptComponent =
				component.type == EditorComponentType::Script ||
				component.type == EditorComponentType::MonoBehaviour;

			if (!isScriptComponent || !component.isActive || component.assetPath.empty()) {
				continue;
			}

			const bool isDllPath =
				component.assetPath.size() >= 4U &&
				component.assetPath.substr(component.assetPath.size() - 4U) == ".dll";

			if (!isDllPath) {
				continue;
			}

			bool hasBinding = false;
			const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(gameObjectId);

			if (bindingIndicesIterator != scriptBindingIndicesByGameObjectId_.end()) {
				for (const size_t bindingIndex : bindingIndicesIterator->second) {
					if (bindingIndex < scriptBindings_.size() &&
						scriptBindings_[bindingIndex].componentIndex == componentIndex) {
						hasBinding = true;
						break;
					}
				}
			}

			if (hasBinding) {
				continue;
			}

			ScriptBinding scriptBinding{};
			scriptBinding.gameObjectId = gameObjectId;
			scriptBinding.componentIndex = componentIndex;
			scriptBinding.componentType = component.type;
			scriptBinding.dllPath = component.assetPath;
			scriptBindings_.push_back(scriptBinding);
			const size_t bindingIndex = scriptBindings_.size() - 1U;
			scriptBindingIndicesByGameObjectId_[gameObjectId].push_back(bindingIndex);

			ScriptModule* scriptModule = FindModule(component.assetPath);

			if (scriptModule == nullptr || !scriptModule->isLoaded) {
				LoadModule(component.assetPath);
				scriptModule = FindModule(component.assetPath);
			}
			else if (std::find(
				scriptModule->attachedGameObjectIds.begin(),
				scriptModule->attachedGameObjectIds.end(),
				gameObjectId) == scriptModule->attachedGameObjectIds.end()) {
				scriptModule->attachedGameObjectIds.push_back(gameObjectId);
			}

			if (scriptModule != nullptr) {
				StartBindingIfNeeded(scriptBindings_[bindingIndex], *scriptModule);
			}
		}

		for (const int32_t childGameObjectId : gameObject->children) {
			pendingGameObjectIds.push_back(childGameObjectId);
		}
	}
}

void EditorScriptManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	CopyKeyState(keyState);

	if (hotReloadCheckFrameTimer_ <= 0) {
		HotReloadChangedModules();
		hotReloadCheckFrameTimer_ = kHotReloadCheckFrameInterval;
	}
	else {
		hotReloadCheckFrameTimer_--;
	}

	lastDeltaTime_ = deltaTime;  // DLL 側の Update にそのまま渡す秒数。

	// 非ActiveでPlayを開始したWave要素もBindingは保持し、初めてActiveになったフレームでStartする。
	for (ScriptBinding& scriptBinding : scriptBindings_) {
		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);

		if (scriptModule != nullptr) {
			StartBindingIfNeeded(scriptBinding, *scriptModule);
		}
	}

	const bool shouldSynchronizeFields = fieldSynchronizationFrameTimer_ <= 0;

	if (shouldSynchronizeFields) {
		fieldSynchronizationFrameTimer_ = kFieldSynchronizationFrameInterval;
	}
	else {
		fieldSynchronizationFrameTimer_--;
	}

	// Inspector 値が実際に変わった Script だけを DLL へ送り、変更のない全フィールド往復を省く。
	if (shouldSynchronizeFields) {
		for (ScriptBinding& scriptBinding : scriptBindings_) {
			ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
			EditorComponent* scriptComponent = FindScriptComponent(scriptBinding);

			if (scriptModule == nullptr || !scriptModule->isLoaded || scriptComponent == nullptr ||
				!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
				continue;
			}

			const size_t componentFieldHash = HashScriptProperties(scriptComponent->scriptProperties);
			if (!scriptBinding.hasSynchronizedFieldHash ||
				componentFieldHash != scriptBinding.synchronizedFieldHash) {
				ApplyComponentFieldsToInstance(scriptBinding, *scriptModule);
				scriptBinding.synchronizedFieldHash = componentFieldHash;
				scriptBinding.hasSynchronizedFieldHash = true;
			}
		}
	}

	DispatchQueuedUiEvents();
	DispatchInputActions();

	for (ScriptBinding& scriptBinding : scriptBindings_) {
		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
		if (scriptModule == nullptr || !scriptModule->isLoaded ||
			!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
			scriptBinding.updateIntervalRemaining = 0.0f;
			scriptBinding.accumulatedUpdateDeltaTime = 0.0f;
			continue;
		}

		float scriptUpdateInterval = 0.0f;
		const EditorGameObject* gameObject = editorScene_->FindGameObject(scriptBinding.gameObjectId);
		const EditorComponent* simulationLod = gameObject != nullptr
			? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::SimulationLOD)
			: nullptr;

		if (simulationLod != nullptr && simulationLod->isActive) {
			if (simulationLod->simulationLodRuntimeLevel == 1) {
				scriptUpdateInterval = (std::max)(
					simulationLod->simulationLodMediumScriptInterval,
					0.0f);
			}
			else if (simulationLod->simulationLodRuntimeLevel >= 2) {
				scriptUpdateInterval = (std::max)(
					simulationLod->simulationLodFarScriptInterval,
					0.0f);
			}
		}

		scriptBinding.accumulatedUpdateDeltaTime += deltaTime;
		scriptBinding.updateIntervalRemaining -= deltaTime;

		if (scriptUpdateInterval > 0.0f && scriptBinding.updateIntervalRemaining > 0.0f) {
			continue;
		}

		const float scriptDeltaTime = scriptUpdateInterval > 0.0f
			? scriptBinding.accumulatedUpdateDeltaTime
			: deltaTime;
		scriptBinding.accumulatedUpdateDeltaTime = 0.0f;
		scriptBinding.updateIntervalRemaining = scriptUpdateInterval;

		if (UsesInstanceApi(*scriptModule) && scriptBinding.instance != nullptr &&
			scriptModule->updateInstanceFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"Update",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->updateInstanceFunction(scriptBinding.instance, scriptDeltaTime);
		}
		else if (!UsesInstanceApi(*scriptModule) && scriptModule->updateFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"Update",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->updateFunction(scriptBinding.gameObjectId, scriptDeltaTime);
		}

		if (shouldSynchronizeFields) {
			ReadInstanceFieldsToComponent(scriptBinding, *scriptModule);
			EditorComponent* scriptComponent = FindScriptComponent(scriptBinding);

			if (scriptComponent != nullptr) {
				scriptBinding.synchronizedFieldHash = HashScriptProperties(scriptComponent->scriptProperties);
				scriptBinding.hasSynchronizedFieldHash = true;
			}
		}
	}

	// Script callback中にBinding配列を拡張すると反復中の参照が無効になるため、ここでまとめて反映する。
	for (const int32_t rootGameObjectId : pendingRuntimeHierarchyRegistrations_) {
		RegisterRuntimeHierarchy(rootGameObjectId);
	}
	pendingRuntimeHierarchyRegistrations_.clear();
}

void EditorScriptManager::FixedUpdate(float fixedDeltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	lastFixedDeltaTime_ = fixedDeltaTime;  // DLL 側の FixedUpdate にそのまま渡す秒数。

	// 接触した GameObject に付いた Script だけへ通知し、Script 数 x 接触数の全走査を避ける。
	for (const EditorJoltPhysicsManager::PhysicsEvent& physicsEvent : physicsEvents_) {
		const int32_t gameObjectId = physicsEvent.collision.selfGameObjectId;
		const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(gameObjectId);

		if (bindingIndicesIterator == scriptBindingIndicesByGameObjectId_.end()) {
			continue;
		}

		const EditorScriptPhysicsEvent scriptPhysicsEvent = ConvertPhysicsEvent(physicsEvent);

		for (const size_t bindingIndex : bindingIndicesIterator->second) {
			if (bindingIndex >= scriptBindings_.size()) {
				continue;
			}

			const ScriptBinding& scriptBinding = scriptBindings_[bindingIndex];

			if (!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
				continue;
			}

			ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);

			if (scriptModule == nullptr || !scriptModule->isLoaded) {
				continue;
			}

			if (UsesInstanceApi(*scriptModule) && scriptBinding.instance != nullptr &&
				scriptModule->physicsEventInstanceFunction != nullptr) {
				EditorProfilerManager::Scope profilerScope(
					profilerManager_,
					"PhysicsEvent",
					scriptBinding.dllPath,
					scriptBinding.gameObjectId);
				scriptModule->physicsEventInstanceFunction(scriptBinding.instance, &scriptPhysicsEvent);
			}
			else if (!UsesInstanceApi(*scriptModule) && scriptModule->physicsEventFunction != nullptr) {
				EditorProfilerManager::Scope profilerScope(
					profilerManager_,
					"PhysicsEvent",
					scriptBinding.dllPath,
					scriptBinding.gameObjectId);
				scriptModule->physicsEventFunction(gameObjectId, &scriptPhysicsEvent);
			}
		}
	}

	DispatchWireEvents();

	for (const ScriptBinding& scriptBinding : scriptBindings_) {
		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);

		if (scriptModule == nullptr || !scriptModule->isLoaded ||
			!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
			continue;
		}

		if (UsesInstanceApi(*scriptModule) && scriptBinding.instance != nullptr &&
			scriptModule->fixedUpdateInstanceFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"FixedUpdate",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->fixedUpdateInstanceFunction(scriptBinding.instance, fixedDeltaTime);
		}
		else if (!UsesInstanceApi(*scriptModule) && scriptModule->fixedUpdateFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"FixedUpdate",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->fixedUpdateFunction(scriptBinding.gameObjectId, fixedDeltaTime);
		}
	}
}

void EditorScriptManager::SetPhysicsEvents(const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& physicsEvents) {
	physicsEvents_ = physicsEvents;  // Jolt の接触イベントを Script 実行前に受け取り、FixedUpdate から参照できるようにする。
}

void EditorScriptManager::SetWireEvents(
	const std::vector<EditorPhysicsManager::RuntimeWireEvent>& wireEvents) {
	wireEvents_ = wireEvents;
}

void EditorScriptManager::DispatchWireEvents() {
	for (const EditorPhysicsManager::RuntimeWireEvent& wireEvent : wireEvents_) {
		EditorScriptWireEvent scriptWireEvent{};
		scriptWireEvent.handle = wireEvent.handle;
		scriptWireEvent.firstGameObjectId = wireEvent.firstGameObjectId;
		scriptWireEvent.secondGameObjectId = wireEvent.secondGameObjectId;
		scriptWireEvent.ownerGameObjectId = wireEvent.ownerGameObjectId;
		scriptWireEvent.tension = wireEvent.tension;

		switch (wireEvent.type) {
		case EditorPhysicsManager::RuntimeWireEventType::Connected:
			scriptWireEvent.type = EditorScriptWireEventTypeConnected;
			break;
		case EditorPhysicsManager::RuntimeWireEventType::TensionChanged:
			scriptWireEvent.type = EditorScriptWireEventTypeTensionChanged;
			break;
		case EditorPhysicsManager::RuntimeWireEventType::Broken:
			scriptWireEvent.type = EditorScriptWireEventTypeBroken;
			break;
		case EditorPhysicsManager::RuntimeWireEventType::TargetLost:
			scriptWireEvent.type = EditorScriptWireEventTypeTargetLost;
			break;
		case EditorPhysicsManager::RuntimeWireEventType::Destroyed:
			scriptWireEvent.type = EditorScriptWireEventTypeDestroyed;
			break;
		default:
			continue;
		}

		const int32_t notificationGameObjectIds[3] = {
			wireEvent.firstGameObjectId,
			wireEvent.secondGameObjectId,
			wireEvent.ownerGameObjectId};

		for (int32_t notificationIndex = 0; notificationIndex < 3; notificationIndex++) {
			const int32_t gameObjectId = notificationGameObjectIds[notificationIndex];
			const bool isDuplicate =
				(notificationIndex >= 1 && gameObjectId == notificationGameObjectIds[0]) ||
				(notificationIndex >= 2 && gameObjectId == notificationGameObjectIds[1]);

			if (gameObjectId < 0 || isDuplicate) {
				continue;
			}

			const auto bindingIndicesIterator =
				scriptBindingIndicesByGameObjectId_.find(gameObjectId);

			if (bindingIndicesIterator == scriptBindingIndicesByGameObjectId_.end()) {
				continue;
			}

			for (const size_t bindingIndex : bindingIndicesIterator->second) {
				if (bindingIndex >= scriptBindings_.size()) {
					continue;
				}

				const ScriptBinding& scriptBinding = scriptBindings_[bindingIndex];

				if (!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
					continue;
				}

				ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);

				if (scriptModule == nullptr || !scriptModule->isLoaded) {
					continue;
				}

				if (UsesInstanceApi(*scriptModule) && scriptBinding.instance != nullptr &&
					scriptModule->wireEventInstanceFunction != nullptr) {
					scriptModule->wireEventInstanceFunction(
						scriptBinding.instance,
						&scriptWireEvent);
				}
				else if (!UsesInstanceApi(*scriptModule) &&
					scriptModule->wireEventFunction != nullptr) {
					scriptModule->wireEventFunction(gameObjectId, &scriptWireEvent);
				}
			}
		}
	}

	// 1描画フレームで固定更新が複数回進んでも同じWireイベントを再送しない。
	wireEvents_.clear();
}

void EditorScriptManager::DispatchAnimationEvent(
	int32_t gameObjectId,
	const std::string& eventName,
	float eventTime,
	const std::string& effectAssetPath,
	const Vector3& localOffset) {
	const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(gameObjectId);

	if (bindingIndicesIterator == scriptBindingIndicesByGameObjectId_.end()) {
		return;
	}

	// 同じ GameObject に複数の Script Component がある場合は、各 DLL へ同じ Event を通知する。
	for (const size_t bindingIndex : bindingIndicesIterator->second) {
		if (bindingIndex >= scriptBindings_.size()) {
			continue;
		}

		const ScriptBinding& scriptBinding = scriptBindings_[bindingIndex];

		if (!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
			continue;
		}

		ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
		if (scriptModule == nullptr || !scriptModule->isLoaded) {
			continue;
		}

		const EditorScriptAnimationEvent scriptAnimationEvent{
			eventName.c_str(),
			effectAssetPath.c_str(),
			eventTime,
			{localOffset.x, localOffset.y, localOffset.z},
		};

		if (UsesInstanceApi(*scriptModule) && scriptBinding.instance != nullptr &&
			scriptModule->animationEventInstanceFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"AnimationEvent",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->animationEventInstanceFunction(scriptBinding.instance, &scriptAnimationEvent);
		}
		else if (!UsesInstanceApi(*scriptModule) && scriptModule->animationEventFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"AnimationEvent",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule->animationEventFunction(gameObjectId, &scriptAnimationEvent);
		}
	}
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
	wireEvents_.clear();
	pendingRuntimeHierarchyRegistrations_.clear();
	UnloadAllModules();
	scriptBindings_.clear();
	scriptBindingIndicesByGameObjectId_.clear();
	inputActionActiveStates_.clear();
	missingActionWarnings_.clear();
	queuedUiEvents_.clear();
	requestedSceneLoad_ = {};
	requestedSceneUnloadPath_.clear();
	currentKeyState_.fill(0);
	previousKeyState_.fill(0);
	EditorSharedState::ApplyRuntimeCursorLock(false);
	EditorSharedState::ApplyRuntimeCursorVisibility(true);
	hotReloadCheckFrameTimer_ = 0;
	fieldSynchronizationFrameTimer_ = 0;
}

bool EditorScriptManager::IsStarted() const {
	return isStarted_;
}

EditorScriptManager::ScriptDebugInfo EditorScriptManager::GetDebugInfo(int32_t gameObjectId) const {
	ScriptDebugInfo debugInfo{};
	const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(gameObjectId);

	if (bindingIndicesIterator != scriptBindingIndicesByGameObjectId_.end() &&
		!bindingIndicesIterator->second.empty()) {
		const size_t bindingIndex = bindingIndicesIterator->second.front();

		if (bindingIndex < scriptBindings_.size()) {
			debugInfo.hasBinding = true;
			debugInfo.sourceDllPath = scriptBindings_[bindingIndex].dllPath;
		}
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

bool EditorScriptManager::RefreshExposedFields(EditorComponent& scriptComponent) {
	const bool isScriptComponent =
		scriptComponent.type == EditorComponentType::Script ||
		scriptComponent.type == EditorComponentType::MonoBehaviour;

	if (!isScriptComponent || scriptComponent.assetPath.empty()) {
		return false;
	}

	ScriptModule* loadedModule = FindModule(scriptComponent.assetPath);
	if (loadedModule != nullptr &&
		loadedModule->isLoaded &&
		loadedModule->getFieldCountFunction != nullptr &&
		loadedModule->getFieldDescriptorFunction != nullptr) {
		std::vector<EditorScriptFieldDescriptor> fieldDescriptors;
		const int32_t fieldCount = (std::clamp)(loadedModule->getFieldCountFunction(), 0, 512);
		fieldDescriptors.reserve(static_cast<size_t>(fieldCount));

		for (int32_t fieldIndex = 0; fieldIndex < fieldCount; fieldIndex++) {
			EditorScriptFieldDescriptor fieldDescriptor{};
			if (loadedModule->getFieldDescriptorFunction(fieldIndex, &fieldDescriptor)) {
				fieldDescriptors.push_back(fieldDescriptor);
			}
		}

		SynchronizeComponentProperties(scriptComponent, fieldDescriptors);
		return true;
	}

	const std::filesystem::path sourcePath = std::filesystem::absolute(scriptComponent.assetPath);
	std::error_code fileError;
	if (!std::filesystem::exists(sourcePath, fileError) || fileError) {
		return false;
	}

	const std::filesystem::file_time_type currentWriteTime = std::filesystem::last_write_time(sourcePath, fileError);
	if (fileError) {
		return false;
	}

	ScriptMetadata& scriptMetadata = scriptMetadataCache_[scriptComponent.assetPath];
	if (!scriptMetadata.isValid || scriptMetadata.lastWriteTime != currentWriteTime) {
		ScriptMetadata loadedMetadata{};
		if (!ReadMetadataFromDll(scriptComponent.assetPath, loadedMetadata)) {
			return false;
		}

		scriptMetadata = loadedMetadata;
	}

	SynchronizeComponentProperties(scriptComponent, scriptMetadata.fieldDescriptors);
	return true;
}

std::vector<std::string> EditorScriptManager::GetRegisteredActionNames(int32_t gameObjectId) {
	std::vector<std::string> actionNames;

	if (editorScene_ == nullptr) {
		return actionNames;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return actionNames;
	}

	for (EditorComponent& component : gameObject->components) {
		const bool isScriptComponent =
			component.type == EditorComponentType::Script ||
			component.type == EditorComponentType::MonoBehaviour;

		if (!isScriptComponent || component.assetPath.empty()) {
			continue;
		}

		ScriptModule* loadedModule = FindModule(component.assetPath);

		if (loadedModule != nullptr && loadedModule->isLoaded) {
			ReadRegisteredActionNames(
				loadedModule->getActionCountFunction,
				loadedModule->getActionNameFunction,
				actionNames);
			continue;
		}

		RefreshExposedFields(component);
		const auto metadataIterator = scriptMetadataCache_.find(component.assetPath);

		if (metadataIterator == scriptMetadataCache_.end()) {
			continue;
		}

		for (const std::string& actionName : metadataIterator->second.actionNames) {
			if (std::find(actionNames.begin(), actionNames.end(), actionName) == actionNames.end()) {
				actionNames.push_back(actionName);
			}
		}
	}

	return actionNames;
}

void EditorScriptManager::QueueActionEvent(
	int32_t gameObjectId,
	const std::string& functionName,
	int32_t valueType,
	float buttonValue,
	EditorScriptVector2 vector2Value) {
	if (!isStarted_ || functionName.empty()) {
		return;
	}

	QueuedUiEvent uiEvent{};
	uiEvent.gameObjectId = gameObjectId;
	uiEvent.functionName = functionName;
	uiEvent.valueType = valueType;
	uiEvent.buttonValue = buttonValue;
	uiEvent.vector2Value = vector2Value;
	uiEvent.isUiEvent = false;
	queuedUiEvents_.push_back(uiEvent);
}

void EditorScriptManager::QueueUiEvent(
	int32_t gameObjectId,
	const std::string& functionName,
	int32_t valueType,
	float buttonValue,
	EditorScriptVector2 vector2Value) {
	const size_t queuedEventCount = queuedUiEvents_.size();
	QueueActionEvent(gameObjectId, functionName, valueType, buttonValue, vector2Value);

	if (queuedUiEvents_.size() > queuedEventCount) {
		queuedUiEvents_.back().isUiEvent = true;
	}
}

bool EditorScriptManager::QueueActionPayload(
	int32_t gameObjectId,
	const std::string& functionName,
	const EditorScriptActionPayload& payload) {
	const size_t previousCount = queuedUiEvents_.size();
	QueueActionEvent(gameObjectId, functionName);

	if (queuedUiEvents_.size() > previousCount) {
		queuedUiEvents_.back().payload = payload;
		return true;
	}

	return false;
}

bool EditorScriptManager::RequestSceneLoad(const std::string& scenePath) {
	return RequestSceneLoadInternal(scenePath);
}

bool EditorScriptManager::RequestSceneLoadAsync(const std::string& scenePath, bool isAdditive) {
	return RequestSceneLoadInternal(scenePath, isAdditive, true);
}

bool EditorScriptManager::RequestSceneUnload(const std::string& scenePath) {
	if (scenePath.empty()) {
		return false;
	}

	requestedSceneUnloadPath_ = std::filesystem::path(scenePath).generic_string();
	return true;
}

bool EditorScriptManager::SetGameObjectActive(int32_t gameObjectId, bool isActive) {
	return SetGameObjectActiveInternal(gameObjectId, isActive);
}

bool EditorScriptManager::ConsumeSceneLoadRequest(EditorSceneLoadRequest& sceneLoadRequest) {
	if (requestedSceneLoad_.scenePath.empty()) {
		return false;
	}

	sceneLoadRequest = requestedSceneLoad_;
	requestedSceneLoad_ = {};
	return true;
}

bool EditorScriptManager::ConsumeSceneUnloadRequest(std::string& scenePath) {
	if (requestedSceneUnloadPath_.empty()) {
		return false;
	}

	scenePath.swap(requestedSceneUnloadPath_);
	requestedSceneUnloadPath_.clear();
	return true;
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

EditorScriptVector2 EditorScriptManager::ScriptGetMouseDeltaBridge() {
	if (gActiveScriptManager == nullptr) {
		return EditorScriptVector2{};
	}

	return gActiveScriptManager->GetMouseDeltaInternal();
}

bool EditorScriptManager::ScriptIsMouseButtonDownBridge(int32_t mouseButton) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->IsMouseButtonDownInternal(mouseButton);
}

bool EditorScriptManager::ScriptWasMouseButtonPressedBridge(int32_t mouseButton) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->WasMouseButtonPressedInternal(mouseButton);
}

bool EditorScriptManager::ScriptWasMouseButtonReleasedBridge(int32_t mouseButton) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->WasMouseButtonReleasedInternal(mouseButton);
}

void EditorScriptManager::ScriptSetCursorLockedBridge(bool isLocked) {
	if (gActiveScriptManager != nullptr) {
		gActiveScriptManager->SetCursorLockedInternal(isLocked);
	}
}

bool EditorScriptManager::ScriptIsCursorLockedBridge() {
	return EditorSharedState::g_runtimeCursorLocked;
}

void EditorScriptManager::ScriptSetCursorVisibleBridge(bool isVisible) {
	if (gActiveScriptManager != nullptr) {
		gActiveScriptManager->SetCursorVisibleInternal(isVisible);
	}
}

bool EditorScriptManager::ScriptIsCursorVisibleBridge() {
	return EditorSharedState::g_runtimeCursorVisible;
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

float EditorScriptManager::ScriptGetMassBridge(int32_t gameObjectId) {
	if (gActiveScriptManager == nullptr) {
		return 0.0f;
	}

	return gActiveScriptManager->GetMassInternal(gameObjectId);
}

bool EditorScriptManager::ScriptCaptureAreaStateBridge(int32_t areaRootGameObjectId) {
	return EditorSharedState::g_editorRuntimeManager.CaptureAreaState(areaRootGameObjectId);
}

bool EditorScriptManager::ScriptResetAreaBridge(int32_t areaRootGameObjectId) {
	return EditorSharedState::g_editorRuntimeManager.ResetArea(areaRootGameObjectId);
}

bool EditorScriptManager::ScriptHasAreaStateBridge(int32_t areaRootGameObjectId) {
	return EditorSharedState::g_editorRuntimeManager.HasAreaState(areaRootGameObjectId);
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

bool EditorScriptManager::ScriptAddForceAtPositionBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* force,
	const EditorScriptVector3* worldPosition) {
	if (gActiveScriptManager == nullptr || force == nullptr || worldPosition == nullptr) {
		return false;
	}

	return gActiveScriptManager->AddForceAtPositionInternal(gameObjectId, *force, *worldPosition);
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

int32_t EditorScriptManager::ScriptAddExplosionImpulseBridge(
	const EditorScriptVector3* center,
	float radius,
	float impulseStrength,
	float upwardModifier) {
	if (gActiveScriptManager == nullptr || center == nullptr) {
		return 0;
	}

	return gActiveScriptManager->AddExplosionImpulseInternal(
		*center,
		radius,
		impulseStrength,
		upwardModifier);
}

EditorScriptJointHandle EditorScriptManager::ScriptCreateSpringJointBridge(
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const EditorScriptSpringJointDesc* springJointDesc) {
	if (gActiveScriptManager == nullptr || springJointDesc == nullptr) {
		return kInvalidEditorScriptJointHandle;
	}

	return gActiveScriptManager->CreateSpringJointInternal(
		ownerGameObjectId,
		connectedGameObjectId,
		*springJointDesc);
}

bool EditorScriptManager::ScriptDestroyJointBridge(EditorScriptJointHandle jointHandle) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->DestroyJointInternal(jointHandle);
}

bool EditorScriptManager::ScriptSetSpringJointSettingsBridge(
	EditorScriptJointHandle jointHandle,
	const EditorScriptSpringJointDesc* springJointDesc) {
	return gActiveScriptManager != nullptr && springJointDesc != nullptr &&
		gActiveScriptManager->SetSpringJointSettingsInternal(jointHandle, *springJointDesc);
}

bool EditorScriptManager::ScriptIsJointValidBridge(EditorScriptJointHandle jointHandle) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->IsJointValidInternal(jointHandle);
}

EditorScriptJointHandle EditorScriptManager::ScriptCreateJointBridge(
	EditorScriptJointType jointType,
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const EditorScriptJointDesc* jointDesc) {
	if (gActiveScriptManager == nullptr || jointDesc == nullptr) {
		return kInvalidEditorScriptJointHandle;
	}

	return gActiveScriptManager->CreateJointInternal(
		jointType,
		ownerGameObjectId,
		connectedGameObjectId,
		*jointDesc);
}

bool EditorScriptManager::ScriptSetJointSettingsBridge(
	EditorScriptJointHandle jointHandle,
	const EditorScriptJointDesc* jointDesc) {
	return gActiveScriptManager != nullptr && jointDesc != nullptr &&
		gActiveScriptManager->SetJointSettingsInternal(jointHandle, *jointDesc);
}

bool EditorScriptManager::ScriptAttachRopeBridge(
	int32_t ownerGameObjectId,
	int32_t targetGameObjectId,
	const EditorScriptVector3* ownerLocalAnchor,
	const EditorScriptVector3* targetAnchor,
	float maximumLength) {
	if (gActiveScriptManager == nullptr || ownerLocalAnchor == nullptr || targetAnchor == nullptr) {
		return false;
	}

	return gActiveScriptManager->AttachRopeInternal(
		ownerGameObjectId,
		targetGameObjectId,
		*ownerLocalAnchor,
		*targetAnchor,
		maximumLength);
}

bool EditorScriptManager::ScriptDetachRopeBridge(int32_t ownerGameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->DetachRopeInternal(ownerGameObjectId);
}

bool EditorScriptManager::ScriptSetRopeLengthBridge(int32_t ownerGameObjectId, float maximumLength) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetRopeLengthInternal(ownerGameObjectId, maximumLength);
}

bool EditorScriptManager::ScriptRepairRopeBridge(int32_t ownerGameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->RepairRopeInternal(ownerGameObjectId);
}

EditorScriptRopeState EditorScriptManager::ScriptGetRopeStateBridge(int32_t ownerGameObjectId) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->GetRopeStateInternal(ownerGameObjectId)
		: EditorScriptRopeState{};
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

bool EditorScriptManager::ScriptSetAnimatorFloatBridge(
	int32_t gameObjectId,
	const char* parameterName,
	float value) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetAnimatorFloatInternal(gameObjectId, parameterName, value);
}

bool EditorScriptManager::ScriptSetAnimatorIntBridge(
	int32_t gameObjectId,
	const char* parameterName,
	int32_t value) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetAnimatorIntInternal(gameObjectId, parameterName, value);
}

bool EditorScriptManager::ScriptSetAnimatorBoolBridge(
	int32_t gameObjectId,
	const char* parameterName,
	bool value) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetAnimatorBoolInternal(gameObjectId, parameterName, value);
}

bool EditorScriptManager::ScriptSetAnimatorTriggerBridge(
	int32_t gameObjectId,
	const char* parameterName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetAnimatorTriggerInternal(gameObjectId, parameterName);
}

bool EditorScriptManager::ScriptSetAnimatorVector2Bridge(
	int32_t gameObjectId,
	const char* parameterName,
	const EditorScriptVector2* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->SetAnimatorVector2Internal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptSetAnimatorVector3Bridge(
	int32_t gameObjectId,
	const char* parameterName,
	const EditorScriptVector3* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->SetAnimatorVector3Internal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptPlayAnimationActionBridge(
	int32_t gameObjectId,
	int32_t clipIndex,
	float blendIn,
	float blendOut,
	float playbackSpeed,
	int32_t priority,
	bool loop) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->animationManager_ != nullptr &&
		gActiveScriptManager->animationManager_->PlayAction(
			gameObjectId,
			clipIndex,
			blendIn,
			blendOut,
			playbackSpeed,
			priority,
			loop);
}

bool EditorScriptManager::ScriptPlayEffectBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->effectManager_ != nullptr &&
		gActiveScriptManager->effectManager_->PlayEffect(gameObjectId);
}

bool EditorScriptManager::ScriptPlayEffectAtBridge(
	int32_t gameObjectId,
	const char* effectAssetPath,
	const EditorScriptVector3* localOffset) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->effectManager_ != nullptr &&
		effectAssetPath != nullptr &&
		localOffset != nullptr &&
		gActiveScriptManager->effectManager_->PlayEffectAt(
			gameObjectId,
			effectAssetPath,
			ToEditorVector3(*localOffset));
}

void EditorScriptManager::ScriptStopEffectBridge(int32_t gameObjectId) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->effectManager_ != nullptr) {
		gActiveScriptManager->effectManager_->StopEffect(gameObjectId);
	}
}

int32_t EditorScriptManager::ScriptPlayEffekseerAtPositionBridge(
	const char* effectAssetPath,
	const EditorScriptVector3* position,
	const EditorScriptVector3* rotationEuler) {
	if (gActiveScriptManager == nullptr ||
		gActiveScriptManager->effekseerManager_ == nullptr ||
		effectAssetPath == nullptr ||
		position == nullptr) {
		return -1;
	}

	const Vector3 resolvedRotation = rotationEuler != nullptr
		? ToEditorVector3(*rotationEuler)
		: Vector3{0.0f, 0.0f, 0.0f};
	return gActiveScriptManager->effekseerManager_->PlayEffectAt(
		effectAssetPath,
		ToEditorVector3(*position),
		resolvedRotation);
}

bool EditorScriptManager::ScriptSetEffekseerEffectPositionBridge(
	int32_t effekseerPlaybackHandle,
	const EditorScriptVector3* position) {
	if (gActiveScriptManager == nullptr ||
		gActiveScriptManager->effekseerManager_ == nullptr ||
		position == nullptr) {
		return false;
	}

	gActiveScriptManager->effekseerManager_->SetEffectPositionAt(effekseerPlaybackHandle, ToEditorVector3(*position));
	return true;
}

void EditorScriptManager::ScriptStopEffekseerEffectAtPositionBridge(int32_t effekseerPlaybackHandle) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->effekseerManager_ != nullptr) {
		gActiveScriptManager->effekseerManager_->StopEffectAt(effekseerPlaybackHandle);
	}
}

EditorScriptWireHandle EditorScriptManager::ScriptCreateWireBridge(
	const EditorScriptWireDesc* wireDesc) {
	return gActiveScriptManager != nullptr && wireDesc != nullptr
		? gActiveScriptManager->CreateWireInternal(*wireDesc)
		: kInvalidEditorScriptWireHandle;
}

bool EditorScriptManager::ScriptDestroyWireBridge(EditorScriptWireHandle wireHandle) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->DestroyWireInternal(wireHandle);
}

bool EditorScriptManager::ScriptSetWireLengthByHandleBridge(
	EditorScriptWireHandle wireHandle,
	float maximumLength) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetWireLengthByHandleInternal(wireHandle, maximumLength);
}

bool EditorScriptManager::ScriptSetWireShrinkSpeedBridge(
	EditorScriptWireHandle wireHandle,
	float shrinkSpeed) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetWireShrinkSpeedInternal(wireHandle, shrinkSpeed);
}

bool EditorScriptManager::ScriptRepairWireBridge(EditorScriptWireHandle wireHandle) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->RepairWireInternal(wireHandle);
}

bool EditorScriptManager::ScriptGetWireStateByHandleBridge(
	EditorScriptWireHandle wireHandle,
	EditorScriptWireState* wireState) {
	return gActiveScriptManager != nullptr && wireState != nullptr &&
		gActiveScriptManager->GetWireStateByHandleInternal(wireHandle, *wireState);
}

int32_t EditorScriptManager::ScriptGetWireCountForGameObjectBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->GetWireCountForGameObjectInternal(gameObjectId)
		: 0;
}

bool EditorScriptManager::ScriptGetWireForGameObjectBridge(
	int32_t gameObjectId,
	int32_t wireIndex,
	EditorScriptWireState* wireState) {
	return gActiveScriptManager != nullptr && wireState != nullptr &&
		gActiveScriptManager->GetWireForGameObjectInternal(
			gameObjectId,
			wireIndex,
			*wireState);
}

bool EditorScriptManager::ScriptCanConnectWireBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->CanConnectWireInternal(gameObjectId);
}

bool EditorScriptManager::ScriptPlayVfxAtPositionBridge(
	const char* effectId,
	const EditorScriptVector3* position) {
	if (gActiveScriptManager == nullptr ||
		gActiveScriptManager->vfxManager_ == nullptr ||
		effectId == nullptr ||
		position == nullptr) {
		return false;
	}

	return gActiveScriptManager->vfxManager_->PlayEffect(
		effectId,
		ToEditorVector3(*position)).IsValid();
}

bool EditorScriptManager::ScriptPlayAudioBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->audioManager_ != nullptr &&
		gActiveScriptManager->audioManager_->Play(gameObjectId);
}

void EditorScriptManager::ScriptStopAudioBridge(int32_t gameObjectId) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->audioManager_ != nullptr) {
		gActiveScriptManager->audioManager_->Stop(gameObjectId);
	}
}

namespace {
	// Script 側は 0=SFX / 1=BGM / 2=Ambience / 3=UI の整数で Bus を指定する。
	// 範囲外を渡されても既定の SFX へ落として落ちないようにする。
	EditorAudioBus ResolveScriptAudioBus(int32_t audioBus) {
		const int32_t busCount = static_cast<int32_t>(EditorAudioBus::Count);
		const int32_t clampedBus = (audioBus >= 0 && audioBus < busCount) ? audioBus : 0;
		return static_cast<EditorAudioBus>(clampedBus);
	}
}

void EditorScriptManager::ScriptSetAudioBusVolumeBridge(int32_t audioBus, float volume) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->audioManager_ != nullptr) {
		gActiveScriptManager->audioManager_->SetBusVolume(ResolveScriptAudioBus(audioBus), volume);
	}
}

float EditorScriptManager::ScriptGetAudioBusVolumeBridge(int32_t audioBus) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->audioManager_ != nullptr
		? gActiveScriptManager->audioManager_->GetBusVolume(ResolveScriptAudioBus(audioBus))
		: 0.0f;
}

void EditorScriptManager::ScriptSetAudioMasterVolumeBridge(float volume) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->audioManager_ != nullptr) {
		gActiveScriptManager->audioManager_->SetMasterVolume(volume);
	}
}

float EditorScriptManager::ScriptGetAudioMasterVolumeBridge() {
	return gActiveScriptManager != nullptr && gActiveScriptManager->audioManager_ != nullptr
		? gActiveScriptManager->audioManager_->GetMasterVolume()
		: 0.0f;
}

int32_t EditorScriptManager::ScriptGetAliveParticleCountBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->effectManager_ != nullptr
		? gActiveScriptManager->effectManager_->GetAliveParticleCount(gameObjectId)
		: 0;
}

bool EditorScriptManager::ScriptGetAnimatorFloatBridge(
	int32_t gameObjectId,
	const char* parameterName,
	float* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->GetAnimatorFloatInternal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptGetAnimatorIntBridge(
	int32_t gameObjectId,
	const char* parameterName,
	int32_t* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->GetAnimatorIntInternal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptGetAnimatorBoolBridge(
	int32_t gameObjectId,
	const char* parameterName,
	bool* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->GetAnimatorBoolInternal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptGetAnimatorVector2Bridge(
	int32_t gameObjectId,
	const char* parameterName,
	EditorScriptVector2* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->GetAnimatorVector2Internal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptGetAnimatorVector3Bridge(
	int32_t gameObjectId,
	const char* parameterName,
	EditorScriptVector3* value) {
	return gActiveScriptManager != nullptr && value != nullptr &&
		gActiveScriptManager->GetAnimatorVector3Internal(gameObjectId, parameterName, *value);
}

bool EditorScriptManager::ScriptResetAnimatorTriggerBridge(
	int32_t gameObjectId,
	const char* parameterName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->ResetAnimatorTriggerInternal(gameObjectId, parameterName);
}

bool EditorScriptManager::ScriptPlayAnimationBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->animationManager_ != nullptr &&
		gActiveScriptManager->animationManager_->PlayAnimation(gameObjectId);
}

bool EditorScriptManager::ScriptStopAnimationBridge(int32_t gameObjectId) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->animationManager_ == nullptr ||
		!gActiveScriptManager->animationManager_->IsAnimationPlaying(gameObjectId)) {
		return false;
	}

	gActiveScriptManager->animationManager_->StopAnimation(gameObjectId);
	return true;
}

bool EditorScriptManager::ScriptIsAnimationPlayingBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->animationManager_ != nullptr &&
		gActiveScriptManager->animationManager_->IsAnimationPlaying(gameObjectId);
}

float EditorScriptManager::ScriptGetAnimationTimeBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->animationManager_ != nullptr
		? gActiveScriptManager->animationManager_->GetAnimationTime(gameObjectId)
		: 0.0f;
}

bool EditorScriptManager::ScriptSetAnimationTimeBridge(int32_t gameObjectId, float playbackTime) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->animationManager_ != nullptr &&
		gActiveScriptManager->animationManager_->SetAnimationTime(gameObjectId, playbackTime);
}

bool EditorScriptManager::ScriptSetAnimationSpeedBridge(int32_t gameObjectId, float playbackSpeed) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->animationManager_ != nullptr &&
		gActiveScriptManager->animationManager_->SetAnimationSpeed(gameObjectId, playbackSpeed);
}

bool EditorScriptManager::ScriptGetAnimatorStateNameBridge(
	int32_t gameObjectId,
	char* stateName,
	int32_t stateNameCapacity) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->animationManager_ == nullptr ||
		stateName == nullptr || stateNameCapacity <= 0) {
		return false;
	}

	CopyStringToFixedBuffer(
		gActiveScriptManager->animationManager_->GetAnimatorStateName(gameObjectId),
		stateName,
		static_cast<size_t>(stateNameCapacity));
	return true;
}

bool EditorScriptManager::ScriptIsEffectPlayingBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->effectManager_ != nullptr &&
		gActiveScriptManager->effectManager_->IsEffectPlaying(gameObjectId);
}

int32_t EditorScriptManager::ScriptFindGameObjectByNameBridge(const char* gameObjectName) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->FindGameObjectByNameInternal(gameObjectName)
		: -1;
}

bool EditorScriptManager::ScriptSetGameObjectActiveBridge(int32_t gameObjectId, bool isActive) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetGameObjectActiveInternal(gameObjectId, isActive);
}

bool EditorScriptManager::ScriptIsGameObjectActiveBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->IsGameObjectActiveInternal(gameObjectId);
}

bool EditorScriptManager::ScriptSetComponentActiveBridge(
	int32_t gameObjectId,
	const char* componentTypeName,
	bool isActive) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetComponentActiveInternal(gameObjectId, componentTypeName, isActive);
}

bool EditorScriptManager::ScriptIsComponentActiveBridge(
	int32_t gameObjectId,
	const char* componentTypeName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->IsComponentActiveInternal(gameObjectId, componentTypeName);
}

bool EditorScriptManager::ScriptLoadSceneBridge(const char* scenePath) {
	return gActiveScriptManager != nullptr && scenePath != nullptr &&
		gActiveScriptManager->RequestSceneLoadInternal(scenePath);
}

bool EditorScriptManager::ScriptLoadSceneByBuildIndexBridge(int32_t sceneIndex) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->RequestSceneLoadByBuildIndexInternal(sceneIndex);
}

bool EditorScriptManager::ScriptSetRailPausedBridge(int32_t gameObjectId, bool isPaused) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetPaused(gameObjectId, isPaused);
}

bool EditorScriptManager::ScriptIsRailPausedBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->IsPaused(gameObjectId);
}

bool EditorScriptManager::ScriptSetRailSpeedBridge(int32_t gameObjectId, float speed) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetSpeed(gameObjectId, speed);
}

bool EditorScriptManager::ScriptSetRailReverseBridge(int32_t gameObjectId, bool isReversed) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetReverse(gameObjectId, isReversed);
}

bool EditorScriptManager::ScriptSetRailNormalizedProgressBridge(
	int32_t gameObjectId,
	float normalizedProgress) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetNormalizedProgress(
			gameObjectId,
			normalizedProgress);
}

bool EditorScriptManager::ScriptSetRailPathBridge(
	int32_t gameObjectId,
	int32_t railPathGameObjectId,
	bool preservesProgress) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetRailPath(
			gameObjectId,
			railPathGameObjectId,
			preservesProgress);
}

bool EditorScriptManager::ScriptSetRailMoveInputBridge(
	int32_t gameObjectId,
	const EditorScriptVector2* moveInput) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		moveInput != nullptr &&
		gActiveScriptManager->railMovementManager_->SetMoveInput(
			gameObjectId,
			EditorScriptVector2{moveInput->x, moveInput->y});
}

bool EditorScriptManager::ScriptSetRailOffsetBridge(
	int32_t gameObjectId,
	const EditorScriptVector2* offset) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		offset != nullptr &&
		gActiveScriptManager->railMovementManager_->SetOffset(
			gameObjectId,
			EditorScriptVector2{offset->x, offset->y});
}

bool EditorScriptManager::ScriptGetRailOffsetBridge(
	int32_t gameObjectId,
	EditorScriptVector2* offset) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->railMovementManager_ == nullptr ||
		offset == nullptr) {
		return false;
	}

	EditorScriptVector2 editorOffset{};

	if (!gActiveScriptManager->railMovementManager_->GetOffset(gameObjectId, editorOffset)) {
		return false;
	}

	offset->x = editorOffset.x;
	offset->y = editorOffset.y;
	return true;
}

bool EditorScriptManager::ScriptGetRailNormalizedProgressBridge(
	int32_t gameObjectId,
	float* normalizedProgress) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		normalizedProgress != nullptr &&
		gActiveScriptManager->railMovementManager_->GetNormalizedProgress(
			gameObjectId,
			*normalizedProgress);
}

bool EditorScriptManager::ScriptGetRailLengthBridge(int32_t gameObjectId, float* railLength) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		railLength != nullptr &&
		gActiveScriptManager->railMovementManager_->GetRailLength(gameObjectId, *railLength);
}

bool EditorScriptManager::ScriptGetRailPositionBridge(
	int32_t gameObjectId,
	float normalizedProgress,
	EditorScriptVector3* position) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->railMovementManager_ == nullptr ||
		position == nullptr) {
		return false;
	}

	Vector3 editorPosition{};

	if (!gActiveScriptManager->railMovementManager_->GetRailPosition(
			gameObjectId,
			normalizedProgress,
			editorPosition)) {
		return false;
	}

	*position = ToScriptVector3(editorPosition);
	return true;
}

bool EditorScriptManager::ScriptGetRailDirectionBridge(
	int32_t gameObjectId,
	float normalizedProgress,
	EditorScriptVector3* direction) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->railMovementManager_ == nullptr ||
		direction == nullptr) {
		return false;
	}

	Vector3 editorDirection{};

	if (!gActiveScriptManager->railMovementManager_->GetRailDirection(
			gameObjectId,
			normalizedProgress,
			editorDirection)) {
		return false;
	}

	*direction = ToScriptVector3(editorDirection);
	return true;
}

bool EditorScriptManager::ScriptConsumeRailEndReachedBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->ConsumeEndReached(gameObjectId);
}

bool EditorScriptManager::ScriptViewportPointToRayBridge(
	const EditorScriptVector2* normalizedPosition,
	EditorScriptRay* ray) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->targetingManager_ == nullptr ||
		normalizedPosition == nullptr || ray == nullptr) {
		return false;
	}

	EditorTargetingManager::AimRay aimRay{};

	if (!gActiveScriptManager->targetingManager_->ViewportPointToRay(*normalizedPosition, aimRay)) {
		return false;
	}

	ray->origin = ToScriptVector3(aimRay.origin);
	ray->direction = ToScriptVector3(aimRay.direction);
	return true;
}

bool EditorScriptManager::ScriptGetAimRayBridge(
	int32_t screenAimGameObjectId,
	EditorScriptRay* ray) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->targetingManager_ == nullptr ||
		ray == nullptr) {
		return false;
	}

	EditorTargetingManager::AimRay aimRay{};

	if (!gActiveScriptManager->targetingManager_->GetAimRay(screenAimGameObjectId, aimRay)) {
		return false;
	}

	ray->origin = ToScriptVector3(aimRay.origin);
	ray->direction = ToScriptVector3(aimRay.direction);
	return true;
}

bool EditorScriptManager::ScriptPhysicsRaycastBridge(
	const EditorScriptRay* ray,
	float distance,
	EditorScriptPhysicsHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->physicsManager_ == nullptr ||
		ray == nullptr || hit == nullptr) {
		return false;
	}

	EditorJoltPhysicsManager::PhysicsHit physicsHit{};
	const bool hasHit = gActiveScriptManager->physicsManager_->Raycast(
		ToEditorVector3(ray->origin),
		ToEditorVector3(ray->direction),
		distance,
		physicsHit);

	if (!hasHit) {
		return false;
	}

	hit->gameObjectId = physicsHit.gameObjectId;
	hit->point = ToScriptVector3(physicsHit.point);
	hit->normal = ToScriptVector3(physicsHit.normal);
	hit->distance = physicsHit.distance;
	hit->isTrigger = physicsHit.isTrigger;
	return true;
}

bool EditorScriptManager::ScriptPhysicsRaycastIgnoringHierarchyBridge(
	const EditorScriptRay* ray,
	float distance,
	int32_t ignoreHierarchyRootGameObjectId,
	EditorScriptPhysicsHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->physicsManager_ == nullptr ||
		gActiveScriptManager->editorScene_ == nullptr || ray == nullptr || hit == nullptr) {
		return false;
	}

	EditorScene& editorScene = *gActiveScriptManager->editorScene_;
	std::vector<int32_t> ignoredGameObjectIds;

	if (ignoreHierarchyRootGameObjectId >= 0) {
		// Root自身または祖先にRootを持つ全Objectを除外リストへ集める(発射時のBuildAttackIgnoredGameObjectsと同じ考え方)。
		for (const EditorGameObject& candidate : editorScene.GetGameObjects()) {
			int32_t currentGameObjectId = candidate.id;

			while (currentGameObjectId >= 0) {
				if (currentGameObjectId == ignoreHierarchyRootGameObjectId) {
					ignoredGameObjectIds.push_back(candidate.id);
					break;
				}

				const EditorGameObject* currentGameObject = editorScene.FindGameObject(currentGameObjectId);
				currentGameObjectId = currentGameObject != nullptr ? currentGameObject->parentId : -1;
			}
		}
	}

	EditorJoltPhysicsManager::PhysicsHit physicsHit{};
	const bool hasHit = gActiveScriptManager->physicsManager_->RaycastIgnoringGameObjects(
		ToEditorVector3(ray->origin),
		ToEditorVector3(ray->direction),
		distance,
		ignoredGameObjectIds,
		physicsHit);

	if (!hasHit) {
		return false;
	}

	hit->gameObjectId = physicsHit.gameObjectId;
	hit->point = ToScriptVector3(physicsHit.point);
	hit->normal = ToScriptVector3(physicsHit.normal);
	hit->distance = physicsHit.distance;
	hit->isTrigger = physicsHit.isTrigger;
	return true;
}

bool EditorScriptManager::ScriptPhysicsSphereCastBridge(
	const EditorScriptRay* ray,
	float radius,
	float distance,
	EditorScriptPhysicsHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->physicsManager_ == nullptr ||
		ray == nullptr || hit == nullptr) {
		return false;
	}

	EditorJoltPhysicsManager::PhysicsHit physicsHit{};
	const bool hasHit = gActiveScriptManager->physicsManager_->SphereCast(
		ToEditorVector3(ray->origin),
		radius,
		ToEditorVector3(ray->direction),
		distance,
		physicsHit);

	if (!hasHit) {
		return false;
	}

	hit->gameObjectId = physicsHit.gameObjectId;
	hit->point = ToScriptVector3(physicsHit.point);
	hit->normal = ToScriptVector3(physicsHit.normal);
	hit->distance = physicsHit.distance;
	hit->isTrigger = physicsHit.isTrigger;
	return true;
}

bool EditorScriptManager::ScriptPhysicsCapsuleCastBridge(
	const EditorScriptRay* ray,
	float radius,
	float height,
	float distance,
	EditorScriptPhysicsHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->physicsManager_ == nullptr ||
		ray == nullptr || hit == nullptr) {
		return false;
	}

	EditorJoltPhysicsManager::PhysicsHit physicsHit{};
	const bool hasHit = gActiveScriptManager->physicsManager_->CapsuleCast(
		ToEditorVector3(ray->origin),
		radius,
		height,
		ToEditorVector3(ray->direction),
		distance,
		physicsHit);

	if (!hasHit) {
		return false;
	}

	hit->gameObjectId = physicsHit.gameObjectId;
	hit->point = ToScriptVector3(physicsHit.point);
	hit->normal = ToScriptVector3(physicsHit.normal);
	hit->distance = physicsHit.distance;
	hit->isTrigger = physicsHit.isTrigger;
	return true;
}

bool EditorScriptManager::ScriptSampleOceanSurfaceBridge(
	int32_t queryGameObjectId,
	const EditorScriptVector3* worldPosition,
	EditorScriptOceanSurfaceHit* hit) {
	return ScriptSampleOceanSurfaceDetailedBridge(
		queryGameObjectId,
		worldPosition,
		hit,
		nullptr);
}

bool EditorScriptManager::ScriptSampleOceanSurfaceDetailedBridge(
	int32_t queryGameObjectId,
	const EditorScriptVector3* worldPosition,
	EditorScriptOceanSurfaceHit* hit,
	float* foam) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr ||
		worldPosition == nullptr || hit == nullptr) {
		return false;
	}

	const uint64_t surfaceSampleKey = queryGameObjectId >= 0
		? static_cast<uint64_t>(static_cast<uint32_t>(queryGameObjectId))
		: 0xffffffffull;
	const Vector3 queryPosition = ToEditorVector3(*worldPosition);
	EditorOceanSurfaceSample surfaceSample{};

	if (!SampleEditorOceanSurface(
			*gActiveScriptManager->editorScene_,
			-1,
			queryPosition,
			surfaceSampleKey,
			GetEditorOceanElapsedTime(),
			surfaceSample)) {
		return false;
	}

	hit->oceanGameObjectId = surfaceSample.oceanGameObjectId;
	hit->point = ToScriptVector3(surfaceSample.position);
	hit->normal = ToScriptVector3(surfaceSample.normal);
	hit->velocity = ToScriptVector3(surfaceSample.velocity);
	hit->signedDistance = Dot(
		Subtract(queryPosition, surfaceSample.position),
		surfaceSample.normal);

	if (foam != nullptr) {
		*foam = surfaceSample.foam;
	}

	return true;
}

bool EditorScriptManager::ScriptGetWaterSurfaceFoamBridge(
	int32_t gameObjectId,
	float* foam) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		foam != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetWaterSurfaceFoam(
			gameObjectId,
			*foam);
}

bool EditorScriptManager::ScriptGetOceanProbeFoamBridge(
	int32_t gameObjectId,
	int32_t probeIndex,
	float* foam) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		foam != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetOceanProbeFoam(
			gameObjectId,
			probeIndex,
			*foam);
}

bool EditorScriptManager::ScriptApplyDamageBridge(
	int32_t targetGameObjectId,
	float damage,
	int32_t sourceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		gActiveScriptManager->damageManager_->ApplyDamage(
			targetGameObjectId,
			damage,
			sourceGameObjectId);
}

bool EditorScriptManager::ScriptApplyDamageContextBridge(EditorScriptDamageContext* damageContext) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		damageContext != nullptr &&
		gActiveScriptManager->damageManager_->ApplyDamage(*damageContext);
}

bool EditorScriptManager::ScriptGetLastDamageContextBridge(
	int32_t targetGameObjectId,
	EditorScriptDamageContext* damageContext) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		damageContext != nullptr &&
		gActiveScriptManager->damageManager_->GetLastDamageContext(
			targetGameObjectId,
			*damageContext);
}

bool EditorScriptManager::ScriptGetHealthBridge(
	int32_t gameObjectId,
	float* currentHealth,
	float* maximumHealth) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		currentHealth != nullptr && maximumHealth != nullptr &&
		gActiveScriptManager->damageManager_->GetHealth(
			gameObjectId,
			*currentHealth,
			*maximumHealth);
}

bool EditorScriptManager::ScriptSetHealthBridge(int32_t gameObjectId, float currentHealth) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		gActiveScriptManager->damageManager_->SetHealth(gameObjectId, currentHealth);
}

int32_t EditorScriptManager::ScriptSpawnFromPoolBridge(
	int32_t poolGameObjectId,
	const EditorScriptVector3* position,
	const EditorScriptVector3* rotation) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->objectPoolManager_ == nullptr ||
		position == nullptr || rotation == nullptr) {
		return -1;
	}

	return gActiveScriptManager->objectPoolManager_->Spawn(
		poolGameObjectId,
		ToEditorVector3(*position),
		ToEditorVector3(*rotation));
}

int32_t EditorScriptManager::ScriptSpawnFromSpawnerBridge(int32_t spawnerGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->objectPoolManager_ != nullptr
		? gActiveScriptManager->objectPoolManager_->SpawnFromSpawner(spawnerGameObjectId)
		: -1;
}

bool EditorScriptManager::ScriptReleaseToPoolBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->objectPoolManager_ != nullptr &&
		gActiveScriptManager->objectPoolManager_->Release(gameObjectId);
}

bool EditorScriptManager::ScriptFireHitscanBridge(int32_t weaponGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		gActiveScriptManager->weaponManager_->FireHitscan(weaponGameObjectId);
}

bool EditorScriptManager::ScriptFireProjectileBridge(int32_t emitterGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		gActiveScriptManager->weaponManager_->FireProjectile(emitterGameObjectId);
}

bool EditorScriptManager::ScriptGetWeaponAccuracySpreadBridge(
	int32_t gameObjectId,
	float* spreadDegrees) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		spreadDegrees != nullptr &&
		gActiveScriptManager->weaponManager_->GetAccuracySpread(gameObjectId, *spreadDegrees);
}

bool EditorScriptManager::ScriptPlayTimeScaleBridge(
	int32_t gameObjectId,
	float scaleOverride,
	float durationOverride) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->PlayTimeScale(gameObjectId, scaleOverride, durationOverride);
}

float EditorScriptManager::ScriptGetTimeScaleBridge() {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr
		? gActiveScriptManager->runtimePropertyManager_->GetTimeScale()
		: 1.0f;
}

bool EditorScriptManager::ScriptGetInterceptPredictionBridge(
	int32_t gameObjectId,
	EditorScriptVector3* position,
	float* timeSeconds) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->targetingManager_ == nullptr ||
		position == nullptr || timeSeconds == nullptr) {
		return false;
	}

	Vector3 predictedPosition{};

	if (!gActiveScriptManager->targetingManager_->GetInterceptPrediction(
		gameObjectId,
		predictedPosition,
		*timeSeconds)) {
		return false;
	}

	*position = ToScriptVector3(predictedPosition);
	return true;
}

bool EditorScriptManager::ScriptSetObjectiveBridge(
	int32_t gameObjectId,
	const char* objectiveId,
	int32_t state,
	float currentValue) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		objectiveId != nullptr && objectiveId[0] != '\0' &&
		gActiveScriptManager->runtimePropertyManager_->SetObjective(
			gameObjectId,
			objectiveId,
			state,
			currentValue);
}

bool EditorScriptManager::ScriptGetObjectiveBridge(
	int32_t gameObjectId,
	const char* objectiveId,
	int32_t* state,
	float* currentValue,
	float* targetValue) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		objectiveId != nullptr && objectiveId[0] != '\0' && state != nullptr &&
		currentValue != nullptr && targetValue != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetObjective(
			gameObjectId,
			objectiveId,
			*state,
			*currentValue,
			*targetValue);
}

bool EditorScriptManager::ScriptStartEncounterBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->waveSpawnerManager_ != nullptr &&
		gActiveScriptManager->waveSpawnerManager_->StartEncounter(gameObjectId);
}

bool EditorScriptManager::ScriptResolveSpawnPointBridge(
	int32_t gameObjectId,
	EditorScriptVector3* position,
	EditorScriptVector3* rotation) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->waveSpawnerManager_ == nullptr ||
		position == nullptr || rotation == nullptr) {
		return false;
	}

	Vector3 resolvedPosition = ToEditorVector3(*position);
	Vector3 resolvedRotation = ToEditorVector3(*rotation);

	if (!gActiveScriptManager->waveSpawnerManager_->ResolveSpawnPoint(
		gameObjectId,
		resolvedPosition,
		resolvedRotation)) {
		return false;
	}

	*position = ToScriptVector3(resolvedPosition);
	*rotation = ToScriptVector3(resolvedRotation);
	return true;
}

bool EditorScriptManager::ScriptApplyDifficultyBridge(int32_t gameObjectId, int32_t difficultyIndex) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->ApplyDifficulty(gameObjectId, difficultyIndex);
}

bool EditorScriptManager::ScriptGetDamageDirectionBridge(
	int32_t gameObjectId,
	EditorScriptVector2* direction,
	float* alpha,
	int32_t* sourceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		direction != nullptr && alpha != nullptr && sourceGameObjectId != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetDamageDirection(
			gameObjectId,
			*direction,
			*alpha,
			*sourceGameObjectId);
}

bool EditorScriptManager::ScriptGetBallisticPredictionBridge(
	int32_t gameObjectId,
	EditorScriptBallisticPrediction* prediction) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->targetingManager_ == nullptr ||
		prediction == nullptr) {
		return false;
	}

	Vector3 launchDirection{};
	Vector3 impactPosition{};
	float flightTime = 0.0f;

	if (!gActiveScriptManager->targetingManager_->GetBallisticPrediction(
		gameObjectId,
		launchDirection,
		impactPosition,
		flightTime)) {
		*prediction = {};
		return false;
	}

	const EditorGameObject* gameObject = gActiveScriptManager->editorScene_ != nullptr
		? gActiveScriptManager->editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	const EditorComponent* component = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::BallisticPrediction)
		: nullptr;
	prediction->valid = true;
	prediction->launchDirection = ToScriptVector3(launchDirection);
	prediction->impactPosition = ToScriptVector3(impactPosition);
	prediction->flightTime = flightTime;
	prediction->trajectoryPointCount = component != nullptr
		? static_cast<int32_t>(component->ballisticTrajectoryPoints.size())
		: 0;
	prediction->launchVelocity = component != nullptr
		? ToScriptVector3(component->ballisticLaunchVelocity)
		: EditorScriptVector3{};
	prediction->sourceVelocity = component != nullptr
		? ToScriptVector3(component->ballisticSourceVelocity)
		: EditorScriptVector3{};
	return true;
}

bool EditorScriptManager::ScriptGetBallisticTrajectoryPointBridge(
	int32_t gameObjectId,
	int32_t pointIndex,
	EditorScriptVector3* point) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->targetingManager_ == nullptr ||
		point == nullptr) {
		return false;
	}

	Vector3 trajectoryPoint{};

	if (!gActiveScriptManager->targetingManager_->GetBallisticTrajectoryPoint(
		gameObjectId,
		pointIndex,
		trajectoryPoint)) {
		return false;
	}

	*point = ToScriptVector3(trajectoryPoint);
	return true;
}

bool EditorScriptManager::ScriptGetDamageEventBufferCountBridge(
	int32_t gameObjectId,
	int32_t* eventCount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr &&
		eventCount != nullptr &&
		gActiveScriptManager->damageManager_->GetDamageEventCount(gameObjectId, *eventCount);
}

bool EditorScriptManager::ScriptGetDamageEventBufferEntryBridge(
	int32_t gameObjectId,
	int32_t eventIndex,
	EditorScriptDamageEvent* damageEvent) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->damageManager_ == nullptr ||
		damageEvent == nullptr) {
		return false;
	}

	EditorDamageEventRuntimeEntry runtimeEntry{};

	if (!gActiveScriptManager->damageManager_->GetDamageEvent(
		gameObjectId,
		eventIndex,
		runtimeEntry)) {
		return false;
	}

	damageEvent->sourceGameObjectId = runtimeEntry.sourceGameObjectId;
	damageEvent->worldDirection = ToScriptVector3(runtimeEntry.worldDirection);
	damageEvent->damage = runtimeEntry.damage;
	damageEvent->damageTagId = runtimeEntry.damageTagId;
	damageEvent->remainingSeconds = runtimeEntry.remainingSeconds;
	return true;
}

bool EditorScriptManager::ScriptSetGamePausedBridge(int32_t gameObjectId, bool isPaused) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetGamePaused(gameObjectId, isPaused);
}

bool EditorScriptManager::ScriptIsGamePausedBridge() {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->IsGamePaused();
}

bool EditorScriptManager::ScriptGetSurfaceWakeStateBridge(
	int32_t gameObjectId,
	float* speed,
	float* intensity) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		speed != nullptr && intensity != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetSurfaceWakeState(
			gameObjectId,
			*speed,
			*intensity);
}

bool EditorScriptManager::ScriptOceanSegmentCastBridge(
	int32_t queryGameObjectId,
	int32_t oceanGameObjectId,
	const EditorScriptVector3* startPosition,
	const EditorScriptVector3* endPosition,
	float clearance,
	EditorScriptOceanSegmentHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr ||
		startPosition == nullptr || endPosition == nullptr || hit == nullptr) {
		return false;
	}

	EditorOceanSegmentHit oceanHit{};
	const uint64_t sampleKey = queryGameObjectId >= 0
		? static_cast<uint64_t>(static_cast<uint32_t>(queryGameObjectId))
		: 0xffffffffull;

	if (!CastEditorOceanSegment(
			*gActiveScriptManager->editorScene_,
			oceanGameObjectId,
			ToEditorVector3(*startPosition),
			ToEditorVector3(*endPosition),
			clearance,
			sampleKey,
			GetEditorOceanElapsedTime(),
			16,
			5,
			oceanHit)) {
		*hit = {};
		return false;
	}

	*hit = ToScriptOceanSegmentHit(oceanHit);
	return true;
}

bool EditorScriptManager::ScriptOceanRaycastBridge(
	int32_t queryGameObjectId,
	int32_t oceanGameObjectId,
	const EditorScriptRay* ray,
	float maximumDistance,
	float clearance,
	EditorScriptOceanSegmentHit* hit) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr ||
		ray == nullptr || hit == nullptr) {
		return false;
	}

	EditorOceanSegmentHit oceanHit{};
	// queryGameObjectId(自機など毎フレーム不変のID)だけをKeyにすると、GPU FFT読み戻し
	// キャッシュが「前回そのIDで問い合わせた別のレイ位置」の古い結果を即返してしまう
	// (Weapon側のOcean Segment Castと同種の不具合)。呼び出しごとに一意な値を混ぜて、
	// 常にその場で正確なCPU波高計算を使わせる。
	const uint64_t sampleKey =
		(static_cast<uint64_t>(static_cast<uint32_t>(queryGameObjectId)) << 32u) |
		static_cast<uint64_t>(gActiveScriptManager->nextOceanQueryCallId_++);

	if (!RaycastEditorOceanSurface(
			*gActiveScriptManager->editorScene_,
			oceanGameObjectId,
			ToEditorVector3(ray->origin),
			ToEditorVector3(ray->direction),
			maximumDistance,
			clearance,
			sampleKey,
			GetEditorOceanElapsedTime(),
			16,
			5,
			oceanHit)) {
		*hit = {};
		return false;
	}

	*hit = ToScriptOceanSegmentHit(oceanHit);
	return true;
}

bool EditorScriptManager::ScriptQueryOceanOcclusionBridge(
	int32_t queryGameObjectId,
	int32_t oceanGameObjectId,
	const EditorScriptVector3* startPosition,
	const EditorScriptVector3* endPosition,
	float clearance,
	EditorScriptOceanOcclusion* occlusion) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr ||
		startPosition == nullptr || endPosition == nullptr || occlusion == nullptr) {
		return false;
	}

	EditorOceanOcclusionResult result{};
	const uint64_t sampleKey = queryGameObjectId >= 0
		? static_cast<uint64_t>(static_cast<uint32_t>(queryGameObjectId))
		: 0xffffffffull;

	if (!QueryEditorOceanOcclusion(
			*gActiveScriptManager->editorScene_,
			oceanGameObjectId,
			ToEditorVector3(*startPosition),
			ToEditorVector3(*endPosition),
			clearance,
			sampleKey,
			GetEditorOceanElapsedTime(),
			16,
			5,
			result)) {
		*occlusion = {};
		return false;
	}

	occlusion->blocked = result.isBlocked;
	occlusion->minimumClearance = result.minimumClearance;
	occlusion->maximumSurfaceHeight = result.maximumSurfaceHeight;
	occlusion->intersection = ToScriptOceanSegmentHit(result.intersection);
	return true;
}

bool EditorScriptManager::ScriptGetWaterSurfaceStateBridge(
	int32_t gameObjectId,
	EditorScriptWaterSurfaceState* state) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		state == nullptr) {
		return false;
	}

	Vector3 position{};
	Vector3 normal{};
	Vector3 velocity{};

	if (!gActiveScriptManager->runtimePropertyManager_->GetWaterSurfaceState(
			gameObjectId,
			state->state,
			state->signedDistance,
			state->oceanGameObjectId,
			position,
			normal,
			velocity)) {
		*state = {};
		state->oceanGameObjectId = -1;
		return false;
	}

	state->surfacePosition = ToScriptVector3(position);
	state->surfaceNormal = ToScriptVector3(normal);
	state->surfaceVelocity = ToScriptVector3(velocity);
	return true;
}

bool EditorScriptManager::ScriptGetOceanProbeSampleBridge(
	int32_t gameObjectId,
	int32_t probeIndex,
	EditorScriptOceanProbeSample* sample) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		sample == nullptr) {
		return false;
	}

	EditorOceanProbeEntry probeEntry{};

	if (!gActiveScriptManager->runtimePropertyManager_->GetOceanProbeSample(
			gameObjectId,
			probeIndex,
			probeEntry)) {
		*sample = {};
		return false;
	}

	sample->valid = probeEntry.isValid;
	sample->distance = probeEntry.distance;
	sample->position = ToScriptVector3(probeEntry.position);
	sample->normal = ToScriptVector3(probeEntry.normal);
	sample->velocity = ToScriptVector3(probeEntry.velocity);
	sample->relativeHeight = probeEntry.relativeHeight;
	return true;
}

bool EditorScriptManager::ScriptPlayCameraBlendBridge(int32_t componentOwnerGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->cameraEffectManager_ != nullptr &&
		gActiveScriptManager->cameraEffectManager_->PlayBlend(componentOwnerGameObjectId);
}

bool EditorScriptManager::ScriptPlayCameraShakeBridge(int32_t componentOwnerGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->cameraEffectManager_ != nullptr &&
		gActiveScriptManager->cameraEffectManager_->PlayShake(componentOwnerGameObjectId);
}

bool EditorScriptManager::ScriptTriggerRailBranchBridge(int32_t componentOwnerGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railBranchManager_ != nullptr &&
		gActiveScriptManager->railBranchManager_->Trigger(componentOwnerGameObjectId);
}

bool EditorScriptManager::ScriptLoadSceneAsyncBridge(const char* scenePath, bool isAdditive) {
	return gActiveScriptManager != nullptr && scenePath != nullptr &&
		gActiveScriptManager->RequestSceneLoadAsync(scenePath, isAdditive);
}

bool EditorScriptManager::ScriptUnloadSceneBridge(const char* scenePath) {
	return gActiveScriptManager != nullptr && scenePath != nullptr &&
		gActiveScriptManager->RequestSceneUnload(scenePath);
}

float EditorScriptManager::ScriptGetSceneLoadProgressBridge() {
	return gActiveScriptManager != nullptr ? gActiveScriptManager->sceneLoadProgress_ : 0.0f;
}

bool EditorScriptManager::ScriptIsSceneLoadingBridge() {
	return gActiveScriptManager != nullptr && gActiveScriptManager->isSceneLoading_;
}

bool EditorScriptManager::ScriptIsSceneLoadedBridge(const char* scenePath) {
	if (gActiveScriptManager == nullptr || scenePath == nullptr) {
		return false;
	}

	return gActiveScriptManager->IsSceneRuntimeLoaded(scenePath);
}

void EditorScriptManager::ScriptSetSceneFloatBridge(const char* key, float value) {
	if (gActiveScriptManager != nullptr && key != nullptr && key[0] != '\0') {
		gActiveScriptManager->sceneFloatValues_[key] = value;
	}
}

bool EditorScriptManager::ScriptGetSceneFloatBridge(const char* key, float* value) {
	if (gActiveScriptManager == nullptr || key == nullptr || value == nullptr) {
		return false;
	}

	const auto valueIterator = gActiveScriptManager->sceneFloatValues_.find(key);
	if (valueIterator == gActiveScriptManager->sceneFloatValues_.end()) {
		return false;
	}

	*value = valueIterator->second;
	return true;
}

void EditorScriptManager::ScriptSetSceneStringBridge(const char* key, const char* value) {
	if (gActiveScriptManager != nullptr && key != nullptr && key[0] != '\0' && value != nullptr) {
		gActiveScriptManager->sceneStringValues_[key] = value;
	}
}

bool EditorScriptManager::ScriptGetSceneStringBridge(
	const char* key,
	char* value,
	int32_t valueCapacity) {
	if (gActiveScriptManager == nullptr || key == nullptr || value == nullptr || valueCapacity <= 0) {
		return false;
	}

	const auto valueIterator = gActiveScriptManager->sceneStringValues_.find(key);
	if (valueIterator == gActiveScriptManager->sceneStringValues_.end()) {
		return false;
	}

	CopyStringToFixedBuffer(
		valueIterator->second,
		value,
		static_cast<size_t>(valueCapacity));
	return true;
}

bool EditorScriptManager::ScriptPlayActionSequenceBridge(int32_t sequenceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->actionSequenceManager_ != nullptr &&
		gActiveScriptManager->actionSequenceManager_->Play(sequenceGameObjectId);
}

bool EditorScriptManager::ScriptPauseActionSequenceBridge(
	int32_t sequenceGameObjectId,
	bool isPaused) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->actionSequenceManager_ != nullptr &&
		gActiveScriptManager->actionSequenceManager_->Pause(sequenceGameObjectId, isPaused);
}

bool EditorScriptManager::ScriptStopActionSequenceBridge(int32_t sequenceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->actionSequenceManager_ != nullptr &&
		gActiveScriptManager->actionSequenceManager_->StopSequence(sequenceGameObjectId);
}

bool EditorScriptManager::ScriptSignalActionSequenceBridge(
	int32_t sequenceGameObjectId,
	const char* signalName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->actionSequenceManager_ != nullptr &&
		signalName != nullptr &&
		gActiveScriptManager->actionSequenceManager_->Signal(sequenceGameObjectId, signalName);
}

bool EditorScriptManager::ScriptIsActionSequencePlayingBridge(int32_t sequenceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->actionSequenceManager_ != nullptr &&
		gActiveScriptManager->actionSequenceManager_->IsPlaying(sequenceGameObjectId);
}

bool EditorScriptManager::ScriptSaveSlotBridge(const char* slotName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		slotName != nullptr && gActiveScriptManager->saveManager_->SaveSlot(slotName);
}

bool EditorScriptManager::ScriptLoadSlotBridge(const char* slotName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		slotName != nullptr && gActiveScriptManager->saveManager_->LoadSlot(slotName);
}

bool EditorScriptManager::ScriptDeleteSlotBridge(const char* slotName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		slotName != nullptr && gActiveScriptManager->saveManager_->DeleteSlot(slotName);
}

bool EditorScriptManager::ScriptHasSlotBridge(const char* slotName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		slotName != nullptr && gActiveScriptManager->saveManager_->HasSlot(slotName);
}

bool EditorScriptManager::ScriptActivateCheckpointBridge(
	int32_t checkpointGameObjectId,
	bool shouldLoad) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		gActiveScriptManager->saveManager_->ActivateCheckpoint(checkpointGameObjectId, shouldLoad);
}

void EditorScriptManager::ScriptSetSaveFloatBridge(const char* key, float value) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		key != nullptr && key[0] != '\0') {
		gActiveScriptManager->saveManager_->SetFloat(key, value);
	}
}

bool EditorScriptManager::ScriptGetSaveFloatBridge(const char* key, float* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		key != nullptr && value != nullptr && gActiveScriptManager->saveManager_->GetFloat(key, *value);
}

void EditorScriptManager::ScriptSetSaveStringBridge(const char* key, const char* value) {
	if (gActiveScriptManager != nullptr && gActiveScriptManager->saveManager_ != nullptr &&
		key != nullptr && key[0] != '\0' && value != nullptr) {
		gActiveScriptManager->saveManager_->SetString(key, value);
	}
}

bool EditorScriptManager::ScriptGetSaveStringBridge(
	const char* key,
	char* value,
	int32_t valueCapacity) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->saveManager_ == nullptr ||
		key == nullptr || value == nullptr || valueCapacity <= 0) {
		return false;
	}

	std::string savedValue;
	if (!gActiveScriptManager->saveManager_->GetString(key, savedValue)) {
		return false;
	}

	CopyStringToFixedBuffer(savedValue, value, static_cast<size_t>(valueCapacity));
	return true;
}

bool EditorScriptManager::ScriptLoadoutSelectSlotBridge(int32_t gameObjectId, int32_t slotIndex) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SelectSlot(gameObjectId, slotIndex);
}

bool EditorScriptManager::ScriptLoadoutSelectNextBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SelectNext(gameObjectId);
}

bool EditorScriptManager::ScriptLoadoutSelectPreviousBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SelectPrevious(gameObjectId);
}

bool EditorScriptManager::ScriptLoadoutFireBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->FireSelected(gameObjectId);
}

bool EditorScriptManager::ScriptLoadoutReloadBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->Reload(gameObjectId);
}

bool EditorScriptManager::ScriptLoadoutGetAmmoBridge(
	int32_t gameObjectId,
	int32_t* currentAmmo,
	int32_t* reserveAmmo) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		currentAmmo != nullptr && reserveAmmo != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->GetAmmo(gameObjectId, *currentAmmo, *reserveAmmo);
}

bool EditorScriptManager::ScriptLoadoutGetAmmoAtSlotBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t* currentAmmo,
	int32_t* reserveAmmo,
	int32_t* maximumAmmo) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		currentAmmo != nullptr && reserveAmmo != nullptr && maximumAmmo != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->GetAmmoAtSlot(
			gameObjectId,
			slotIndex,
			*currentAmmo,
			*reserveAmmo,
			*maximumAmmo);
}

bool EditorScriptManager::ScriptLoadoutAddMagazineAmmoBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->AddMagazineAmmo(gameObjectId, slotIndex, amount);
}

bool EditorScriptManager::ScriptLoadoutAddReserveAmmoBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->AddReserveAmmo(gameObjectId, slotIndex, amount);
}

bool EditorScriptManager::ScriptLoadoutSetMagazineAmmoBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SetMagazineAmmo(gameObjectId, slotIndex, amount);
}

bool EditorScriptManager::ScriptLoadoutSetReserveAmmoBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SetReserveAmmo(gameObjectId, slotIndex, amount);
}

bool EditorScriptManager::ScriptLoadoutSetMaximumAmmoBridge(
	int32_t gameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->SetMaximumAmmo(gameObjectId, slotIndex, amount);
}

bool EditorScriptManager::ScriptLoadoutRefillMagazineBridge(int32_t gameObjectId, int32_t slotIndex) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponLoadoutManager_ != nullptr &&
		gActiveScriptManager->weaponLoadoutManager_->RefillMagazine(gameObjectId, slotIndex);
}

bool EditorScriptManager::ScriptFireWeaponGroupBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		gActiveScriptManager->weaponManager_->FireWeaponGroup(gameObjectId);
}

bool EditorScriptManager::ScriptIsWeaponGroupFiringBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		gActiveScriptManager->weaponManager_->IsWeaponGroupFiring(gameObjectId);
}

bool EditorScriptManager::ScriptGetTurretAimStateBridge(
	int32_t gameObjectId,
	EditorScriptTurretAimState* state) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr || state == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = gActiveScriptManager->editorScene_->FindGameObject(gameObjectId);
	const EditorComponent* component = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::TurretAim)
		: nullptr;

	if (component == nullptr || !component->isActive) {
		return false;
	}

	state->targetGameObjectId = component->turretCurrentTargetGameObjectId;
	state->canReachTarget = component->turretCanReachTarget;
	state->isAimed = component->turretIsAimed;
	state->reservedPadding[0] = 0U;
	state->reservedPadding[1] = 0U;
	state->yawErrorDegrees = component->turretYawErrorDegrees;
	state->pitchErrorDegrees = component->turretPitchErrorDegrees;
	return true;
}

bool EditorScriptManager::ScriptGetFireLineStateBridge(
	int32_t gameObjectId,
	EditorScriptFireLineState* state) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr || state == nullptr) {
		return false;
	}

	const EditorComponent* component = EditorComponentUtility::FindInheritedComponent(
		*gActiveScriptManager->editorScene_,
		gameObjectId,
		EditorComponentType::FireLineCheck);

	if (component == nullptr || !component->isActive) {
		return false;
	}

	state->isClear = component->fireLineClear;
	state->reservedPadding[0] = 0U;
	state->reservedPadding[1] = 0U;
	state->reservedPadding[2] = 0U;
	state->blockingGameObjectId = component->fireLineBlockingGameObjectId;
	state->blockingDistance = component->fireLineBlockingDistance;
	return true;
}

bool EditorScriptManager::ScriptApplyStatusEffectBridge(
	int32_t gameObjectId,
	const char* effectId,
	int32_t sourceGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		effectId != nullptr && effectId[0] != '\0' &&
		gActiveScriptManager->runtimePropertyManager_->ApplyStatusEffect(gameObjectId, effectId, sourceGameObjectId);
}

bool EditorScriptManager::ScriptRemoveStatusEffectBridge(int32_t gameObjectId, const char* effectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		effectId != nullptr && effectId[0] != '\0' &&
		gActiveScriptManager->runtimePropertyManager_->RemoveStatusEffect(gameObjectId, effectId);
}

bool EditorScriptManager::ScriptClearStatusEffectsBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->ClearStatusEffects(gameObjectId);
}

bool EditorScriptManager::ScriptHasStatusEffectBridge(int32_t gameObjectId, const char* effectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		effectId != nullptr && effectId[0] != '\0' &&
		gActiveScriptManager->runtimePropertyManager_->HasStatusEffect(gameObjectId, effectId);
}

bool EditorScriptManager::ScriptGetStatusEffectCountBridge(int32_t gameObjectId, int32_t* effectCount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		effectCount != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetStatusEffectCount(gameObjectId, *effectCount);
}

bool EditorScriptManager::ScriptGetStatusEffectEntryBridge(
	int32_t gameObjectId,
	int32_t effectIndex,
	EditorScriptStatusEffectEntry* effectEntry) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		effectEntry == nullptr) {
		return false;
	}

	EditorStatusEffectRuntimeEntry runtimeEntry{};

	if (!gActiveScriptManager->runtimePropertyManager_->GetStatusEffectEntry(
		gameObjectId,
		effectIndex,
		runtimeEntry)) {
		return false;
	}

	*effectEntry = {};
	const size_t copyLength = (std::min)(runtimeEntry.effectId.size(), sizeof(effectEntry->effectId) - 1u);
	std::copy_n(runtimeEntry.effectId.data(), copyLength, effectEntry->effectId);
	effectEntry->effectId[copyLength] = '\0';
	effectEntry->sourceGameObjectId = runtimeEntry.sourceGameObjectId;
	effectEntry->remainingSeconds = runtimeEntry.remainingSeconds;
	effectEntry->tickRemainingSeconds = runtimeEntry.tickRemainingSeconds;
	effectEntry->stackCount = runtimeEntry.stackCount;
	return true;
}

bool EditorScriptManager::ScriptGetCurrentTargetBridge(
	int32_t gameObjectId,
	int32_t* targetGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->targetingManager_ != nullptr &&
		targetGameObjectId != nullptr &&
		gActiveScriptManager->targetingManager_->GetCurrentTarget(gameObjectId, *targetGameObjectId);
}

bool EditorScriptManager::ScriptSetExplicitTargetBridge(
	int32_t gameObjectId,
	int32_t targetGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->targetingManager_ != nullptr &&
		gActiveScriptManager->targetingManager_->SetExplicitTarget(gameObjectId, targetGameObjectId);
}

bool EditorScriptManager::ScriptSetRuntimeFloatBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	float value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetFloat(gameObjectId, componentName, propertyName, value);
}

bool EditorScriptManager::ScriptGetRuntimeFloatBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	float* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetFloat(gameObjectId, componentName, propertyName, *value);
}

bool EditorScriptManager::ScriptSetRuntimeIntBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	int32_t value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetInt(gameObjectId, componentName, propertyName, value);
}

bool EditorScriptManager::ScriptGetRuntimeIntBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	int32_t* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetInt(gameObjectId, componentName, propertyName, *value);
}

bool EditorScriptManager::ScriptSetRuntimeBoolBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	bool value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetBool(gameObjectId, componentName, propertyName, value);
}

bool EditorScriptManager::ScriptGetRuntimeBoolBridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	bool* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetBool(gameObjectId, componentName, propertyName, *value);
}

bool EditorScriptManager::ScriptSetRuntimeVector2Bridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	const EditorScriptVector2* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetVector2(
			gameObjectId,
			componentName,
			propertyName,
			*value);
}

bool EditorScriptManager::ScriptGetRuntimeVector2Bridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	EditorScriptVector2* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetVector2(
			gameObjectId,
			componentName,
			propertyName,
			*value);
}

bool EditorScriptManager::ScriptSetRuntimeVector3Bridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	const EditorScriptVector3* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		componentName != nullptr && propertyName != nullptr && value != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetVector3(
			gameObjectId,
			componentName,
			propertyName,
			{value->x, value->y, value->z});
}

bool EditorScriptManager::ScriptGetRuntimeVector3Bridge(
	int32_t gameObjectId,
	const char* componentName,
	const char* propertyName,
	EditorScriptVector3* value) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		componentName == nullptr || propertyName == nullptr || value == nullptr) {
		return false;
	}

	Vector3 runtimeValue{};

	if (!gActiveScriptManager->runtimePropertyManager_->GetVector3(
			gameObjectId,
			componentName,
			propertyName,
			runtimeValue)) {
		return false;
	}

	*value = {runtimeValue.x, runtimeValue.y, runtimeValue.z};
	return true;
}

bool EditorScriptManager::ScriptPlayPropertyTweenBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->PlayTween(gameObjectId);
}

bool EditorScriptManager::ScriptStopPropertyTweenBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->StopTween(gameObjectId);
}

bool EditorScriptManager::ScriptIsPropertyTweenPlayingBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->IsTweenPlaying(gameObjectId);
}

bool EditorScriptManager::ScriptRelayActionBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->Relay(gameObjectId);
}

bool EditorScriptManager::ScriptHasComponentBridge(
	int32_t gameObjectId,
	const char* componentTypeName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->HasComponentInternal(gameObjectId, componentTypeName);
}

bool EditorScriptManager::ScriptInvokeScriptActionBridge(
	int32_t gameObjectId,
	const char* functionName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->InvokeScriptActionInternal(gameObjectId, functionName);
}

bool EditorScriptManager::ScriptInvokeScriptActionPayloadBridge(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptActionPayload* payload) {
	if (gActiveScriptManager == nullptr || functionName == nullptr || payload == nullptr) {
		return false;
	}

	return gActiveScriptManager->QueueActionPayload(gameObjectId, functionName, *payload);
}

bool EditorScriptManager::ScriptStartTimerBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->StartTimer(gameObjectId);
}

bool EditorScriptManager::ScriptPauseTimerBridge(int32_t gameObjectId, bool isPaused) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->PauseTimer(gameObjectId, isPaused);
}

bool EditorScriptManager::ScriptGetTimerRemainingBridge(
	int32_t gameObjectId,
	float* remainingSeconds) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		remainingSeconds != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetTimerRemaining(gameObjectId, *remainingSeconds);
}

bool EditorScriptManager::ScriptChangeGenericStateBridge(
	int32_t gameObjectId,
	const char* stateName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		stateName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->ChangeState(gameObjectId, stateName);
}

bool EditorScriptManager::ScriptGetGenericStateBridge(
	int32_t gameObjectId,
	char* stateName,
	int32_t stateNameCapacity) {
	if (gActiveScriptManager == nullptr ||
		gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		stateName == nullptr ||
		stateNameCapacity <= 0) {
		return false;
	}

	std::string stateNameValue;

	if (!gActiveScriptManager->runtimePropertyManager_->GetState(gameObjectId, stateNameValue)) {
		return false;
	}

	CopyStringToFixedBuffer(
		stateNameValue,
		stateName,
		static_cast<size_t>(stateNameCapacity));
	return true;
}

bool EditorScriptManager::ScriptSetAttributeValueBridge(int32_t gameObjectId, float value) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetAttribute(gameObjectId, value);
}

bool EditorScriptManager::ScriptGetAttributeValueBridge(
	int32_t gameObjectId,
	float* current,
	float* maximum) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		current != nullptr &&
		maximum != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetAttribute(gameObjectId, *current, *maximum);
}

bool EditorScriptManager::ScriptGetTargetLockStateBridge(
	int32_t gameObjectId,
	float* progress,
	bool* isLocked,
	int32_t* targetGameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		progress != nullptr &&
		isLocked != nullptr &&
		targetGameObjectId != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetTargetLockState(
			gameObjectId,
			*progress,
			*isLocked,
			*targetGameObjectId);
}

bool EditorScriptManager::ScriptSetNamedAttributeValueBridge(
	int32_t gameObjectId,
	const char* attributeName,
	float value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		attributeName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetNamedAttribute(gameObjectId, attributeName, value);
}

bool EditorScriptManager::ScriptGetNamedAttributeValueBridge(
	int32_t gameObjectId,
	const char* attributeName,
	float* current,
	float* maximum) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		attributeName != nullptr && current != nullptr && maximum != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetNamedAttribute(gameObjectId, attributeName, *current, *maximum);
}

bool EditorScriptManager::ScriptSetCounterValueBridge(int32_t gameObjectId, float value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->SetCounter(gameObjectId, value);
}

bool EditorScriptManager::ScriptAddCounterValueBridge(int32_t gameObjectId, float deltaValue) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->AddCounter(gameObjectId, deltaValue);
}

bool EditorScriptManager::ScriptGetCounterValueBridge(int32_t gameObjectId, float* value) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		value != nullptr && gActiveScriptManager->runtimePropertyManager_->GetCounter(gameObjectId, *value);
}

bool EditorScriptManager::ScriptEvaluateGenericConditionBridge(int32_t gameObjectId, bool* result) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		result != nullptr && gActiveScriptManager->runtimePropertyManager_->EvaluateCondition(gameObjectId, *result);
}

bool EditorScriptManager::ScriptGetMultiTargetLockCountBridge(int32_t gameObjectId, int32_t* targetCount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		targetCount != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetMultiTargetLockCount(gameObjectId, *targetCount);
}

bool EditorScriptManager::ScriptGetMultiTargetLockTargetBridge(
	int32_t gameObjectId,
	int32_t targetIndex,
	int32_t* targetGameObjectId,
	float* progress,
	bool* isLocked) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		targetGameObjectId != nullptr && progress != nullptr && isLocked != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetMultiTargetLockTarget(
			gameObjectId, targetIndex, *targetGameObjectId, *progress, *isLocked);
}

bool EditorScriptManager::ScriptGetGameplayDataValueBridge(
	int32_t gameObjectId,
	const char* key,
	int32_t* valueType,
	char* value,
	int32_t valueCapacity) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		key == nullptr || valueType == nullptr || value == nullptr || valueCapacity <= 0) {
		return false;
	}

	std::string dataValue;

	if (!gActiveScriptManager->runtimePropertyManager_->GetGameplayDataValue(gameObjectId, key, *valueType, dataValue)) {
		return false;
	}

	CopyStringToFixedBuffer(dataValue, value, static_cast<size_t>(valueCapacity));
	return true;
}

int32_t EditorScriptManager::ScriptHashDamageTagBridge(const char* damageTag) {
	return damageTag != nullptr ? EditorDamageManager::HashDamageTag(damageTag) : 0;
}

int32_t EditorScriptManager::ScriptApplyAreaDamageBridge(
	int32_t areaDamageGameObjectId,
	int32_t instigatorGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->damageManager_ != nullptr
		? gActiveScriptManager->damageManager_->ApplyAreaDamage(areaDamageGameObjectId, instigatorGameObjectId)
		: 0;
}

bool EditorScriptManager::ScriptDetonateProjectileBridge(int32_t projectileGameObjectId) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->weaponManager_ != nullptr &&
		gActiveScriptManager->weaponManager_->DetonateProjectile(projectileGameObjectId);
}

bool EditorScriptManager::ScriptGetThreatTrackerCountBridge(
	int32_t gameObjectId,
	int32_t* threatCount) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		threatCount != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetThreatCount(gameObjectId, *threatCount);
}

bool EditorScriptManager::ScriptGetThreatTrackerEntryBridge(
	int32_t gameObjectId,
	int32_t threatIndex,
	EditorScriptThreatInfo* threatInfo) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->runtimePropertyManager_ == nullptr ||
		threatInfo == nullptr) {
		return false;
	}

	EditorThreatRuntimeEntry threat{};

	if (!gActiveScriptManager->runtimePropertyManager_->GetThreat(gameObjectId, threatIndex, threat)) {
		return false;
	}

	threatInfo->projectileGameObjectId = threat.projectileGameObjectId;
	threatInfo->sourceGameObjectId = threat.sourceGameObjectId;
	threatInfo->distance = threat.distance;
	threatInfo->closingSpeed = threat.closingSpeed;
	threatInfo->estimatedArrivalSeconds = threat.estimatedArrivalSeconds;
	return true;
}

bool EditorScriptManager::ScriptStartNamedCooldownBridge(
	int32_t gameObjectId,
	const char* cooldownName,
	float durationOverride) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		cooldownName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->StartCooldown(
			gameObjectId, cooldownName, durationOverride);
}

bool EditorScriptManager::ScriptResetNamedCooldownBridge(
	int32_t gameObjectId,
	const char* cooldownName) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		cooldownName != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->ResetCooldown(gameObjectId, cooldownName);
}

bool EditorScriptManager::ScriptGetNamedCooldownBridge(
	int32_t gameObjectId,
	const char* cooldownName,
	float* remainingSeconds,
	bool* isReady) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->runtimePropertyManager_ != nullptr &&
		cooldownName != nullptr && remainingSeconds != nullptr && isReady != nullptr &&
		gActiveScriptManager->runtimePropertyManager_->GetCooldown(
			gameObjectId, cooldownName, *remainingSeconds, *isReady);
}

bool EditorScriptManager::ScriptResetRuntimeStateBridge(int32_t gameObjectId) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->objectPoolManager_ == nullptr ||
		gActiveScriptManager->editorScene_ == nullptr ||
		gActiveScriptManager->editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	gActiveScriptManager->objectPoolManager_->ResetObjectRuntimeState(gameObjectId);
	return true;
}

bool EditorScriptManager::ScriptGetRailStateBridge(
	int32_t gameObjectId,
	EditorScriptRailState* state) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		state != nullptr && gActiveScriptManager->railMovementManager_->GetState(gameObjectId, *state);
}

bool EditorScriptManager::ScriptSetRailDistanceBridge(
	int32_t gameObjectId,
	float distance) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->SetDistance(gameObjectId, distance);
}

bool EditorScriptManager::ScriptGetRailClosestProgressBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* worldPosition,
	float* normalizedProgress) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		worldPosition != nullptr && normalizedProgress != nullptr &&
		gActiveScriptManager->railMovementManager_->GetClosestNormalizedProgress(
			gameObjectId,
			{worldPosition->x, worldPosition->y, worldPosition->z},
			*normalizedProgress);
}

bool EditorScriptManager::ScriptGetRailFrameBridge(
	int32_t gameObjectId,
	float normalizedProgress,
	EditorScriptRailFrame* frame) {
	return gActiveScriptManager != nullptr && gActiveScriptManager->railMovementManager_ != nullptr &&
		frame != nullptr &&
		gActiveScriptManager->railMovementManager_->GetRailFrame(
			gameObjectId,
			normalizedProgress,
			*frame);
}

bool EditorScriptManager::ScriptSetRailSpeedProfileEnabledBridge(
	int32_t gameObjectId,
	bool isEnabled) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = gActiveScriptManager->editorScene_->FindGameObject(gameObjectId);
	EditorComponent* speedProfile = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailSpeedProfile)
		: nullptr;

	if (speedProfile == nullptr) {
		return false;
	}

	speedProfile->railSpeedProfileEnabled = isEnabled;
	return true;
}

bool EditorScriptManager::ScriptGetRailSpeedMultiplierBridge(
	int32_t gameObjectId,
	float* speedMultiplier) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->railMovementManager_ != nullptr &&
		speedMultiplier != nullptr &&
		gActiveScriptManager->railMovementManager_->GetSpeedMultiplier(
			gameObjectId,
			*speedMultiplier);
}

bool EditorScriptManager::ScriptGetRailActiveZoneBridge(
	int32_t gameObjectId,
	char* zoneId,
	int32_t zoneIdCapacity) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr ||
		zoneId == nullptr || zoneIdCapacity <= 0) {
		return false;
	}

	const EditorGameObject* gameObject = gActiveScriptManager->editorScene_->FindGameObject(gameObjectId);
	const EditorComponent* railZone = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailZone)
		: nullptr;

	if (railZone == nullptr || railZone->railZoneActiveIndex < 0 ||
		railZone->railZoneActiveIndex >= static_cast<int32_t>(railZone->railZoneEntries.size())) {
		zoneId[0] = '\0';
		return false;
	}

	CopyStringToFixedBuffer(
		railZone->railZoneEntries[static_cast<size_t>(railZone->railZoneActiveIndex)].zoneId,
		zoneId,
		static_cast<size_t>(zoneIdCapacity));
	return true;
}

bool EditorScriptManager::ScriptRearmRailEventMarkersBridge(
	int32_t gameObjectId,
	const char* markerId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->railMovementManager_ != nullptr &&
		gActiveScriptManager->railMovementManager_->RearmEventMarkers(
			gameObjectId,
			markerId != nullptr ? markerId : "");
}

bool EditorScriptManager::ScriptGetSimulationLodLevelBridge(
	int32_t gameObjectId,
	int32_t* lodLevel) {
	if (gActiveScriptManager == nullptr || gActiveScriptManager->editorScene_ == nullptr || lodLevel == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = gActiveScriptManager->editorScene_->FindGameObject(gameObjectId);
	const EditorComponent* simulationLod = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::SimulationLOD)
		: nullptr;

	if (simulationLod == nullptr) {
		return false;
	}

	*lodLevel = simulationLod->simulationLodRuntimeLevel;
	return true;
}

bool EditorScriptManager::ScriptStartWaveSpawnerBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->waveSpawnerManager_ != nullptr &&
		gActiveScriptManager->waveSpawnerManager_->StartWave(gameObjectId);
}

bool EditorScriptManager::ScriptIsWaveSpawnerCompleteBridge(
	int32_t gameObjectId,
	bool waitsForAllDefeated) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->waveSpawnerManager_ != nullptr &&
		gActiveScriptManager->waveSpawnerManager_->IsWaveComplete(
			gameObjectId,
			waitsForAllDefeated);
}

void EditorScriptManager::BuildScriptBindings() {
	scriptBindings_.clear();
	scriptBindingIndicesByGameObjectId_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!HasRunnableScriptComponent(gameObject)) {
			continue;
		}

		for (size_t componentIndex = 0U; componentIndex < gameObject.components.size(); componentIndex++) {
			const EditorComponent& component = gameObject.components[componentIndex];
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
			scriptBinding.componentIndex = componentIndex;
			scriptBinding.componentType = component.type;
			scriptBinding.dllPath = component.assetPath;
			scriptBindings_.push_back(scriptBinding);
			scriptBindingIndicesByGameObjectId_[gameObject.id].push_back(scriptBindings_.size() - 1U);
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
	runtimeApi_.SetAnimatorFloat = ScriptSetAnimatorFloatBridge;
	runtimeApi_.SetAnimatorInt = ScriptSetAnimatorIntBridge;
	runtimeApi_.SetAnimatorBool = ScriptSetAnimatorBoolBridge;
	runtimeApi_.SetAnimatorTrigger = ScriptSetAnimatorTriggerBridge;
	runtimeApi_.SetAnimatorVector2 = ScriptSetAnimatorVector2Bridge;
	runtimeApi_.SetAnimatorVector3 = ScriptSetAnimatorVector3Bridge;
	runtimeApi_.PlayAnimationAction = ScriptPlayAnimationActionBridge;
	runtimeApi_.PlayEffect = ScriptPlayEffectBridge;
	runtimeApi_.PlayEffectAt = ScriptPlayEffectAtBridge;
	runtimeApi_.StopEffect = ScriptStopEffectBridge;
	runtimeApi_.PlayAudio = ScriptPlayAudioBridge;
	runtimeApi_.StopAudio = ScriptStopAudioBridge;
	runtimeApi_.SetAudioBusVolume = ScriptSetAudioBusVolumeBridge;
	runtimeApi_.GetAudioBusVolume = ScriptGetAudioBusVolumeBridge;
	runtimeApi_.SetAudioMasterVolume = ScriptSetAudioMasterVolumeBridge;
	runtimeApi_.GetAudioMasterVolume = ScriptGetAudioMasterVolumeBridge;
	runtimeApi_.GetAliveParticleCount = ScriptGetAliveParticleCountBridge;
	runtimeApi_.GetAnimatorFloat = ScriptGetAnimatorFloatBridge;
	runtimeApi_.GetAnimatorInt = ScriptGetAnimatorIntBridge;
	runtimeApi_.GetAnimatorBool = ScriptGetAnimatorBoolBridge;
	runtimeApi_.GetAnimatorVector2 = ScriptGetAnimatorVector2Bridge;
	runtimeApi_.GetAnimatorVector3 = ScriptGetAnimatorVector3Bridge;
	runtimeApi_.ResetAnimatorTrigger = ScriptResetAnimatorTriggerBridge;
	runtimeApi_.PlayAnimation = ScriptPlayAnimationBridge;
	runtimeApi_.StopAnimation = ScriptStopAnimationBridge;
	runtimeApi_.IsAnimationPlaying = ScriptIsAnimationPlayingBridge;
	runtimeApi_.GetAnimationTime = ScriptGetAnimationTimeBridge;
	runtimeApi_.SetAnimationTime = ScriptSetAnimationTimeBridge;
	runtimeApi_.SetAnimationSpeed = ScriptSetAnimationSpeedBridge;
	runtimeApi_.GetAnimatorStateName = ScriptGetAnimatorStateNameBridge;
	runtimeApi_.IsEffectPlaying = ScriptIsEffectPlayingBridge;
	runtimeApi_.FindGameObjectByName = ScriptFindGameObjectByNameBridge;
	runtimeApi_.SetGameObjectActive = ScriptSetGameObjectActiveBridge;
	runtimeApi_.IsGameObjectActive = ScriptIsGameObjectActiveBridge;
	runtimeApi_.LoadScene = ScriptLoadSceneBridge;
	runtimeApi_.LoadSceneByBuildIndex = ScriptLoadSceneByBuildIndexBridge;
	runtimeApi_.SetRailPaused = ScriptSetRailPausedBridge;
	runtimeApi_.IsRailPaused = ScriptIsRailPausedBridge;
	runtimeApi_.SetRailSpeed = ScriptSetRailSpeedBridge;
	runtimeApi_.SetRailReverse = ScriptSetRailReverseBridge;
	runtimeApi_.SetRailNormalizedProgress = ScriptSetRailNormalizedProgressBridge;
	runtimeApi_.SetRailPath = ScriptSetRailPathBridge;
	runtimeApi_.SetRailMoveInput = ScriptSetRailMoveInputBridge;
	runtimeApi_.SetRailOffset = ScriptSetRailOffsetBridge;
	runtimeApi_.GetRailOffset = ScriptGetRailOffsetBridge;
	runtimeApi_.GetRailNormalizedProgress = ScriptGetRailNormalizedProgressBridge;
	runtimeApi_.GetRailLength = ScriptGetRailLengthBridge;
	runtimeApi_.GetRailPosition = ScriptGetRailPositionBridge;
	runtimeApi_.GetRailDirection = ScriptGetRailDirectionBridge;
	runtimeApi_.ConsumeRailEndReached = ScriptConsumeRailEndReachedBridge;
	runtimeApi_.ViewportPointToRay = ScriptViewportPointToRayBridge;
	runtimeApi_.GetAimRay = ScriptGetAimRayBridge;
	runtimeApi_.PhysicsRaycast = ScriptPhysicsRaycastBridge;
	runtimeApi_.PhysicsSphereCast = ScriptPhysicsSphereCastBridge;
	runtimeApi_.PhysicsCapsuleCast = ScriptPhysicsCapsuleCastBridge;
	runtimeApi_.ApplyDamage = ScriptApplyDamageBridge;
	runtimeApi_.GetHealth = ScriptGetHealthBridge;
	runtimeApi_.SetHealth = ScriptSetHealthBridge;
	runtimeApi_.SpawnFromPool = ScriptSpawnFromPoolBridge;
	runtimeApi_.SpawnFromSpawner = ScriptSpawnFromSpawnerBridge;
	runtimeApi_.ReleaseToPool = ScriptReleaseToPoolBridge;
	runtimeApi_.FireHitscan = ScriptFireHitscanBridge;
	runtimeApi_.FireProjectile = ScriptFireProjectileBridge;
	runtimeApi_.PlayCameraBlend = ScriptPlayCameraBlendBridge;
	runtimeApi_.PlayCameraShake = ScriptPlayCameraShakeBridge;
	runtimeApi_.TriggerRailBranch = ScriptTriggerRailBranchBridge;
	runtimeApi_.LoadSceneAsync = ScriptLoadSceneAsyncBridge;
	runtimeApi_.UnloadScene = ScriptUnloadSceneBridge;
	runtimeApi_.GetSceneLoadProgress = ScriptGetSceneLoadProgressBridge;
	runtimeApi_.IsSceneLoading = ScriptIsSceneLoadingBridge;
	runtimeApi_.IsSceneLoaded = ScriptIsSceneLoadedBridge;
	runtimeApi_.SetSceneFloat = ScriptSetSceneFloatBridge;
	runtimeApi_.GetSceneFloat = ScriptGetSceneFloatBridge;
	runtimeApi_.SetSceneString = ScriptSetSceneStringBridge;
	runtimeApi_.GetSceneString = ScriptGetSceneStringBridge;
	runtimeApi_.PlayActionSequence = ScriptPlayActionSequenceBridge;
	runtimeApi_.PauseActionSequence = ScriptPauseActionSequenceBridge;
	runtimeApi_.StopActionSequence = ScriptStopActionSequenceBridge;
	runtimeApi_.SignalActionSequence = ScriptSignalActionSequenceBridge;
	runtimeApi_.IsActionSequencePlaying = ScriptIsActionSequencePlayingBridge;
	runtimeApi_.SaveSlot = ScriptSaveSlotBridge;
	runtimeApi_.LoadSlot = ScriptLoadSlotBridge;
	runtimeApi_.DeleteSlot = ScriptDeleteSlotBridge;
	runtimeApi_.HasSlot = ScriptHasSlotBridge;
	runtimeApi_.ActivateCheckpoint = ScriptActivateCheckpointBridge;
	runtimeApi_.SetSaveFloat = ScriptSetSaveFloatBridge;
	runtimeApi_.GetSaveFloat = ScriptGetSaveFloatBridge;
	runtimeApi_.SetSaveString = ScriptSetSaveStringBridge;
	runtimeApi_.GetSaveString = ScriptGetSaveStringBridge;
	runtimeApi_.SampleOceanSurface = ScriptSampleOceanSurfaceBridge;
	runtimeApi_.SetComponentActive = ScriptSetComponentActiveBridge;
	runtimeApi_.IsComponentActive = ScriptIsComponentActiveBridge;
	runtimeApi_.AddForceAtPosition = ScriptAddForceAtPositionBridge;
	runtimeApi_.AddExplosionImpulse = ScriptAddExplosionImpulseBridge;
	runtimeApi_.AttachRope = ScriptAttachRopeBridge;
	runtimeApi_.DetachRope = ScriptDetachRopeBridge;
	runtimeApi_.SetRopeLength = ScriptSetRopeLengthBridge;
	runtimeApi_.RepairRope = ScriptRepairRopeBridge;
	runtimeApi_.GetRopeState = ScriptGetRopeStateBridge;
	runtimeApi_.LoadoutSelectSlot = ScriptLoadoutSelectSlotBridge;
	runtimeApi_.LoadoutSelectNext = ScriptLoadoutSelectNextBridge;
	runtimeApi_.LoadoutSelectPrevious = ScriptLoadoutSelectPreviousBridge;
	runtimeApi_.LoadoutFire = ScriptLoadoutFireBridge;
	runtimeApi_.LoadoutReload = ScriptLoadoutReloadBridge;
	runtimeApi_.LoadoutGetAmmo = ScriptLoadoutGetAmmoBridge;
	runtimeApi_.GetCurrentTarget = ScriptGetCurrentTargetBridge;
	runtimeApi_.SetExplicitTarget = ScriptSetExplicitTargetBridge;
	runtimeApi_.SetRuntimeFloat = ScriptSetRuntimeFloatBridge;
	runtimeApi_.GetRuntimeFloat = ScriptGetRuntimeFloatBridge;
	runtimeApi_.SetRuntimeInt = ScriptSetRuntimeIntBridge;
	runtimeApi_.GetRuntimeInt = ScriptGetRuntimeIntBridge;
	runtimeApi_.SetRuntimeBool = ScriptSetRuntimeBoolBridge;
	runtimeApi_.GetRuntimeBool = ScriptGetRuntimeBoolBridge;
	runtimeApi_.SetRuntimeVector3 = ScriptSetRuntimeVector3Bridge;
	runtimeApi_.GetRuntimeVector3 = ScriptGetRuntimeVector3Bridge;
	runtimeApi_.PlayPropertyTween = ScriptPlayPropertyTweenBridge;
	runtimeApi_.StopPropertyTween = ScriptStopPropertyTweenBridge;
	runtimeApi_.IsPropertyTweenPlaying = ScriptIsPropertyTweenPlayingBridge;
	runtimeApi_.RelayAction = ScriptRelayActionBridge;
	runtimeApi_.HasComponent = ScriptHasComponentBridge;
	runtimeApi_.InvokeScriptAction = ScriptInvokeScriptActionBridge;
	runtimeApi_.SetRuntimeVector2 = ScriptSetRuntimeVector2Bridge;
	runtimeApi_.GetRuntimeVector2 = ScriptGetRuntimeVector2Bridge;
	runtimeApi_.GetRailState = ScriptGetRailStateBridge;
	runtimeApi_.SetRailDistance = ScriptSetRailDistanceBridge;
	runtimeApi_.GetRailClosestProgress = ScriptGetRailClosestProgressBridge;
	runtimeApi_.GetRailFrame = ScriptGetRailFrameBridge;
	runtimeApi_.ApplyDamageContext = ScriptApplyDamageContextBridge;
	runtimeApi_.GetLastDamageContext = ScriptGetLastDamageContextBridge;
	runtimeApi_.InvokeScriptActionPayload = ScriptInvokeScriptActionPayloadBridge;
	runtimeApi_.StartTimer = ScriptStartTimerBridge;
	runtimeApi_.PauseTimer = ScriptPauseTimerBridge;
	runtimeApi_.GetTimerRemaining = ScriptGetTimerRemainingBridge;
	runtimeApi_.ChangeGenericState = ScriptChangeGenericStateBridge;
	runtimeApi_.GetGenericState = ScriptGetGenericStateBridge;
	runtimeApi_.SetAttributeValue = ScriptSetAttributeValueBridge;
	runtimeApi_.GetAttributeValue = ScriptGetAttributeValueBridge;
	runtimeApi_.GetTargetLockState = ScriptGetTargetLockStateBridge;
	runtimeApi_.SetNamedAttributeValue = ScriptSetNamedAttributeValueBridge;
	runtimeApi_.GetNamedAttributeValue = ScriptGetNamedAttributeValueBridge;
	runtimeApi_.SetCounterValue = ScriptSetCounterValueBridge;
	runtimeApi_.AddCounterValue = ScriptAddCounterValueBridge;
	runtimeApi_.GetCounterValue = ScriptGetCounterValueBridge;
	runtimeApi_.EvaluateGenericCondition = ScriptEvaluateGenericConditionBridge;
	runtimeApi_.GetMultiTargetLockCount = ScriptGetMultiTargetLockCountBridge;
	runtimeApi_.GetMultiTargetLockTarget = ScriptGetMultiTargetLockTargetBridge;
	runtimeApi_.GetGameplayDataValue = ScriptGetGameplayDataValueBridge;
	runtimeApi_.HashDamageTag = ScriptHashDamageTagBridge;
	runtimeApi_.ApplyAreaDamage = ScriptApplyAreaDamageBridge;
	runtimeApi_.DetonateProjectile = ScriptDetonateProjectileBridge;
	runtimeApi_.GetThreatTrackerCount = ScriptGetThreatTrackerCountBridge;
	runtimeApi_.GetThreatTrackerEntry = ScriptGetThreatTrackerEntryBridge;
	runtimeApi_.StartNamedCooldown = ScriptStartNamedCooldownBridge;
	runtimeApi_.ResetNamedCooldown = ScriptResetNamedCooldownBridge;
	runtimeApi_.GetNamedCooldown = ScriptGetNamedCooldownBridge;
	runtimeApi_.ResetRuntimeState = ScriptResetRuntimeStateBridge;
	runtimeApi_.GetWeaponAccuracySpread = ScriptGetWeaponAccuracySpreadBridge;
	runtimeApi_.PlayTimeScale = ScriptPlayTimeScaleBridge;
	runtimeApi_.GetTimeScale = ScriptGetTimeScaleBridge;
	runtimeApi_.GetInterceptPrediction = ScriptGetInterceptPredictionBridge;
	runtimeApi_.SetObjective = ScriptSetObjectiveBridge;
	runtimeApi_.GetObjective = ScriptGetObjectiveBridge;
	runtimeApi_.StartEncounter = ScriptStartEncounterBridge;
	runtimeApi_.ResolveSpawnPoint = ScriptResolveSpawnPointBridge;
	runtimeApi_.ApplyDifficulty = ScriptApplyDifficultyBridge;
	runtimeApi_.GetDamageDirection = ScriptGetDamageDirectionBridge;
	runtimeApi_.GetBallisticPrediction = ScriptGetBallisticPredictionBridge;
	runtimeApi_.GetBallisticTrajectoryPoint = ScriptGetBallisticTrajectoryPointBridge;
	runtimeApi_.GetDamageEventBufferCount = ScriptGetDamageEventBufferCountBridge;
	runtimeApi_.GetDamageEventBufferEntry = ScriptGetDamageEventBufferEntryBridge;
	runtimeApi_.SetGamePaused = ScriptSetGamePausedBridge;
	runtimeApi_.IsGamePaused = ScriptIsGamePausedBridge;
	runtimeApi_.GetSurfaceWakeState = ScriptGetSurfaceWakeStateBridge;
	runtimeApi_.OceanSegmentCast = ScriptOceanSegmentCastBridge;
	runtimeApi_.OceanRaycast = ScriptOceanRaycastBridge;
	runtimeApi_.QueryOceanOcclusion = ScriptQueryOceanOcclusionBridge;
	runtimeApi_.GetWaterSurfaceState = ScriptGetWaterSurfaceStateBridge;
	runtimeApi_.GetOceanProbeSample = ScriptGetOceanProbeSampleBridge;
	runtimeApi_.LoadoutGetAmmoAtSlot = ScriptLoadoutGetAmmoAtSlotBridge;
	runtimeApi_.LoadoutAddMagazineAmmo = ScriptLoadoutAddMagazineAmmoBridge;
	runtimeApi_.LoadoutAddReserveAmmo = ScriptLoadoutAddReserveAmmoBridge;
	runtimeApi_.LoadoutSetMagazineAmmo = ScriptLoadoutSetMagazineAmmoBridge;
	runtimeApi_.LoadoutSetReserveAmmo = ScriptLoadoutSetReserveAmmoBridge;
	runtimeApi_.LoadoutSetMaximumAmmo = ScriptLoadoutSetMaximumAmmoBridge;
	runtimeApi_.LoadoutRefillMagazine = ScriptLoadoutRefillMagazineBridge;
	runtimeApi_.FireWeaponGroup = ScriptFireWeaponGroupBridge;
	runtimeApi_.IsWeaponGroupFiring = ScriptIsWeaponGroupFiringBridge;
	runtimeApi_.GetTurretAimState = ScriptGetTurretAimStateBridge;
	runtimeApi_.GetFireLineState = ScriptGetFireLineStateBridge;
	runtimeApi_.ApplyStatusEffect = ScriptApplyStatusEffectBridge;
	runtimeApi_.RemoveStatusEffect = ScriptRemoveStatusEffectBridge;
	runtimeApi_.ClearStatusEffects = ScriptClearStatusEffectsBridge;
	runtimeApi_.HasStatusEffect = ScriptHasStatusEffectBridge;
	runtimeApi_.GetStatusEffectCount = ScriptGetStatusEffectCountBridge;
	runtimeApi_.GetStatusEffectEntry = ScriptGetStatusEffectEntryBridge;
	runtimeApi_.SampleOceanSurfaceDetailed = ScriptSampleOceanSurfaceDetailedBridge;
	runtimeApi_.GetWaterSurfaceFoam = ScriptGetWaterSurfaceFoamBridge;
	runtimeApi_.GetOceanProbeFoam = ScriptGetOceanProbeFoamBridge;
	runtimeApi_.SetRailSpeedProfileEnabled = ScriptSetRailSpeedProfileEnabledBridge;
	runtimeApi_.GetRailSpeedMultiplier = ScriptGetRailSpeedMultiplierBridge;
	runtimeApi_.GetRailActiveZone = ScriptGetRailActiveZoneBridge;
	runtimeApi_.RearmRailEventMarkers = ScriptRearmRailEventMarkersBridge;
	runtimeApi_.GetSimulationLodLevel = ScriptGetSimulationLodLevelBridge;
	runtimeApi_.StartWaveSpawner = ScriptStartWaveSpawnerBridge;
	runtimeApi_.IsWaveSpawnerComplete = ScriptIsWaveSpawnerCompleteBridge;
	runtimeApi_.PhysicsRaycastIgnoringHierarchy = ScriptPhysicsRaycastIgnoringHierarchyBridge;
	runtimeApi_.PlayEffekseerAtPosition = ScriptPlayEffekseerAtPositionBridge;
	runtimeApi_.SetEffekseerEffectPosition = ScriptSetEffekseerEffectPositionBridge;
	runtimeApi_.StopEffekseerEffectAtPosition = ScriptStopEffekseerEffectAtPositionBridge;
	runtimeApi_.PlayVfxAtPosition = ScriptPlayVfxAtPositionBridge;
	runtimeApi_.CreateSpringJoint = ScriptCreateSpringJointBridge;
	runtimeApi_.DestroyJoint = ScriptDestroyJointBridge;
	runtimeApi_.SetSpringJointSettings = ScriptSetSpringJointSettingsBridge;
	runtimeApi_.IsJointValid = ScriptIsJointValidBridge;
	runtimeApi_.CreateJoint = ScriptCreateJointBridge;
	runtimeApi_.SetJointSettings = ScriptSetJointSettingsBridge;
	runtimeApi_.GetMouseDelta = ScriptGetMouseDeltaBridge;
	runtimeApi_.IsMouseButtonDown = ScriptIsMouseButtonDownBridge;
	runtimeApi_.WasMouseButtonPressed = ScriptWasMouseButtonPressedBridge;
	runtimeApi_.WasMouseButtonReleased = ScriptWasMouseButtonReleasedBridge;
	runtimeApi_.SetCursorLocked = ScriptSetCursorLockedBridge;
	runtimeApi_.IsCursorLocked = ScriptIsCursorLockedBridge;
	runtimeApi_.SetCursorVisible = ScriptSetCursorVisibleBridge;
	runtimeApi_.IsCursorVisible = ScriptIsCursorVisibleBridge;
	runtimeApi_.CreateWire = ScriptCreateWireBridge;
	runtimeApi_.DestroyWire = ScriptDestroyWireBridge;
	runtimeApi_.SetWireLengthByHandle = ScriptSetWireLengthByHandleBridge;
	runtimeApi_.SetWireShrinkSpeed = ScriptSetWireShrinkSpeedBridge;
	runtimeApi_.RepairWire = ScriptRepairWireBridge;
	runtimeApi_.GetWireStateByHandle = ScriptGetWireStateByHandleBridge;
	runtimeApi_.GetWireCountForGameObject = ScriptGetWireCountForGameObjectBridge;
	runtimeApi_.GetWireForGameObject = ScriptGetWireForGameObjectBridge;
	runtimeApi_.CanConnectWire = ScriptCanConnectWireBridge;
	runtimeApi_.AddComponent = ScriptAddComponentBridge;
	runtimeApi_.RemoveComponent = ScriptRemoveComponentBridge;
	runtimeApi_.FindGameObjectsWithComponent = ScriptFindGameObjectsWithComponentBridge;
	runtimeApi_.InstantiateGameObject = ScriptInstantiateGameObjectBridge;
	runtimeApi_.DestroyGameObject = ScriptDestroyGameObjectBridge;
	runtimeApi_.WorldToLocalPoint = ScriptWorldToLocalPointBridge;
	runtimeApi_.LocalToWorldPoint = ScriptLocalToWorldPointBridge;
	runtimeApi_.WorldToLocalDirection = ScriptWorldToLocalDirectionBridge;
	runtimeApi_.LocalToWorldDirection = ScriptLocalToWorldDirectionBridge;
	runtimeApi_.PhysicsRaycastFiltered = ScriptPhysicsRaycastFilteredBridge;
	runtimeApi_.SetRendererColor = ScriptSetRendererColorBridge;
	runtimeApi_.SetRendererEmission = ScriptSetRendererEmissionBridge;
	runtimeApi_.SetHookVisualState = ScriptSetHookVisualStateBridge;
	runtimeApi_.CreateGameObject = ScriptCreateGameObjectBridge;
	runtimeApi_.GetParentGameObject = ScriptGetParentGameObjectBridge;
	runtimeApi_.SetParentGameObject = ScriptSetParentGameObjectBridge;
	runtimeApi_.GetChildGameObjectCount = ScriptGetChildGameObjectCountBridge;
	runtimeApi_.GetChildGameObject = ScriptGetChildGameObjectBridge;
	runtimeApi_.ReloadPrimaryScene = ScriptReloadPrimarySceneBridge;
	runtimeApi_.GetMass = ScriptGetMassBridge;
	runtimeApi_.CaptureAreaState = ScriptCaptureAreaStateBridge;
	runtimeApi_.ResetArea = ScriptResetAreaBridge;
	runtimeApi_.HasAreaState = ScriptHasAreaStateBridge;
}

bool EditorScriptManager::ScriptAddComponentBridge(
	int32_t gameObjectId,
	const char* componentTypeName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->AddComponentInternal(gameObjectId, componentTypeName);
}

bool EditorScriptManager::ScriptRemoveComponentBridge(
	int32_t gameObjectId,
	const char* componentTypeName) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->RemoveComponentInternal(gameObjectId, componentTypeName);
}

int32_t EditorScriptManager::ScriptFindGameObjectsWithComponentBridge(
	const char* componentTypeName,
	int32_t* gameObjectIds,
	int32_t capacity) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->FindGameObjectsWithComponentInternal(
			componentTypeName,
			gameObjectIds,
			capacity)
		: 0;
}

int32_t EditorScriptManager::ScriptInstantiateGameObjectBridge(
	int32_t sourceGameObjectId,
	const EditorScriptVector3* position,
	const EditorScriptVector3* rotation) {
	if (gActiveScriptManager == nullptr || position == nullptr || rotation == nullptr) {
		return -1;
	}

	return gActiveScriptManager->InstantiateGameObjectInternal(
		sourceGameObjectId,
		*position,
		*rotation);
}

bool EditorScriptManager::ScriptDestroyGameObjectBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->DestroyGameObjectInternal(gameObjectId);
}

bool EditorScriptManager::ScriptSetRendererColorBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* color) {
	return gActiveScriptManager != nullptr && color != nullptr &&
		gActiveScriptManager->SetRendererColorInternal(gameObjectId, *color);
}

bool EditorScriptManager::ScriptSetRendererEmissionBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* color,
	float strength) {
	return gActiveScriptManager != nullptr && color != nullptr &&
		gActiveScriptManager->SetRendererEmissionInternal(gameObjectId, *color, strength);
}

bool EditorScriptManager::ScriptSetHookVisualStateBridge(
	int32_t gameObjectId,
	int32_t visualState) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetHookVisualStateInternal(gameObjectId, visualState);
}

int32_t EditorScriptManager::ScriptCreateGameObjectBridge(const char* name) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->CreateGameObjectInternal(name)
		: -1;
}

int32_t EditorScriptManager::ScriptGetParentGameObjectBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->GetParentGameObjectInternal(gameObjectId)
		: -1;
}

bool EditorScriptManager::ScriptSetParentGameObjectBridge(
	int32_t childGameObjectId,
	int32_t parentGameObjectId,
	bool preserveWorldTransform) {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->SetParentGameObjectInternal(
			childGameObjectId,
			parentGameObjectId,
			preserveWorldTransform);
}

int32_t EditorScriptManager::ScriptGetChildGameObjectCountBridge(int32_t gameObjectId) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->GetChildGameObjectCountInternal(gameObjectId)
		: 0;
}

int32_t EditorScriptManager::ScriptGetChildGameObjectBridge(
	int32_t gameObjectId,
	int32_t childIndex) {
	return gActiveScriptManager != nullptr
		? gActiveScriptManager->GetChildGameObjectInternal(gameObjectId, childIndex)
		: -1;
}

bool EditorScriptManager::ScriptReloadPrimarySceneBridge() {
	return gActiveScriptManager != nullptr &&
		gActiveScriptManager->ReloadPrimarySceneInternal();
}

bool EditorScriptManager::ScriptWorldToLocalPointBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* worldPoint,
	EditorScriptVector3* localPoint) {
	return gActiveScriptManager != nullptr && worldPoint != nullptr && localPoint != nullptr &&
		gActiveScriptManager->WorldToLocalPointInternal(gameObjectId, *worldPoint, *localPoint);
}

bool EditorScriptManager::ScriptLocalToWorldPointBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* localPoint,
	EditorScriptVector3* worldPoint) {
	return gActiveScriptManager != nullptr && localPoint != nullptr && worldPoint != nullptr &&
		gActiveScriptManager->LocalToWorldPointInternal(gameObjectId, *localPoint, *worldPoint);
}

bool EditorScriptManager::ScriptWorldToLocalDirectionBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* worldDirection,
	EditorScriptVector3* localDirection) {
	return gActiveScriptManager != nullptr && worldDirection != nullptr && localDirection != nullptr &&
		gActiveScriptManager->WorldToLocalDirectionInternal(
			gameObjectId,
			*worldDirection,
			*localDirection);
}

bool EditorScriptManager::ScriptLocalToWorldDirectionBridge(
	int32_t gameObjectId,
	const EditorScriptVector3* localDirection,
	EditorScriptVector3* worldDirection) {
	return gActiveScriptManager != nullptr && localDirection != nullptr && worldDirection != nullptr &&
		gActiveScriptManager->LocalToWorldDirectionInternal(
			gameObjectId,
			*localDirection,
			*worldDirection);
}

bool EditorScriptManager::ScriptPhysicsRaycastFilteredBridge(
	const EditorScriptRay* ray,
	float distance,
	uint32_t physicsLayerMask,
	bool includeTriggers,
	const char* requiredComponentTypeName,
	EditorScriptPhysicsHit* hit) {
	return gActiveScriptManager != nullptr && ray != nullptr && hit != nullptr &&
		gActiveScriptManager->PhysicsRaycastFilteredInternal(
			*ray,
			distance,
			physicsLayerMask,
			includeTriggers,
			requiredComponentTypeName,
			*hit);
}

void EditorScriptManager::StartBindingsForModule(ScriptModule& scriptModule) {
	if (!scriptModule.isLoaded) {
		return;
	}

	for (ScriptBinding& scriptBinding : scriptBindings_) {
		if (scriptBinding.dllPath == scriptModule.sourceDllPath) {
			StartBindingIfNeeded(scriptBinding, scriptModule);
		}
	}
}

void EditorScriptManager::StartBindingIfNeeded(
	ScriptBinding& scriptBinding,
	ScriptModule& scriptModule) {
	if (!scriptModule.isLoaded || scriptBinding.hasStarted ||
		!IsScriptBindingActive(scriptBinding)) {
		return;
	}

	if (UsesInstanceApi(scriptModule) && scriptBinding.instance == nullptr) {
		EditorProfilerManager::Scope profilerScope(
			profilerManager_,
			"CreateInstance",
			scriptBinding.dllPath,
			scriptBinding.gameObjectId);
		scriptBinding.instance = scriptModule.createInstanceFunction(scriptBinding.gameObjectId);

		if (scriptBinding.instance == nullptr) {
			PushConsoleMessage(
				"C++ Script インスタンス生成失敗: GameObject=" +
				std::to_string(scriptBinding.gameObjectId) + " DLL=" + scriptBinding.dllPath);
			return;
		}
	}

	std::vector<EditorScriptFieldDescriptor> fieldDescriptors;
	if (scriptModule.getFieldCountFunction != nullptr && scriptModule.getFieldDescriptorFunction != nullptr) {
		const int32_t fieldCount = (std::clamp)(scriptModule.getFieldCountFunction(), 0, 512);
		fieldDescriptors.reserve(static_cast<size_t>(fieldCount));

		for (int32_t fieldIndex = 0; fieldIndex < fieldCount; fieldIndex++) {
			EditorScriptFieldDescriptor fieldDescriptor{};
			if (scriptModule.getFieldDescriptorFunction(fieldIndex, &fieldDescriptor)) {
				fieldDescriptors.push_back(fieldDescriptor);
			}
		}
	}

	EditorComponent* scriptComponent = FindScriptComponent(scriptBinding);
	if (scriptComponent != nullptr) {
		SynchronizeComponentProperties(*scriptComponent, fieldDescriptors);
		ApplyComponentFieldsToInstance(scriptBinding, scriptModule);
		scriptBinding.synchronizedFieldHash = HashScriptProperties(scriptComponent->scriptProperties);
		scriptBinding.hasSynchronizedFieldHash = true;
	}

	if (UsesInstanceApi(scriptModule) && scriptBinding.instance != nullptr &&
		scriptModule.startInstanceFunction != nullptr) {
		EditorProfilerManager::Scope profilerScope(
			profilerManager_,
			"Start",
			scriptBinding.dllPath,
			scriptBinding.gameObjectId);
		scriptModule.startInstanceFunction(scriptBinding.instance);
	}
	else if (!UsesInstanceApi(scriptModule) && scriptModule.startFunction != nullptr) {
		EditorProfilerManager::Scope profilerScope(
			profilerManager_,
			"Start",
			scriptBinding.dllPath,
			scriptBinding.gameObjectId);
		scriptModule.startFunction(scriptBinding.gameObjectId);
	}

	scriptBinding.hasStarted = true;
	scriptBinding.updateIntervalRemaining = 0.0f;
	scriptBinding.accumulatedUpdateDeltaTime = 0.0f;
}

void EditorScriptManager::StopBindingsForModule(ScriptModule& scriptModule) {
	if (!scriptModule.isLoaded) {
		return;
	}

	for (ScriptBinding& scriptBinding : scriptBindings_) {
		if (scriptBinding.dllPath != scriptModule.sourceDllPath || !scriptBinding.hasStarted) {
			continue;
		}

		if (UsesInstanceApi(scriptModule) && scriptBinding.instance != nullptr &&
			scriptModule.stopInstanceFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"Stop",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule.stopInstanceFunction(scriptBinding.instance);
		}
		else if (!UsesInstanceApi(scriptModule) && scriptModule.stopFunction != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"Stop",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule.stopFunction(scriptBinding.gameObjectId);
		}

		if (UsesInstanceApi(scriptModule) && scriptBinding.instance != nullptr) {
			EditorProfilerManager::Scope profilerScope(
				profilerManager_,
				"DestroyInstance",
				scriptBinding.dllPath,
				scriptBinding.gameObjectId);
			scriptModule.destroyInstanceFunction(scriptBinding.instance);
			scriptBinding.instance = nullptr;
		}

		scriptBinding.hasStarted = false;
		scriptBinding.updateIntervalRemaining = 0.0f;
		scriptBinding.accumulatedUpdateDeltaTime = 0.0f;
	}
}

bool EditorScriptManager::UsesInstanceApi(const ScriptModule& scriptModule) const {
	return scriptModule.createInstanceFunction != nullptr &&
		scriptModule.destroyInstanceFunction != nullptr;
}

bool EditorScriptManager::InvokeBindingAction(
	const ScriptBinding& scriptBinding,
	ScriptModule& scriptModule,
	const char* functionName,
	const EditorScriptInputActionContext& inputContext) {
	if (functionName == nullptr) {
		return false;
	}

	if (UsesInstanceApi(scriptModule)) {
		if (scriptBinding.instance == nullptr || scriptModule.invokeActionInstanceFunction == nullptr) {
			return false;
		}

		EditorProfilerManager::Scope profilerScope(
			profilerManager_,
			functionName,
			scriptBinding.dllPath,
			scriptBinding.gameObjectId);
		return scriptModule.invokeActionInstanceFunction(
			scriptBinding.instance,
			functionName,
			&inputContext);
	}

	if (scriptModule.invokeActionFunction == nullptr) {
		return false;
	}

	EditorProfilerManager::Scope profilerScope(
		profilerManager_,
		functionName,
		scriptBinding.dllPath,
		scriptBinding.gameObjectId);
	return scriptModule.invokeActionFunction(
		scriptBinding.gameObjectId,
		functionName,
		&inputContext);
}

void EditorScriptManager::DispatchQueuedUiEvents() {
	if (editorScene_ == nullptr || queuedUiEvents_.empty()) {
		return;
	}

	std::vector<QueuedUiEvent> uiEvents;
	uiEvents.swap(queuedUiEvents_);  // Script 側から再度イベントが積まれても、次フレームへ回す。

	for (const QueuedUiEvent& uiEvent : uiEvents) {
		if (uiEvent.gameObjectId < 0 || uiEvent.functionName.empty()) {
			continue;
		}

		EditorScriptInputActionContext inputContext{};
		inputContext.gameObjectId = uiEvent.gameObjectId;
		inputContext.phase = EditorScriptInputPhasePerformed;
		inputContext.valueType = uiEvent.valueType;
		inputContext.buttonValue = uiEvent.buttonValue;
		inputContext.vector2Value = uiEvent.vector2Value;
		inputContext.payloadType = uiEvent.payload.type;
		inputContext.payloadGameObjectId = uiEvent.payload.gameObjectId;
		inputContext.payloadInt = uiEvent.payload.intValue;
		inputContext.payloadFloat = uiEvent.payload.floatValue;
		inputContext.payloadBool = uiEvent.payload.boolValue;
		inputContext.payloadVector3 = uiEvent.payload.vector3Value;
		CopyStringToFixedBuffer(uiEvent.payload.stringValue, inputContext.payloadString, sizeof(inputContext.payloadString));
		const char* actionMapName = uiEvent.isUiEvent ? "UI" : "Event";
		const char* actionName = uiEvent.isUiEvent ? "Button" : "Trigger";
		const char* bindingPath = uiEvent.isUiEvent ? "UI/Button" : "Event/Trigger";
		CopyStringToFixedBuffer(actionMapName, inputContext.actionMapName, sizeof(inputContext.actionMapName));
		CopyStringToFixedBuffer(actionName, inputContext.actionName, sizeof(inputContext.actionName));
		CopyStringToFixedBuffer(bindingPath, inputContext.bindingPath, sizeof(inputContext.bindingPath));

		bool hasScriptCandidate = false;
		bool wasInvoked = false;

		const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(uiEvent.gameObjectId);

		if (bindingIndicesIterator != scriptBindingIndicesByGameObjectId_.end()) {
			for (const size_t bindingIndex : bindingIndicesIterator->second) {
				if (bindingIndex >= scriptBindings_.size()) {
					continue;
				}

				const ScriptBinding& scriptBinding = scriptBindings_[bindingIndex];

				if (!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
					continue;
				}

				ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
				if (scriptModule == nullptr || !scriptModule->isLoaded) {
					continue;
				}

				hasScriptCandidate = true;
				wasInvoked = InvokeBindingAction(
					scriptBinding,
					*scriptModule,
					uiEvent.functionName.c_str(),
					inputContext);

				if (wasInvoked) {
					break;
				}
			}
		}

		if (hasScriptCandidate && !wasInvoked) {
			const std::string warningKey =
				std::string(uiEvent.isUiEvent ? "UI|" : "Event|") +
				std::to_string(uiEvent.gameObjectId) + "|" + uiEvent.functionName;

			if (missingActionWarnings_.insert(warningKey).second) {
				const EditorGameObject* gameObject = editorScene_->FindGameObject(uiEvent.gameObjectId);
				const std::string gameObjectName = gameObject != nullptr ? gameObject->name : "Unknown";
				PushConsoleMessage(
					std::string(uiEvent.isUiEvent ? "Button関数" : "Event Action") +
					"が未登録です: " + uiEvent.functionName + " (GameObject=" + gameObjectName + ")");
			}
		}
	}
}

void EditorScriptManager::DispatchInputActions() {
	if (editorScene_ == nullptr || inputManager_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* playerInput =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::PlayerInput);
		if (playerInput == nullptr || !playerInput->isActive) {
			continue;
		}

		const auto bindingIndicesIterator = scriptBindingIndicesByGameObjectId_.find(gameObject.id);

		for (const EditorInputEventBinding& eventBinding : playerInput->inputEventBindings) {
			if (eventBinding.actionMapName.empty() ||
				eventBinding.actionName.empty() ||
				eventBinding.functionName.empty()) {
				continue;
			}

			EditorScriptInputActionContext inputContext{};
			inputContext.gameObjectId = gameObject.id;
			inputContext.valueType = eventBinding.valueType;
			CopyStringToFixedBuffer(eventBinding.actionMapName, inputContext.actionMapName, sizeof(inputContext.actionMapName));
			CopyStringToFixedBuffer(eventBinding.actionName, inputContext.actionName, sizeof(inputContext.actionName));
			CopyStringToFixedBuffer(
				inputManager_->GetActionBindingPath(gameObject.id, eventBinding.actionMapName, eventBinding.actionName),
				inputContext.bindingPath,
				sizeof(inputContext.bindingPath));

			auto invokeAction = [&](int32_t inputPhase) {
				inputContext.phase = inputPhase;
				bool hasScriptCandidate = false;
				bool wasInvoked = false;

				if (bindingIndicesIterator != scriptBindingIndicesByGameObjectId_.end()) {
					for (const size_t bindingIndex : bindingIndicesIterator->second) {
						if (bindingIndex >= scriptBindings_.size()) {
							continue;
						}

						const ScriptBinding& scriptBinding = scriptBindings_[bindingIndex];

						if (!scriptBinding.hasStarted || !IsScriptBindingActive(scriptBinding)) {
							continue;
						}

						ScriptModule* scriptModule = FindModule(scriptBinding.dllPath);
						if (scriptModule == nullptr || !scriptModule->isLoaded) {
							continue;
						}

						hasScriptCandidate = true;
						wasInvoked = InvokeBindingAction(
							scriptBinding,
							*scriptModule,
							eventBinding.functionName.c_str(),
							inputContext);

						if (wasInvoked) {
							break;  // 1 つの Event 欄は、登録関数を持つ 1 つの C++ Component だけを呼ぶ。
						}
					}
				}

				if (hasScriptCandidate && !wasInvoked) {
					const std::string warningKey =
						std::to_string(gameObject.id) + "|" + eventBinding.functionName;

					if (missingActionWarnings_.insert(warningKey).second) {
						PushConsoleMessage(
							"Input関数が未登録です: " + eventBinding.functionName + " (GameObject=" + gameObject.name + ")");
					}
				}
			};

			if (eventBinding.valueType == EditorScriptInputValueTypeVector2) {
				float inputX = 0.0f;
				float inputY = 0.0f;
				inputManager_->TryGetActionVector2(
					gameObject.id,
					eventBinding.actionMapName,
					eventBinding.actionName,
					inputX,
					inputY);
				inputContext.vector2Value = {inputX, inputY};
				const bool isActive = std::fabs(inputX) > 0.0001f || std::fabs(inputY) > 0.0001f;
				const std::string actionStateKey =
					MakeInputActionStateKey(gameObject.id, eventBinding.actionMapName, eventBinding.actionName);
				const bool wasActive = inputActionActiveStates_[actionStateKey];

				if (isActive && !wasActive) {
					invokeAction(EditorScriptInputPhaseStarted);
				}

				if (isActive) {
					invokeAction(EditorScriptInputPhasePerformed);
				}

				if (!isActive && wasActive) {
					invokeAction(EditorScriptInputPhaseCanceled);
				}

				inputActionActiveStates_[actionStateKey] = isActive;
				continue;
			}

			inputContext.buttonValue = inputManager_->IsActionPressed(
				gameObject.id,
				eventBinding.actionMapName,
				eventBinding.actionName)
				? 1.0f
				: 0.0f;

			if (inputManager_->WasActionJustPressed(gameObject.id, eventBinding.actionMapName, eventBinding.actionName)) {
				invokeAction(EditorScriptInputPhaseStarted);
				invokeAction(EditorScriptInputPhasePerformed);
			}

			if (inputManager_->WasActionJustReleased(gameObject.id, eventBinding.actionMapName, eventBinding.actionName)) {
				invokeAction(EditorScriptInputPhaseCanceled);
			}
		}
	}
}

void EditorScriptManager::ApplyComponentFieldsToInstance(
	const ScriptBinding& scriptBinding,
	ScriptModule& scriptModule) {
	const bool usesInstanceApi = UsesInstanceApi(scriptModule);
	const bool canSetInstanceField =
		usesInstanceApi && scriptBinding.instance != nullptr &&
		scriptModule.setFieldValueInstanceFunction != nullptr;
	const bool canSetLegacyField =
		!usesInstanceApi && scriptModule.setFieldValueFunction != nullptr;

	if (!canSetInstanceField && !canSetLegacyField) {
		return;
	}

	EditorComponent* scriptComponent = FindScriptComponent(scriptBinding);
	if (scriptComponent == nullptr) {
		return;
	}

	for (const EditorScriptProperty& scriptProperty : scriptComponent->scriptProperties) {
		const EditorScriptFieldValue fieldValue = MakeScriptFieldValue(scriptProperty);

		if (canSetInstanceField) {
			scriptModule.setFieldValueInstanceFunction(
				scriptBinding.instance,
				scriptProperty.name.c_str(),
				&fieldValue);
		}
		else {
			scriptModule.setFieldValueFunction(
				scriptBinding.gameObjectId,
				scriptProperty.name.c_str(),
				&fieldValue);
		}
	}
}

void EditorScriptManager::ReadInstanceFieldsToComponent(
	const ScriptBinding& scriptBinding,
	ScriptModule& scriptModule) {
	const bool usesInstanceApi = UsesInstanceApi(scriptModule);
	const bool canGetInstanceField =
		usesInstanceApi && scriptBinding.instance != nullptr &&
		scriptModule.getFieldValueInstanceFunction != nullptr;
	const bool canGetLegacyField =
		!usesInstanceApi && scriptModule.getFieldValueFunction != nullptr;

	if (!canGetInstanceField && !canGetLegacyField) {
		return;
	}

	EditorComponent* scriptComponent = FindScriptComponent(scriptBinding);
	if (scriptComponent == nullptr) {
		return;
	}

	for (EditorScriptProperty& scriptProperty : scriptComponent->scriptProperties) {
		EditorScriptFieldValue fieldValue{};
		const bool wasRead = canGetInstanceField
			? scriptModule.getFieldValueInstanceFunction(
				scriptBinding.instance,
				scriptProperty.name.c_str(),
				&fieldValue)
			: scriptModule.getFieldValueFunction(
				scriptBinding.gameObjectId,
				scriptProperty.name.c_str(),
				&fieldValue);

		if (!wasRead) {
			continue;
		}

		ApplyScriptFieldValue(scriptProperty, fieldValue);
	}
}

void EditorScriptManager::SynchronizeComponentProperties(
	EditorComponent& scriptComponent,
	const std::vector<EditorScriptFieldDescriptor>& fieldDescriptors) {
	std::vector<EditorScriptProperty> synchronizedProperties;
	synchronizedProperties.reserve(fieldDescriptors.size());

	for (const EditorScriptFieldDescriptor& fieldDescriptor : fieldDescriptors) {
		EditorScriptProperty synchronizedProperty = MakeScriptProperty(fieldDescriptor);
		const auto existingPropertyIt = std::find_if(
			scriptComponent.scriptProperties.begin(),
			scriptComponent.scriptProperties.end(),
			[&fieldDescriptor](const EditorScriptProperty& scriptProperty) {
				return scriptProperty.name == fieldDescriptor.name &&
					scriptProperty.type == fieldDescriptor.defaultValue.type;
			});

		if (existingPropertyIt != scriptComponent.scriptProperties.end()) {
			const std::string displayName = synchronizedProperty.displayName;
			const float minValue = synchronizedProperty.minValue;
			const float maxValue = synchronizedProperty.maxValue;
			const float step = synchronizedProperty.step;
			const bool hasRange = synchronizedProperty.hasRange;
			synchronizedProperty = *existingPropertyIt;
			synchronizedProperty.displayName = displayName;
			synchronizedProperty.minValue = minValue;
			synchronizedProperty.maxValue = maxValue;
			synchronizedProperty.step = step;
			synchronizedProperty.hasRange = hasRange;
		}

		synchronizedProperties.push_back(synchronizedProperty);
	}

	scriptComponent.scriptProperties = synchronizedProperties;
}

EditorComponent* EditorScriptManager::FindScriptComponent(const ScriptBinding& scriptBinding) {
	if (editorScene_ == nullptr) {
		return nullptr;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(scriptBinding.gameObjectId);
	if (gameObject == nullptr || scriptBinding.componentIndex >= gameObject->components.size()) {
		return nullptr;
	}

	EditorComponent& component = gameObject->components[scriptBinding.componentIndex];
	if (component.type == scriptBinding.componentType && component.assetPath == scriptBinding.dllPath) {
		return &component;
	}

	return nullptr;
}

bool EditorScriptManager::IsScriptBindingActive(const ScriptBinding& scriptBinding) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(scriptBinding.gameObjectId);

	if (gameObject == nullptr || !gameObject->isActive ||
		scriptBinding.componentIndex >= gameObject->components.size()) {
		return false;
	}

	const EditorComponent& component = gameObject->components[scriptBinding.componentIndex];
	if (component.type == scriptBinding.componentType && component.assetPath == scriptBinding.dllPath) {
		return component.isActive;
	}

	return false;
}

bool EditorScriptManager::ReadMetadataFromDll(const std::string& dllPath, ScriptMetadata& scriptMetadata) {
	const std::filesystem::path sourcePath = std::filesystem::absolute(dllPath);
	std::error_code fileError;
	scriptMetadata = {};
	scriptMetadata.lastWriteTime = std::filesystem::last_write_time(sourcePath, fileError);
	if (fileError) {
		return false;
	}

	const std::filesystem::path cacheDirectory = std::filesystem::path("runtime_cache") / "scripts" / "metadata";
	std::filesystem::create_directories(cacheDirectory, fileError);
	if (fileError) {
		return false;
	}

	reloadGeneration_++;
	const std::filesystem::path copiedDllPath =
		cacheDirectory /
		(sourcePath.stem().generic_string() + "_metadata_" + std::to_string(reloadGeneration_) + ".dll");
	std::filesystem::copy_file(
		sourcePath,
		copiedDllPath,
		std::filesystem::copy_options::overwrite_existing,
		fileError);
	if (fileError) {
		return false;
	}

	const std::wstring copiedDllPathWide = ConvertString(copiedDllPath.generic_string());
	HMODULE moduleHandle = LoadLibraryW(copiedDllPathWide.c_str());
	if (moduleHandle == nullptr) {
		std::filesystem::remove(copiedDllPath, fileError);
		return false;
	}

#pragma warning(push)
#pragma warning(disable : 4191)
	const EditorScriptGetFieldCountFn getFieldCountFunction =
		reinterpret_cast<EditorScriptGetFieldCountFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldCount"));
	const EditorScriptGetFieldDescriptorFn getFieldDescriptorFunction =
		reinterpret_cast<EditorScriptGetFieldDescriptorFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldDescriptor"));
	const EditorScriptGetActionCountFn getActionCountFunction =
		reinterpret_cast<EditorScriptGetActionCountFn>(GetProcAddress(moduleHandle, "EditorScript_GetActionCount"));
	const EditorScriptGetActionNameFn getActionNameFunction =
		reinterpret_cast<EditorScriptGetActionNameFn>(GetProcAddress(moduleHandle, "EditorScript_GetActionName"));
#pragma warning(pop)

	if (getFieldCountFunction != nullptr && getFieldDescriptorFunction != nullptr) {
		const int32_t fieldCount = (std::clamp)(getFieldCountFunction(), 0, 512);
		scriptMetadata.fieldDescriptors.reserve(static_cast<size_t>(fieldCount));

		for (int32_t fieldIndex = 0; fieldIndex < fieldCount; fieldIndex++) {
			EditorScriptFieldDescriptor fieldDescriptor{};
			if (getFieldDescriptorFunction(fieldIndex, &fieldDescriptor)) {
				scriptMetadata.fieldDescriptors.push_back(fieldDescriptor);
			}
		}
	}

	ReadRegisteredActionNames(
		getActionCountFunction,
		getActionNameFunction,
		scriptMetadata.actionNames);

	FreeLibrary(moduleHandle);
	std::filesystem::remove(copiedDllPath, fileError);
	scriptMetadata.isValid = true;
	return true;
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
	scriptModule.wireEventFunction =
		reinterpret_cast<EditorScriptWireEventFn>(GetProcAddress(moduleHandle, "EditorScript_OnWireEvent"));
	scriptModule.animationEventFunction =
		reinterpret_cast<EditorScriptAnimationEventFn>(GetProcAddress(moduleHandle, "EditorScript_OnAnimationEvent"));
	scriptModule.stopFunction =
		reinterpret_cast<EditorScriptStopFn>(GetProcAddress(moduleHandle, "EditorScript_Stop"));
	scriptModule.getFieldCountFunction =
		reinterpret_cast<EditorScriptGetFieldCountFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldCount"));
	scriptModule.getFieldDescriptorFunction =
		reinterpret_cast<EditorScriptGetFieldDescriptorFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldDescriptor"));
	scriptModule.getFieldValueFunction =
		reinterpret_cast<EditorScriptGetFieldValueFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldValue"));
	scriptModule.setFieldValueFunction =
		reinterpret_cast<EditorScriptSetFieldValueFn>(GetProcAddress(moduleHandle, "EditorScript_SetFieldValue"));
	scriptModule.invokeActionFunction =
		reinterpret_cast<EditorScriptInvokeActionFn>(GetProcAddress(moduleHandle, "EditorScript_InvokeAction"));
	scriptModule.getActionCountFunction =
		reinterpret_cast<EditorScriptGetActionCountFn>(GetProcAddress(moduleHandle, "EditorScript_GetActionCount"));
	scriptModule.getActionNameFunction =
		reinterpret_cast<EditorScriptGetActionNameFn>(GetProcAddress(moduleHandle, "EditorScript_GetActionName"));
	scriptModule.createInstanceFunction =
		reinterpret_cast<EditorScriptCreateInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_CreateInstance"));
	scriptModule.destroyInstanceFunction =
		reinterpret_cast<EditorScriptDestroyInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_DestroyInstance"));
	scriptModule.startInstanceFunction =
		reinterpret_cast<EditorScriptStartInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_StartInstance"));
	scriptModule.updateInstanceFunction =
		reinterpret_cast<EditorScriptUpdateInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_UpdateInstance"));
	scriptModule.fixedUpdateInstanceFunction =
		reinterpret_cast<EditorScriptFixedUpdateInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_FixedUpdateInstance"));
	scriptModule.physicsEventInstanceFunction =
		reinterpret_cast<EditorScriptPhysicsEventInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_OnPhysicsEventInstance"));
	scriptModule.wireEventInstanceFunction =
		reinterpret_cast<EditorScriptWireEventInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_OnWireEventInstance"));
	scriptModule.animationEventInstanceFunction =
		reinterpret_cast<EditorScriptAnimationEventInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_OnAnimationEventInstance"));
	scriptModule.stopInstanceFunction =
		reinterpret_cast<EditorScriptStopInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_StopInstance"));
	scriptModule.getFieldValueInstanceFunction =
		reinterpret_cast<EditorScriptGetFieldValueInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_GetFieldValueInstance"));
	scriptModule.setFieldValueInstanceFunction =
		reinterpret_cast<EditorScriptSetFieldValueInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_SetFieldValueInstance"));
	scriptModule.invokeActionInstanceFunction =
		reinterpret_cast<EditorScriptInvokeActionInstanceFn>(GetProcAddress(moduleHandle, "EditorScript_InvokeActionInstance"));
#pragma warning(pop)

	const bool hasCreateInstance = scriptModule.createInstanceFunction != nullptr;
	const bool hasDestroyInstance = scriptModule.destroyInstanceFunction != nullptr;

	if (hasCreateInstance != hasDestroyInstance) {
		moduleStatusMessages_[dllPath] = "DLL Instance API 不完全: Create / Destroy の両方が必要";
		PushConsoleMessage("DLL Instance API 不完全: " + dllPath);
		UnloadModule(scriptModule);
		return false;
	}

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
	scriptModule.wireEventFunction = nullptr;
	scriptModule.animationEventFunction = nullptr;
	scriptModule.stopFunction = nullptr;
	scriptModule.getFieldCountFunction = nullptr;
	scriptModule.getFieldDescriptorFunction = nullptr;
	scriptModule.getFieldValueFunction = nullptr;
	scriptModule.setFieldValueFunction = nullptr;
	scriptModule.invokeActionFunction = nullptr;
	scriptModule.getActionCountFunction = nullptr;
	scriptModule.getActionNameFunction = nullptr;
	scriptModule.createInstanceFunction = nullptr;
	scriptModule.destroyInstanceFunction = nullptr;
	scriptModule.startInstanceFunction = nullptr;
	scriptModule.updateInstanceFunction = nullptr;
	scriptModule.fixedUpdateInstanceFunction = nullptr;
	scriptModule.physicsEventInstanceFunction = nullptr;
	scriptModule.wireEventInstanceFunction = nullptr;
	scriptModule.animationEventInstanceFunction = nullptr;
	scriptModule.stopInstanceFunction = nullptr;
	scriptModule.getFieldValueInstanceFunction = nullptr;
	scriptModule.setFieldValueInstanceFunction = nullptr;
	scriptModule.invokeActionInstanceFunction = nullptr;
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
	scriptPhysicsEvent.contactImpulse = physicsEvent.collision.contactImpulse;
	scriptPhysicsEvent.selfMass = physicsEvent.collision.selfMass;
	scriptPhysicsEvent.otherMass = physicsEvent.collision.otherMass;
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

EditorScriptVector2 EditorScriptManager::GetMouseDeltaInternal() const {
	return {
		static_cast<float>(EditorSharedState::g_mouseState.lX),
		static_cast<float>(EditorSharedState::g_mouseState.lY)};
}

bool EditorScriptManager::IsMouseButtonDownInternal(int32_t mouseButton) const {
	if (mouseButton < 0 || mouseButton >= 4) {
		return false;
	}

	return (EditorSharedState::g_mouseState.rgbButtons[mouseButton] & 0x80u) != 0u;
}

bool EditorScriptManager::WasMouseButtonPressedInternal(int32_t mouseButton) const {
	if (mouseButton < 0 || mouseButton >= 4) {
		return false;
	}

	const bool isPressed = (EditorSharedState::g_mouseState.rgbButtons[mouseButton] & 0x80u) != 0u;
	const bool wasPressed = (EditorSharedState::g_preMouseState.rgbButtons[mouseButton] & 0x80u) != 0u;
	return isPressed && !wasPressed;
}

bool EditorScriptManager::WasMouseButtonReleasedInternal(int32_t mouseButton) const {
	if (mouseButton < 0 || mouseButton >= 4) {
		return false;
	}

	const bool isPressed = (EditorSharedState::g_mouseState.rgbButtons[mouseButton] & 0x80u) != 0u;
	const bool wasPressed = (EditorSharedState::g_preMouseState.rgbButtons[mouseButton] & 0x80u) != 0u;
	return !isPressed && wasPressed;
}

void EditorScriptManager::SetCursorLockedInternal(bool isLocked) {
	EditorSharedState::ApplyRuntimeCursorLock(isLocked);
}

void EditorScriptManager::SetCursorVisibleInternal(bool isVisible) {
	EditorSharedState::ApplyRuntimeCursorVisibility(isVisible);
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

float EditorScriptManager::GetMassInternal(int32_t gameObjectId) const {
	if (editorScene_ == nullptr) {
		return 0.0f;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return 0.0f;
	}

	const EditorComponent* rigidBodyComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
	if (rigidBodyComponent == nullptr) {
		return 0.0f;
	}

	return rigidBodyComponent->mass;
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

bool EditorScriptManager::AddForceAtPositionInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& force,
	const EditorScriptVector3& worldPosition) {
	if (physicsManager_ == nullptr) {
		return false;
	}

	return physicsManager_->AddForceAtPosition(
		gameObjectId,
		ToEditorVector3(force),
		ToEditorVector3(worldPosition));
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

int32_t EditorScriptManager::AddExplosionImpulseInternal(
	const EditorScriptVector3& center,
	float radius,
	float impulseStrength,
	float upwardModifier) {
	if (physicsManager_ == nullptr) {
		return 0;
	}

	return physicsManager_->AddExplosionImpulse(
		ToEditorVector3(center),
		radius,
		impulseStrength,
		upwardModifier);
}

bool EditorScriptManager::AttachRopeInternal(
	int32_t ownerGameObjectId,
	int32_t targetGameObjectId,
	const EditorScriptVector3& ownerLocalAnchor,
	const EditorScriptVector3& targetAnchor,
	float maximumLength) {
	return physicsManager_ != nullptr && physicsManager_->AttachRope(
		ownerGameObjectId,
		targetGameObjectId,
		ToEditorVector3(ownerLocalAnchor),
		ToEditorVector3(targetAnchor),
		maximumLength);
}

EditorScriptJointHandle EditorScriptManager::CreateSpringJointInternal(
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const EditorScriptSpringJointDesc& springJointDesc) {
	if (physicsManager_ == nullptr) {
		return kInvalidEditorScriptJointHandle;
	}

	return physicsManager_->CreateSpringJoint(
		ownerGameObjectId,
		connectedGameObjectId,
		ToEditorVector3(springJointDesc.ownerAnchor),
		ToEditorVector3(springJointDesc.connectedAnchor),
		springJointDesc.minDistance,
		springJointDesc.maxDistance,
		springJointDesc.frequency,
		springJointDesc.damping);
}

bool EditorScriptManager::DestroyJointInternal(EditorScriptJointHandle jointHandle) {
	return physicsManager_ != nullptr && physicsManager_->DestroyJoint(jointHandle);
}

bool EditorScriptManager::SetSpringJointSettingsInternal(
	EditorScriptJointHandle jointHandle,
	const EditorScriptSpringJointDesc& springJointDesc) {
	return physicsManager_ != nullptr && physicsManager_->SetSpringJointSettings(
		jointHandle,
		ToEditorVector3(springJointDesc.ownerAnchor),
		ToEditorVector3(springJointDesc.connectedAnchor),
		springJointDesc.minDistance,
		springJointDesc.maxDistance,
		springJointDesc.frequency,
		springJointDesc.damping);
}

bool EditorScriptManager::IsJointValidInternal(EditorScriptJointHandle jointHandle) const {
	return physicsManager_ != nullptr && physicsManager_->IsJointValid(jointHandle);
}

EditorScriptJointHandle EditorScriptManager::CreateJointInternal(
	EditorScriptJointType jointType,
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const EditorScriptJointDesc& jointDesc) {
	if (physicsManager_ == nullptr) {
		return kInvalidEditorScriptJointHandle;
	}

	const int32_t jointTypeValue = static_cast<int32_t>(jointType);
	if (jointTypeValue < static_cast<int32_t>(EditorScriptJointType::Fixed) ||
		jointTypeValue > static_cast<int32_t>(EditorScriptJointType::Character)) {
		return kInvalidEditorScriptJointHandle;
	}

	return physicsManager_->CreateJoint(
		ToRuntimeJointType(jointType),
		ownerGameObjectId,
		connectedGameObjectId,
		ToRuntimeJointSettings(jointDesc));
}

bool EditorScriptManager::SetJointSettingsInternal(
	EditorScriptJointHandle jointHandle,
	const EditorScriptJointDesc& jointDesc) {
	return physicsManager_ != nullptr && physicsManager_->SetJointSettings(
		jointHandle,
		ToRuntimeJointSettings(jointDesc));
}

bool EditorScriptManager::DetachRopeInternal(int32_t ownerGameObjectId) {
	return physicsManager_ != nullptr && physicsManager_->DetachRope(ownerGameObjectId);
}

bool EditorScriptManager::SetRopeLengthInternal(int32_t ownerGameObjectId, float maximumLength) {
	return physicsManager_ != nullptr && physicsManager_->SetRopeLength(ownerGameObjectId, maximumLength);
}

bool EditorScriptManager::RepairRopeInternal(int32_t ownerGameObjectId) {
	return physicsManager_ != nullptr && physicsManager_->RepairRope(ownerGameObjectId);
}

EditorScriptRopeState EditorScriptManager::GetRopeStateInternal(int32_t ownerGameObjectId) const {
	EditorScriptRopeState ropeState{};
	ropeState.targetGameObjectId = -1;

	if (physicsManager_ == nullptr) {
		return ropeState;
	}

	ropeState.hasComponent = physicsManager_->GetRopeState(
		ownerGameObjectId,
		ropeState.isActive,
		ropeState.isBroken,
		ropeState.targetGameObjectId,
		ropeState.maximumLength,
		ropeState.currentLength,
		ropeState.currentTension);
	return ropeState;
}

EditorScriptWireHandle EditorScriptManager::CreateWireInternal(
	const EditorScriptWireDesc& wireDesc) {
	if (physicsManager_ == nullptr) {
		return kInvalidEditorScriptWireHandle;
	}

	EditorPhysicsManager::RuntimeWireDesc runtimeDesc{};
	runtimeDesc.firstGameObjectId = wireDesc.firstGameObjectId;
	runtimeDesc.secondGameObjectId = wireDesc.secondGameObjectId;
	runtimeDesc.ownerGameObjectId = wireDesc.ownerGameObjectId;
	runtimeDesc.rendererSettingsGameObjectId = wireDesc.rendererSettingsGameObjectId;
	runtimeDesc.firstLocalAnchor = ToEditorVector3(wireDesc.firstLocalAnchor);
	runtimeDesc.secondLocalAnchor = ToEditorVector3(wireDesc.secondLocalAnchor);
	runtimeDesc.maximumLength = wireDesc.maximumLength;
	runtimeDesc.minimumLength = wireDesc.minimumLength;
	runtimeDesc.stiffness = wireDesc.stiffness;
	runtimeDesc.damping = wireDesc.damping;
	runtimeDesc.maximumTension = wireDesc.maximumTension;
	runtimeDesc.breakingTension = wireDesc.breakingTension;
	runtimeDesc.shrinkSpeed = wireDesc.shrinkSpeed;
	runtimeDesc.applyReaction = wireDesc.applyReaction;
	runtimeDesc.requireConnectable = wireDesc.requireConnectable;
	return physicsManager_->CreateWire(runtimeDesc);
}

bool EditorScriptManager::DestroyWireInternal(EditorScriptWireHandle wireHandle) {
	return physicsManager_ != nullptr && physicsManager_->DestroyWire(wireHandle);
}

bool EditorScriptManager::SetWireLengthByHandleInternal(
	EditorScriptWireHandle wireHandle,
	float maximumLength) {
	return physicsManager_ != nullptr &&
		physicsManager_->SetWireLength(wireHandle, maximumLength);
}

bool EditorScriptManager::SetWireShrinkSpeedInternal(
	EditorScriptWireHandle wireHandle,
	float shrinkSpeed) {
	return physicsManager_ != nullptr &&
		physicsManager_->SetWireShrinkSpeed(wireHandle, shrinkSpeed);
}

bool EditorScriptManager::RepairWireInternal(EditorScriptWireHandle wireHandle) {
	return physicsManager_ != nullptr && physicsManager_->RepairWire(wireHandle);
}

bool EditorScriptManager::GetWireStateByHandleInternal(
	EditorScriptWireHandle wireHandle,
	EditorScriptWireState& wireState) const {
	if (physicsManager_ == nullptr) {
		return false;
	}

	EditorPhysicsManager::RuntimeWireState runtimeState{};

	if (!physicsManager_->GetWireState(wireHandle, runtimeState)) {
		return false;
	}

	wireState = ToScriptWireState(runtimeState);
	return true;
}

int32_t EditorScriptManager::GetWireCountForGameObjectInternal(int32_t gameObjectId) const {
	return physicsManager_ != nullptr
		? physicsManager_->GetWireCountForGameObject(gameObjectId)
		: 0;
}

bool EditorScriptManager::GetWireForGameObjectInternal(
	int32_t gameObjectId,
	int32_t wireIndex,
	EditorScriptWireState& wireState) const {
	if (physicsManager_ == nullptr) {
		return false;
	}

	EditorPhysicsManager::RuntimeWireState runtimeState{};

	if (!physicsManager_->GetWireForGameObject(gameObjectId, wireIndex, runtimeState)) {
		return false;
	}

	wireState = ToScriptWireState(runtimeState);
	return true;
}

bool EditorScriptManager::CanConnectWireInternal(int32_t gameObjectId) const {
	return physicsManager_ != nullptr && physicsManager_->CanConnectWire(gameObjectId);
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
	const EditorComponent* animatorComponent =
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Animator);
	const EditorComponent* activeAnimationComponent = animationComponent != nullptr
		? animationComponent
		: animatorComponent;

	if (activeAnimationComponent == nullptr) {
		return animationState;
	}

	animationState.hasComponent = true;
	animationState.isLoop = activeAnimationComponent->animationLoop;
	animationState.playOnAwake = activeAnimationComponent->animationPlayOnAwake;
	animationState.animationType = activeAnimationComponent->animationType;
	animationState.animationSpeed = activeAnimationComponent->animationSpeed;
	animationState.animationAmplitude = activeAnimationComponent->animationAmplitude;
	animationState.isPlaying = false;
	animationState.currentTime = 0.0f;

	if (animationManager_ != nullptr) {
		animationState.isPlaying = animationManager_->IsAnimationPlaying(gameObjectId);
		animationState.currentTime = animationManager_->GetAnimationTime(gameObjectId);
	}
	else {
		animationState.isPlaying = isStarted_ && activeAnimationComponent->isActive;
	}

	std::string animationAssetPath = activeAnimationComponent->assetPath;
	if (animatorComponent != nullptr && EditorAssetUtility::HasExtension(animationAssetPath, ".animgraph")) {
		animationAssetPath.clear();
	}
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
		const int32_t maximumClipIndex = animationState.clipCount - 1;
		const int32_t clipIndex = (std::clamp)(
			activeAnimationComponent->animationClipIndex,
			0,
			maximumClipIndex);
		const ModelAnimationClipData& selectedClip = modelData.animationClips[
			static_cast<size_t>(clipIndex)];
		animationState.currentClipDuration = selectedClip.durationSeconds;
		CopyStringToFixedBuffer(
			selectedClip.name,
			animationState.currentClipName,
			sizeof(animationState.currentClipName));
	}

	return animationState;
}

bool EditorScriptManager::SetAnimatorFloatInternal(
	int32_t gameObjectId,
	const char* parameterName,
	float value) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetFloat(gameObjectId, parameterName, value);
}

bool EditorScriptManager::SetAnimatorIntInternal(
	int32_t gameObjectId,
	const char* parameterName,
	int32_t value) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetInt(gameObjectId, parameterName, value);
}

bool EditorScriptManager::SetAnimatorBoolInternal(
	int32_t gameObjectId,
	const char* parameterName,
	bool value) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetBool(gameObjectId, parameterName, value);
}

bool EditorScriptManager::SetAnimatorTriggerInternal(
	int32_t gameObjectId,
	const char* parameterName) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetTrigger(gameObjectId, parameterName);
}

bool EditorScriptManager::SetAnimatorVector2Internal(
	int32_t gameObjectId,
	const char* parameterName,
	const EditorScriptVector2& value) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetVector2(gameObjectId, parameterName, {value.x, value.y});
}

bool EditorScriptManager::SetAnimatorVector3Internal(
	int32_t gameObjectId,
	const char* parameterName,
	const EditorScriptVector3& value) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->SetVector3(gameObjectId, parameterName, {value.x, value.y, value.z});
}

bool EditorScriptManager::GetAnimatorFloatInternal(
	int32_t gameObjectId,
	const char* parameterName,
	float& value) const {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->GetFloat(gameObjectId, parameterName, value);
}

bool EditorScriptManager::GetAnimatorIntInternal(
	int32_t gameObjectId,
	const char* parameterName,
	int32_t& value) const {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->GetInt(gameObjectId, parameterName, value);
}

bool EditorScriptManager::GetAnimatorBoolInternal(
	int32_t gameObjectId,
	const char* parameterName,
	bool& value) const {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->GetBool(gameObjectId, parameterName, value);
}

bool EditorScriptManager::GetAnimatorVector2Internal(
	int32_t gameObjectId,
	const char* parameterName,
	EditorScriptVector2& value) const {
	Vector2 editorValue{};
	const bool isFound = animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->GetVector2(gameObjectId, parameterName, editorValue);

	if (isFound) {
		value = {editorValue.x, editorValue.y};
	}

	return isFound;
}

bool EditorScriptManager::GetAnimatorVector3Internal(
	int32_t gameObjectId,
	const char* parameterName,
	EditorScriptVector3& value) const {
	Vector3 editorValue{};
	const bool isFound = animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->GetVector3(gameObjectId, parameterName, editorValue);

	if (isFound) {
		value = ToScriptVector3(editorValue);
	}

	return isFound;
}

bool EditorScriptManager::ResetAnimatorTriggerInternal(
	int32_t gameObjectId,
	const char* parameterName) {
	return animationManager_ != nullptr && parameterName != nullptr &&
		animationManager_->ResetTrigger(gameObjectId, parameterName);
}

int32_t EditorScriptManager::FindGameObjectByNameInternal(const char* gameObjectName) const {
	if (editorScene_ == nullptr || gameObjectName == nullptr) {
		return -1;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (gameObject.name == gameObjectName) {
			return gameObject.id;
		}
	}

	return -1;
}

bool EditorScriptManager::SetGameObjectActiveInternal(int32_t gameObjectId, bool isActive) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	gameObject->isActive = isActive;

	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, isActive);
	}

	return true;
}

bool EditorScriptManager::IsGameObjectActiveInternal(int32_t gameObjectId) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	return gameObject != nullptr && gameObject->isActive;
}

bool EditorScriptManager::SetComponentActiveInternal(
	int32_t gameObjectId,
	const char* componentTypeName,
	bool isActive) {
	if (editorScene_ == nullptr || componentTypeName == nullptr || componentTypeName[0] == '\0') {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	for (EditorComponent& component : gameObject->components) {
		if (ToString(component.type) != componentTypeName) {
			continue;
		}

		component.isActive = isActive;

		if (component.type == EditorComponentType::RigidBody && physicsManager_ != nullptr) {
			physicsManager_->SetGameObjectSimulationActive(gameObjectId, isActive);
		}

		return true;
	}

	return false;
}

bool EditorScriptManager::IsComponentActiveInternal(
	int32_t gameObjectId,
	const char* componentTypeName) const {
	if (editorScene_ == nullptr || componentTypeName == nullptr || componentTypeName[0] == '\0') {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	for (const EditorComponent& component : gameObject->components) {
		if (ToString(component.type) == componentTypeName) {
			return component.isActive;
		}
	}

	return false;
}

bool EditorScriptManager::HasComponentInternal(
	int32_t gameObjectId,
	const char* componentTypeName) const {
	if (editorScene_ == nullptr || componentTypeName == nullptr || componentTypeName[0] == '\0') {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return false;
	}

	for (const EditorComponent& component : gameObject->components) {
		if (ToString(component.type) == componentTypeName) {
			return true;
		}
	}

	return false;
}

bool EditorScriptManager::AddComponentInternal(
	int32_t gameObjectId,
	const char* componentTypeName) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorComponentType componentType = EditorComponentType::Transform;

	if (!TryResolveComponentType(componentTypeName, componentType) ||
		!editorScene_->AddComponent(gameObjectId, componentType)) {
		return false;
	}

	if (physicsManager_ != nullptr &&
		(componentType == EditorComponentType::RigidBody ||
		 IsColliderComponentType(componentType))) {
		physicsManager_->RegisterRuntimeHierarchy(gameObjectId);
	}

	return true;
}

bool EditorScriptManager::RemoveComponentInternal(
	int32_t gameObjectId,
	const char* componentTypeName) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorComponentType componentType = EditorComponentType::Transform;

	if (!TryResolveComponentType(componentTypeName, componentType)) {
		return false;
	}

	// Jolt Bodyが参照中のComponentを消す前にBroadPhaseから外し、削除後の不正参照を防ぐ。
	if (physicsManager_ != nullptr &&
		(componentType == EditorComponentType::RigidBody ||
		 IsColliderComponentType(componentType))) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, false);
	}

	return editorScene_->RemoveComponent(gameObjectId, componentType);
}

int32_t EditorScriptManager::FindGameObjectsWithComponentInternal(
	const char* componentTypeName,
	int32_t* gameObjectIds,
	int32_t capacity) const {
	if (editorScene_ == nullptr || componentTypeName == nullptr ||
		componentTypeName[0] == '\0') {
		return 0;
	}

	int32_t foundCount = 0;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive || !HasComponentInternal(gameObject.id, componentTypeName)) {
			continue;
		}

		if (gameObjectIds != nullptr && foundCount < capacity) {
			gameObjectIds[foundCount] = gameObject.id;
		}

		foundCount++;
	}

	return foundCount;
}

int32_t EditorScriptManager::InstantiateGameObjectInternal(
	int32_t sourceGameObjectId,
	const EditorScriptVector3& position,
	const EditorScriptVector3& rotation) {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(sourceGameObjectId) == nullptr) {
		return -1;
	}

	const int32_t instanceGameObjectId = editorScene_->DuplicateGameObject(sourceGameObjectId);
	EditorGameObject* instanceGameObject = editorScene_->FindGameObject(instanceGameObjectId);

	if (instanceGameObject == nullptr) {
		return -1;
	}

	instanceGameObject->parentId = -1;
	instanceGameObject->translate = ToEditorVector3(position);
	instanceGameObject->rotate = ToEditorVector3(rotation);
	instanceGameObject->isActive = true;

	if (physicsManager_ != nullptr) {
		physicsManager_->RegisterRuntimeHierarchy(instanceGameObjectId);
	}

	pendingRuntimeHierarchyRegistrations_.push_back(instanceGameObjectId);
	return instanceGameObjectId;
}

bool EditorScriptManager::DestroyGameObjectInternal(int32_t gameObjectId) {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	// 実行中は参照を保持するManagerがあるため即時eraseせず、階層を非Active化する。
	std::vector<int32_t> pendingGameObjectIds{gameObjectId};

	while (!pendingGameObjectIds.empty()) {
		const int32_t currentGameObjectId = pendingGameObjectIds.back();
		pendingGameObjectIds.pop_back();
		EditorGameObject* currentGameObject = editorScene_->FindGameObject(currentGameObjectId);

		if (currentGameObject == nullptr) {
			continue;
		}

		currentGameObject->isActive = false;

		if (physicsManager_ != nullptr) {
			physicsManager_->SetGameObjectSimulationActive(currentGameObjectId, false);
		}

		for (const int32_t childGameObjectId : currentGameObject->children) {
			pendingGameObjectIds.push_back(childGameObjectId);
		}
	}

	return true;
}

bool EditorScriptManager::SetRendererColorInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& color) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return false;
	}

	EditorComponent* renderer = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::ModelRenderer);

	if (renderer == nullptr) {
		renderer = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::SkinnedMeshRenderer);
	}

	if (renderer == nullptr) {
		return false;
	}

	renderer->color = {
		(std::clamp)(color.x, 0.0f, 1.0f),
		(std::clamp)(color.y, 0.0f, 1.0f),
		(std::clamp)(color.z, 0.0f, 1.0f)};
	return true;
}

bool EditorScriptManager::SetRendererEmissionInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& color,
	float strength) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return false;
	}

	EditorComponent* renderer = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::ModelRenderer);

	if (renderer == nullptr) {
		renderer = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::SkinnedMeshRenderer);
	}

	if (renderer == nullptr) {
		return false;
	}

	renderer->emissionColor = {
		(std::clamp)(color.x, 0.0f, 1.0f),
		(std::clamp)(color.y, 0.0f, 1.0f),
		(std::clamp)(color.z, 0.0f, 1.0f)};
	renderer->emissionStrength = (std::max)(strength, 0.0f);
	return true;
}

bool EditorScriptManager::SetHookVisualStateInternal(
	int32_t gameObjectId,
	int32_t visualState) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return false;
	}

	const EditorComponent* hook = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::WireConnectable);

	if (hook == nullptr) {
		return false;
	}

	const Vector3* visualColor = &hook->wireConnectableNormalColor;

	if (visualState == 1) {
		visualColor = &hook->wireConnectableTargetedColor;
	}
	else if (visualState == 2) {
		visualColor = &hook->wireConnectableSelectedColor;
	}
	else if (visualState == 3) {
		visualColor = &hook->wireConnectableConnectedColor;
	}

	const EditorScriptVector3 scriptColor = ToScriptVector3(*visualColor);
	const bool colorChanged = SetRendererColorInternal(gameObjectId, scriptColor);
	const bool emissionChanged = SetRendererEmissionInternal(
		gameObjectId,
		scriptColor,
		hook->wireConnectableEmissionStrength);
	return colorChanged || emissionChanged;
}

int32_t EditorScriptManager::CreateGameObjectInternal(const char* name) {
	if (editorScene_ == nullptr) {
		return -1;
	}

	const std::string resolvedName = name != nullptr && name[0] != '\0'
		? name
		: "GameObject";
	return editorScene_->CreateGameObject(resolvedName);
}

int32_t EditorScriptManager::GetParentGameObjectInternal(int32_t gameObjectId) const {
	if (editorScene_ == nullptr) {
		return -1;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	return gameObject != nullptr ? gameObject->parentId : -1;
}

bool EditorScriptManager::SetParentGameObjectInternal(
	int32_t childGameObjectId,
	int32_t parentGameObjectId,
	bool preserveWorldTransform) {
	return editorScene_ != nullptr && editorScene_->SetParent(
		childGameObjectId,
		parentGameObjectId,
		preserveWorldTransform);
}

int32_t EditorScriptManager::GetChildGameObjectCountInternal(int32_t gameObjectId) const {
	if (editorScene_ == nullptr) {
		return 0;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	return gameObject != nullptr ? static_cast<int32_t>(gameObject->children.size()) : 0;
}

int32_t EditorScriptManager::GetChildGameObjectInternal(
	int32_t gameObjectId,
	int32_t childIndex) const {
	if (editorScene_ == nullptr || childIndex < 0) {
		return -1;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr || childIndex >= static_cast<int32_t>(gameObject->children.size())) {
		return -1;
	}

	return gameObject->children[static_cast<size_t>(childIndex)];
}

bool EditorScriptManager::ReloadPrimarySceneInternal() {
	return !loadedScenePaths_.empty() && RequestSceneLoadInternal(loadedScenePaths_.front());
}

bool EditorScriptManager::WorldToLocalPointInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& worldPoint,
	EditorScriptVector3& localPoint) const {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	localPoint = ToScriptVector3(Transform(
		ToEditorVector3(worldPoint),
		Inverse(editorScene_->GetWorldMatrix(gameObjectId))));
	return true;
}

bool EditorScriptManager::LocalToWorldPointInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& localPoint,
	EditorScriptVector3& worldPoint) const {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	worldPoint = ToScriptVector3(Transform(
		ToEditorVector3(localPoint),
		editorScene_->GetWorldMatrix(gameObjectId)));
	return true;
}

bool EditorScriptManager::WorldToLocalDirectionInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& worldDirection,
	EditorScriptVector3& localDirection) const {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	const Matrix4x4 inverseWorldMatrix = Inverse(editorScene_->GetWorldMatrix(gameObjectId));
	const Vector3 localOrigin = Transform({0.0f, 0.0f, 0.0f}, inverseWorldMatrix);
	const Vector3 localEnd = Transform(ToEditorVector3(worldDirection), inverseWorldMatrix);
	localDirection = ToScriptVector3(Subtract(localEnd, localOrigin));
	return true;
}

bool EditorScriptManager::LocalToWorldDirectionInternal(
	int32_t gameObjectId,
	const EditorScriptVector3& localDirection,
	EditorScriptVector3& worldDirection) const {
	if (editorScene_ == nullptr || editorScene_->FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	const Matrix4x4 worldMatrix = editorScene_->GetWorldMatrix(gameObjectId);
	const Vector3 worldOrigin = Transform({0.0f, 0.0f, 0.0f}, worldMatrix);
	const Vector3 worldEnd = Transform(ToEditorVector3(localDirection), worldMatrix);
	worldDirection = ToScriptVector3(Subtract(worldEnd, worldOrigin));
	return true;
}

bool EditorScriptManager::PhysicsRaycastFilteredInternal(
	const EditorScriptRay& ray,
	float distance,
	uint32_t physicsLayerMask,
	bool includeTriggers,
	const char* requiredComponentTypeName,
	EditorScriptPhysicsHit& hit) const {
	if (editorScene_ == nullptr || physicsManager_ == nullptr ||
		distance < 0.0f || physicsLayerMask == 0U) {
		return false;
	}

	constexpr int32_t kMaximumSkippedHits = 128;
	std::vector<int32_t> ignoredGameObjectIds;
	ignoredGameObjectIds.reserve(kMaximumSkippedHits);

	for (int32_t skippedHitCount = 0;
		skippedHitCount < kMaximumSkippedHits;
		skippedHitCount++) {
		EditorJoltPhysicsManager::PhysicsHit physicsHit{};

		if (!physicsManager_->RaycastIgnoringGameObjects(
			ToEditorVector3(ray.origin),
			ToEditorVector3(ray.direction),
			distance,
			ignoredGameObjectIds,
			physicsHit)) {
			return false;
		}

		const EditorGameObject* hitGameObject =
			editorScene_->FindGameObject(physicsHit.gameObjectId);
		bool passesLayer = false;

		if (hitGameObject != nullptr) {
			for (const EditorComponent& component : hitGameObject->components) {
				if (!component.isActive || !IsColliderComponentType(component.type)) {
					continue;
				}

				const int32_t layerIndex = (std::clamp)(component.physicsLayer, 0, 31);
				passesLayer = (physicsLayerMask & (1U << static_cast<uint32_t>(layerIndex))) != 0U;
				break;
			}
		}

		const bool passesTrigger = includeTriggers || !physicsHit.isTrigger;
		const bool passesComponent = requiredComponentTypeName == nullptr ||
			requiredComponentTypeName[0] == '\0' ||
			HasComponentInternal(physicsHit.gameObjectId, requiredComponentTypeName);

		if (passesLayer && passesTrigger && passesComponent) {
			hit.gameObjectId = physicsHit.gameObjectId;
			hit.point = ToScriptVector3(physicsHit.point);
			hit.normal = ToScriptVector3(physicsHit.normal);
			hit.distance = physicsHit.distance;
			hit.isTrigger = physicsHit.isTrigger;
			return true;
		}

		ignoredGameObjectIds.push_back(physicsHit.gameObjectId);
	}

	return false;
}

bool EditorScriptManager::InvokeScriptActionInternal(
	int32_t gameObjectId,
	const char* functionName) {
	if (!isStarted_ || editorScene_ == nullptr || functionName == nullptr || functionName[0] == '\0') {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr || !gameObject->isActive) {
		return false;
	}

	const std::vector<std::string> actionNames = GetRegisteredActionNames(gameObjectId);

	if (std::find(actionNames.begin(), actionNames.end(), functionName) == actionNames.end()) {
		return false;
	}

	QueueActionEvent(gameObjectId, functionName);
	return true;
}

bool EditorScriptManager::RequestSceneLoadInternal(
	const std::string& scenePath,
	bool isAdditive,
	bool isAsynchronous) {
	if (scenePath.empty()) {
		PushConsoleMessage("Scene: 遷移先が空です");
		return false;
	}

	const std::string normalizedScenePath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();

	if (!EditorAssetUtility::HasExtension(normalizedScenePath, ".scene")) {
		PushConsoleMessage("Scene: .scene 以外は読み込めません " + normalizedScenePath);
		return false;
	}

	if (EditorSharedState::g_isStandaloneGame &&
		std::find(
			EditorSharedState::g_gameBuildScenePaths.begin(),
			EditorSharedState::g_gameBuildScenePaths.end(),
			normalizedScenePath) == EditorSharedState::g_gameBuildScenePaths.end()) {
		PushConsoleMessage("Scene: Build Settings に含まれていません " + normalizedScenePath);
		return false;
	}

	std::error_code fileError;
	if (!std::filesystem::exists(normalizedScenePath, fileError) || fileError) {
		PushConsoleMessage("Scene: ファイルがありません " + normalizedScenePath);
		return false;
	}

	requestedSceneLoad_.scenePath = normalizedScenePath;
	requestedSceneLoad_.isAdditive = isAdditive;
	requestedSceneLoad_.isAsynchronous = isAsynchronous;
	return true;
}

bool EditorScriptManager::RequestSceneLoadByBuildIndexInternal(int32_t sceneIndex) {
	if (sceneIndex < 0 ||
		static_cast<size_t>(sceneIndex) >=
			EditorSharedState::g_gameBuildScenePaths.size()) {
		PushConsoleMessage("Scene: Build Index が範囲外です");
		return false;
	}

	return RequestSceneLoadInternal(
		EditorSharedState::g_gameBuildScenePaths[static_cast<size_t>(sceneIndex)]);
}
