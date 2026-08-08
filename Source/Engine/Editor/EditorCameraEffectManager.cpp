#include "EditorCameraEffectManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace EditorSharedState;

namespace {
	float Lerp(float sourceValue, float targetValue, float ratio) {
		return sourceValue + (targetValue - sourceValue) * ratio;
	}

	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 MultiplyVector3(float scalar, const Vector3& value) {
		return {scalar * value.x, scalar * value.y, scalar * value.z};
	}

	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	float LengthVector3(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 RotateAroundYaw(const Vector3& value, float yaw) {
		const float cosine = std::cos(yaw);
		const float sine = std::sin(yaw);
		return {
			value.x * cosine + value.z * sine,
			value.y,
			-value.x * sine + value.z * cosine};
	}

	float CalculateDampingRatio(float damping, float deltaTime) {
		return 1.0f - std::exp(-(std::max)(damping, 0.0f) * deltaTime);
	}
}

void EditorCameraEffectManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
	blendRuntime_ = {};
	shakeRuntimes_.clear();
	followComposerWasActive_ = false;
	isStarted_ = false;
}

void EditorCameraEffectManager::Start() {
	blendRuntime_ = {};
	shakeRuntimes_.clear();
	g_runtimeGameCameraOverrideActive = false;
	g_runtimeGameCameraPositionOffset = {0.0f, 0.0f, 0.0f};
	g_runtimeGameCameraRotationOffset = {0.0f, 0.0f, 0.0f};
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	std::vector<int32_t> blendOwnerIds;
	std::vector<int32_t> shakeOwnerIds;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* blendComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraBlend);
		const EditorComponent* shakeComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraShake);

		if (gameObject.isActive && blendComponent != nullptr && blendComponent->isActive &&
			blendComponent->cameraBlendPlayOnStart) {
			blendOwnerIds.push_back(gameObject.id);
		}

		if (gameObject.isActive && shakeComponent != nullptr && shakeComponent->isActive &&
			shakeComponent->cameraShakePlayOnStart) {
			shakeOwnerIds.push_back(gameObject.id);
		}
	}

	for (const int32_t blendOwnerId : blendOwnerIds) {
		PlayBlend(blendOwnerId);
	}

	for (const int32_t shakeOwnerId : shakeOwnerIds) {
		PlayShake(shakeOwnerId);
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* composer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraFollowComposer);

		if (composer != nullptr) {
			composer->cameraComposerRuntimeInitialized = false;
		}
	}
}

