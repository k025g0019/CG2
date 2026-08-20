#include "EditorRuntimePropertyManager.h"

#include "EditorComponentUtility.h"
#include "EditorDamageManager.h"
#include "EditorAudioManager.h"
#include "EditorInputManager.h"
#include "EditorOceanSystem.h"
#include "EditorScriptManager.h"
#include "EditorTargetingManager.h"
#include "EditorWeaponManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
	struct ComponentNameEntry {
		const char* name;
		EditorComponentType type;
	};

	constexpr ComponentNameEntry kRuntimeComponentNames[] = {
		{"Ocean", EditorComponentType::Ocean},
		{"Light", EditorComponentType::Light},
		{"Camera", EditorComponentType::Camera},
		{"PostProcess", EditorComponentType::PostProcess},
		{"AudioSource", EditorComponentType::AudioSource},
		{"ParticleSystem", EditorComponentType::ParticleSystem},
		{"VisualEffect", EditorComponentType::VisualEffect},
		{"RailMovement", EditorComponentType::RailMovement},
		{"Health", EditorComponentType::Health},
		{"WeaponLoadout", EditorComponentType::WeaponLoadout},
		{"TargetSteering", EditorComponentType::TargetSteering},
		{"TargetPoint", EditorComponentType::TargetPoint},
		{"Team", EditorComponentType::Team},
		{"TargetSelector", EditorComponentType::TargetSelector},
		{"MovementModifier", EditorComponentType::MovementModifier},
		{"Timer", EditorComponentType::Timer},
		{"GenericStateMachine", EditorComponentType::GenericStateMachine},
		{"Attribute", EditorComponentType::Attribute},
		{"DestructiblePart", EditorComponentType::DestructiblePart},
		{"FormationFollower", EditorComponentType::FormationFollower},
		{"TargetLock", EditorComponentType::TargetLock},
		{"MultiTargetLock", EditorComponentType::MultiTargetLock},
		{"GenericCounter", EditorComponentType::GenericCounter},
		{"GenericCondition", EditorComponentType::GenericCondition},
		{"GameplayData", EditorComponentType::GameplayData},
		{"WeaponAccuracy", EditorComponentType::WeaponAccuracy},
		{"TimeScale", EditorComponentType::TimeScale}};

	EditorScriptActionPayload MakeGameObjectPayload(int32_t gameObjectId) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeGameObject;
		payload.gameObjectId = gameObjectId;
		return payload;
	}

	EditorScriptActionPayload MakeFloatPayload(float value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeFloat;
		payload.floatValue = value;
		return payload;
	}

	EditorScriptActionPayload MakeStringPayload(const std::string& value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeString;
		const size_t copyLength = (std::min)(value.size(), sizeof(payload.stringValue) - 1u);
		std::copy_n(value.data(), copyLength, payload.stringValue);
		payload.stringValue[copyLength] = '\0';
		return payload;
	}

	EditorScriptActionPayload MakeBoolPayload(bool value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeBool;
		payload.boolValue = value;
		return payload;
	}

	Vector3 TransformDirection(const Vector3& direction, const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = Multiply(
			Multiply(MakeRotateXMatrix(rotation.x), MakeRotateYMatrix(rotation.y)),
			MakeRotateZMatrix(rotation.z));
		return {
			direction.x * rotationMatrix.matrix[0][0] +
				direction.y * rotationMatrix.matrix[1][0] +
				direction.z * rotationMatrix.matrix[2][0],
			direction.x * rotationMatrix.matrix[0][1] +
				direction.y * rotationMatrix.matrix[1][1] +
				direction.z * rotationMatrix.matrix[2][1],
			direction.x * rotationMatrix.matrix[0][2] +
				direction.y * rotationMatrix.matrix[1][2] +
				direction.z * rotationMatrix.matrix[2][2]};
	}

	Vector3 AddRuntimeVector(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 MultiplyRuntimeVector(float scalar, const Vector3& value) {
		return {scalar * value.x, scalar * value.y, scalar * value.z};
	}

	float DotRuntimeVector(const Vector3& firstValue, const Vector3& secondValue) {
		return firstValue.x * secondValue.x +
			firstValue.y * secondValue.y +
			firstValue.z * secondValue.z;
	}

	Vector3 NormalizeRuntimeVector(const Vector3& value) {
		const float vectorLength = std::sqrt(DotRuntimeVector(value, value));

		if (vectorLength <= 0.0001f) {
			return {0.0f, 0.0f, 1.0f};
		}

		return MultiplyRuntimeVector(1.0f / vectorLength, value);
	}

	bool CompareNumber(float leftValue, float rightValue, int32_t compareMode) {
		constexpr float equalityTolerance = 0.0001f;

		switch (compareMode) {
		case 0: return leftValue >= rightValue;
		case 1: return leftValue <= rightValue;
		case 2: return std::fabs(leftValue - rightValue) <= equalityTolerance;
		case 3: return leftValue > rightValue;
		case 4: return leftValue < rightValue;
		case 5: return std::fabs(leftValue - rightValue) > equalityTolerance;
		default: return false;
		}
	}

	bool LoadGameplayDataAsset(
		const std::string& assetPath,
		std::vector<EditorGameplayDataEntry>& entries) {
		if (assetPath.empty()) {
			return false;
		}

		std::ifstream file(assetPath, std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		std::vector<EditorGameplayDataEntry> loadedEntries;
		std::string line;

		while (std::getline(file, line)) {
			if (line.size() >= 3u && static_cast<unsigned char>(line[0]) == 0xEFu &&
				static_cast<unsigned char>(line[1]) == 0xBBu && static_cast<unsigned char>(line[2]) == 0xBFu) {
				line.erase(0u, 3u);
			}

			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			if (line.empty() || line[0] == '#') {
				continue;
			}

			std::stringstream lineStream(line);
			std::string recordType;
			std::string key;
			std::string typeText;
			std::string value;
			std::getline(lineStream, recordType, '|');
			std::getline(lineStream, key, '|');
			std::getline(lineStream, typeText, '|');
			std::getline(lineStream, value);

			if (recordType != "Entry" || key.empty()) {
				continue;
			}

			int32_t valueType = 0;

			try {
				valueType = std::stoi(typeText);
			}
			catch (...) {
				valueType = 0;
			}

			loadedEntries.push_back(EditorGameplayDataEntry{key, (std::clamp)(valueType, 0, 4), value});
		}

		if (loadedEntries.empty()) {
			return false;
		}

		entries = std::move(loadedEntries);
		return true;
	}
}

void EditorRuntimePropertyManager::Initialize(
	EditorScene* editorScene,
	EditorScriptManager* scriptManager,
	EditorTargetingManager* targetingManager,
	EditorWeaponManager* weaponManager,
	EditorDamageManager* damageManager,
	EditorInputManager* inputManager,
	EditorAudioManager* audioManager,
	EditorEffectManager* effectManager) {
	editorScene_ = editorScene;
	scriptManager_ = scriptManager;
	targetingManager_ = targetingManager;
	weaponManager_ = weaponManager;
	damageManager_ = damageManager;
	inputManager_ = inputManager;
	audioManager_ = audioManager;
	effectManager_ = effectManager;
}

void EditorRuntimePropertyManager::Start() {
	tweenRuntimes_.clear();
	targetLockRuntimes_.clear();
	previousAttributeValues_.clear();
	initialAttributeValues_.clear();
	previousNamedAttributeValues_.clear();
	initialNamedAttributeValues_.clear();
	multiTargetLostSeconds_.clear();
	previousThreatGameObjectIds_.clear();
	damageIndicatorSequences_.clear();
	surfaceWakePreviousPositions_.clear();
	surfaceWakeActiveStates_.clear();
	waterSurfaceUnderwaterStates_.clear();
	timeScaleRuntime_ = {};
	timeScaleRuntime_.currentScale = 1.0f;
	gamePauseOwnerGameObjectId_ = -1;

	if (inputManager_ != nullptr) {
		inputManager_->SetPauseInputMaps(false, "Gameplay", "UI");
	}

	if (audioManager_ != nullptr) {
		audioManager_->SetPaused(false);
	}

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorGameObject* mutableGameObject = editorScene_->FindGameObject(gameObject.id);
		if (mutableGameObject == nullptr) {
			continue;
		}

		if (effectManager_ != nullptr) {
			const EditorComponent* surfaceWake = EditorComponentUtility::FindComponent(
				*mutableGameObject,
				EditorComponentType::SurfaceWakeEmitter);

			if (surfaceWake != nullptr) {
				effectManager_->StopEffect(surfaceWake->surfaceWakeLeftEffectGameObjectId);
				effectManager_->StopEffect(surfaceWake->surfaceWakeRightEffectGameObjectId);
				effectManager_->StopEffect(surfaceWake->surfaceWakeBowEffectGameObjectId);
			}
		}

		if (EditorComponent* gamePause = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::GamePause)) {
			gamePause->gamePausePaused = false;
		}

		if (EditorComponent* statusEffectSet = EditorComponentUtility::FindComponent(
			*mutableGameObject,
			EditorComponentType::StatusEffectSet)) {
			statusEffectSet->statusEffectRuntimeEntries.clear();
		}

		if (EditorComponent* surfaceWake = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::SurfaceWakeEmitter)) {
			Vector3 worldScale{};
			Vector3 worldRotation{};
			Vector3 worldPosition{};
			editorScene_->GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition);
			surfaceWakePreviousPositions_[gameObject.id] = worldPosition;
			surfaceWakeActiveStates_[gameObject.id] = false;
			surfaceWake->surfaceWakeCurrentSpeed = 0.0f;
			surfaceWake->surfaceWakeCurrentIntensity = 0.0f;
		}

		if (EditorComponent* waterSurface = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::WaterSurfaceState)) {
			Vector3 worldScale{};
			Vector3 worldRotation{};
			Vector3 worldPosition{};
			editorScene_->GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition);
			(void)worldScale;
			const Vector3 queryPosition = AddRuntimeVector(
				worldPosition,
				TransformDirection(waterSurface->waterSurfaceLocalOffset, worldRotation));
			EditorOceanSurfaceSample oceanSample{};
			const uint64_t sampleKey =
				static_cast<uint64_t>(static_cast<uint32_t>(gameObject.id)) ^ 0x57535441ull;
			const bool hasSample = SampleEditorOceanSurface(
				*editorScene_,
				waterSurface->waterSurfaceOceanGameObjectId,
				queryPosition,
				sampleKey,
				GetEditorOceanElapsedTime(),
				oceanSample);

			if (hasSample) {
				const Vector3 surfaceOffset = {
					queryPosition.x - oceanSample.position.x,
					queryPosition.y - oceanSample.position.y,
					queryPosition.z - oceanSample.position.z};
				waterSurface->waterSurfaceSignedDistance = DotRuntimeVector(
					surfaceOffset,
					oceanSample.normal) - waterSurface->waterSurfaceClearance;
				waterSurface->waterSurfaceCurrentOceanGameObjectId = oceanSample.oceanGameObjectId;
				waterSurface->waterSurfacePosition = oceanSample.position;
				waterSurface->waterSurfaceNormal = oceanSample.normal;
				waterSurface->waterSurfaceVelocity = oceanSample.velocity;
				waterSurface->waterSurfaceFoam = oceanSample.foam;
				const bool isUnderwater = waterSurface->waterSurfaceSignedDistance <= 0.0f;
				waterSurface->waterSurfaceState = isUnderwater ? 2 : 0;
				waterSurfaceUnderwaterStates_[gameObject.id] = isUnderwater;
			}
			else {
				waterSurface->waterSurfaceState = 0;
				waterSurface->waterSurfaceSignedDistance = 0.0f;
				waterSurface->waterSurfaceCurrentOceanGameObjectId = -1;
				waterSurface->waterSurfaceFoam = 0.0f;
				waterSurfaceUnderwaterStates_[gameObject.id] = false;
			}
		}

		if (EditorComponent* timer = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::Timer)) {
			timer->timerRemaining = (std::max)(timer->timerDuration, 0.001f);
			timer->timerPaused = !timer->timerPlayOnStart;
		}

		if (EditorComponent* state = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::GenericStateMachine)) {
			state->stateMachineCurrentState = state->stateMachineInitialState;
		}

		if (EditorComponent* attribute = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::Attribute)) {
			attribute->attributeMaximum = (std::max)(attribute->attributeMaximum, attribute->attributeMinimum);
			attribute->attributeCurrent = (std::clamp)(attribute->attributeCurrent, attribute->attributeMinimum, attribute->attributeMaximum);
			previousAttributeValues_[gameObject.id] = attribute->attributeCurrent;
			initialAttributeValues_[gameObject.id] = attribute->attributeCurrent;
		}

		if (EditorComponent* attributeSet = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::AttributeSet)) {
			for (EditorNamedAttributeEntry& entry : attributeSet->attributeSetEntries) {
				entry.maximum = (std::max)(entry.maximum, entry.minimum);
				entry.current = (std::clamp)(entry.current, entry.minimum, entry.maximum);
				previousNamedAttributeValues_[gameObject.id][entry.name] = entry.current;
				initialNamedAttributeValues_[gameObject.id][entry.name] = entry.current;
			}
		}

		if (EditorComponent* counter = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::GenericCounter)) {
			counter->counterMaximumValue = (std::max)(counter->counterMaximumValue, counter->counterMinimumValue);
			counter->counterCurrentValue = (std::clamp)(
				counter->counterInitialValue,
				counter->counterMinimumValue,
				counter->counterMaximumValue);
			counter->counterWasSatisfied = CompareNumber(
				counter->counterCurrentValue,
				counter->counterThresholdValue,
				counter->counterCompareMode);
		}

		if (EditorComponent* part = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::DestructiblePart)) {
			part->destructibleDestroyed = false;
		}

		if (EditorComponent* lock = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::TargetLock)) {
			lock->targetLockProgress = 0.0f;
			lock->targetLockLocked = false;
			lock->targetLockCurrentGameObjectId = -1;
			targetLockRuntimes_[gameObject.id] = {};
		}

		if (EditorComponent* multiLock = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::MultiTargetLock)) {
			multiLock->multiTargetLockTargetGameObjectIds.clear();
			multiLock->multiTargetLockProgressValues.clear();
			multiLock->multiTargetLockCompletedValues.clear();
		}

		if (EditorComponent* gameplayData = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::GameplayData)) {
			LoadGameplayDataAsset(gameplayData->gameplayDataAssetPath, gameplayData->gameplayDataEntries);
		}

		if (EditorComponent* cooldownSet = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::CooldownSet)) {
			for (EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
				entry.duration = (std::max)(entry.duration, 0.0f);
				entry.remaining = entry.startReady ? 0.0f : entry.duration;
				entry.wasRunning = entry.remaining > 0.0f;
			}
		}

		if (EditorComponent* threatTracker = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::ThreatTracker)) {
			threatTracker->threatTrackerEntries.clear();
		}

		if (EditorComponent* indicator = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::DamageDirectionIndicator)) {
			indicator->damageDirectionRemaining = 0.0f;
			indicator->damageDirectionSourceGameObjectId = -1;
		}

		if (EditorComponent* difficulty = EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::DifficultyParameterSet)) {
			if (difficulty->isActive && difficulty->difficultyApplyOnStart) ApplyDifficulty(gameObject.id, difficulty->difficultySelectedIndex);
		}
		const EditorComponent* tweenComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::PropertyTween);

		if (gameObject.isActive && tweenComponent != nullptr && tweenComponent->isActive) {
			tweenRuntimes_[gameObject.id] = {0.0f, tweenComponent->propertyTweenPlayOnStart};
		}

		const EditorComponent* relayComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ActionRelay);

		if (gameObject.isActive && relayComponent != nullptr && relayComponent->isActive &&
			relayComponent->actionRelayOnStart) {
			Relay(gameObject.id);
		}

		const EditorComponent* timeScale = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::TimeScale);

		if (!timeScaleRuntime_.isPlaying && gameObject.isActive && timeScale != nullptr &&
			timeScale->isActive && timeScale->timeScalePlayOnStart) {
			PlayTimeScale(gameObject.id);
		}
	}
}

