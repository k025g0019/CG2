#include "EditorWaveSpawnerManager.h"

#include "EditorComponentUtility.h"
#include "EditorPhysicsManager.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"

#include <algorithm>

void EditorWaveSpawnerManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorPhysicsManager* physicsManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	physicsManager_ = physicsManager;
	scriptManager_ = scriptManager;
	waveRuntimes_.clear();
	originalActiveStates_.clear();
	isStarted_ = false;
}

void EditorWaveSpawnerManager::Start() {
	waveRuntimes_.clear();
	originalActiveStates_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& waveGameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* waveComponent = EditorComponentUtility::FindComponent(
			waveGameObject,
			EditorComponentType::WaveSpawner);

		if (waveComponent == nullptr || !waveComponent->isActive || !waveGameObject.isActive) {
			continue;
		}

		WaveRuntime waveRuntime{};
		waveRuntime.childGameObjectIds = waveGameObject.children;
		waveRuntime.spawnTimer = 0.0f;

		if (waveComponent->waveDeactivateChildrenOnStart) {
			for (const int32_t childGameObjectId : waveRuntime.childGameObjectIds) {
				SetRuntimeActiveRecursive(childGameObjectId, false);
			}
		}

		waveRuntimes_[waveGameObject.id] = std::move(waveRuntime);
	}
}

void EditorWaveSpawnerManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (auto& waveRuntimePair : waveRuntimes_) {
		EditorGameObject* waveGameObject = editorScene_->FindGameObject(waveRuntimePair.first);

		if (waveGameObject == nullptr || !waveGameObject->isActive) {
			continue;
		}

		const EditorComponent* waveComponent = EditorComponentUtility::FindComponent(
			*waveGameObject,
			EditorComponentType::WaveSpawner);

		if (waveComponent == nullptr || !waveComponent->isActive) {
			continue;
		}

		WaveRuntime& waveRuntime = waveRuntimePair.second;

		if (!waveRuntime.hasTriggered) {
			if (!IsTriggerSatisfied(*waveComponent)) {
				continue;
			}

			waveRuntime.hasTriggered = true;
			waveRuntime.spawnTimer = 0.0f;
			QueueAction(*waveGameObject, *waveComponent, waveComponent->waveStartedActionName, 1.0f);
		}

		if (waveRuntime.nextChildIndex >= static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
			CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
			continue;
		}

		const float spawnInterval = (std::max)(waveComponent->waveSpawnInterval, 0.0f);

		if (spawnInterval <= 0.0f) {
			while (waveRuntime.nextChildIndex < static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
				const int32_t spawnedGameObjectId = SpawnNextChild(waveRuntime);
				QueueAction(
					*waveGameObject,
					*waveComponent,
					waveComponent->waveSpawnedActionName,
					static_cast<float>(spawnedGameObjectId));
			}

			CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
			continue;
		}

		waveRuntime.spawnTimer -= deltaTime;

		while (waveRuntime.spawnTimer <= 0.0f &&
			waveRuntime.nextChildIndex < static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
			const int32_t spawnedGameObjectId = SpawnNextChild(waveRuntime);
			QueueAction(
				*waveGameObject,
				*waveComponent,
				waveComponent->waveSpawnedActionName,
				static_cast<float>(spawnedGameObjectId));
			waveRuntime.spawnTimer += spawnInterval;
		}

		CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
	}
}

void EditorWaveSpawnerManager::Stop() {
	waveRuntimes_.clear();
	originalActiveStates_.clear();
	isStarted_ = false;
}

bool EditorWaveSpawnerManager::IsTriggerSatisfied(const EditorComponent& component) const {
	if (component.waveTriggerMode == 0) {
		return true;
	}

	if (component.waveTriggerMode != 1 ||
		component.waveTriggerSourceGameObjectId < 0 ||
		railMovementManager_ == nullptr) {
		return false;
	}

	float normalizedProgress = 0.0f;
	const bool hasProgress = railMovementManager_->GetNormalizedProgress(
		component.waveTriggerSourceGameObjectId,
		normalizedProgress);
	return
		hasProgress &&
		normalizedProgress >= (std::clamp)(component.waveTriggerValue, 0.0f, 1.0f);
}

void EditorWaveSpawnerManager::SetRuntimeActiveRecursive(int32_t gameObjectId, bool isActive) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return;
	}

	const std::vector<int32_t> childGameObjectIds = gameObject->children;

	if (!isActive) {
		originalActiveStates_.try_emplace(gameObjectId, gameObject->isActive);
	}

	const auto originalActiveIterator = originalActiveStates_.find(gameObjectId);
	const bool targetActive = isActive
		? (originalActiveIterator != originalActiveStates_.end()
			? originalActiveIterator->second
			: gameObject->isActive)
		: false;

	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, targetActive);
	}

	gameObject->isActive = targetActive;

	for (const int32_t childGameObjectId : childGameObjectIds) {
		SetRuntimeActiveRecursive(childGameObjectId, isActive);
	}
}

int32_t EditorWaveSpawnerManager::SpawnNextChild(WaveRuntime& waveRuntime) {
	if (waveRuntime.nextChildIndex < 0 ||
		waveRuntime.nextChildIndex >= static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
		return -1;
	}

	const int32_t childGameObjectId =
		waveRuntime.childGameObjectIds[static_cast<size_t>(waveRuntime.nextChildIndex)];
	waveRuntime.nextChildIndex++;
	SetRuntimeActiveRecursive(childGameObjectId, true);
	return childGameObjectId;
}

void EditorWaveSpawnerManager::QueueAction(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	const std::string& actionName,
	float value) const {
	if (scriptManager_ == nullptr || actionName.empty()) {
		return;
	}

	const int32_t targetGameObjectId = component.waveActionTargetGameObjectId >= 0
		? component.waveActionTargetGameObjectId
		: ownerGameObject.id;
	scriptManager_->QueueActionEvent(
		targetGameObjectId,
		actionName,
		EditorScriptInputValueTypeButton,
		value,
		EditorScriptVector2{});
}

void EditorWaveSpawnerManager::CompleteWaveIfNeeded(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	WaveRuntime& waveRuntime) {
	if (waveRuntime.hasCompleted ||
		waveRuntime.nextChildIndex < static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
		return;
	}

	waveRuntime.hasCompleted = true;
	QueueAction(
		ownerGameObject,
		component,
		component.waveCompletedActionName,
		static_cast<float>(waveRuntime.childGameObjectIds.size()));
}
