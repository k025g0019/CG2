#include "EditorGameplayEventManager.h"

#include "EditorComponentUtility.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"

#include <algorithm>

void EditorGameplayEventManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	scriptManager_ = scriptManager;
	timelineRuntimes_.clear();
	thresholdRuntimes_.clear();
	isStarted_ = false;
}

void EditorGameplayEventManager::Start() {
	timelineRuntimes_.clear();
	thresholdRuntimes_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* timelineComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::TimelineEvent);

		if (timelineComponent != nullptr && timelineComponent->isActive) {
			timelineRuntimes_[gameObject.id] = TimelineRuntime{};
		}

		const EditorComponent* thresholdComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ThresholdState);

		if (thresholdComponent != nullptr && thresholdComponent->isActive) {
			thresholdRuntimes_[gameObject.id] = ThresholdRuntime{};
		}
	}
}

void EditorGameplayEventManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	UpdateTimelineEvents(deltaTime);
	UpdateThresholdStates();
}

void EditorGameplayEventManager::Stop() {
	timelineRuntimes_.clear();
	thresholdRuntimes_.clear();
	isStarted_ = false;
}

void EditorGameplayEventManager::UpdateTimelineEvents(float deltaTime) {
	for (auto& timelineRuntimePair : timelineRuntimes_) {
		EditorGameObject* eventGameObject = editorScene_->FindGameObject(timelineRuntimePair.first);

		if (eventGameObject == nullptr || !eventGameObject->isActive) {
			continue;
		}

		const EditorComponent* eventComponent = EditorComponentUtility::FindComponent(
			*eventGameObject,
			EditorComponentType::TimelineEvent);

		if (eventComponent == nullptr || !eventComponent->isActive) {
			continue;
		}

		TimelineRuntime& timelineRuntime = timelineRuntimePair.second;
		timelineRuntime.elapsedTime += deltaTime;
		bool isConditionMet = false;

		if (eventComponent->timelineSourceMode == 0) {
			isConditionMet =
				timelineRuntime.elapsedTime >= (std::max)(eventComponent->timelineTriggerValue, 0.0f);
		}
		else if (eventComponent->timelineSourceMode == 1 &&
			eventComponent->timelineSourceGameObjectId >= 0 &&
			railMovementManager_ != nullptr) {
			float normalizedProgress = 0.0f;
			const bool hasProgress = railMovementManager_->GetNormalizedProgress(
				eventComponent->timelineSourceGameObjectId,
				normalizedProgress);
			isConditionMet =
				hasProgress &&
				normalizedProgress >= (std::clamp)(eventComponent->timelineTriggerValue, 0.0f, 1.0f);
		}

		const bool canTrigger =
			isConditionMet &&
			!timelineRuntime.wasConditionMet &&
			(!eventComponent->timelineTriggerOnce || !timelineRuntime.hasTriggered);

		if (canTrigger) {
			QueueAction(
				*eventGameObject,
				eventComponent->timelineTargetGameObjectId,
				eventComponent->timelineActionName,
				eventComponent->timelineTriggerValue);
			timelineRuntime.hasTriggered = true;
		}

		timelineRuntime.wasConditionMet = isConditionMet;
	}
}

void EditorGameplayEventManager::UpdateThresholdStates() {
	for (auto& thresholdRuntimePair : thresholdRuntimes_) {
		EditorGameObject* stateGameObject = editorScene_->FindGameObject(thresholdRuntimePair.first);

		if (stateGameObject == nullptr || !stateGameObject->isActive) {
			continue;
		}

		const EditorComponent* stateComponent = EditorComponentUtility::FindComponent(
			*stateGameObject,
			EditorComponentType::ThresholdState);

		if (stateComponent == nullptr || !stateComponent->isActive) {
			continue;
		}

		float sourceValue = 0.0f;
		bool isDescending = false;

		if (!ReadThresholdValue(*stateGameObject, *stateComponent, sourceValue, isDescending)) {
			continue;
		}

		const float secondThreshold = (std::clamp)(stateComponent->thresholdSecondValue, 0.0f, 1.0f);
		const float thirdThreshold = (std::clamp)(stateComponent->thresholdThirdValue, 0.0f, 1.0f);
		int32_t nextState = 0;

		if (isDescending) {
			const float upperThreshold = (std::max)(secondThreshold, thirdThreshold);
			const float lowerThreshold = (std::min)(secondThreshold, thirdThreshold);

			if (sourceValue <= lowerThreshold) {
				nextState = 2;
			}
			else if (sourceValue <= upperThreshold) {
				nextState = 1;
			}
		}
		else {
			const float lowerThreshold = (std::min)(secondThreshold, thirdThreshold);
			const float upperThreshold = (std::max)(secondThreshold, thirdThreshold);

			if (sourceValue >= upperThreshold) {
				nextState = 2;
			}
			else if (sourceValue >= lowerThreshold) {
				nextState = 1;
			}
		}

		ThresholdRuntime& thresholdRuntime = thresholdRuntimePair.second;

		if (thresholdRuntime.currentState == nextState) {
			continue;
		}

		thresholdRuntime.currentState = nextState;
		const std::string& actionName = nextState == 0
			? stateComponent->thresholdFirstActionName
			: (nextState == 1
				? stateComponent->thresholdSecondActionName
				: stateComponent->thresholdThirdActionName);
		QueueAction(
			*stateGameObject,
			stateComponent->thresholdTargetGameObjectId,
			actionName,
			static_cast<float>(nextState + 1));
	}
}

bool EditorGameplayEventManager::ReadThresholdValue(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	float& value,
	bool& isDescending) const {
	const int32_t sourceGameObjectId = component.thresholdSourceGameObjectId >= 0
		? component.thresholdSourceGameObjectId
		: ownerGameObject.id;
	const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(sourceGameObjectId);

	if (sourceGameObject == nullptr) {
		return false;
	}

	if (component.thresholdSourceMode == 0) {
		const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::Health);

		if (healthComponent == nullptr || healthComponent->healthMaximum <= 0.0f) {
			return false;
		}

		value = (std::clamp)(
			healthComponent->healthCurrent / healthComponent->healthMaximum,
			0.0f,
			1.0f);
		isDescending = true;
		return true;
	}

	if (component.thresholdSourceMode == 1 && railMovementManager_ != nullptr) {
		isDescending = false;
		return railMovementManager_->GetNormalizedProgress(sourceGameObjectId, value);
	}

	return false;
}

void EditorGameplayEventManager::QueueAction(
	const EditorGameObject& ownerGameObject,
	int32_t targetGameObjectId,
	const std::string& actionName,
	float value) const {
	if (scriptManager_ == nullptr || actionName.empty()) {
		return;
	}

	const int32_t resolvedTargetGameObjectId = targetGameObjectId >= 0
		? targetGameObjectId
		: ownerGameObject.id;
	scriptManager_->QueueActionEvent(
		resolvedTargetGameObjectId,
		actionName,
		EditorScriptInputValueTypeButton,
		value,
		EditorScriptVector2{});
}