float EditorRuntimePropertyManager::UpdateTimeScale(float unscaledDeltaTime) {
	const bool pausesGameTime = IsGamePaused() && gamePauseOwnerGameObjectId_ >= 0 &&
		FindTypedComponent(gamePauseOwnerGameObjectId_, EditorComponentType::GamePause) != nullptr &&
		FindTypedComponent(gamePauseOwnerGameObjectId_, EditorComponentType::GamePause)->gamePausePauseGameTime;

	if (!timeScaleRuntime_.isPlaying || unscaledDeltaTime < 0.0f) {
		return pausesGameTime ? 0.0f : timeScaleRuntime_.currentScale;
	}

	timeScaleRuntime_.elapsedSeconds += unscaledDeltaTime;
	const float blendRatio = timeScaleRuntime_.blendSeconds > 0.0f
		? (std::clamp)(timeScaleRuntime_.elapsedSeconds / timeScaleRuntime_.blendSeconds, 0.0f, 1.0f)
		: 1.0f;
	const float smoothBlendRatio = blendRatio * blendRatio * (3.0f - 2.0f * blendRatio);
	timeScaleRuntime_.currentScale = timeScaleRuntime_.startScale +
		(timeScaleRuntime_.targetScale - timeScaleRuntime_.startScale) * smoothBlendRatio;

	if (timeScaleRuntime_.elapsedSeconds < timeScaleRuntime_.durationSeconds) {
		return pausesGameTime ? 0.0f : timeScaleRuntime_.currentScale;
	}

	const int32_t ownerGameObjectId = timeScaleRuntime_.ownerGameObjectId;
	const EditorGameObject* owner = editorScene_ != nullptr
		? editorScene_->FindGameObject(ownerGameObjectId)
		: nullptr;
	const EditorComponent* timeScale = owner != nullptr
		? EditorComponentUtility::FindComponent(*owner, EditorComponentType::TimeScale)
		: nullptr;
	timeScaleRuntime_ = {};
	timeScaleRuntime_.currentScale = 1.0f;

	if (scriptManager_ != nullptr && timeScale != nullptr && !timeScale->timeScaleCompletedActionName.empty()) {
		scriptManager_->QueueActionPayload(
			timeScale->timeScaleActionTargetGameObjectId >= 0
				? timeScale->timeScaleActionTargetGameObjectId
				: ownerGameObjectId,
			timeScale->timeScaleCompletedActionName,
			MakeFloatPayload(1.0f));
	}

	return pausesGameTime ? 0.0f : 1.0f;
}

bool EditorRuntimePropertyManager::PlayTimeScale(
	int32_t gameObjectId,
	float scaleOverride,
	float durationOverride) {
	EditorComponent* timeScale = FindTypedComponent(gameObjectId, EditorComponentType::TimeScale);

	if (timeScale == nullptr || !timeScale->isActive) {
		return false;
	}

	TimeScaleRuntime runtime{};
	runtime.ownerGameObjectId = gameObjectId;
	runtime.startScale = timeScaleRuntime_.currentScale;
	runtime.targetScale = (std::clamp)(
		scaleOverride >= 0.0f ? scaleOverride : timeScale->timeScaleValue,
		0.0f,
		4.0f);
	runtime.currentScale = runtime.startScale;
	runtime.durationSeconds = (std::max)(
		durationOverride >= 0.0f ? durationOverride : timeScale->timeScaleDuration,
		0.001f);
	runtime.blendSeconds = (std::clamp)(timeScale->timeScaleBlendSeconds, 0.0f, runtime.durationSeconds);
	runtime.isPlaying = true;
	timeScaleRuntime_ = runtime;
	return true;
}

float EditorRuntimePropertyManager::GetTimeScale() const {
	return timeScaleRuntime_.currentScale;
}