void EditorCameraEffectManager::Update(float deltaTime) {
	if (!isStarted_ || deltaTime < 0.0f) {
		return;
	}

	const bool followComposerApplied = !blendRuntime_.isActive && UpdateFollowComposer(deltaTime);

	if (blendRuntime_.isActive) {
		blendRuntime_.elapsedTime += deltaTime;
		float blendRatio = blendRuntime_.duration > 0.0f
			? (std::clamp)(blendRuntime_.elapsedTime / blendRuntime_.duration, 0.0f, 1.0f)
			: 1.0f;

		if (blendRuntime_.easing == 1) {
			blendRatio = blendRatio * blendRatio * (3.0f - 2.0f * blendRatio);
		}

		g_runtimeGameCameraOverrideTransform = LerpTransform(
			blendRuntime_.sourceTransform,
			blendRuntime_.targetTransform,
			blendRatio);
		g_runtimeGameCameraOverrideActive = true;

		if (blendRuntime_.elapsedTime >= blendRuntime_.duration) {
			blendRuntime_.isActive = false;  // 最終姿勢は次のBlendまたはStopまで保持する。
			g_runtimeGameCameraOverrideTransform = blendRuntime_.targetTransform;
		}
	}

	Vector3 positionOffset{0.0f, 0.0f, 0.0f};
	Vector3 rotationOffset{0.0f, 0.0f, 0.0f};
	const EditorComponent* mixer = nullptr;
	const float speedFeedbackStrength = UpdateSpeedFeedback(deltaTime);

	if (editorScene_ != nullptr) {
		for (const EditorGameObject& object : editorScene_->GetGameObjects()) {
			const EditorComponent* candidate = EditorComponentUtility::FindComponent(
				object,
				EditorComponentType::CameraFeedbackMixer);

			if (object.isActive && candidate != nullptr && candidate->isActive) {
				mixer = candidate;
				break;
			}
		}
	}

	int32_t highestPriority = INT32_MIN;

	for (size_t shakeIndex = 0u; shakeIndex < shakeRuntimes_.size();) {
		ShakeRuntime& shakeRuntime = shakeRuntimes_[shakeIndex];
		shakeRuntime.elapsedTime += deltaTime;
		const float normalizedTime = shakeRuntime.duration > 0.0f
			? (std::clamp)(shakeRuntime.elapsedTime / shakeRuntime.duration, 0.0f, 1.0f)
			: 1.0f;
		const float decay = 1.0f - normalizedTime;
		const float phase = shakeRuntime.elapsedTime * shakeRuntime.frequency * 2.0f * std::numbers::pi_v<float>;
		const Vector3 positionWave{
			std::sin(phase),
			std::sin(phase * 1.37f + 1.1f),
			std::sin(phase * 0.83f + 2.2f)};
		const Vector3 rotationWave{
			std::sin(phase * 1.19f + 0.5f),
			std::sin(phase * 0.91f + 1.7f),
			std::sin(phase * 1.51f + 2.8f)};
		const Vector3 currentPosition = MultiplyVector3(decay, {
				positionWave.x * shakeRuntime.positionAmplitude.x,
				positionWave.y * shakeRuntime.positionAmplitude.y,
				positionWave.z * shakeRuntime.positionAmplitude.z});
		const Vector3 currentRotation = MultiplyVector3(decay, {
				rotationWave.x * shakeRuntime.rotationAmplitude.x,
				rotationWave.y * shakeRuntime.rotationAmplitude.y,
				rotationWave.z * shakeRuntime.rotationAmplitude.z});

		if (mixer != nullptr && mixer->cameraFeedbackMixMode == 1) {
			if (shakeRuntime.priority >= highestPriority) {
				highestPriority = shakeRuntime.priority;
				positionOffset = currentPosition;
				rotationOffset = currentRotation;
			}
		}
		else {
			positionOffset = AddVector3(positionOffset, currentPosition);
			rotationOffset = AddVector3(rotationOffset, currentRotation);
		}

		if (shakeRuntime.elapsedTime >= shakeRuntime.duration) {
			shakeRuntimes_.erase(shakeRuntimes_.begin() + static_cast<std::ptrdiff_t>(shakeIndex));
			continue;
		}

		shakeIndex++;
	}

	if (mixer != nullptr) {
		const float strength =
			(std::max)(mixer->cameraFeedbackGlobalStrength, 0.0f) * speedFeedbackStrength;
		positionOffset = MultiplyVector3(strength, positionOffset);
		rotationOffset = MultiplyVector3(strength, rotationOffset);
		positionOffset = {
			(std::clamp)(positionOffset.x, -mixer->cameraFeedbackMaximumPosition.x, mixer->cameraFeedbackMaximumPosition.x),
			(std::clamp)(positionOffset.y, -mixer->cameraFeedbackMaximumPosition.y, mixer->cameraFeedbackMaximumPosition.y),
			(std::clamp)(positionOffset.z, -mixer->cameraFeedbackMaximumPosition.z, mixer->cameraFeedbackMaximumPosition.z)};
		rotationOffset = {
			(std::clamp)(rotationOffset.x, -mixer->cameraFeedbackMaximumRotation.x, mixer->cameraFeedbackMaximumRotation.x),
			(std::clamp)(rotationOffset.y, -mixer->cameraFeedbackMaximumRotation.y, mixer->cameraFeedbackMaximumRotation.y),
			(std::clamp)(rotationOffset.z, -mixer->cameraFeedbackMaximumRotation.z, mixer->cameraFeedbackMaximumRotation.z)};
	}

	g_runtimeGameCameraPositionOffset = positionOffset;
	g_runtimeGameCameraRotationOffset = rotationOffset;

	if (!blendRuntime_.isActive && !followComposerApplied && followComposerWasActive_) {
		g_runtimeGameCameraOverrideActive = false;
	}

	followComposerWasActive_ = followComposerApplied;
}

