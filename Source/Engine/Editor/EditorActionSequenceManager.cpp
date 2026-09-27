#include "EditorActionSequenceManager.h"

#include "EditorComponentUtility.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"

#include <algorithm>
#include <utility>

void EditorActionSequenceManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	scriptManager_ = scriptManager;
}

void EditorActionSequenceManager::Start() {
	if (editorScene_ == nullptr) {
		return;
	}

	std::unordered_map<int32_t, SequenceRuntime> previousRuntimes =
		std::move(sequenceRuntimes_);
	sequenceRuntimes_.clear();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* sequenceComponent = FindSequenceComponent(gameObject.id);

		if (!gameObject.isActive || sequenceComponent == nullptr || !sequenceComponent->isActive) {
			continue;
		}

		SequenceRuntime sequenceRuntime{};
		const auto previousIterator = previousRuntimes.find(gameObject.id);
		const bool restoresRuntime = previousIterator != previousRuntimes.end();

		if (restoresRuntime) {
			sequenceRuntime = std::move(previousIterator->second);
			sequenceRuntime.stepGameObjectIds.clear();
		}

		for (const int32_t childGameObjectId : gameObject.children) {
			if (FindStepComponent(childGameObjectId) != nullptr) {
				sequenceRuntime.stepGameObjectIds.push_back(childGameObjectId);
			}
		}

		sequenceRuntimes_[gameObject.id] = sequenceRuntime;

		if (!restoresRuntime && sequenceComponent->actionSequencePlayOnStart) {
			Play(gameObject.id);
		}
	}
}

void EditorActionSequenceManager::Update(float deltaTime) {
	for (auto& [sequenceGameObjectId, sequenceRuntime] : sequenceRuntimes_) {
		if (!sequenceRuntime.isPlaying || sequenceRuntime.isPaused) {
			continue;
		}

		const EditorGameObject* sequenceGameObject = editorScene_ != nullptr
			? editorScene_->FindGameObject(sequenceGameObjectId)
			: nullptr;
		const EditorComponent* sequenceComponent = FindSequenceComponent(sequenceGameObjectId);
		if (sequenceGameObject == nullptr || !sequenceGameObject->isActive ||
			sequenceComponent == nullptr || !sequenceComponent->isActive) {
			continue;
		}

		if (sequenceRuntime.activeSteps.empty() &&
			!StartNextBatch(sequenceGameObjectId, sequenceRuntime)) {
			continue;
		}

		bool areAllStepsCompleted = true;
		for (ActiveStep& activeStep : sequenceRuntime.activeSteps) {
			if (!UpdateActiveStep(sequenceRuntime, activeStep, deltaTime)) {
				areAllStepsCompleted = false;
			}
		}

		if (areAllStepsCompleted) {
			sequenceRuntime.activeSteps.clear();
		}
	}
}

void EditorActionSequenceManager::Stop() {
	// Additive Scene反映では同じSequenceを継続するため、Startまで状態を保持する。
}

void EditorActionSequenceManager::ResetSessionState() {
	sequenceRuntimes_.clear();
}

bool EditorActionSequenceManager::Play(int32_t sequenceGameObjectId) {
	auto sequenceIterator = sequenceRuntimes_.find(sequenceGameObjectId);
	if (sequenceIterator == sequenceRuntimes_.end()) {
		return false;
	}

	SequenceRuntime& sequenceRuntime = sequenceIterator->second;
	sequenceRuntime.activeSteps.clear();
	sequenceRuntime.receivedSignals.clear();
	sequenceRuntime.nextStepIndex = 0;
	sequenceRuntime.isPlaying = true;
	sequenceRuntime.isPaused = false;
	return true;
}

bool EditorActionSequenceManager::Pause(int32_t sequenceGameObjectId, bool isPaused) {
	auto sequenceIterator = sequenceRuntimes_.find(sequenceGameObjectId);
	if (sequenceIterator == sequenceRuntimes_.end() || !sequenceIterator->second.isPlaying) {
		return false;
	}

	sequenceIterator->second.isPaused = isPaused;
	return true;
}