void EditorRuntimePropertyManager::Update(float deltaTime) {
	if (deltaTime <= 0.0f) {
		return;
	}

	UpdateSurfaceWakes(deltaTime);
	UpdateOceanGameplayQueries();

	if (editorScene_ != nullptr) {
		float cameraYaw = 0.0f;
		int32_t selectedCameraPriority = INT32_MIN;

		for (const EditorGameObject& cameraGameObject : editorScene_->GetGameObjects()) {
			const EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
				cameraGameObject,
				EditorComponentType::Camera);

			if (!cameraGameObject.isActive || cameraComponent == nullptr || !cameraComponent->isActive ||
				cameraComponent->cameraPriority <= selectedCameraPriority) {
				continue;
			}

			Vector3 cameraScale{};
			Vector3 cameraRotation{};
			Vector3 cameraPosition{};
			editorScene_->GetWorldTransform(
				cameraGameObject.id,
				cameraScale,
				cameraRotation,
				cameraPosition);
			cameraYaw = cameraRotation.y;
			selectedCameraPriority = cameraComponent->cameraPriority;
		}

		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			EditorComponent* indicator = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::DamageDirectionIndicator);
			if (indicator != nullptr && indicator->isActive && damageManager_ != nullptr) {
				const uint64_t sequence = damageManager_->GetDamageSequence(gameObject.id);
				if (sequence > damageIndicatorSequences_[gameObject.id]) {
					damageIndicatorSequences_[gameObject.id] = sequence;
					EditorScriptDamageContext damage{};
					if (damageManager_->GetLastDamageContext(gameObject.id, damage) && damage.baseDamage >= indicator->damageDirectionMinimumDamage) {
						const EditorGameObject* source = editorScene_->FindGameObject(damage.sourceGameObjectId);
						if (source != nullptr) {
							Vector3 ownerScale{}, ownerRotation{}, ownerPosition{}, sourceScale{}, sourceRotation{}, sourcePosition{};
							editorScene_->GetWorldTransform(gameObject.id, ownerScale, ownerRotation, ownerPosition);
							editorScene_->GetWorldTransform(source->id, sourceScale, sourceRotation, sourcePosition);
							const float worldDirectionX = sourcePosition.x - ownerPosition.x;
							const float worldDirectionZ = sourcePosition.z - ownerPosition.z;
							const float cameraRightX = std::cos(cameraYaw);
							const float cameraRightZ = -std::sin(cameraYaw);
							const float cameraForwardX = std::sin(cameraYaw);
							const float cameraForwardZ = std::cos(cameraYaw);
							const float screenDirectionX =
								worldDirectionX * cameraRightX + worldDirectionZ * cameraRightZ;
							const float screenDirectionY = -(
								worldDirectionX * cameraForwardX + worldDirectionZ * cameraForwardZ);
							const float directionLength = std::sqrt(
								screenDirectionX * screenDirectionX + screenDirectionY * screenDirectionY);

							if (directionLength > 0.0001f) {
								indicator->damageDirectionNormalized = {
									screenDirectionX / directionLength,
									screenDirectionY / directionLength};
							}
							indicator->damageDirectionSourceGameObjectId = source->id;
							indicator->damageDirectionRemaining = (std::max)(indicator->damageDirectionDuration, 0.0f);
						}
					}
				}
				indicator->damageDirectionRemaining = (std::max)(indicator->damageDirectionRemaining - deltaTime, 0.0f);
			}

			EditorComponent* objectives = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ObjectiveTracker);
			if (objectives != nullptr && objectives->isActive) {
				for (EditorObjectiveEntry& entry : objectives->objectiveEntries) {
					if (entry.state == 1 && entry.targetValue > 0.0f && entry.currentValue >= entry.targetValue) SetObjective(gameObject.id, entry.objectiveId, 2, entry.currentValue);
				}
			}

			EditorComponent* timer = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Timer);
			if (timer != nullptr && timer->isActive && !timer->timerPaused) {
				timer->timerRemaining -= deltaTime;

				if (timer->timerRemaining <= 0.0f) {
					const int32_t targetId = timer->timerActionTargetGameObjectId >= 0 ? timer->timerActionTargetGameObjectId : gameObject.id;

					if (scriptManager_ != nullptr && !timer->timerActionName.empty()) {
						scriptManager_->QueueActionEvent(targetId, timer->timerActionName);
					}

					timer->timerRemaining = (std::max)(timer->timerDuration, 0.001f);
					timer->timerPaused = !timer->timerRepeat;
				}
			}

			EditorComponent* attribute = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Attribute);
			if (attribute != nullptr && attribute->isActive) {
				attribute->attributeMaximum = (std::max)(attribute->attributeMaximum, attribute->attributeMinimum);
				attribute->attributeCurrent = (std::clamp)(attribute->attributeCurrent + attribute->attributeRegenerationPerSecond * deltaTime, attribute->attributeMinimum, attribute->attributeMaximum);
				float& previous = previousAttributeValues_[gameObject.id];

				if (previous != attribute->attributeCurrent) {
					previous = attribute->attributeCurrent;
					const int32_t targetId = attribute->attributeActionTargetGameObjectId >= 0 ? attribute->attributeActionTargetGameObjectId : gameObject.id;

					if (scriptManager_ != nullptr && !attribute->attributeChangedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							targetId,
							attribute->attributeChangedActionName,
							MakeFloatPayload(attribute->attributeCurrent));
					}
				}
			}

			EditorComponent* attributeSet = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::AttributeSet);
			if (attributeSet != nullptr && attributeSet->isActive) {
				for (EditorNamedAttributeEntry& entry : attributeSet->attributeSetEntries) {
					entry.maximum = (std::max)(entry.maximum, entry.minimum);
					entry.current = (std::clamp)(
						entry.current + entry.regenerationPerSecond * deltaTime,
						entry.minimum,
						entry.maximum);
					float& previousValue = previousNamedAttributeValues_[gameObject.id][entry.name];

					if (previousValue == entry.current) {
						continue;
					}

					previousValue = entry.current;
					const int32_t actionTargetId = attributeSet->attributeSetActionTargetGameObjectId >= 0
						? attributeSet->attributeSetActionTargetGameObjectId
						: gameObject.id;

					if (scriptManager_ != nullptr && !attributeSet->attributeSetChangedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionTargetId,
							attributeSet->attributeSetChangedActionName,
							MakeStringPayload(entry.name));
					}
				}
			}

			EditorComponent* counter = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::GenericCounter);
			if (counter != nullptr && counter->isActive) {
				const bool isSatisfied = CompareNumber(
					counter->counterCurrentValue,
					counter->counterThresholdValue,
					counter->counterCompareMode);
				const bool shouldNotify = isSatisfied && (!counter->counterFireOnce || !counter->counterWasSatisfied);

				if (shouldNotify && scriptManager_ != nullptr && !counter->counterThresholdActionName.empty()) {
					const int32_t actionTargetId = counter->counterActionTargetGameObjectId >= 0
						? counter->counterActionTargetGameObjectId
						: gameObject.id;
					scriptManager_->QueueActionPayload(
						actionTargetId,
						counter->counterThresholdActionName,
						MakeFloatPayload(counter->counterCurrentValue));
				}

				counter->counterWasSatisfied = isSatisfied;
			}

			EditorComponent* condition = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::GenericCondition);
			if (condition != nullptr && condition->isActive && condition->conditionEvaluateEveryFrame) {
				EvaluateConditionComponent(gameObject.id, *condition, true);
			}

			EditorComponent* cooldownSet = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CooldownSet);
			if (cooldownSet != nullptr && cooldownSet->isActive) {
				for (EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
					const bool wasRunning = entry.remaining > 0.0f;
					entry.remaining = (std::max)(entry.remaining - deltaTime, 0.0f);

					if (wasRunning && entry.remaining <= 0.0f && scriptManager_ != nullptr &&
						!cooldownSet->cooldownSetCompletedActionName.empty()) {
						const int32_t actionTargetId = cooldownSet->cooldownSetActionTargetGameObjectId >= 0
							? cooldownSet->cooldownSetActionTargetGameObjectId
							: gameObject.id;
						scriptManager_->QueueActionPayload(
							actionTargetId,
							cooldownSet->cooldownSetCompletedActionName,
							MakeStringPayload(entry.name));
					}

					entry.wasRunning = entry.remaining > 0.0f;
				}
			}

			EditorComponent* statusEffectSet = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::StatusEffectSet);

			if (statusEffectSet != nullptr && statusEffectSet->isActive) {
				for (auto effectIterator = statusEffectSet->statusEffectRuntimeEntries.begin();
					effectIterator != statusEffectSet->statusEffectRuntimeEntries.end();) {
					auto definitionIterator = std::find_if(
						statusEffectSet->statusEffectDefinitions.begin(),
						statusEffectSet->statusEffectDefinitions.end(),
						[&effectIterator](const EditorStatusEffectDefinitionEntry& definition) {
							return definition.effectId == effectIterator->effectId;
						});

					if (definitionIterator == statusEffectSet->statusEffectDefinitions.end()) {
						effectIterator = statusEffectSet->statusEffectRuntimeEntries.erase(effectIterator);
						continue;
					}

					EditorStatusEffectRuntimeEntry& runtimeEntry = *effectIterator;
					const EditorStatusEffectDefinitionEntry& definition = *definitionIterator;
					runtimeEntry.remainingSeconds -= deltaTime;

					if (definition.tickInterval > 0.0f) {
						runtimeEntry.tickRemainingSeconds -= deltaTime;

						if (runtimeEntry.tickRemainingSeconds <= 0.0f &&
							runtimeEntry.remainingSeconds > 0.0f) {
							runtimeEntry.tickRemainingSeconds += (std::max)(definition.tickInterval, 0.001f);

							if (scriptManager_ != nullptr && !definition.tickActionName.empty()) {
								const int32_t actionTargetGameObjectId = statusEffectSet->statusEffectActionTargetGameObjectId >= 0
									? statusEffectSet->statusEffectActionTargetGameObjectId
									: gameObject.id;
								scriptManager_->QueueActionPayload(
									actionTargetGameObjectId,
									definition.tickActionName,
									MakeStringPayload(runtimeEntry.effectId));
							}
						}
					}

					if (runtimeEntry.remainingSeconds > 0.0f) {
						++effectIterator;
						continue;
					}

					if (scriptManager_ != nullptr && !definition.endedActionName.empty()) {
						const int32_t actionTargetGameObjectId = statusEffectSet->statusEffectActionTargetGameObjectId >= 0
							? statusEffectSet->statusEffectActionTargetGameObjectId
							: gameObject.id;
						scriptManager_->QueueActionPayload(
							actionTargetGameObjectId,
							definition.endedActionName,
							MakeStringPayload(runtimeEntry.effectId));
					}

					effectIterator = statusEffectSet->statusEffectRuntimeEntries.erase(effectIterator);
				}
			}

			EditorComponent* threatTracker = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ThreatTracker);
			if (threatTracker != nullptr && threatTracker->isActive && weaponManager_ != nullptr) {
				const int32_t trackedGameObjectId = threatTracker->threatTrackerTargetGameObjectId >= 0
					? threatTracker->threatTrackerTargetGameObjectId
					: gameObject.id;
				std::vector<EditorWeaponManager::ThreatInfo> incomingThreats;
				weaponManager_->GetIncomingThreats(
					trackedGameObjectId,
					(std::max)(threatTracker->threatTrackerMaximumDistance, 0.0f),
					(std::max)(threatTracker->threatTrackerMinimumClosingSpeed, 0.0f),
					(std::max)(threatTracker->threatTrackerMaximumMissDistance, 0.0f),
					(std::clamp)(threatTracker->threatTrackerMaximumCount, 1, 64),
					incomingThreats);
				std::unordered_set<int32_t> currentThreatGameObjectIds;
				threatTracker->threatTrackerEntries.clear();

				for (const EditorWeaponManager::ThreatInfo& threat : incomingThreats) {
					currentThreatGameObjectIds.insert(threat.projectileGameObjectId);
					threatTracker->threatTrackerEntries.push_back(EditorThreatRuntimeEntry{
						threat.projectileGameObjectId,
						threat.sourceGameObjectId,
						threat.distance,
						threat.closingSpeed,
						threat.estimatedArrivalSeconds});
				}

				std::unordered_set<int32_t>& previousThreatIds = previousThreatGameObjectIds_[gameObject.id];
				const int32_t actionTargetId = threatTracker->threatTrackerActionTargetGameObjectId >= 0
					? threatTracker->threatTrackerActionTargetGameObjectId
					: gameObject.id;

				if (scriptManager_ != nullptr) {
					for (const int32_t currentThreatId : currentThreatGameObjectIds) {
						if (!previousThreatIds.contains(currentThreatId) && !threatTracker->threatTrackerAddedActionName.empty()) {
							scriptManager_->QueueActionPayload(
								actionTargetId,
								threatTracker->threatTrackerAddedActionName,
								MakeGameObjectPayload(currentThreatId));
						}
					}

					for (const int32_t previousThreatId : previousThreatIds) {
						if (!currentThreatGameObjectIds.contains(previousThreatId) && !threatTracker->threatTrackerLostActionName.empty()) {
							scriptManager_->QueueActionPayload(
								actionTargetId,
								threatTracker->threatTrackerLostActionName,
								MakeGameObjectPayload(previousThreatId));
						}
					}
				}

				previousThreatIds = std::move(currentThreatGameObjectIds);
			}

			EditorComponent* part = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::DestructiblePart);
			if (part != nullptr && part->isActive && !part->destructibleDestroyed) {
				const int32_t healthId = part->destructibleHealthGameObjectId >= 0 ? part->destructibleHealthGameObjectId : gameObject.id;
				EditorGameObject* healthObject = editorScene_->FindGameObject(healthId);
				EditorComponent* health = healthObject != nullptr ? EditorComponentUtility::FindComponent(*healthObject, EditorComponentType::Health) : nullptr;

				if (health != nullptr && health->healthCurrent <= 0.0f) {
					part->destructibleDestroyed = true;

					if (part->destructibleDisableChildren && scriptManager_ != nullptr) {
						for (const int32_t childId : gameObject.children) {
							scriptManager_->SetGameObjectActive(childId, false);
						}
					}

					std::stringstream componentNames(part->destructibleDisableComponentNames);
					std::string componentName;

					while (std::getline(componentNames, componentName, ';')) {
						if (!componentName.empty()) {
							EditorComponent* disabledComponent = FindComponent(gameObject.id, componentName);

							if (disabledComponent != nullptr) {
								disabledComponent->isActive = false;
							}
						}
					}

					const int32_t targetId = part->destructibleActionTargetGameObjectId >= 0 ? part->destructibleActionTargetGameObjectId : gameObject.id;

					if (scriptManager_ != nullptr && !part->destructibleDestroyedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							targetId,
							part->destructibleDestroyedActionName,
							MakeGameObjectPayload(gameObject.id));
					}
				}
			}

			EditorComponent* formation = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::FormationFollower);
			if (formation != nullptr && formation->isActive) {
				const EditorGameObject* leader = editorScene_->FindGameObject(formation->formationLeaderGameObjectId);

				if (leader != nullptr) {
					const Vector3 worldOffset = TransformDirection(formation->formationLocalOffset, leader->rotate);
					const Vector3 target{
						leader->translate.x + worldOffset.x,
						leader->translate.y + worldOffset.y,
						leader->translate.z + worldOffset.z};
					const float positionRate = formation->formationPositionSpeed <= 0.0f ? 1.0f : (std::clamp)(formation->formationPositionSpeed * deltaTime, 0.0f, 1.0f);
					gameObject.translate = {
						gameObject.translate.x + (target.x - gameObject.translate.x) * positionRate,
						gameObject.translate.y + (target.y - gameObject.translate.y) * positionRate,
						gameObject.translate.z + (target.z - gameObject.translate.z) * positionRate};

					if (formation->formationFollowRotation) {
						const float rotationRate = formation->formationRotationSpeed <= 0.0f ? 1.0f : (std::clamp)(formation->formationRotationSpeed * deltaTime / 180.0f, 0.0f, 1.0f);
						gameObject.rotate = {
							gameObject.rotate.x + (leader->rotate.x - gameObject.rotate.x) * rotationRate,
							gameObject.rotate.y + (leader->rotate.y - gameObject.rotate.y) * rotationRate,
							gameObject.rotate.z + (leader->rotate.z - gameObject.rotate.z) * rotationRate};
					}
				}
			}

			EditorComponent* lock = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::TargetLock);

			if (lock != nullptr && lock->isActive) {
				const int32_t selectorId = lock->targetLockSelectorGameObjectId >= 0 ? lock->targetLockSelectorGameObjectId : gameObject.id;
				EditorGameObject* selectorObject = editorScene_->FindGameObject(selectorId);
				EditorComponent* selector = selectorObject != nullptr ? EditorComponentUtility::FindComponent(*selectorObject, EditorComponentType::TargetSelector) : nullptr;
				const int32_t targetId = selector != nullptr ? selector->targetSelectorCurrentTargetGameObjectId : -1;
				TargetLockRuntime& runtime = targetLockRuntimes_[gameObject.id];

				if (targetId >= 0 && targetId == lock->targetLockCurrentGameObjectId) {
					runtime.lostSeconds = 0.0f;
					runtime.elapsedSeconds += deltaTime;
				}
				else if (targetId >= 0) {
					const int32_t previousTargetId = lock->targetLockCurrentGameObjectId;
					const int32_t actionId = lock->targetLockActionTargetGameObjectId >= 0 ? lock->targetLockActionTargetGameObjectId : gameObject.id;

					if (previousTargetId >= 0 &&
						scriptManager_ != nullptr &&
						!lock->targetLockLostActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionId,
							lock->targetLockLostActionName,
							MakeGameObjectPayload(previousTargetId));
					}

					runtime.elapsedSeconds = 0.0f;
					runtime.lostSeconds = 0.0f;
					lock->targetLockCurrentGameObjectId = targetId;
					lock->targetLockLocked = false;

					if (scriptManager_ != nullptr && !lock->targetLockStartedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionId,
							lock->targetLockStartedActionName,
							MakeGameObjectPayload(targetId));
					}
				}
				else {
					runtime.lostSeconds += deltaTime;

					if (runtime.lostSeconds > lock->targetLockLostGraceSeconds && lock->targetLockCurrentGameObjectId >= 0) {
						const int32_t lostTargetId = lock->targetLockCurrentGameObjectId;
						const int32_t actionId = lock->targetLockActionTargetGameObjectId >= 0 ? lock->targetLockActionTargetGameObjectId : gameObject.id;

						if (scriptManager_ != nullptr && !lock->targetLockLostActionName.empty()) {
							scriptManager_->QueueActionPayload(
								actionId,
								lock->targetLockLostActionName,
								MakeGameObjectPayload(lostTargetId));
						}

						runtime = {};
						lock->targetLockCurrentGameObjectId = -1;
						lock->targetLockLocked = false;
					}
				}

				lock->targetLockProgress = lock->targetLockCurrentGameObjectId >= 0 && lock->targetLockSeconds <= 0.0f
					? 1.0f
					: (std::clamp)(
						runtime.elapsedSeconds / (std::max)(lock->targetLockSeconds, 0.001f),
						0.0f,
						1.0f);

				if (!lock->targetLockLocked && lock->targetLockProgress >= 1.0f) {
					lock->targetLockLocked = true;
					const int32_t actionId = lock->targetLockActionTargetGameObjectId >= 0 ? lock->targetLockActionTargetGameObjectId : gameObject.id;

					if (scriptManager_ != nullptr && !lock->targetLockCompletedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionId,
							lock->targetLockCompletedActionName,
							MakeGameObjectPayload(lock->targetLockCurrentGameObjectId));
					}
				}
			}

			EditorComponent* multiLock = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MultiTargetLock);

			if (multiLock != nullptr && multiLock->isActive && targetingManager_ != nullptr) {
				const int32_t selectorId = multiLock->multiTargetLockSelectorGameObjectId >= 0
					? multiLock->multiTargetLockSelectorGameObjectId
					: gameObject.id;
				const int32_t maximumCount = (std::clamp)(multiLock->multiTargetLockMaximumCount, 1, 64);
				std::vector<int32_t> candidateIds;
				targetingManager_->GetCandidateTargets(selectorId, maximumCount * 4, candidateIds);
				auto& lostSecondsByTarget = multiTargetLostSeconds_[gameObject.id];
				const int32_t actionTargetId = multiLock->multiTargetLockActionTargetGameObjectId >= 0
					? multiLock->multiTargetLockActionTargetGameObjectId
					: gameObject.id;

				for (size_t lockIndex = 0u; lockIndex < multiLock->multiTargetLockTargetGameObjectIds.size();) {
					const int32_t targetId = multiLock->multiTargetLockTargetGameObjectIds[lockIndex];
					const bool isCandidate = std::find(candidateIds.begin(), candidateIds.end(), targetId) != candidateIds.end();

					if (isCandidate) {
						lostSecondsByTarget[targetId] = 0.0f;
						++lockIndex;
						continue;
					}

					lostSecondsByTarget[targetId] += deltaTime;

					if (lostSecondsByTarget[targetId] <= multiLock->multiTargetLockLostGraceSeconds) {
						++lockIndex;
						continue;
					}

					if (scriptManager_ != nullptr && !multiLock->multiTargetLockLostActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionTargetId,
							multiLock->multiTargetLockLostActionName,
							MakeGameObjectPayload(targetId));
					}

					lostSecondsByTarget.erase(targetId);
					multiLock->multiTargetLockTargetGameObjectIds.erase(
						multiLock->multiTargetLockTargetGameObjectIds.begin() + static_cast<ptrdiff_t>(lockIndex));
					multiLock->multiTargetLockProgressValues.erase(
						multiLock->multiTargetLockProgressValues.begin() + static_cast<ptrdiff_t>(lockIndex));
					multiLock->multiTargetLockCompletedValues.erase(
						multiLock->multiTargetLockCompletedValues.begin() + static_cast<ptrdiff_t>(lockIndex));
				}

				if (multiLock->multiTargetLockAutoAcquire) {
					for (const int32_t candidateId : candidateIds) {
						if (static_cast<int32_t>(multiLock->multiTargetLockTargetGameObjectIds.size()) >= maximumCount) break;
						if (std::find(
								multiLock->multiTargetLockTargetGameObjectIds.begin(),
								multiLock->multiTargetLockTargetGameObjectIds.end(),
								candidateId) != multiLock->multiTargetLockTargetGameObjectIds.end()) {
							continue;
						}

						multiLock->multiTargetLockTargetGameObjectIds.push_back(candidateId);
						multiLock->multiTargetLockProgressValues.push_back(0.0f);
						multiLock->multiTargetLockCompletedValues.push_back(false);
						lostSecondsByTarget[candidateId] = 0.0f;

						if (scriptManager_ != nullptr && !multiLock->multiTargetLockAddedActionName.empty()) {
							scriptManager_->QueueActionPayload(
								actionTargetId,
								multiLock->multiTargetLockAddedActionName,
								MakeGameObjectPayload(candidateId));
						}
					}
				}

				for (size_t lockIndex = 0u; lockIndex < multiLock->multiTargetLockProgressValues.size(); ++lockIndex) {
					if (multiLock->multiTargetLockCompletedValues[lockIndex]) continue;
					const float lockSeconds = (std::max)(multiLock->multiTargetLockSecondsPerTarget, 0.001f);
					multiLock->multiTargetLockProgressValues[lockIndex] = (std::clamp)(
						multiLock->multiTargetLockProgressValues[lockIndex] + deltaTime / lockSeconds,
						0.0f,
						1.0f);

					if (multiLock->multiTargetLockProgressValues[lockIndex] < 1.0f) continue;
					multiLock->multiTargetLockCompletedValues[lockIndex] = true;

					if (scriptManager_ != nullptr && !multiLock->multiTargetLockCompletedActionName.empty()) {
						scriptManager_->QueueActionPayload(
							actionTargetId,
							multiLock->multiTargetLockCompletedActionName,
							MakeGameObjectPayload(multiLock->multiTargetLockTargetGameObjectIds[lockIndex]));
					}
				}
			}
		}
	}

	for (auto& [tweenGameObjectId, runtime] : tweenRuntimes_) {
		if (!runtime.isPlaying) {
			continue;
		}

		EditorComponent* tweenComponent = FindTweenComponent(tweenGameObjectId);

		if (tweenComponent == nullptr || !tweenComponent->isActive) {
			runtime.isPlaying = false;
			continue;
		}

		const float duration = (std::max)(tweenComponent->propertyTweenDuration, 0.001f);
		runtime.elapsedSeconds += deltaTime;
		const float normalizedTime = (std::clamp)(runtime.elapsedSeconds / duration, 0.0f, 1.0f);
		const float curveValue = EvaluateCurve(tweenComponent->propertyTweenCurve, normalizedTime);
		const int32_t targetGameObjectId = tweenComponent->propertyTweenTargetGameObjectId >= 0
			? tweenComponent->propertyTweenTargetGameObjectId
			: tweenGameObjectId;

		if (tweenComponent->propertyTweenValueType == 1) {
			const Vector3 value{
				tweenComponent->propertyTweenStartValue.x +
					(tweenComponent->propertyTweenEndValue.x - tweenComponent->propertyTweenStartValue.x) * curveValue,
				tweenComponent->propertyTweenStartValue.y +
					(tweenComponent->propertyTweenEndValue.y - tweenComponent->propertyTweenStartValue.y) * curveValue,
				tweenComponent->propertyTweenStartValue.z +
					(tweenComponent->propertyTweenEndValue.z - tweenComponent->propertyTweenStartValue.z) * curveValue};
			SetVector3(
				targetGameObjectId,
				tweenComponent->propertyTweenComponentName,
				tweenComponent->propertyTweenPropertyName,
				value);
		}
		else {
			const float value = tweenComponent->propertyTweenStartValue.x +
				(tweenComponent->propertyTweenEndValue.x - tweenComponent->propertyTweenStartValue.x) * curveValue;
			SetFloat(
				targetGameObjectId,
				tweenComponent->propertyTweenComponentName,
				tweenComponent->propertyTweenPropertyName,
				value);
		}

		if (runtime.elapsedSeconds < duration) {
			continue;
		}

		if (tweenComponent->propertyTweenLoop) {
			runtime.elapsedSeconds = 0.0f;
			continue;
		}

		runtime.isPlaying = false;

		if (scriptManager_ != nullptr && !tweenComponent->propertyTweenCompletedActionName.empty()) {
			const int32_t actionTargetGameObjectId =
				tweenComponent->propertyTweenActionTargetGameObjectId >= 0
				? tweenComponent->propertyTweenActionTargetGameObjectId
				: tweenGameObjectId;
			scriptManager_->QueueActionEvent(
				actionTargetGameObjectId,
				tweenComponent->propertyTweenCompletedActionName);
		}
	}
}