void EditorCameraEffectManager::Stop() {
	blendRuntime_ = {};
	shakeRuntimes_.clear();
	followComposerWasActive_ = false;
	g_runtimeGameCameraOverrideActive = false;
	g_runtimeGameCameraPositionOffset = {0.0f, 0.0f, 0.0f};
	g_runtimeGameCameraRotationOffset = {0.0f, 0.0f, 0.0f};

	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* composer = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::CameraFollowComposer);

			if (composer != nullptr) {
				composer->cameraComposerRuntimeInitialized = false;
			}
		}
	}

	isStarted_ = false;
}

bool EditorCameraEffectManager::PlayBlend(int32_t componentOwnerGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(componentOwnerGameObjectId);
	if (ownerGameObject == nullptr || !ownerGameObject->isActive) {
		return false;
	}

	const EditorComponent* blendComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::CameraBlend);

	if (blendComponent == nullptr || !blendComponent->isActive) {
		return false;
	}

	const EditorGameObject* targetGameObject = editorScene_->FindGameObject(
		blendComponent->cameraBlendTargetGameObjectId);

	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		return false;
	}

	Transforms sourceTransform{};
	if (blendComponent->cameraBlendSourceGameObjectId >= 0) {
		const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(
			blendComponent->cameraBlendSourceGameObjectId);

		if (sourceGameObject == nullptr || !sourceGameObject->isActive) {
			return false;
		}

		sourceTransform = ResolveWorldTransform(*sourceGameObject);
	}
	else if (g_runtimeGameCameraOverrideActive) {
		sourceTransform = g_runtimeGameCameraOverrideTransform;
	}
	else if (!FindHighestPriorityCameraTransform(sourceTransform)) {
		return false;
	}

	blendRuntime_.sourceTransform = sourceTransform;
	blendRuntime_.targetTransform = ResolveWorldTransform(*targetGameObject);
	blendRuntime_.elapsedTime = 0.0f;
	blendRuntime_.duration = (std::max)(blendComponent->cameraBlendDuration, 0.0f);
	blendRuntime_.easing = (std::clamp)(blendComponent->cameraBlendEasing, 0, 1);
	blendRuntime_.isActive = true;
	g_runtimeGameCameraOverrideTransform = sourceTransform;
	g_runtimeGameCameraOverrideActive = true;
	return true;
}

bool EditorCameraEffectManager::PlayShake(int32_t componentOwnerGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(componentOwnerGameObjectId);
	if (ownerGameObject == nullptr || !ownerGameObject->isActive) {
		return false;
	}

	const EditorComponent* shakeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::CameraShake);

	if (shakeComponent == nullptr || !shakeComponent->isActive) {
		return false;
	}
	const EditorComponent* mixer = nullptr;

	for (const EditorGameObject& object : editorScene_->GetGameObjects()) {
		const EditorComponent* candidate = EditorComponentUtility::FindComponent(
			object,
			EditorComponentType::CameraFeedbackMixer);

		if (object.isActive && candidate != nullptr && candidate->isActive) {
			mixer = candidate;
			break;
		}
	}

	if (mixer != nullptr && static_cast<int32_t>(shakeRuntimes_.size()) >=
		(std::max)(mixer->cameraFeedbackMaximumConcurrent, 1)) {
		return false;
	}

	ShakeRuntime shakeRuntime{};
	shakeRuntime.positionAmplitude = shakeComponent->cameraShakePositionAmplitude;
	shakeRuntime.rotationAmplitude = shakeComponent->cameraShakeRotationAmplitude;
	shakeRuntime.frequency = (std::max)(shakeComponent->cameraShakeFrequency, 0.0f);
	shakeRuntime.duration = (std::max)(shakeComponent->cameraShakeDuration, 0.01f);
	shakeRuntime.priority = shakeComponent->cameraShakePriority;
	shakeRuntimes_.push_back(shakeRuntime);
	return true;
}

