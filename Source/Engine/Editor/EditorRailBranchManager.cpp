#include "EditorRailBranchManager.h"

#include "EditorComponentUtility.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"

#include <algorithm>

void EditorRailBranchManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	scriptManager_ = scriptManager;
	branchRuntimes_.clear();
	isStarted_ = false;
}

void EditorRailBranchManager::Start() {
	branchRuntimes_.clear();
	isStarted_ = true;
}

void EditorRailBranchManager::Update() {
	if (!isStarted_ || editorScene_ == nullptr || railMovementManager_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* railBranchComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailBranch);

		if (railBranchComponent == nullptr || !railBranchComponent->isActive ||
			railBranchComponent->railBranchTriggerMode != 0) {
			continue;
		}

		const int32_t followerGameObjectId = railBranchComponent->railBranchFollowerGameObjectId >= 0
			? railBranchComponent->railBranchFollowerGameObjectId
			: gameObject.id;
		float normalizedProgress = 0.0f;

		if (!railMovementManager_->GetNormalizedProgress(followerGameObjectId, normalizedProgress)) {
			continue;
		}

		BranchRuntime& branchRuntime = branchRuntimes_[gameObject.id];
		const float triggerProgress = (std::clamp)(railBranchComponent->railBranchTriggerNormalized, 0.0f, 1.0f);
		const bool crossedProgress = branchRuntime.hasPreviousProgress &&
			branchRuntime.previousProgress < triggerProgress && normalizedProgress >= triggerProgress;

		if ((!railBranchComponent->railBranchTriggerOnce || !branchRuntime.hasTriggered) && crossedProgress) {
			branchRuntime.hasTriggered = ExecuteBranch(gameObject, *railBranchComponent);
		}

		if (!railBranchComponent->railBranchTriggerOnce && normalizedProgress < triggerProgress) {
			branchRuntime.hasTriggered = false;
		}

		branchRuntime.previousProgress = normalizedProgress;
		branchRuntime.hasPreviousProgress = true;
	}
}

void EditorRailBranchManager::Stop() {
	branchRuntimes_.clear();
	isStarted_ = false;
}

bool EditorRailBranchManager::Trigger(int32_t railBranchGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(railBranchGameObjectId);

	if (gameObject == nullptr || !gameObject->isActive) {
		return false;
	}

	const EditorComponent* railBranchComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::RailBranch);

	if (railBranchComponent == nullptr || !railBranchComponent->isActive) {
		return false;
	}

	BranchRuntime& branchRuntime = branchRuntimes_[gameObject->id];

	if (railBranchComponent->railBranchTriggerOnce && branchRuntime.hasTriggered) {
		return false;
	}

	branchRuntime.hasTriggered = ExecuteBranch(*gameObject, *railBranchComponent);
	return branchRuntime.hasTriggered;
}

bool EditorRailBranchManager::ExecuteBranch(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	if (railMovementManager_ == nullptr || component.railBranchTargetPathGameObjectId < 0) {
		return false;
	}

	const int32_t followerGameObjectId = component.railBranchFollowerGameObjectId >= 0
		? component.railBranchFollowerGameObjectId
		: gameObject.id;
	const bool changedRail = railMovementManager_->SetRailPath(
		followerGameObjectId,
		component.railBranchTargetPathGameObjectId,
		component.railBranchPreserveProgress);

	if (changedRail) {
		QueueAction(gameObject, component);
	}

	return changedRail;
}

void EditorRailBranchManager::QueueAction(
	const EditorGameObject& gameObject,
	const EditorComponent& component) const {
	if (scriptManager_ == nullptr || component.railBranchActionName.empty()) {
		return;
	}

	const int32_t actionTargetGameObjectId = component.railBranchActionTargetGameObjectId >= 0
		? component.railBranchActionTargetGameObjectId
		: gameObject.id;
	scriptManager_->QueueActionEvent(
		actionTargetGameObjectId,
		component.railBranchActionName,
		EditorScriptInputValueTypeButton,
		1.0f,
		EditorScriptVector2{});
}