void EditorRuntimePropertyManager::Stop() {
	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* statusEffectSet = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::StatusEffectSet);

			if (statusEffectSet != nullptr) {
				statusEffectSet->statusEffectRuntimeEntries.clear();
			}
		}
	}

	if (effectManager_ != nullptr && editorScene_ != nullptr) {
		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const EditorComponent* surfaceWake = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::SurfaceWakeEmitter);

			if (surfaceWake == nullptr) {
				continue;
			}

			const int32_t effectGameObjectIds[] = {
				surfaceWake->surfaceWakeLeftEffectGameObjectId,
				surfaceWake->surfaceWakeRightEffectGameObjectId,
				surfaceWake->surfaceWakeBowEffectGameObjectId};

			for (const int32_t effectGameObjectId : effectGameObjectIds) {
				if (effectGameObjectId >= 0) {
					effectManager_->StopEffect(effectGameObjectId);
				}
			}
		}
	}

	if (inputManager_ != nullptr) {
		inputManager_->SetPauseInputMaps(false, "Gameplay", "UI");
	}

	if (audioManager_ != nullptr) {
		audioManager_->SetPaused(false);
	}

	tweenRuntimes_.clear();
	targetLockRuntimes_.clear();
	previousAttributeValues_.clear();
	initialAttributeValues_.clear();
	previousNamedAttributeValues_.clear();
	initialNamedAttributeValues_.clear();
	multiTargetLostSeconds_.clear();
	previousThreatGameObjectIds_.clear();
	damageIndicatorSequences_.clear();
	surfaceWakePreviousPositions_.clear();
	surfaceWakeActiveStates_.clear();
	waterSurfaceUnderwaterStates_.clear();
	timeScaleRuntime_ = {};
	timeScaleRuntime_.currentScale = 1.0f;
	gamePauseOwnerGameObjectId_ = -1;
}

bool EditorRuntimePropertyManager::SetGamePaused(int32_t gameObjectId, bool isPaused) {
	EditorComponent* gamePause = FindTypedComponent(gameObjectId, EditorComponentType::GamePause);

	if (gamePause == nullptr || !gamePause->isActive) {
		return false;
	}

	if (isPaused && gamePauseOwnerGameObjectId_ >= 0 && gamePauseOwnerGameObjectId_ != gameObjectId) {
		EditorComponent* previousPause = FindTypedComponent(
			gamePauseOwnerGameObjectId_,
			EditorComponentType::GamePause);

		if (previousPause != nullptr) {
			previousPause->gamePausePaused = false;
		}
	}

	if (!isPaused && gamePauseOwnerGameObjectId_ >= 0 && gamePauseOwnerGameObjectId_ != gameObjectId) {
		return false;
	}

	const bool stateChanged = gamePause->gamePausePaused != isPaused;
	gamePause->gamePausePaused = isPaused;
	gamePauseOwnerGameObjectId_ = isPaused ? gameObjectId : -1;

	if (inputManager_ != nullptr) {
		inputManager_->SetPauseInputMaps(
			isPaused,
			gamePause->gamePauseGameplayInputMap,
			gamePause->gamePauseUiInputMap);
	}

	if (audioManager_ != nullptr) {
		audioManager_->SetPaused(isPaused && gamePause->gamePausePauseAudio);
	}

	if (stateChanged && scriptManager_ != nullptr) {
		const std::string& actionName = isPaused
			? gamePause->gamePausePausedActionName
			: gamePause->gamePauseResumedActionName;

		if (!actionName.empty()) {
			scriptManager_->QueueActionPayload(
				gamePause->gamePauseActionTargetGameObjectId >= 0
					? gamePause->gamePauseActionTargetGameObjectId
					: gameObjectId,
				actionName,
				MakeBoolPayload(isPaused));
		}
	}

	return true;
}

bool EditorRuntimePropertyManager::IsGamePaused() const {
	if (gamePauseOwnerGameObjectId_ < 0) {
		return false;
	}

	EditorComponent* gamePause = FindTypedComponent(
		gamePauseOwnerGameObjectId_,
		EditorComponentType::GamePause);
	return gamePause != nullptr && gamePause->isActive && gamePause->gamePausePaused;
}

bool EditorRuntimePropertyManager::IsPhysicsPaused() const {
	if (!IsGamePaused()) {
		return false;
	}

	EditorComponent* gamePause = FindTypedComponent(
		gamePauseOwnerGameObjectId_,
		EditorComponentType::GamePause);
	return gamePause != nullptr && gamePause->gamePausePausePhysics;
}

bool EditorRuntimePropertyManager::GetSurfaceWakeState(
	int32_t gameObjectId,
	float& speed,
	float& intensity) const {
	EditorComponent* surfaceWake = FindTypedComponent(
		gameObjectId,
		EditorComponentType::SurfaceWakeEmitter);

	if (surfaceWake == nullptr || !surfaceWake->isActive) {
		return false;
	}

	speed = surfaceWake->surfaceWakeCurrentSpeed;
	intensity = surfaceWake->surfaceWakeCurrentIntensity;
	return true;
}

void EditorRuntimePropertyManager::UpdateSurfaceWakes(float deltaTime) {
	if (editorScene_ == nullptr || effectManager_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	const float oceanElapsedTime = GetEditorOceanElapsedTime();

	for (EditorGameObject& owner : editorScene_->GetGameObjects()) {
		EditorComponent* surfaceWake = EditorComponentUtility::FindComponent(
			owner,
			EditorComponentType::SurfaceWakeEmitter);

		if (surfaceWake == nullptr) {
			continue;
		}

		Vector3 ownerScale{};
		Vector3 ownerRotation{};
		Vector3 ownerPosition{};
		editorScene_->GetWorldTransform(owner.id, ownerScale, ownerRotation, ownerPosition);
		const Vector3 previousPosition = surfaceWakePreviousPositions_.contains(owner.id)
			? surfaceWakePreviousPositions_[owner.id]
			: ownerPosition;
		surfaceWakePreviousPositions_[owner.id] = ownerPosition;
		const float moveX = ownerPosition.x - previousPosition.x;
		const float moveZ = ownerPosition.z - previousPosition.z;
		const Vector3 ownerHorizontalVelocity{
			moveX / deltaTime,
			0.0f,
			moveZ / deltaTime};
		EditorOceanSurfaceSample ownerOceanSample{};
		const uint64_t ownerSurfaceSampleKey =
			(static_cast<uint64_t>(static_cast<uint32_t>(owner.id)) << 32u) |
			0xfffffffeull;
		const bool hasOwnerOceanSample = SampleEditorOceanSurface(
			*editorScene_,
			surfaceWake->surfaceWakeOceanGameObjectId,
			ownerPosition,
			ownerSurfaceSampleKey,
			oceanElapsedTime,
			ownerOceanSample);
		const float relativeVelocityX = ownerHorizontalVelocity.x -
			(hasOwnerOceanSample ? ownerOceanSample.velocity.x : 0.0f);
		const float relativeVelocityZ = ownerHorizontalVelocity.z -
			(hasOwnerOceanSample ? ownerOceanSample.velocity.z : 0.0f);
		const float speed = std::sqrt(
			relativeVelocityX * relativeVelocityX +
			relativeVelocityZ * relativeVelocityZ);
		const float minimumSpeed = (std::max)(surfaceWake->surfaceWakeMinimumSpeed, 0.0f);
		const float maximumSpeed = (std::max)(surfaceWake->surfaceWakeMaximumSpeed, minimumSpeed + 0.01f);
		const float speedIntensity = (std::clamp)(
			(speed - minimumSpeed) / (maximumSpeed - minimumSpeed),
			0.0f,
			1.0f);
		const float breakingWaveAmplification = hasOwnerOceanSample
			? 0.85f + (std::clamp)(ownerOceanSample.foam, 0.0f, 1.0f) * 0.30f
			: 1.0f;
		const float intensity = owner.isActive && surfaceWake->isActive
			? (std::clamp)(speedIntensity * breakingWaveAmplification, 0.0f, 1.0f)
			: 0.0f;
		surfaceWake->surfaceWakeCurrentSpeed = speed;
		surfaceWake->surfaceWakeCurrentIntensity = intensity;
		const bool shouldEmit = intensity > 0.0f;
		const bool wasEmitting = surfaceWakeActiveStates_[owner.id];
		const int32_t effectGameObjectIds[] = {
			surfaceWake->surfaceWakeLeftEffectGameObjectId,
			surfaceWake->surfaceWakeRightEffectGameObjectId,
			surfaceWake->surfaceWakeBowEffectGameObjectId};

		for (size_t effectIndex = 0u; effectIndex < std::size(effectGameObjectIds); effectIndex++) {
			const int32_t effectGameObjectId = effectGameObjectIds[effectIndex];
			EditorGameObject* effectGameObject = editorScene_->FindGameObject(effectGameObjectId);

			if (effectGameObject == nullptr) {
				continue;
			}

			Vector3 effectScale{};
			Vector3 effectRotation{};
			Vector3 unusedEffectPosition{};
			editorScene_->GetWorldTransform(
				effectGameObjectId,
				effectScale,
				effectRotation,
				unusedEffectPosition);  // 位置は下でOwner追従に上書きするため、ここではScale/Rotationだけ使う。

			// FX Wake Left/Right/BowはEFFECTS配下の独立GameObjectで、船に追従する親子関係を
			// 持たない。ここでOwner(船)のWorld位置+Local取り付けオフセットから毎フレーム
			// 追従位置を計算しないと、航跡が常に原点付近に固定されたまま船だけ離れていく
			// (=画面外に出て何も見えない)ことになる。
			const Vector3 attachLocalOffset = effectIndex == 2u
				? Vector3{0.0f, 0.0f, 2.5f}
				: Vector3{effectIndex == 0u ? -1.0f : 1.0f, 0.0f, -2.0f};
			Vector3 effectPosition = AddRuntimeVector(
				ownerPosition,
				TransformDirection(attachLocalOffset, ownerRotation));
			EditorOceanSurfaceSample oceanSample{};
			const uint64_t sampleKey =
				(static_cast<uint64_t>(static_cast<uint32_t>(owner.id)) << 32u) |
				static_cast<uint32_t>(effectGameObjectId);

			if (SampleEditorOceanSurface(
				*editorScene_,
				surfaceWake->surfaceWakeOceanGameObjectId,
					effectPosition,
					sampleKey,
					oceanElapsedTime,
					oceanSample)) {
				effectPosition.y = oceanSample.position.y;
				editorScene_->SetWorldTransform(
					effectGameObjectId,
					effectScale,
					effectRotation,
					effectPosition);
			}

			EditorComponent* particle = EditorComponentUtility::FindComponent(
				*effectGameObject,
				EditorComponentType::ParticleSystem);

			if (particle == nullptr) {
				particle = EditorComponentUtility::FindComponent(
					*effectGameObject,
					EditorComponentType::VisualEffect);
			}

			if (particle != nullptr) {
				const bool isBowEffect = effectIndex == 2u;
				const float bowMultiplier = isBowEffect ? 1.25f : 1.0f;
				const float wakeWidth = (std::max)(surfaceWake->surfaceWakeWidth, 0.01f);
				const Vector3 localDirection = isBowEffect
					? Vector3{0.0f, 0.72f, -0.35f}
					: Vector3{0.0f, 0.14f, -1.0f};
				particle->animationPlayOnAwake = false;
				particle->color = isBowEffect
					? Vector3{0.78f, 0.92f, 1.0f}
					: Vector3{0.66f, 0.86f, 0.96f};
				particle->particleEndColor = {0.9f, 0.97f, 1.0f};
				particle->particleRate =
					(std::max)(surfaceWake->surfaceWakeMaximumEmissionRate, 0.0f) *
					intensity * bowMultiplier;
				particle->particleSize = wakeWidth * (isBowEffect ? 0.08f : 0.10f);
				particle->particleEndSize = wakeWidth * (isBowEffect ? 0.04f : 0.14f);
				particle->particleLifetime = (std::max)(surfaceWake->surfaceWakeLifetime, 0.01f);
				particle->particleSpeed = (isBowEffect ? 2.2f : 0.9f) + intensity * 0.65f;
				particle->particleMaxCount = 256;
				particle->particleShape = 2;
				particle->particleSimulationSpace = 0;
				particle->particleBillboardMode = 2;
				particle->particleBillboardStretch = isBowEffect ? 3.0f : 5.0f;
				particle->particleDuration = 3600.0f;
				particle->particleLooping = true;
				particle->particlePrewarm = false;
				particle->particleBurstCount = 0;
				particle->particleShapeRadius = wakeWidth * (isBowEffect ? 0.07f : 0.06f);
				particle->particleShapeAngle = isBowEffect ? 12.0f : 6.0f;
				particle->particleDirection = TransformDirection(localDirection, ownerRotation);
				particle->particleStartAlpha = isBowEffect ? 0.62f : 0.48f;
				particle->particleEndAlpha = 0.0f;
				particle->particleEmissionStrength = isBowEffect ? 0.16f : 0.08f;
				particle->particleEndSpeedMultiplier = 0.22f;
				particle->particleNoiseStrength = isBowEffect ? 0.06f : 0.025f;
				particle->particleNoiseFrequency = 0.8f;
			}

			if (shouldEmit && !wasEmitting) {
				effectManager_->PlayEffect(effectGameObjectId);
			}
			else if (!shouldEmit) {
				effectManager_->StopEffect(effectGameObjectId);
			}
		}

		surfaceWakeActiveStates_[owner.id] = shouldEmit;
	}
}

bool EditorRuntimePropertyManager::GetWaterSurfaceState(
	int32_t gameObjectId,
	int32_t& state,
	float& signedDistance,
	int32_t& oceanGameObjectId,
	Vector3& position,
	Vector3& normal,
	Vector3& velocity) const {
	EditorComponent* waterSurface = FindTypedComponent(
		gameObjectId,
		EditorComponentType::WaterSurfaceState);

	if (waterSurface == nullptr || !waterSurface->isActive) {
		return false;
	}

	state = waterSurface->waterSurfaceState;
	signedDistance = waterSurface->waterSurfaceSignedDistance;
	oceanGameObjectId = waterSurface->waterSurfaceCurrentOceanGameObjectId;
	position = waterSurface->waterSurfacePosition;
	normal = waterSurface->waterSurfaceNormal;
	velocity = waterSurface->waterSurfaceVelocity;
	return oceanGameObjectId >= 0;
}

bool EditorRuntimePropertyManager::GetWaterSurfaceFoam(
	int32_t gameObjectId,
	float& foam) const {
	EditorComponent* waterSurface = FindTypedComponent(
		gameObjectId,
		EditorComponentType::WaterSurfaceState);

	if (waterSurface == nullptr || !waterSurface->isActive ||
		waterSurface->waterSurfaceCurrentOceanGameObjectId < 0) {
		return false;
	}

	foam = waterSurface->waterSurfaceFoam;
	return true;
}

bool EditorRuntimePropertyManager::GetOceanProbeSample(
	int32_t gameObjectId,
	int32_t probeIndex,
	EditorOceanProbeEntry& probeEntry) const {
	EditorComponent* probeSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::OceanProbeSet);

	if (probeSet == nullptr || !probeSet->isActive || probeIndex < 0 ||
		static_cast<size_t>(probeIndex) >= probeSet->oceanProbeEntries.size()) {
		return false;
	}

	probeEntry = probeSet->oceanProbeEntries[static_cast<size_t>(probeIndex)];
	return probeEntry.isValid;
}