Transforms EditorCameraEffectManager::ResolveWorldTransform(const EditorGameObject& gameObject) const {
	Transforms worldTransform{gameObject.scale, gameObject.rotate, gameObject.translate};
	if (editorScene_ != nullptr) {
		editorScene_->GetWorldTransform(
			gameObject.id,
			worldTransform.scale,
			worldTransform.rotate,
			worldTransform.translate);
	}

	return worldTransform;
}

bool EditorCameraEffectManager::FindHighestPriorityCameraTransform(Transforms& cameraTransform) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* selectedCameraGameObject = nullptr;
	int32_t selectedPriority = INT32_MIN;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Camera);

		if (cameraComponent == nullptr || !cameraComponent->isActive) {
			cameraComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::CinemachineCamera);
		}

		if (cameraComponent == nullptr || !cameraComponent->isActive ||
			cameraComponent->cameraPriority <= selectedPriority) {
			continue;
		}

		selectedCameraGameObject = &gameObject;
		selectedPriority = cameraComponent->cameraPriority;
	}

	if (selectedCameraGameObject == nullptr) {
		return false;
	}

	cameraTransform = ResolveWorldTransform(*selectedCameraGameObject);
	return true;
}

EditorGameObject* EditorCameraEffectManager::FindHighestPriorityCameraGameObject() const {
	if (editorScene_ == nullptr) {
		return nullptr;
	}

	EditorGameObject* selectedCameraGameObject = nullptr;
	int32_t selectedPriority = INT32_MIN;

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Camera);

		if (cameraComponent == nullptr || !cameraComponent->isActive) {
			cameraComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::CinemachineCamera);
		}

		if (cameraComponent == nullptr || !cameraComponent->isActive ||
			cameraComponent->cameraPriority <= selectedPriority) {
			continue;
		}

		selectedCameraGameObject = &gameObject;
		selectedPriority = cameraComponent->cameraPriority;
	}

	return selectedCameraGameObject;
}