bool EditorActionSequenceManager::StopSequence(int32_t sequenceGameObjectId) {
	auto sequenceIterator = sequenceRuntimes_.find(sequenceGameObjectId);
	if (sequenceIterator == sequenceRuntimes_.end()) {
		return false;
	}

	sequenceIterator->second.activeSteps.clear();
	sequenceIterator->second.receivedSignals.clear();
	sequenceIterator->second.nextStepIndex = 0;
	sequenceIterator->second.isPlaying = false;
	sequenceIterator->second.isPaused = false;
	return true;
}

bool EditorActionSequenceManager::Signal(
	int32_t sequenceGameObjectId,
	const std::string& signalName) {
	auto sequenceIterator = sequenceRuntimes_.find(sequenceGameObjectId);
	if (sequenceIterator == sequenceRuntimes_.end() || signalName.empty()) {
		return false;
	}

	sequenceIterator->second.receivedSignals.insert(signalName);
	return true;
}

bool EditorActionSequenceManager::IsPlaying(int32_t sequenceGameObjectId) const {
	const auto sequenceIterator = sequenceRuntimes_.find(sequenceGameObjectId);
	return sequenceIterator != sequenceRuntimes_.end() && sequenceIterator->second.isPlaying;
}

bool EditorActionSequenceManager::StartNextBatch(
	int32_t sequenceGameObjectId,
	SequenceRuntime& sequenceRuntime) {
	const EditorComponent* sequenceComponent = FindSequenceComponent(sequenceGameObjectId);
	if (sequenceComponent == nullptr || sequenceRuntime.stepGameObjectIds.empty()) {
		sequenceRuntime.isPlaying = false;
		return false;
	}

	if (sequenceRuntime.nextStepIndex >= static_cast<int32_t>(sequenceRuntime.stepGameObjectIds.size())) {
		if (!sequenceComponent->actionSequenceLoop) {
			sequenceRuntime.isPlaying = false;
			return false;
		}

		sequenceRuntime.nextStepIndex = 0;
	}

	const int32_t firstStepIndex = sequenceRuntime.nextStepIndex;
	const EditorComponent* firstStepComponent = FindStepComponent(
		sequenceRuntime.stepGameObjectIds[static_cast<size_t>(firstStepIndex)]);
	if (firstStepComponent == nullptr) {
		sequenceRuntime.nextStepIndex++;
		return true;
	}

	const int32_t parallelGroup = firstStepComponent->actionSequenceParallelGroup;
	const int32_t lastStepIndex = parallelGroup < 0
		? firstStepIndex + 1
		: static_cast<int32_t>(sequenceRuntime.stepGameObjectIds.size());

	for (int32_t stepIndex = firstStepIndex; stepIndex < lastStepIndex; ++stepIndex) {
		const EditorComponent* stepComponent = FindStepComponent(
			sequenceRuntime.stepGameObjectIds[static_cast<size_t>(stepIndex)]);

		if (stepComponent == nullptr) {
			continue;
		}

		if (parallelGroup >= 0 && stepComponent->actionSequenceParallelGroup != parallelGroup) {
			break;
		}

		ActiveStep activeStep{};
		activeStep.stepIndex = stepIndex;
		StartStep(sequenceGameObjectId, sequenceRuntime, stepIndex, activeStep);
		sequenceRuntime.activeSteps.push_back(activeStep);
		sequenceRuntime.nextStepIndex = stepIndex + 1;

		// 分岐は次のIndexを書き換えるため、並列Groupへ混ぜず単独で完了させる。
		if (stepComponent->actionSequenceStepType == 4) {
			break;
		}
	}

	return !sequenceRuntime.activeSteps.empty();
}