bool EditorRuntimePropertyManager::GetOceanProbeFoam(
	int32_t gameObjectId,
	int32_t probeIndex,
	float& foam) const {
	EditorOceanProbeEntry probeEntry{};

	if (!GetOceanProbeSample(gameObjectId, probeIndex, probeEntry)) {
		return false;
	}

	foam = probeEntry.foam;
	return true;
}

void EditorRuntimePropertyManager::UpdateOceanGameplayQueries() {
	if (editorScene_ == nullptr) {
		return;
	}

	const float oceanElapsedTime = GetEditorOceanElapsedTime();

	for (EditorGameObject& owner : editorScene_->GetGameObjects()) {
		if (!owner.isActive) {
			continue;
		}

		Vector3 ownerScale{};
		Vector3 ownerRotation{};
		Vector3 ownerPosition{};
		editorScene_->GetWorldTransform(
			owner.id,
			ownerScale,
			ownerRotation,
			ownerPosition);
		(void)ownerScale;
		EditorComponent* waterSurface = EditorComponentUtility::FindComponent(
			owner,
			EditorComponentType::WaterSurfaceState);

		if (waterSurface != nullptr && waterSurface->isActive) {
			const Vector3 queryPosition = AddRuntimeVector(
				ownerPosition,
				TransformDirection(waterSurface->waterSurfaceLocalOffset, ownerRotation));
			EditorOceanSurfaceSample oceanSample{};
			const uint64_t sampleKey =
				static_cast<uint64_t>(static_cast<uint32_t>(owner.id)) ^ 0x57535441ull;
			const bool hasSample = SampleEditorOceanSurface(
				*editorScene_,
				waterSurface->waterSurfaceOceanGameObjectId,
				queryPosition,
				sampleKey,
				oceanElapsedTime,
				oceanSample);

			if (hasSample) {
				const Vector3 surfaceOffset = {
					queryPosition.x - oceanSample.position.x,
					queryPosition.y - oceanSample.position.y,
					queryPosition.z - oceanSample.position.z};
				const float signedDistance = DotRuntimeVector(
					surfaceOffset,
					oceanSample.normal) - waterSurface->waterSurfaceClearance;
				const bool isUnderwater = signedDistance <= 0.0f;
				const bool wasUnderwater = waterSurfaceUnderwaterStates_.contains(owner.id)
					? waterSurfaceUnderwaterStates_[owner.id]
					: isUnderwater;
				waterSurface->waterSurfaceSignedDistance = signedDistance;
				waterSurface->waterSurfaceCurrentOceanGameObjectId = oceanSample.oceanGameObjectId;
				waterSurface->waterSurfacePosition = oceanSample.position;
				waterSurface->waterSurfaceNormal = oceanSample.normal;
				waterSurface->waterSurfaceVelocity = oceanSample.velocity;
				waterSurface->waterSurfaceFoam = oceanSample.foam;
				waterSurface->waterSurfaceState = isUnderwater ? 2 : 0;

				if (isUnderwater != wasUnderwater) {
					waterSurface->waterSurfaceState = isUnderwater ? 1 : 3;
					const std::string& actionName = isUnderwater
						? waterSurface->waterSurfaceEnteredActionName
						: waterSurface->waterSurfaceExitedActionName;

					if (scriptManager_ != nullptr && !actionName.empty()) {
						EditorScriptActionPayload payload{};
						payload.type = EditorScriptActionPayloadTypeGameObject;
						payload.gameObjectId = oceanSample.oceanGameObjectId;
						scriptManager_->QueueActionPayload(
							waterSurface->waterSurfaceActionTargetGameObjectId >= 0
								? waterSurface->waterSurfaceActionTargetGameObjectId
								: owner.id,
							actionName,
							payload);
					}
				}

				waterSurfaceUnderwaterStates_[owner.id] = isUnderwater;
			}
			else {
				waterSurface->waterSurfaceState = 0;
				waterSurface->waterSurfaceSignedDistance = 0.0f;
				waterSurface->waterSurfaceCurrentOceanGameObjectId = -1;
				waterSurface->waterSurfaceFoam = 0.0f;
				waterSurfaceUnderwaterStates_[owner.id] = false;
			}
		}

		EditorComponent* probeSet = EditorComponentUtility::FindComponent(
			owner,
			EditorComponentType::OceanProbeSet);

		if (probeSet == nullptr || !probeSet->isActive) {
			continue;
		}

		const Vector3 probeOrigin = AddRuntimeVector(
			ownerPosition,
			TransformDirection(probeSet->oceanProbeLocalOriginOffset, ownerRotation));
		const Vector3 probeDirection = NormalizeRuntimeVector(
			TransformDirection(probeSet->oceanProbeLocalDirection, ownerRotation));

		for (size_t probeIndex = 0u; probeIndex < probeSet->oceanProbeEntries.size(); probeIndex++) {
			EditorOceanProbeEntry& probeEntry = probeSet->oceanProbeEntries[probeIndex];
			const Vector3 probePosition = AddRuntimeVector(
				probeOrigin,
				MultiplyRuntimeVector((std::max)(probeEntry.distance, 0.0f), probeDirection));
			const uint64_t sampleKey =
				static_cast<uint64_t>(static_cast<uint32_t>(owner.id)) ^
				(0x50524f42ull + static_cast<uint64_t>(probeIndex) * 0x9e3779b9ull);
			EditorOceanSurfaceSample oceanSample{};
			probeEntry.isValid = SampleEditorOceanSurface(
				*editorScene_,
				probeSet->oceanProbeOceanGameObjectId,
				probePosition,
				sampleKey,
				oceanElapsedTime,
				oceanSample);

			if (!probeEntry.isValid) {
				probeEntry.relativeHeight = 0.0f;
				probeEntry.foam = 0.0f;
				continue;
			}

			probeEntry.position = oceanSample.position;
			probeEntry.normal = oceanSample.normal;
			probeEntry.velocity = oceanSample.velocity;
			probeEntry.relativeHeight = oceanSample.position.y - probeOrigin.y;
			probeEntry.foam = oceanSample.foam;
		}
	}
}

bool EditorRuntimePropertyManager::SetObjective(int32_t gameObjectId, const std::string& objectiveId, int32_t state, float currentValue) {
	EditorComponent* tracker = FindTypedComponent(gameObjectId, EditorComponentType::ObjectiveTracker);
	if (tracker == nullptr || objectiveId.empty()) return false;
	for (EditorObjectiveEntry& entry : tracker->objectiveEntries) {
		if (entry.objectiveId != objectiveId) continue;
		entry.state = (std::clamp)(state, 0, 3); entry.currentValue = currentValue;
		if (scriptManager_ != nullptr && !tracker->objectiveChangedActionName.empty()) {
			scriptManager_->QueueActionPayload(tracker->objectiveActionTargetGameObjectId >= 0 ? tracker->objectiveActionTargetGameObjectId : gameObjectId, tracker->objectiveChangedActionName, MakeStringPayload(objectiveId));
		}
		return true;
	}
	return false;
}

bool EditorRuntimePropertyManager::GetObjective(int32_t gameObjectId, const std::string& objectiveId, int32_t& state, float& currentValue, float& targetValue) const {
	EditorComponent* tracker = FindTypedComponent(gameObjectId, EditorComponentType::ObjectiveTracker);
	if (tracker == nullptr) return false;
	for (const EditorObjectiveEntry& entry : tracker->objectiveEntries) if (entry.objectiveId == objectiveId) { state = entry.state; currentValue = entry.currentValue; targetValue = entry.targetValue; return true; }
	return false;
}

bool EditorRuntimePropertyManager::ApplyDifficulty(int32_t gameObjectId, int32_t difficultyIndex) {
	EditorComponent* difficulty = FindTypedComponent(gameObjectId, EditorComponentType::DifficultyParameterSet);
	if (difficulty == nullptr || difficultyIndex < 0 || difficultyIndex >= static_cast<int32_t>(difficulty->difficultyNames.size())) return false;
	bool applied = false;
	for (const EditorDifficultyOverrideEntry& entry : difficulty->difficultyOverrides) {
		if (entry.difficultyIndex != difficultyIndex) continue;
		const int32_t targetId = entry.targetGameObjectId >= 0 ? entry.targetGameObjectId : gameObjectId;
		if (entry.valueType == 0) applied = SetFloat(targetId, entry.componentName, entry.propertyName, entry.floatValue) || applied;
		else if (entry.valueType == 1) applied = SetInt(targetId, entry.componentName, entry.propertyName, entry.intValue) || applied;
		else applied = SetBool(targetId, entry.componentName, entry.propertyName, entry.boolValue) || applied;
	}
	difficulty->difficultySelectedIndex = difficultyIndex;
	if (scriptManager_ != nullptr && !difficulty->difficultyAppliedActionName.empty()) scriptManager_->QueueActionPayload(difficulty->difficultyActionTargetGameObjectId >= 0 ? difficulty->difficultyActionTargetGameObjectId : gameObjectId, difficulty->difficultyAppliedActionName, MakeStringPayload(difficulty->difficultyNames[static_cast<size_t>(difficultyIndex)]));
	return applied || difficulty->difficultyOverrides.empty();
}

bool EditorRuntimePropertyManager::GetDamageDirection(int32_t gameObjectId, EditorScriptVector2& direction, float& alpha, int32_t& sourceGameObjectId) const {
	EditorComponent* indicator = FindTypedComponent(gameObjectId, EditorComponentType::DamageDirectionIndicator);
	if (indicator == nullptr || indicator->damageDirectionRemaining <= 0.0f) return false;
	direction = indicator->damageDirectionNormalized; sourceGameObjectId = indicator->damageDirectionSourceGameObjectId;
	const float fade = (std::max)(indicator->damageDirectionFadeSeconds, 0.0001f);
	alpha = (std::clamp)(indicator->damageDirectionRemaining / fade, 0.0f, 1.0f); return true;
}

bool EditorRuntimePropertyManager::StartTimer(int32_t gameObjectId) {
	EditorComponent* timer = FindTypedComponent(gameObjectId, EditorComponentType::Timer);

	if (timer == nullptr) {
		return false;
	}

	timer->timerRemaining = (std::max)(timer->timerDuration, 0.001f);
	timer->timerPaused = false;
	return true;
}

bool EditorRuntimePropertyManager::PauseTimer(int32_t gameObjectId, bool isPaused) {
	EditorComponent* timer = FindTypedComponent(gameObjectId, EditorComponentType::Timer);

	if (timer == nullptr) {
		return false;
	}

	timer->timerPaused = isPaused;
	return true;
}

bool EditorRuntimePropertyManager::GetTimerRemaining(int32_t gameObjectId, float& remainingSeconds) const {
	EditorComponent* timer = FindTypedComponent(gameObjectId, EditorComponentType::Timer);

	if (timer == nullptr) {
		return false;
	}

	remainingSeconds = timer->timerRemaining;
	return true;
}

bool EditorRuntimePropertyManager::ChangeState(int32_t gameObjectId, const std::string& stateName) {
	EditorComponent* state = FindTypedComponent(gameObjectId, EditorComponentType::GenericStateMachine);

	if (state == nullptr || stateName.empty() || state->stateMachineCurrentState == stateName) {
		return false;
	}

	state->stateMachineCurrentState = stateName;
	const int32_t targetId = state->stateMachineActionTargetGameObjectId >= 0
		? state->stateMachineActionTargetGameObjectId
		: gameObjectId;

	if (scriptManager_ != nullptr && !state->stateMachineChangedActionName.empty()) {
		scriptManager_->QueueActionPayload(
			targetId,
			state->stateMachineChangedActionName,
			MakeStringPayload(stateName));
	}

	return true;
}

bool EditorRuntimePropertyManager::GetState(int32_t gameObjectId, std::string& stateName) const {
	EditorComponent* state = FindTypedComponent(gameObjectId, EditorComponentType::GenericStateMachine);

	if (state == nullptr) {
		return false;
	}

	stateName = state->stateMachineCurrentState;
	return true;
}

bool EditorRuntimePropertyManager::SetAttribute(int32_t gameObjectId, float value) {
	EditorComponent* attribute = FindTypedComponent(gameObjectId, EditorComponentType::Attribute);

	if (attribute == nullptr) {
		return false;
	}

	attribute->attributeCurrent = (std::clamp)(
		value,
		attribute->attributeMinimum,
		attribute->attributeMaximum);
	return true;
}

bool EditorRuntimePropertyManager::GetAttribute(int32_t gameObjectId, float& current, float& maximum) const {
	EditorComponent* attribute = FindTypedComponent(gameObjectId, EditorComponentType::Attribute);

	if (attribute == nullptr) {
		return false;
	}

	current = attribute->attributeCurrent;
	maximum = attribute->attributeMaximum;
	return true;
}

bool EditorRuntimePropertyManager::GetTargetLockState(
	int32_t gameObjectId,
	float& progress,
	bool& isLocked,
	int32_t& targetGameObjectId) const {
	EditorComponent* lock = FindTypedComponent(gameObjectId, EditorComponentType::TargetLock);

	if (lock == nullptr) {
		return false;
	}

	progress = lock->targetLockProgress;
	isLocked = lock->targetLockLocked;
	targetGameObjectId = lock->targetLockCurrentGameObjectId;
	return true;
}