bool EditorCameraEffectManager::UpdateFollowComposer(float deltaTime) {
	EditorGameObject* cameraGameObject = FindHighestPriorityCameraGameObject();

	if (cameraGameObject == nullptr || editorScene_ == nullptr) {
		return false;
	}

	EditorComponent* composer = EditorComponentUtility::FindComponent(
		*cameraGameObject,
		EditorComponentType::CameraFollowComposer);

	if (composer == nullptr || !composer->isActive) {
		return false;
	}

	int32_t targetGameObjectId = composer->cameraComposerTargetGameObjectId;

	if (targetGameObjectId < 0) {
		targetGameObjectId = composer->connectedGameObjectId;
	}

	const EditorGameObject* targetGameObject = editorScene_->FindGameObject(targetGameObjectId);

	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		return false;
	}

	const Transforms targetTransform = ResolveWorldTransform(*targetGameObject);
	Vector3 targetVelocity{0.0f, 0.0f, 0.0f};
	const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
		*targetGameObject,
		EditorComponentType::RigidBody);
	const EditorComponent* railMovement = EditorComponentUtility::FindComponent(
		*targetGameObject,
		EditorComponentType::RailMovement);

	if (rigidBody != nullptr && rigidBody->isActive) {
		targetVelocity = rigidBody->velocity;
	}
	else if (railMovement != nullptr && railMovement->isActive) {
		targetVelocity = railMovement->velocity;
	}

	const float inheritedYaw = composer->cameraComposerInheritTargetYaw
		? targetTransform.rotate.y
		: 0.0f;
	const Vector3 followOffset = RotateAroundYaw(
		composer->cameraComposerFollowOffset,
		inheritedYaw);
	const Vector3 lookAtOffset = RotateAroundYaw(
		composer->cameraComposerLookAtOffset,
		inheritedYaw);
	const Vector3 lookAhead = MultiplyVector3(
		(std::max)(composer->cameraComposerLookAheadSeconds, 0.0f),
		targetVelocity);
	Vector3 desiredPosition = AddVector3(targetTransform.translate, followOffset);
	const Vector3 desiredLookAt = AddVector3(
		AddVector3(targetTransform.translate, lookAtOffset),
		lookAhead);

	if (!composer->cameraComposerRuntimeInitialized) {
		composer->cameraComposerRuntimePosition = desiredPosition;
		composer->cameraComposerRuntimeRotation = ResolveWorldTransform(*cameraGameObject).rotate;
		composer->cameraComposerRuntimeInitialized = true;
	}

	const Vector3 positionDifference = SubtractVector3(
		desiredPosition,
		composer->cameraComposerRuntimePosition);
	const float positionDistance = LengthVector3(positionDifference);
	const float deadZoneDistance = (std::max)(
		composer->cameraComposerDeadZone.x,
		composer->cameraComposerDeadZone.y);

	if (positionDistance <= deadZoneDistance) {
		desiredPosition = composer->cameraComposerRuntimePosition;
	}
	else if (composer->cameraComposerMaximumDistance > 0.0f &&
		positionDistance > composer->cameraComposerMaximumDistance) {
		const float distanceRatio = composer->cameraComposerMaximumDistance / positionDistance;
		desiredPosition = AddVector3(
			composer->cameraComposerRuntimePosition,
			MultiplyVector3(distanceRatio, positionDifference));
	}

	const float positionRatio = CalculateDampingRatio(
		composer->cameraComposerPositionDamping,
		deltaTime);
	composer->cameraComposerRuntimePosition = {
		Lerp(composer->cameraComposerRuntimePosition.x, desiredPosition.x, positionRatio),
		Lerp(composer->cameraComposerRuntimePosition.y, desiredPosition.y, positionRatio),
		Lerp(composer->cameraComposerRuntimePosition.z, desiredPosition.z, positionRatio)};

	const Vector3 lookDirection = SubtractVector3(
		desiredLookAt,
		composer->cameraComposerRuntimePosition);
	const float horizontalLength = std::sqrt(
		lookDirection.x * lookDirection.x + lookDirection.z * lookDirection.z);
	Vector3 desiredRotation{
		-std::atan2(lookDirection.y, (std::max)(horizontalLength, 0.0001f)),
		std::atan2(lookDirection.x, lookDirection.z),
		composer->cameraComposerStabilizePitchRoll ? 0.0f : targetTransform.rotate.z};

	if (!composer->cameraComposerStabilizePitchRoll) {
		desiredRotation.x += targetTransform.rotate.x;
	}

	const float rotationRatio = CalculateDampingRatio(
		composer->cameraComposerRotationDamping,
		deltaTime);
	composer->cameraComposerRuntimeRotation = {
		Lerp(composer->cameraComposerRuntimeRotation.x, desiredRotation.x, rotationRatio),
		Lerp(composer->cameraComposerRuntimeRotation.y, desiredRotation.y, rotationRatio),
		Lerp(composer->cameraComposerRuntimeRotation.z, desiredRotation.z, rotationRatio)};

	g_runtimeGameCameraOverrideTransform = {
		{1.0f, 1.0f, 1.0f},
		composer->cameraComposerRuntimeRotation,
		composer->cameraComposerRuntimePosition};
	g_runtimeGameCameraOverrideActive = true;
	return true;
}