bool EditorActionSequenceManager::StartStep(
	int32_t sequenceGameObjectId,
	SequenceRuntime& sequenceRuntime,
	int32_t stepIndex,
	ActiveStep& activeStep) {
	const int32_t stepGameObjectId =
		sequenceRuntime.stepGameObjectIds[static_cast<size_t>(stepIndex)];
	const EditorComponent* stepComponent = FindStepComponent(stepGameObjectId);

	if (stepComponent == nullptr || editorScene_ == nullptr) {
		return false;
	}

	const int32_t targetGameObjectId = stepComponent->actionSequenceTargetGameObjectId >= 0
		? stepComponent->actionSequenceTargetGameObjectId
		: sequenceGameObjectId;

	switch (stepComponent->actionSequenceStepType) {
	case 0:
		if (scriptManager_ != nullptr) {
			scriptManager_->QueueActionEvent(
				targetGameObjectId,
				stepComponent->actionSequenceActionName);
		}
		break;
	case 1:
		activeStep.remainingSeconds =
			(std::max)(stepComponent->actionSequenceWaitSeconds, 0.0f);
		break;
	case 2:
		if (scriptManager_ != nullptr) {
			scriptManager_->SetGameObjectActive(
				targetGameObjectId,
				stepComponent->actionSequenceActiveValue);
		}
		break;
	case 3:
		if (scriptManager_ != nullptr) {
			activeStep.isWaitingForScene = scriptManager_->RequestSceneLoadAsync(
				stepComponent->actionSequenceScenePath,
				stepComponent->actionSequenceSceneAdditive);
			activeStep.scenePath = stepComponent->actionSequenceScenePath;
		}
		break;
	case 4: {
		const bool conditionResult = EvaluateCondition(*stepComponent, sequenceGameObjectId);
		const int32_t branchStepIndex = conditionResult
			? stepComponent->actionSequenceTrueStepIndex
			: stepComponent->actionSequenceFalseStepIndex;

		if (branchStepIndex >= 0 &&
			branchStepIndex < static_cast<int32_t>(sequenceRuntime.stepGameObjectIds.size())) {
			sequenceRuntime.nextStepIndex = branchStepIndex;
		}
		break;
	}
	case 5:
		activeStep.isWaitingForSignal = true;
		activeStep.signalName = stepComponent->actionSequenceActionName;
		break;
	default:
		break;
	}

	return true;
}

bool EditorActionSequenceManager::UpdateActiveStep(
	SequenceRuntime& sequenceRuntime,
	ActiveStep& activeStep,
	float deltaTime) {
	if (activeStep.isWaitingForScene) {
		if (scriptManager_ != nullptr && scriptManager_->IsSceneRuntimeLoading()) {
			return false;
		}

		activeStep.isWaitingForScene = false;
		return true;
	}

	if (activeStep.isWaitingForSignal) {
		const auto signalIterator = sequenceRuntime.receivedSignals.find(activeStep.signalName);

		if (signalIterator == sequenceRuntime.receivedSignals.end()) {
			return false;
		}

		sequenceRuntime.receivedSignals.erase(signalIterator);
		activeStep.isWaitingForSignal = false;
		return true;
	}

	if (activeStep.remainingSeconds > 0.0f) {
		activeStep.remainingSeconds -= (std::max)(deltaTime, 0.0f);
		return activeStep.remainingSeconds <= 0.0f;
	}

	return true;
}

bool EditorActionSequenceManager::EvaluateCondition(
	const EditorComponent& stepComponent,
	int32_t ownerGameObjectId) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const int32_t sourceGameObjectId = stepComponent.actionSequenceTargetGameObjectId >= 0
		? stepComponent.actionSequenceTargetGameObjectId
		: ownerGameObjectId;
	const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(sourceGameObjectId);
	float sourceValue = 0.0f;

	if (stepComponent.actionSequenceConditionMode == 0) {
		sourceValue = sourceGameObject != nullptr && sourceGameObject->isActive ? 1.0f : 0.0f;
	}
	else if (stepComponent.actionSequenceConditionMode == 1 && sourceGameObject != nullptr) {
		const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::Health);
		if (healthComponent != nullptr && healthComponent->healthMaximum > 0.0f) {
			sourceValue = healthComponent->healthCurrent / healthComponent->healthMaximum;
		}
	}
	else if (stepComponent.actionSequenceConditionMode == 2 && railMovementManager_ != nullptr) {
		railMovementManager_->GetNormalizedProgress(sourceGameObjectId, sourceValue);
	}

	switch (stepComponent.actionSequenceCompareMode) {
	case 1:
		return sourceValue <= stepComponent.actionSequenceCompareValue;
	case 2:
		return sourceValue > stepComponent.actionSequenceCompareValue;
	case 3:
		return sourceValue < stepComponent.actionSequenceCompareValue;
	case 0:
	default:
		return sourceValue >= stepComponent.actionSequenceCompareValue;
	}
}

const EditorComponent* EditorActionSequenceManager::FindSequenceComponent(int32_t gameObjectId) const {
	const EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	return gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::ActionSequence)
		: nullptr;
}

const EditorComponent* EditorActionSequenceManager::FindStepComponent(int32_t gameObjectId) const {
	const EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	return gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::ActionSequenceStep)
		: nullptr;
}