bool EditorRuntimePropertyManager::SetNamedAttribute(
	int32_t gameObjectId,
	const std::string& attributeName,
	float value) {
	EditorComponent* attributeSet = FindTypedComponent(gameObjectId, EditorComponentType::AttributeSet);

	if (attributeSet == nullptr || attributeName.empty()) {
		return false;
	}

	for (EditorNamedAttributeEntry& entry : attributeSet->attributeSetEntries) {
		if (entry.name != attributeName) continue;
		entry.maximum = (std::max)(entry.maximum, entry.minimum);
		entry.current = (std::clamp)(value, entry.minimum, entry.maximum);
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::GetNamedAttribute(
	int32_t gameObjectId,
	const std::string& attributeName,
	float& current,
	float& maximum) const {
	EditorComponent* attributeSet = FindTypedComponent(gameObjectId, EditorComponentType::AttributeSet);

	if (attributeSet == nullptr || attributeName.empty()) {
		return false;
	}

	for (const EditorNamedAttributeEntry& entry : attributeSet->attributeSetEntries) {
		if (entry.name != attributeName) continue;
		current = entry.current;
		maximum = entry.maximum;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::SetCounter(int32_t gameObjectId, float value) {
	EditorComponent* counter = FindTypedComponent(gameObjectId, EditorComponentType::GenericCounter);

	if (counter == nullptr) {
		return false;
	}

	counter->counterMaximumValue = (std::max)(counter->counterMaximumValue, counter->counterMinimumValue);
	const float previousValue = counter->counterCurrentValue;
	counter->counterCurrentValue = (std::clamp)(
		value,
		counter->counterMinimumValue,
		counter->counterMaximumValue);

	if (previousValue != counter->counterCurrentValue && scriptManager_ != nullptr &&
		!counter->counterChangedActionName.empty()) {
		const int32_t actionTargetId = counter->counterActionTargetGameObjectId >= 0
			? counter->counterActionTargetGameObjectId
			: gameObjectId;
		scriptManager_->QueueActionPayload(
			actionTargetId,
			counter->counterChangedActionName,
			MakeFloatPayload(counter->counterCurrentValue));
	}

	return true;
}

bool EditorRuntimePropertyManager::AddCounter(int32_t gameObjectId, float deltaValue) {
	float currentValue = 0.0f;

	if (!GetCounter(gameObjectId, currentValue)) {
		return false;
	}

	return SetCounter(gameObjectId, currentValue + deltaValue);
}

bool EditorRuntimePropertyManager::GetCounter(int32_t gameObjectId, float& value) const {
	EditorComponent* counter = FindTypedComponent(gameObjectId, EditorComponentType::GenericCounter);

	if (counter == nullptr) {
		return false;
	}

	value = counter->counterCurrentValue;
	return true;
}

bool EditorRuntimePropertyManager::EvaluateCondition(int32_t gameObjectId, bool& result) {
	EditorComponent* condition = FindTypedComponent(gameObjectId, EditorComponentType::GenericCondition);

	if (condition == nullptr) {
		return false;
	}

	result = EvaluateConditionComponent(gameObjectId, *condition, true);
	return true;
}

bool EditorRuntimePropertyManager::GetMultiTargetLockCount(
	int32_t gameObjectId,
	int32_t& targetCount) const {
	EditorComponent* multiLock = FindTypedComponent(gameObjectId, EditorComponentType::MultiTargetLock);

	if (multiLock == nullptr) {
		return false;
	}

	targetCount = static_cast<int32_t>(multiLock->multiTargetLockTargetGameObjectIds.size());
	return true;
}

bool EditorRuntimePropertyManager::GetMultiTargetLockTarget(
	int32_t gameObjectId,
	int32_t targetIndex,
	int32_t& targetGameObjectId,
	float& progress,
	bool& isLocked) const {
	EditorComponent* multiLock = FindTypedComponent(gameObjectId, EditorComponentType::MultiTargetLock);

	if (multiLock == nullptr || targetIndex < 0 ||
		targetIndex >= static_cast<int32_t>(multiLock->multiTargetLockTargetGameObjectIds.size())) {
		return false;
	}

	const size_t index = static_cast<size_t>(targetIndex);
	targetGameObjectId = multiLock->multiTargetLockTargetGameObjectIds[index];
	progress = multiLock->multiTargetLockProgressValues[index];
	isLocked = multiLock->multiTargetLockCompletedValues[index];
	return true;
}

bool EditorRuntimePropertyManager::GetGameplayDataValue(
	int32_t gameObjectId,
	const std::string& key,
	int32_t& valueType,
	std::string& value) const {
	EditorComponent* gameplayData = FindTypedComponent(gameObjectId, EditorComponentType::GameplayData);

	if (gameplayData == nullptr || key.empty()) {
		return false;
	}

	for (const EditorGameplayDataEntry& entry : gameplayData->gameplayDataEntries) {
		if (entry.key != key) continue;
		valueType = entry.type;
		value = entry.value;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::StartCooldown(
	int32_t gameObjectId,
	const std::string& cooldownName,
	float durationOverride) {
	EditorComponent* cooldownSet = FindTypedComponent(gameObjectId, EditorComponentType::CooldownSet);
	if (cooldownSet == nullptr || cooldownName.empty()) {
		return false;
	}

	for (EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
		if (entry.name != cooldownName) continue;
		entry.remaining = durationOverride >= 0.0f
			? durationOverride
			: (std::max)(entry.duration, 0.0f);
		entry.wasRunning = entry.remaining > 0.0f;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::ResetCooldown(
	int32_t gameObjectId,
	const std::string& cooldownName) {
	EditorComponent* cooldownSet = FindTypedComponent(gameObjectId, EditorComponentType::CooldownSet);
	if (cooldownSet == nullptr || cooldownName.empty()) {
		return false;
	}

	for (EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
		if (entry.name != cooldownName) continue;
		entry.remaining = 0.0f;
		entry.wasRunning = false;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::GetCooldown(
	int32_t gameObjectId,
	const std::string& cooldownName,
	float& remainingSeconds,
	bool& isReady) const {
	EditorComponent* cooldownSet = FindTypedComponent(gameObjectId, EditorComponentType::CooldownSet);
	if (cooldownSet == nullptr || cooldownName.empty()) {
		return false;
	}

	for (const EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
		if (entry.name != cooldownName) continue;
		remainingSeconds = entry.remaining;
		isReady = entry.remaining <= 0.0f;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::GetThreatCount(
	int32_t gameObjectId,
	int32_t& threatCount) const {
	EditorComponent* threatTracker = FindTypedComponent(gameObjectId, EditorComponentType::ThreatTracker);

	if (threatTracker == nullptr) {
		return false;
	}

	threatCount = static_cast<int32_t>(threatTracker->threatTrackerEntries.size());
	return true;
}

bool EditorRuntimePropertyManager::GetThreat(
	int32_t gameObjectId,
	int32_t threatIndex,
	EditorThreatRuntimeEntry& threat) const {
	EditorComponent* threatTracker = FindTypedComponent(gameObjectId, EditorComponentType::ThreatTracker);
	if (threatTracker == nullptr || threatIndex < 0 ||
		threatIndex >= static_cast<int32_t>(threatTracker->threatTrackerEntries.size())) {
		return false;
	}

	threat = threatTracker->threatTrackerEntries[static_cast<size_t>(threatIndex)];
	return true;
}

bool EditorRuntimePropertyManager::ApplyStatusEffect(
	int32_t gameObjectId,
	const std::string& effectId,
	int32_t sourceGameObjectId) {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);

	if (statusEffectSet == nullptr || effectId.empty()) {
		return false;
	}

	auto definitionIterator = std::find_if(
		statusEffectSet->statusEffectDefinitions.begin(),
		statusEffectSet->statusEffectDefinitions.end(),
		[&effectId](const EditorStatusEffectDefinitionEntry& definition) {
			return definition.effectId == effectId;
		});

	if (definitionIterator == statusEffectSet->statusEffectDefinitions.end()) {
		return false;
	}

	const EditorStatusEffectDefinitionEntry& definition = *definitionIterator;
	auto runtimeIterator = std::find_if(
		statusEffectSet->statusEffectRuntimeEntries.begin(),
		statusEffectSet->statusEffectRuntimeEntries.end(),
		[&effectId](const EditorStatusEffectRuntimeEntry& runtimeEntry) {
			return runtimeEntry.effectId == effectId;
		});

	if (runtimeIterator != statusEffectSet->statusEffectRuntimeEntries.end()) {
		if (definition.stackMode == 2) {
			return false;
		}

		if (definition.stackMode == 1) {
			runtimeIterator->stackCount = (std::min)(
				runtimeIterator->stackCount + 1,
				(std::max)(definition.maximumStacks, 1));
		}

		runtimeIterator->sourceGameObjectId = sourceGameObjectId;
		runtimeIterator->remainingSeconds = (std::max)(definition.duration, 0.001f);
		runtimeIterator->tickRemainingSeconds = (std::max)(definition.tickInterval, 0.0f);
		return true;
	}

	statusEffectSet->statusEffectRuntimeEntries.push_back({
		effectId,
		sourceGameObjectId,
		(std::max)(definition.duration, 0.001f),
		(std::max)(definition.tickInterval, 0.0f),
		1});

	if (scriptManager_ != nullptr && !definition.startedActionName.empty()) {
		const int32_t actionTargetGameObjectId = statusEffectSet->statusEffectActionTargetGameObjectId >= 0
			? statusEffectSet->statusEffectActionTargetGameObjectId
			: gameObjectId;
		scriptManager_->QueueActionPayload(
			actionTargetGameObjectId,
			definition.startedActionName,
			MakeStringPayload(effectId));
	}

	return true;
}

bool EditorRuntimePropertyManager::RemoveStatusEffect(
	int32_t gameObjectId,
	const std::string& effectId) {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);

	if (statusEffectSet == nullptr || effectId.empty()) {
		return false;
	}

	auto runtimeIterator = std::find_if(
		statusEffectSet->statusEffectRuntimeEntries.begin(),
		statusEffectSet->statusEffectRuntimeEntries.end(),
		[&effectId](const EditorStatusEffectRuntimeEntry& runtimeEntry) {
			return runtimeEntry.effectId == effectId;
		});

	if (runtimeIterator == statusEffectSet->statusEffectRuntimeEntries.end()) {
		return false;
	}

	auto definitionIterator = std::find_if(
		statusEffectSet->statusEffectDefinitions.begin(),
		statusEffectSet->statusEffectDefinitions.end(),
		[&effectId](const EditorStatusEffectDefinitionEntry& definition) {
			return definition.effectId == effectId;
		});

	if (definitionIterator != statusEffectSet->statusEffectDefinitions.end() &&
		scriptManager_ != nullptr && !definitionIterator->endedActionName.empty()) {
		const int32_t actionTargetGameObjectId = statusEffectSet->statusEffectActionTargetGameObjectId >= 0
			? statusEffectSet->statusEffectActionTargetGameObjectId
			: gameObjectId;
		scriptManager_->QueueActionPayload(
			actionTargetGameObjectId,
			definitionIterator->endedActionName,
			MakeStringPayload(effectId));
	}

	statusEffectSet->statusEffectRuntimeEntries.erase(runtimeIterator);
	return true;
}

bool EditorRuntimePropertyManager::ClearStatusEffects(int32_t gameObjectId) {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);

	if (statusEffectSet == nullptr) {
		return false;
	}

	while (!statusEffectSet->statusEffectRuntimeEntries.empty()) {
		const std::string effectId = statusEffectSet->statusEffectRuntimeEntries.back().effectId;
		RemoveStatusEffect(gameObjectId, effectId);
	}

	return true;
}

bool EditorRuntimePropertyManager::HasStatusEffect(
	int32_t gameObjectId,
	const std::string& effectId) const {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);
	return statusEffectSet != nullptr && std::any_of(
		statusEffectSet->statusEffectRuntimeEntries.begin(),
		statusEffectSet->statusEffectRuntimeEntries.end(),
		[&effectId](const EditorStatusEffectRuntimeEntry& runtimeEntry) {
			return runtimeEntry.effectId == effectId;
		});
}

bool EditorRuntimePropertyManager::GetStatusEffectCount(
	int32_t gameObjectId,
	int32_t& effectCount) const {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);

	if (statusEffectSet == nullptr) {
		return false;
	}

	effectCount = static_cast<int32_t>(statusEffectSet->statusEffectRuntimeEntries.size());
	return true;
}

bool EditorRuntimePropertyManager::GetStatusEffectEntry(
	int32_t gameObjectId,
	int32_t effectIndex,
	EditorStatusEffectRuntimeEntry& effectEntry) const {
	EditorComponent* statusEffectSet = FindTypedComponent(
		gameObjectId,
		EditorComponentType::StatusEffectSet);

	if (statusEffectSet == nullptr || effectIndex < 0 ||
		effectIndex >= static_cast<int32_t>(statusEffectSet->statusEffectRuntimeEntries.size())) {
		return false;
	}

	effectEntry = statusEffectSet->statusEffectRuntimeEntries[static_cast<size_t>(effectIndex)];
	return true;
}

void EditorRuntimePropertyManager::ResetRuntimeState(
	int32_t gameObjectId,
	const EditorComponent* resetConfiguration) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	const bool resetStateMachine = resetConfiguration == nullptr || resetConfiguration->runtimeResetStateMachine;
	const bool resetAttributes = resetConfiguration == nullptr || resetConfiguration->runtimeResetAttributes;
	const bool resetLocks = resetConfiguration == nullptr || resetConfiguration->runtimeResetLocks;
	const bool resetTimers = resetConfiguration == nullptr || resetConfiguration->runtimeResetTimers;
	const bool resetDestructibleParts = resetConfiguration == nullptr || resetConfiguration->runtimeResetDestructibleParts;
	const bool resetCooldowns = resetConfiguration == nullptr || resetConfiguration->runtimeResetCooldowns;

	if (resetStateMachine) {
		if (EditorComponent* state = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::GenericStateMachine)) {
			state->stateMachineCurrentState = state->stateMachineInitialState;
		}
	}

	if (resetAttributes) {
		if (EditorComponent* attribute = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Attribute)) {
			const auto initialIterator = initialAttributeValues_.find(gameObjectId);
			attribute->attributeCurrent = initialIterator != initialAttributeValues_.end()
				? initialIterator->second
				: attribute->attributeMaximum;
			previousAttributeValues_[gameObjectId] = attribute->attributeCurrent;
		}

		if (EditorComponent* attributeSet = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::AttributeSet)) {
			for (EditorNamedAttributeEntry& entry : attributeSet->attributeSetEntries) {
				const auto ownerIterator = initialNamedAttributeValues_.find(gameObjectId);
				entry.current = entry.maximum;

				if (ownerIterator != initialNamedAttributeValues_.end()) {
					const auto initialIterator = ownerIterator->second.find(entry.name);

					if (initialIterator != ownerIterator->second.end()) {
						entry.current = initialIterator->second;
					}
				}

				previousNamedAttributeValues_[gameObjectId][entry.name] = entry.current;
			}
		}

		if (EditorComponent* counter = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::GenericCounter)) {
			counter->counterCurrentValue = (std::clamp)(
				counter->counterInitialValue,
				counter->counterMinimumValue,
				counter->counterMaximumValue);
			counter->counterWasSatisfied = false;
		}
	}

	if (resetLocks) {
		if (EditorComponent* lock = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::TargetLock)) {
			lock->targetLockProgress = 0.0f;
			lock->targetLockLocked = false;
			lock->targetLockCurrentGameObjectId = -1;
			targetLockRuntimes_[gameObjectId] = {};
		}

		if (EditorComponent* multiLock = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::MultiTargetLock)) {
			multiLock->multiTargetLockTargetGameObjectIds.clear();
			multiLock->multiTargetLockProgressValues.clear();
			multiLock->multiTargetLockCompletedValues.clear();
			multiTargetLostSeconds_.erase(gameObjectId);
		}
	}

	if (resetTimers) {
		if (EditorComponent* timer = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Timer)) {
			timer->timerRemaining = (std::max)(timer->timerDuration, 0.001f);
			timer->timerPaused = !timer->timerPlayOnStart;
		}

		if (EditorComponent* statusEffectSet = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::StatusEffectSet)) {
			statusEffectSet->statusEffectRuntimeEntries.clear();
		}
	}

	if (resetDestructibleParts) {
		if (EditorComponent* part = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::DestructiblePart)) {
			part->destructibleDestroyed = false;

			if (part->destructibleDisableChildren && scriptManager_ != nullptr) {
				for (const int32_t childGameObjectId : gameObject->children) {
					scriptManager_->SetGameObjectActive(childGameObjectId, true);
				}
			}

			std::stringstream componentNames(part->destructibleDisableComponentNames);
			std::string componentName;

			while (std::getline(componentNames, componentName, ';')) {
				if (EditorComponent* resetComponent = FindComponent(gameObjectId, componentName)) {
					resetComponent->isActive = true;
				}
			}
		}
	}

	if (resetCooldowns) {
		if (EditorComponent* cooldownSet = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::CooldownSet)) {
			for (EditorCooldownEntry& entry : cooldownSet->cooldownSetEntries) {
				entry.remaining = entry.startReady ? 0.0f : (std::max)(entry.duration, 0.0f);
				entry.wasRunning = entry.remaining > 0.0f;
			}
		}
	}

	if (EditorComponent* condition = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::GenericCondition)) {
		condition->conditionLastResult = false;
	}

	if (EditorComponent* threatTracker = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::ThreatTracker)) {
		threatTracker->threatTrackerEntries.clear();
		previousThreatGameObjectIds_.erase(gameObjectId);
	}
}