float EditorCameraEffectManager::UpdateSpeedFeedback(float deltaTime) {
	if (editorScene_ == nullptr) {
		return 1.0f;
	}

	float combinedCameraStrength = 1.0f;

	for (EditorGameObject& ownerGameObject : editorScene_->GetGameObjects()) {
		EditorComponent* feedback = EditorComponentUtility::FindComponent(
			ownerGameObject,
			EditorComponentType::SpeedFeedback);

		if (!ownerGameObject.isActive || feedback == nullptr || !feedback->isActive) {
			continue;
		}

		const int32_t sourceGameObjectId = feedback->speedFeedbackSourceGameObjectId >= 0
			? feedback->speedFeedbackSourceGameObjectId
			: ownerGameObject.id;
		const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(sourceGameObjectId);

		if (sourceGameObject == nullptr || !sourceGameObject->isActive) {
			continue;
		}

		Vector3 velocity{0.0f, 0.0f, 0.0f};
		const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::RigidBody);
		const EditorComponent* railMovement = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::RailMovement);

		if (rigidBody != nullptr && rigidBody->isActive) {
			velocity = rigidBody->velocity;
		}
		else if (railMovement != nullptr && railMovement->isActive) {
			velocity = railMovement->velocity;
		}

		const float speedRange = (std::max)(
			feedback->speedFeedbackMaximumSpeed - feedback->speedFeedbackMinimumSpeed,
			0.0001f);
		const float targetNormalized = (std::clamp)(
			(LengthVector3(velocity) - feedback->speedFeedbackMinimumSpeed) / speedRange,
			0.0f,
			1.0f);
		const float feedbackRatio = CalculateDampingRatio(
			feedback->speedFeedbackResponseSpeed,
			deltaTime);
		feedback->speedFeedbackNormalized = Lerp(
			feedback->speedFeedbackNormalized,
			targetNormalized,
			feedbackRatio);

		EditorGameObject* cameraGameObject = feedback->speedFeedbackCameraGameObjectId >= 0
			? editorScene_->FindGameObject(feedback->speedFeedbackCameraGameObjectId)
			: FindHighestPriorityCameraGameObject();

		if (cameraGameObject != nullptr) {
			EditorComponent* camera = EditorComponentUtility::FindComponent(
				*cameraGameObject,
				EditorComponentType::Camera);

			if (camera == nullptr || !camera->isActive) {
				camera = EditorComponentUtility::FindComponent(
					*cameraGameObject,
					EditorComponentType::CinemachineCamera);
			}

			if (camera != nullptr && camera->isActive) {
				camera->cameraFieldOfView = Lerp(
					feedback->speedFeedbackMinimumFovDegrees,
					feedback->speedFeedbackMaximumFovDegrees,
					feedback->speedFeedbackNormalized);
				camera->cameraMotionBlurIntensity = Lerp(
					feedback->speedFeedbackMinimumMotionBlur,
					feedback->speedFeedbackMaximumMotionBlur,
					feedback->speedFeedbackNormalized);
			}
		}

		combinedCameraStrength = (std::max)(
			combinedCameraStrength,
			1.0f + feedback->speedFeedbackNormalized *
				(std::max)(feedback->speedFeedbackCameraStrength, 0.0f));
	}

	return combinedCameraStrength;
}

Transforms EditorCameraEffectManager::LerpTransform(
	const Transforms& sourceTransform,
	const Transforms& targetTransform,
	float ratio) {
	Transforms result{};
	result.translate = {
		Lerp(sourceTransform.translate.x, targetTransform.translate.x, ratio),
		Lerp(sourceTransform.translate.y, targetTransform.translate.y, ratio),
		Lerp(sourceTransform.translate.z, targetTransform.translate.z, ratio)};
	result.rotate = {
		Lerp(sourceTransform.rotate.x, targetTransform.rotate.x, ratio),
		Lerp(sourceTransform.rotate.y, targetTransform.rotate.y, ratio),
		Lerp(sourceTransform.rotate.z, targetTransform.rotate.z, ratio)};
	result.scale = {
		Lerp(sourceTransform.scale.x, targetTransform.scale.x, ratio),
		Lerp(sourceTransform.scale.y, targetTransform.scale.y, ratio),
		Lerp(sourceTransform.scale.z, targetTransform.scale.z, ratio)};
	return result;
}