bool EditorRuntimePropertyManager::SetFloat(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	float value) {
	EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (component == nullptr) {
		return false;
	}

	if (componentName == "Ocean") {
		if (propertyName == "WaveHeight") component->oceanWaveHeight = value;
		else if (propertyName == "WindSpeed") component->oceanWindSpeed = value;
		else if (propertyName == "SwellStrength") component->oceanSwellStrength = value;
		else if (propertyName == "Choppiness") component->oceanChoppiness = value;
		else if (propertyName == "FoamStrength") component->oceanFoamStrength = value;
		else if (propertyName == "ReflectionStrength") component->oceanReflectionStrength = value;
		else if (propertyName == "Roughness") component->oceanRoughness = value;
		else return false;
		return true;
	}

	if (componentName == "Light" && propertyName == "Intensity") component->intensity = value;
	else if (componentName == "Camera" && propertyName == "FieldOfView") component->cameraFieldOfView = value;
	else if (componentName == "Camera" && propertyName == "Exposure") component->cameraExposure = value;
	else if (componentName == "PostProcess" && propertyName == "Exposure") component->compositeExposure = value;
	else if (componentName == "PostProcess" && propertyName == "BloomIntensity") component->bloomIntensity = value;
	else if (componentName == "PostProcess" && propertyName == "Saturation") component->compositeSaturation = value;
	else if (componentName == "PostProcess" && propertyName == "Contrast") component->compositeContrast = value;
	else if (componentName == "AudioSource" && propertyName == "Volume") component->audioVolume = value;
	else if (componentName == "AudioSource" && propertyName == "Pitch") component->audioPitch = value;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "Rate") component->particleRate = value;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "Size") component->particleSize = value;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "BillboardStretch") component->particleBillboardStretch = (std::max)(value, 0.01f);
	else if (componentName == "RailMovement" && propertyName == "Speed") component->railSpeed = value;
	else if (componentName == "RailMovement" && propertyName == "Acceleration") component->railAcceleration = (std::max)(value, 0.0f);
	else if (componentName == "RailMovement" && propertyName == "Deceleration") component->railDeceleration = (std::max)(value, 0.0f);
	else if (componentName == "RailMovement" && propertyName == "LookAheadDistance") component->railLookAheadDistance = (std::max)(value, 0.01f);
	else if (componentName == "Timer" && propertyName == "Duration") component->timerDuration = (std::max)(value, 0.001f);
	else if (componentName == "Attribute" && propertyName == "Current") component->attributeCurrent = (std::clamp)(value, component->attributeMinimum, component->attributeMaximum);
	else if (componentName == "Attribute" && propertyName == "Regeneration") component->attributeRegenerationPerSecond = value;
	else if (componentName == "TargetLock" && propertyName == "LockSeconds") component->targetLockSeconds = (std::max)(value, 0.0f);
	else if (componentName == "TargetLock" && propertyName == "Progress") component->targetLockProgress = (std::clamp)(value, 0.0f, 1.0f);
	else if (componentName == "MultiTargetLock" && propertyName == "LockSeconds") component->multiTargetLockSecondsPerTarget = (std::max)(value, 0.0f);
	else if (componentName == "GenericCounter" && propertyName == "Current") return SetCounter(gameObjectId, value);
	else if (componentName == "GenericCounter" && propertyName == "Threshold") component->counterThresholdValue = value;
	else if (componentName == "RailMovement" && propertyName == "OffsetMoveSpeed") component->railOffsetMoveSpeed = (std::max)(value, 0.0f);
	else if (componentName == "RailMovement" && propertyName == "ShipLateralAssist") component->railShipLateralAssist = (std::clamp)(value, 0.0f, 1.0f);
	else if (componentName == "Health" && propertyName == "Current") component->healthCurrent = value;
	else if (componentName == "TargetSteering" && propertyName == "MaximumSpeed") component->targetSteeringMaximumSpeed = value;
	else if (componentName == "TargetSteering" && propertyName == "TurnSpeed") component->targetSteeringTurnSpeed = value;
	// 相対移動のOffsetや目標距離もScriptから差し替えられるようにし、
	// 同じ敵Prefabのまま左右どちらから並走するかなどをWave側で振り分けられるようにする。
	else if (componentName == "TargetSteering" && propertyName == "SideOffset") component->targetSteeringSideOffset = value;
	else if (componentName == "TargetSteering" && propertyName == "ForwardOffset") component->targetSteeringForwardOffset = value;
	else if (componentName == "TargetSteering" && propertyName == "TargetDistance") component->targetSteeringTargetDistance = (std::max)(value, 0.0f);
	else if (componentName == "TargetSteering" && propertyName == "Duration") component->targetSteeringDuration = (std::max)(value, 0.0f);
	else if (componentName == "TargetPoint" && propertyName == "Priority") component->targetPointPriority = value;
	else if (componentName == "TargetPoint" && propertyName == "Radius") component->targetPointRadius = (std::max)(value, 0.01f);
	else if (componentName == "TargetSelector" && propertyName == "OceanClearance") component->targetSelectorOceanClearance = (std::max)(value, 0.0f);
	else if (componentName == "WeaponAccuracy" && propertyName == "BaseSpread") component->weaponAccuracyBaseSpread = (std::max)(value, 0.0f);
	else if (componentName == "WeaponAccuracy" && propertyName == "CurrentSpread") component->weaponAccuracyCurrentSpread = (std::max)(value, 0.0f);
	else if (componentName == "TimeScale" && propertyName == "Scale") component->timeScaleValue = (std::clamp)(value, 0.0f, 4.0f);
	else if (componentName == "TimeScale" && propertyName == "Duration") component->timeScaleDuration = (std::max)(value, 0.001f);
	else return false;

	return true;
}

bool EditorRuntimePropertyManager::GetFloat(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	float& value) const {
	const EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (component == nullptr) return false;
	if (componentName == "Ocean" && propertyName == "WaveHeight") value = component->oceanWaveHeight;
	else if (componentName == "Ocean" && propertyName == "WindSpeed") value = component->oceanWindSpeed;
	else if (componentName == "Ocean" && propertyName == "SwellStrength") value = component->oceanSwellStrength;
	else if (componentName == "Ocean" && propertyName == "Choppiness") value = component->oceanChoppiness;
	else if (componentName == "Ocean" && propertyName == "FoamStrength") value = component->oceanFoamStrength;
	else if (componentName == "Ocean" && propertyName == "ReflectionStrength") value = component->oceanReflectionStrength;
	else if (componentName == "Ocean" && propertyName == "Roughness") value = component->oceanRoughness;
	else if (componentName == "Light" && propertyName == "Intensity") value = component->intensity;
	else if (componentName == "Camera" && propertyName == "FieldOfView") value = component->cameraFieldOfView;
	else if (componentName == "Camera" && propertyName == "Exposure") value = component->cameraExposure;
	else if (componentName == "PostProcess" && propertyName == "Exposure") value = component->compositeExposure;
	else if (componentName == "PostProcess" && propertyName == "BloomIntensity") value = component->bloomIntensity;
	else if (componentName == "PostProcess" && propertyName == "Saturation") value = component->compositeSaturation;
	else if (componentName == "PostProcess" && propertyName == "Contrast") value = component->compositeContrast;
	else if (componentName == "AudioSource" && propertyName == "Volume") value = component->audioVolume;
	else if (componentName == "AudioSource" && propertyName == "Pitch") value = component->audioPitch;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "Rate") value = component->particleRate;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "Size") value = component->particleSize;
	else if ((componentName == "ParticleSystem" || componentName == "VisualEffect") && propertyName == "BillboardStretch") value = component->particleBillboardStretch;
	else if (componentName == "RailMovement" && propertyName == "Speed") value = component->railSpeed;
	else if (componentName == "RailMovement" && propertyName == "Acceleration") value = component->railAcceleration;
	else if (componentName == "RailMovement" && propertyName == "Deceleration") value = component->railDeceleration;
	else if (componentName == "RailMovement" && propertyName == "LookAheadDistance") value = component->railLookAheadDistance;
	else if (componentName == "Timer" && propertyName == "Duration") value = component->timerDuration;
	else if (componentName == "Timer" && propertyName == "Remaining") value = component->timerRemaining;
	else if (componentName == "Attribute" && propertyName == "Current") value = component->attributeCurrent;
	else if (componentName == "Attribute" && propertyName == "Maximum") value = component->attributeMaximum;
	else if (componentName == "Attribute" && propertyName == "Regeneration") value = component->attributeRegenerationPerSecond;
	else if (componentName == "TargetLock" && propertyName == "LockSeconds") value = component->targetLockSeconds;
	else if (componentName == "TargetLock" && propertyName == "Progress") value = component->targetLockProgress;
	else if (componentName == "MultiTargetLock" && propertyName == "LockSeconds") value = component->multiTargetLockSecondsPerTarget;
	else if (componentName == "GenericCounter" && propertyName == "Current") value = component->counterCurrentValue;
	else if (componentName == "GenericCounter" && propertyName == "Threshold") value = component->counterThresholdValue;
	else if (componentName == "RailMovement" && propertyName == "OffsetMoveSpeed") value = component->railOffsetMoveSpeed;
	else if (componentName == "RailMovement" && propertyName == "ShipLateralAssist") value = component->railShipLateralAssist;
	else if (componentName == "Health" && propertyName == "Current") value = component->healthCurrent;
	// HP割合でBoss段階増援などを判定できるよう、最大値も読めるようにする。
	else if (componentName == "Health" && propertyName == "Maximum") value = component->healthMaximum;
	else if (componentName == "TargetSteering" && propertyName == "MaximumSpeed") value = component->targetSteeringMaximumSpeed;
	else if (componentName == "TargetSteering" && propertyName == "TurnSpeed") value = component->targetSteeringTurnSpeed;
	else if (componentName == "TargetSteering" && propertyName == "SideOffset") value = component->targetSteeringSideOffset;
	else if (componentName == "TargetSteering" && propertyName == "ForwardOffset") value = component->targetSteeringForwardOffset;
	else if (componentName == "TargetSteering" && propertyName == "TargetDistance") value = component->targetSteeringTargetDistance;
	else if (componentName == "TargetSteering" && propertyName == "Duration") value = component->targetSteeringDuration;
	else if (componentName == "TargetPoint" && propertyName == "Priority") value = component->targetPointPriority;
	else if (componentName == "TargetPoint" && propertyName == "Radius") value = component->targetPointRadius;
	else if (componentName == "TargetSelector" && propertyName == "OceanClearance") value = component->targetSelectorOceanClearance;
	else if (componentName == "WeaponAccuracy" && propertyName == "BaseSpread") value = component->weaponAccuracyBaseSpread;
	else if (componentName == "WeaponAccuracy" && propertyName == "CurrentSpread") value = component->weaponAccuracyCurrentSpread;
	else if (componentName == "TimeScale" && propertyName == "Scale") value = component->timeScaleValue;
	else if (componentName == "TimeScale" && propertyName == "Duration") value = component->timeScaleDuration;
	else return false;

	return true;
}

bool EditorRuntimePropertyManager::SetInt(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	int32_t value) {
	EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (componentName == "WeaponLoadout" && component != nullptr && propertyName == "SelectedSlot") {
		component->weaponLoadoutSelectedSlotIndex = value;
		return true;
	}

	if (componentName == "Team" && component != nullptr && propertyName == "TeamId") {
		component->teamId = value;
		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "TeamFilter") {
		component->targetSelectorTeamFilter = (std::clamp)(value, 0, 3);
		return true;
	}

	// 敵1体の行動遷移(Chase→Parallel→Retreatなど)をScriptから切り替えるための入口。
	// Runtime側の現在Modeも同時に更新するため、TargetingManagerへ委譲する。
	if (componentName == "TargetSteering" && component != nullptr && propertyName == "MoveMode") {
		component->targetSteeringMoveMode = (std::clamp)(value, 0, 6);

		if (targetingManager_ != nullptr) {
			targetingManager_->SetSteeringMoveMode(gameObjectId, component->targetSteeringMoveMode);
		}

		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "SpecificTeamId") {
		component->targetSelectorSpecificTeamId = value;
		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "OcclusionMode") {
		component->targetSelectorOcclusionMode = (std::clamp)(value, 0, 3);
		return true;
	}

	if ((componentName == "ParticleSystem" || componentName == "VisualEffect") &&
		component != nullptr && propertyName == "BillboardMode") {
		component->particleBillboardMode = (std::clamp)(value, 0, 3);
		return true;
	}

	if (componentName == "RailMovement" && component != nullptr && propertyName == "MovementMode") {
		component->railMovementMode = (std::clamp)(value, 0, 2);
		return true;
	}

	if (componentName == "RailMovement" && component != nullptr && propertyName == "LocalForwardAxis") {
		component->railLocalForwardAxis = (std::clamp)(value, 0, 3);
		return true;
	}

	if ((componentName == "Camera" || componentName == "CinemachineCamera") &&
		component != nullptr && propertyName == "FollowPositionSpace") {
		component->cameraFollowPositionSpace = (std::clamp)(value, 0, 1);
		return true;
	}

	if ((componentName == "Camera" || componentName == "CinemachineCamera") &&
		component != nullptr && propertyName == "FollowRotationMode") {
		component->cameraFollowRotationMode = (std::clamp)(value, 0, 2);
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::GetInt(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	int32_t& value) const {
	const EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (componentName == "WeaponLoadout" && component != nullptr && propertyName == "SelectedSlot") {
		value = component->weaponLoadoutSelectedSlotIndex;
		return true;
	}

	if (componentName == "Team" && component != nullptr && propertyName == "TeamId") {
		value = component->teamId;
		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "TeamFilter") {
		value = component->targetSelectorTeamFilter;
		return true;
	}

	if (componentName == "TargetSteering" && component != nullptr && propertyName == "MoveMode") {
		// Runtime遷移後の実際のModeを優先して返す。
		if (targetingManager_ != nullptr && targetingManager_->GetSteeringMoveMode(gameObjectId, value)) {
			return true;
		}

		value = component->targetSteeringMoveMode;
		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "SpecificTeamId") {
		value = component->targetSelectorSpecificTeamId;
		return true;
	}

	if (componentName == "TargetSelector" && component != nullptr && propertyName == "OcclusionMode") {
		value = component->targetSelectorOcclusionMode;
		return true;
	}

	if ((componentName == "ParticleSystem" || componentName == "VisualEffect") &&
		component != nullptr && propertyName == "BillboardMode") {
		value = component->particleBillboardMode;
		return true;
	}

	if (componentName == "RailMovement" && component != nullptr && propertyName == "MovementMode") {
		value = component->railMovementMode;
		return true;
	}

	if (componentName == "RailMovement" && component != nullptr && propertyName == "LocalForwardAxis") {
		value = component->railLocalForwardAxis;
		return true;
	}

	if ((componentName == "Camera" || componentName == "CinemachineCamera") &&
		component != nullptr && propertyName == "FollowPositionSpace") {
		value = component->cameraFollowPositionSpace;
		return true;
	}

	if ((componentName == "Camera" || componentName == "CinemachineCamera") &&
		component != nullptr && propertyName == "FollowRotationMode") {
		value = component->cameraFollowRotationMode;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::SetBool(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	bool value) {
	if (componentName == "GameObject" && propertyName == "Active" && editorScene_ != nullptr) {
		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

		if (gameObject != nullptr) {
			gameObject->isActive = value;
			return true;
		}
	}

	EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (component == nullptr) return false;
	if (propertyName == "Active") component->isActive = value;
	else if (componentName == "Camera" && propertyName == "DofEnabled") component->cameraDofEnabled = value;
	else if (componentName == "AudioSource" && propertyName == "Loop") component->audioLoop = value;
	else if (componentName == "RailMovement" && (propertyName == "Paused" || propertyName == "StartPaused")) component->railStartPaused = value;
	else if (componentName == "RailMovement" && propertyName == "Loop") component->railLoop = value;
	else if (componentName == "RailMovement" && propertyName == "OrientToPath") component->railOrientToPath = value;
	else if (componentName == "RailMovement" && propertyName == "StopAtEnd") component->railStopAtEnd = value;
	else if (componentName == "RailMovement" && propertyName == "UsePlayerInput") component->railUsePlayerInput = value;
	else if (componentName == "RailMovement" && propertyName == "ShipHorizontalThrust") component->railShipHorizontalThrust = value;
	else if (componentName == "Team" && propertyName == "Targetable") component->teamTargetable = value;
	else if (componentName == "TargetSelector" && propertyName == "IncludeNeutral") component->targetSelectorIncludeNeutral = value;
	else return false;
	return true;
}

bool EditorRuntimePropertyManager::GetBool(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	bool& value) const {
	if (componentName == "GameObject" && propertyName == "Active" && editorScene_ != nullptr) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr) return false;
		value = gameObject->isActive;
		return true;
	}

	const EditorComponent* component = FindComponent(gameObjectId, componentName);
	if (component == nullptr) return false;
	if (propertyName == "Active") value = component->isActive;
	else if (componentName == "Camera" && propertyName == "DofEnabled") value = component->cameraDofEnabled;
	else if (componentName == "AudioSource" && propertyName == "Loop") value = component->audioLoop;
	else if (componentName == "RailMovement" && (propertyName == "Paused" || propertyName == "StartPaused")) value = component->railStartPaused;
	else if (componentName == "RailMovement" && propertyName == "Loop") value = component->railLoop;
	else if (componentName == "RailMovement" && propertyName == "OrientToPath") value = component->railOrientToPath;
	else if (componentName == "RailMovement" && propertyName == "StopAtEnd") value = component->railStopAtEnd;
	else if (componentName == "RailMovement" && propertyName == "UsePlayerInput") value = component->railUsePlayerInput;
	else if (componentName == "RailMovement" && propertyName == "ShipHorizontalThrust") value = component->railShipHorizontalThrust;
	else if (componentName == "Team" && propertyName == "Targetable") value = component->teamTargetable;
	else if (componentName == "TargetSelector" && propertyName == "IncludeNeutral") value = component->targetSelectorIncludeNeutral;
	else return false;
	return true;
}

bool EditorRuntimePropertyManager::SetVector2(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	const EditorScriptVector2& value) {
	EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (component == nullptr) {
		return false;
	}

	if (componentName == "RailMovement" && propertyName == "MovementRange") {
		component->railMovementRange = {
			(std::max)(value.x, 0.0f),
			(std::max)(value.y, 0.0f)};
		return true;
	}

	if (componentName == "RailMovement" && propertyName == "StartOffset") {
		component->railStartOffset = {
			(std::clamp)(value.x, -component->railMovementRange.x, component->railMovementRange.x),
			(std::clamp)(value.y, -component->railMovementRange.y, component->railMovementRange.y)};
		return true;
	}

	if (componentName == "MovementModifier" && propertyName == "InputRange") {
		component->movementModifierInputRange = {
			(std::max)(value.x, 0.0f),
			(std::max)(value.y, 0.0f)};
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::GetVector2(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	EditorScriptVector2& value) const {
	const EditorComponent* component = FindComponent(gameObjectId, componentName);

	if (component == nullptr) {
		return false;
	}

	if (componentName == "RailMovement" && propertyName == "MovementRange") {
		value = component->railMovementRange;
		return true;
	}

	if (componentName == "RailMovement" && propertyName == "StartOffset") {
		value = component->railStartOffset;
		return true;
	}

	if (componentName == "MovementModifier" && propertyName == "InputRange") {
		value = component->movementModifierInputRange;
		return true;
	}

	return false;
}

bool EditorRuntimePropertyManager::SetVector3(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	const Vector3& value) {
	if (componentName == "Transform" && editorScene_ != nullptr) {
		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr) return false;
		if (propertyName == "Position") gameObject->translate = value;
		else if (propertyName == "Rotation") gameObject->rotate = value;
		else if (propertyName == "Scale") gameObject->scale = value;
		else return false;
		return true;
	}

	EditorComponent* component = FindComponent(gameObjectId, componentName);
	if (component == nullptr) return false;
	if (componentName == "MovementModifier" && propertyName == "PositionOffset") component->movementModifierLocalPositionOffset = value;
	else if (componentName == "MovementModifier" && propertyName == "RotationOffset") component->movementModifierLocalRotationOffset = value;
	else if (componentName == "TargetPoint" && propertyName == "AimOffset") component->targetPointAimOffset = value;
	else return false;
	return true;
}

bool EditorRuntimePropertyManager::GetVector3(
	int32_t gameObjectId,
	const std::string& componentName,
	const std::string& propertyName,
	Vector3& value) const {
	if (componentName == "Transform" && editorScene_ != nullptr) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr) return false;
		if (propertyName == "Position") value = gameObject->translate;
		else if (propertyName == "Rotation") value = gameObject->rotate;
		else if (propertyName == "Scale") value = gameObject->scale;
		else return false;
		return true;
	}

	const EditorComponent* component = FindComponent(gameObjectId, componentName);
	if (component == nullptr) return false;
	if (componentName == "MovementModifier" && propertyName == "PositionOffset") value = component->movementModifierLocalPositionOffset;
	else if (componentName == "MovementModifier" && propertyName == "RotationOffset") value = component->movementModifierLocalRotationOffset;
	else if (componentName == "TargetPoint" && propertyName == "AimOffset") value = component->targetPointAimOffset;
	else return false;
	return true;
}

bool EditorRuntimePropertyManager::PlayTween(int32_t tweenGameObjectId) {
	if (FindTweenComponent(tweenGameObjectId) == nullptr) return false;
	tweenRuntimes_[tweenGameObjectId] = {0.0f, true};
	return true;
}

bool EditorRuntimePropertyManager::StopTween(int32_t tweenGameObjectId) {
	auto runtimeIterator = tweenRuntimes_.find(tweenGameObjectId);
	if (runtimeIterator == tweenRuntimes_.end()) return false;
	runtimeIterator->second.isPlaying = false;
	return true;
}

bool EditorRuntimePropertyManager::IsTweenPlaying(int32_t tweenGameObjectId) const {
	const auto runtimeIterator = tweenRuntimes_.find(tweenGameObjectId);
	return runtimeIterator != tweenRuntimes_.end() && runtimeIterator->second.isPlaying;
}

bool EditorRuntimePropertyManager::Relay(int32_t relayGameObjectId) {
	const EditorGameObject* relayGameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(relayGameObjectId)
		: nullptr;
	const EditorComponent* relayComponent = relayGameObject != nullptr
		? EditorComponentUtility::FindComponent(*relayGameObject, EditorComponentType::ActionRelay)
		: nullptr;

	if (relayGameObject == nullptr || relayComponent == nullptr || !relayComponent->isActive ||
		scriptManager_ == nullptr) {
		return false;
	}

	bool wasRelayed = false;

	for (const int32_t childGameObjectId : relayGameObject->children) {
		const EditorGameObject* childGameObject = editorScene_->FindGameObject(childGameObjectId);
		const EditorComponent* targetComponent = childGameObject != nullptr
			? EditorComponentUtility::FindComponent(*childGameObject, EditorComponentType::ActionRelayTarget)
			: nullptr;

		if (targetComponent == nullptr || !targetComponent->isActive ||
			!targetComponent->actionRelayTargetEnabled || targetComponent->actionRelayActionName.empty()) {
			continue;
		}

		const int32_t targetGameObjectId = targetComponent->actionRelayTargetGameObjectId >= 0
			? targetComponent->actionRelayTargetGameObjectId
			: relayGameObjectId;
		scriptManager_->QueueActionEvent(targetGameObjectId, targetComponent->actionRelayActionName);
		wasRelayed = true;
	}

	return wasRelayed;
}

bool EditorRuntimePropertyManager::EvaluateConditionComponent(
	int32_t gameObjectId,
	EditorComponent& condition,
	bool notifyAction) {
	const int32_t sourceGameObjectId = condition.conditionSourceGameObjectId >= 0
		? condition.conditionSourceGameObjectId
		: gameObjectId;
	bool result = false;
	bool hasValue = false;
	float numberValue = 0.0f;

	if (condition.conditionSourceType == 0) {
		hasValue = GetFloat(
			sourceGameObjectId,
			condition.conditionComponentName,
			condition.conditionPropertyName,
			numberValue);
	}
	else if (condition.conditionSourceType == 1) {
		int32_t intValue = 0;
		hasValue = GetInt(
			sourceGameObjectId,
			condition.conditionComponentName,
			condition.conditionPropertyName,
			intValue);
		numberValue = static_cast<float>(intValue);
	}
	else if (condition.conditionSourceType == 2) {
		bool boolValue = false;
		hasValue = GetBool(
			sourceGameObjectId,
			condition.conditionComponentName,
			condition.conditionPropertyName,
			boolValue);
		numberValue = boolValue ? 1.0f : 0.0f;
	}
	else if (condition.conditionSourceType == 3) {
		float maximum = 0.0f;
		hasValue = GetNamedAttribute(sourceGameObjectId, condition.conditionPropertyName, numberValue, maximum);
	}
	else if (condition.conditionSourceType == 4) {
		hasValue = GetCounter(sourceGameObjectId, numberValue);
	}
	else if (condition.conditionSourceType == 5) {
		const EditorGameObject* sourceGameObject = editorScene_ != nullptr
			? editorScene_->FindGameObject(sourceGameObjectId)
			: nullptr;
		hasValue = sourceGameObject != nullptr;
		numberValue = sourceGameObject != nullptr && sourceGameObject->isActive ? 1.0f : 0.0f;
	}
	else if (condition.conditionSourceType == 6) {
		std::string stateName;
		hasValue = GetState(sourceGameObjectId, stateName);
		result = condition.conditionCompareMode == 5
			? stateName != condition.conditionCompareString
			: stateName == condition.conditionCompareString;
	}
	else if (condition.conditionSourceType == 7) {
		float progress = 0.0f;
		bool isLocked = false;
		int32_t targetGameObjectId = -1;
		hasValue = GetTargetLockState(sourceGameObjectId, progress, isLocked, targetGameObjectId);

		if (!hasValue) {
			int32_t targetCount = 0;
			hasValue = GetMultiTargetLockCount(sourceGameObjectId, targetCount);
			isLocked = false;

			for (int32_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
				if (GetMultiTargetLockTarget(
						sourceGameObjectId,
						targetIndex,
						targetGameObjectId,
						progress,
						isLocked) && isLocked) {
					break;
				}
			}
		}

		numberValue = isLocked ? 1.0f : 0.0f;
	}

	if (condition.conditionSourceType != 6) {
		result = hasValue && CompareNumber(numberValue, condition.conditionCompareFloat, condition.conditionCompareMode);
	}

	const bool resultChanged = result != condition.conditionLastResult;
	condition.conditionLastResult = result;

	if (!notifyAction || scriptManager_ == nullptr ||
		(condition.conditionFireOnChangeOnly && !resultChanged)) {
		return result;
	}

	const int32_t actionTargetId = condition.conditionActionTargetGameObjectId >= 0
		? condition.conditionActionTargetGameObjectId
		: gameObjectId;
	const std::string& actionName = result
		? condition.conditionTrueActionName
		: condition.conditionFalseActionName;

	if (!actionName.empty()) {
		scriptManager_->QueueActionPayload(actionTargetId, actionName, MakeFloatPayload(numberValue));
	}

	return result;
}

EditorComponent* EditorRuntimePropertyManager::FindComponent(
	int32_t gameObjectId,
	const std::string& componentName) const {
	for (const ComponentNameEntry& entry : kRuntimeComponentNames) {
		if (componentName == entry.name) return FindTypedComponent(gameObjectId, entry.type);
	}
	return nullptr;
}

EditorComponent* EditorRuntimePropertyManager::FindTypedComponent(
	int32_t gameObjectId,
	EditorComponentType componentType) const {
	EditorGameObject* gameObject = editorScene_ != nullptr ? editorScene_->FindGameObject(gameObjectId) : nullptr;
	return gameObject != nullptr ? EditorComponentUtility::FindComponent(*gameObject, componentType) : nullptr;
}

EditorComponent* EditorRuntimePropertyManager::FindTweenComponent(int32_t tweenGameObjectId) const {
	return FindTypedComponent(tweenGameObjectId, EditorComponentType::PropertyTween);
}

float EditorRuntimePropertyManager::EvaluateCurve(int32_t curveType, float normalizedTime) const {
	const float time = (std::clamp)(normalizedTime, 0.0f, 1.0f);
	if (curveType == 1) return time * time * (3.0f - 2.0f * time);
	if (curveType == 2) return time * time;
	if (curveType == 3) return 1.0f - (1.0f - time) * (1.0f - time);
	return time;
}
