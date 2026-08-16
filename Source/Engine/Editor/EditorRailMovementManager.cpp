#include "EditorRailMovementManager.h"

#include "EditorComponentUtility.h"
#include "EditorInputManager.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"
#include "Vector.h"
#include "Vector&Matrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

#include <windows.h>

namespace {
	constexpr int32_t kSmoothSamplesPerSegment = 16;
	constexpr float kRailMinimumDistance = 0.0001f;
	// 前進力は「位置に吸い付く」のではなく、目標速度との差（スクリュー推進）が主体。
	constexpr float kRailAlongPositionSpringScale = 0.25f;
	constexpr float kRailEngineResponseScale = 2.0f;
	// 波などでレールから押し出された距離がこの半径に達するまでに、
	// 船体推進モードの横補正と推力方向をレール側へ段階的に引き寄せる。
	constexpr float kRailAuthorityRadius = 2.0f;

	Vector3 ResolveWorldPosition(const EditorScene& editorScene, const EditorGameObject& gameObject) {
		const Matrix4x4 worldMatrix = editorScene.GetWorldMatrix(gameObject.id);
		return Vector3{
			worldMatrix.matrix[3][0],
			worldMatrix.matrix[3][1],
			worldMatrix.matrix[3][2]};
	}

	int32_t ResolveControlPointIndex(int32_t pointIndex, int32_t pointCount, bool isLooping) {
		if (isLooping) {
			const int32_t wrappedIndex = pointIndex % pointCount;
			return wrappedIndex < 0 ? wrappedIndex + pointCount : wrappedIndex;
		}

		return (std::clamp)(pointIndex, 0, pointCount - 1);
	}

	Vector3 EvaluateCatmullRom(
		const Vector3& firstPoint,
		const Vector3& secondPoint,
		const Vector3& thirdPoint,
		const Vector3& fourthPoint,
		float normalizedTime) {
		const float squaredTime = normalizedTime * normalizedTime;
		const float cubedTime = squaredTime * normalizedTime;
		Vector3 result{};
		result.x = 0.5f * (
			2.0f * secondPoint.x +
			(-firstPoint.x + thirdPoint.x) * normalizedTime +
			(2.0f * firstPoint.x - 5.0f * secondPoint.x +
				4.0f * thirdPoint.x - fourthPoint.x) * squaredTime +
			(-firstPoint.x + 3.0f * secondPoint.x -
				3.0f * thirdPoint.x + fourthPoint.x) * cubedTime);
		result.y = 0.5f * (
			2.0f * secondPoint.y +
			(-firstPoint.y + thirdPoint.y) * normalizedTime +
			(2.0f * firstPoint.y - 5.0f * secondPoint.y +
				4.0f * thirdPoint.y - fourthPoint.y) * squaredTime +
			(-firstPoint.y + 3.0f * secondPoint.y -
				3.0f * thirdPoint.y + fourthPoint.y) * cubedTime);
		result.z = 0.5f * (
			2.0f * secondPoint.z +
			(-firstPoint.z + thirdPoint.z) * normalizedTime +
			(2.0f * firstPoint.z - 5.0f * secondPoint.z +
				4.0f * thirdPoint.z - fourthPoint.z) * squaredTime +
			(-firstPoint.z + 3.0f * secondPoint.z -
				3.0f * thirdPoint.z + fourthPoint.z) * cubedTime);
		return result;
	}

	bool BuildRailSamples(
		const EditorScene& editorScene,
		const EditorGameObject& railPathGameObject,
		bool isLooping,
		bool usesSmoothCurve,
		std::vector<EditorRailRuntimeSample>& railSamples) {
		std::vector<Vector3> controlPointPositions;
		controlPointPositions.reserve(railPathGameObject.children.size());

		for (int32_t childGameObjectId : railPathGameObject.children) {
			const EditorGameObject* controlPointGameObject =
				editorScene.FindGameObject(childGameObjectId);

			if (controlPointGameObject != nullptr && controlPointGameObject->isActive) {
				controlPointPositions.push_back(
					ResolveWorldPosition(editorScene, *controlPointGameObject));
			}
		}

		if (controlPointPositions.size() < 2u) {
			return false;
		}

		railSamples.clear();
		const int32_t controlPointCount = static_cast<int32_t>(controlPointPositions.size());
		const int32_t segmentCount = isLooping ? controlPointCount : controlPointCount - 1;
		const int32_t samplesPerSegment = usesSmoothCurve ? kSmoothSamplesPerSegment : 1;
		Vector3 previousPosition = controlPointPositions[0u];
		railSamples.push_back({previousPosition, 0.0f});
		float accumulatedDistance = 0.0f;

		for (int32_t segmentIndex = 0; segmentIndex < segmentCount; segmentIndex++) {
			const int32_t firstIndex = ResolveControlPointIndex(
				segmentIndex - 1,
				controlPointCount,
				isLooping);
			const int32_t secondIndex = ResolveControlPointIndex(
				segmentIndex,
				controlPointCount,
				isLooping);
			const int32_t thirdIndex = ResolveControlPointIndex(
				segmentIndex + 1,
				controlPointCount,
				isLooping);
			const int32_t fourthIndex = ResolveControlPointIndex(
				segmentIndex + 2,
				controlPointCount,
				isLooping);

			for (int32_t sampleIndex = 1; sampleIndex <= samplesPerSegment; sampleIndex++) {
				const float normalizedTime =
					static_cast<float>(sampleIndex) / static_cast<float>(samplesPerSegment);
				const Vector3 samplePosition = usesSmoothCurve ?
					EvaluateCatmullRom(
						controlPointPositions[static_cast<size_t>(firstIndex)],
						controlPointPositions[static_cast<size_t>(secondIndex)],
						controlPointPositions[static_cast<size_t>(thirdIndex)],
						controlPointPositions[static_cast<size_t>(fourthIndex)],
						normalizedTime) :
					controlPointPositions[static_cast<size_t>(thirdIndex)];
				accumulatedDistance += Length(Subtract(samplePosition, previousPosition));
				railSamples.push_back({samplePosition, accumulatedDistance});
				previousPosition = samplePosition;
			}
		}

		return accumulatedDistance > kRailMinimumDistance;
	}

	Vector3 SampleRailPosition(
		const std::vector<EditorRailRuntimeSample>& railSamples,
		float distance) {
		const auto upperSample = std::lower_bound(
			railSamples.begin(),
			railSamples.end(),
			distance,
			[](const EditorRailRuntimeSample& railSample, float targetDistance) {
				return railSample.distance < targetDistance;
			});

		if (upperSample == railSamples.begin()) {
			return upperSample->position;
		}

		if (upperSample == railSamples.end()) {
			return railSamples.back().position;
		}

		const EditorRailRuntimeSample& previousSample = *(upperSample - 1);
		const float sampleDistance = upperSample->distance - previousSample.distance;
		const float interpolation = sampleDistance > kRailMinimumDistance ?
			(distance - previousSample.distance) / sampleDistance : 0.0f;
		return Add(
			previousSample.position,
			Multiply(interpolation, Subtract(upperSample->position, previousSample.position)));
	}

	float NormalizeRailDistance(float distance, float totalDistance, bool isLooping) {
		if (!isLooping) {
			return (std::clamp)(distance, 0.0f, totalDistance);
		}

		float normalizedDistance = std::fmod(distance, totalDistance);

		if (normalizedDistance < 0.0f) {
			normalizedDistance += totalDistance;
		}

		return normalizedDistance;
	}

	size_t CombineSignature(size_t currentSignature, size_t value) {
		constexpr size_t kSignatureSeed = static_cast<size_t>(0x9e3779b9u);
		return currentSignature ^
			(value + kSignatureSeed + (currentSignature << 6u) + (currentSignature >> 2u));
	}

	size_t BuildRailPathSignature(
		const EditorScene& editorScene,
		const EditorGameObject& railPathGameObject) {
		size_t signature = std::hash<int32_t>{}(railPathGameObject.id);

		for (int32_t childGameObjectId : railPathGameObject.children) {
			const EditorGameObject* controlPointGameObject =
				editorScene.FindGameObject(childGameObjectId);

			if (controlPointGameObject == nullptr) {
				signature = CombineSignature(signature, std::hash<int32_t>{}(childGameObjectId));
				continue;
			}

			const Vector3 worldPosition = ResolveWorldPosition(editorScene, *controlPointGameObject);
			signature = CombineSignature(signature, std::hash<int32_t>{}(controlPointGameObject->id));
			signature = CombineSignature(signature, std::hash<bool>{}(controlPointGameObject->isActive));
			signature = CombineSignature(signature, std::hash<float>{}(worldPosition.x));
			signature = CombineSignature(signature, std::hash<float>{}(worldPosition.y));
			signature = CombineSignature(signature, std::hash<float>{}(worldPosition.z));
		}

		return signature;
	}

	float MoveTowards(float currentValue, float targetValue, float maximumDelta) {
		if (maximumDelta <= 0.0f) {
			return targetValue;
		}

		const float difference = targetValue - currentValue;

		if (std::abs(difference) <= maximumDelta) {
			return targetValue;
		}

		return currentValue + std::copysign(maximumDelta, difference);
	}

	float EvaluateRailSpeedProfile(const EditorComponent* speedProfile, float normalizedProgress) {
		if (speedProfile == nullptr || !speedProfile->isActive ||
			!speedProfile->railSpeedProfileEnabled || speedProfile->railSpeedKeys.empty()) {
			return 1.0f;
		}

		const float progress = (std::clamp)(normalizedProgress, 0.0f, 1.0f);
		const EditorRailSpeedKey* lowerKey = nullptr;
		const EditorRailSpeedKey* upperKey = nullptr;

		for (const EditorRailSpeedKey& speedKey : speedProfile->railSpeedKeys) {
			if (speedKey.normalizedProgress <= progress &&
				(lowerKey == nullptr || speedKey.normalizedProgress > lowerKey->normalizedProgress)) {
				lowerKey = &speedKey;
			}

			if (speedKey.normalizedProgress >= progress &&
				(upperKey == nullptr || speedKey.normalizedProgress < upperKey->normalizedProgress)) {
				upperKey = &speedKey;
			}
		}

		if (lowerKey == nullptr) {
			lowerKey = upperKey;
		}

		if (upperKey == nullptr) {
			upperKey = lowerKey;
		}

		if (lowerKey == nullptr || upperKey == nullptr) {
			return 1.0f;
		}

		const float keyDistance = upperKey->normalizedProgress - lowerKey->normalizedProgress;
		const float interpolation = keyDistance > 0.0001f
			? (progress - lowerKey->normalizedProgress) / keyDistance
			: 0.0f;
		return (std::max)(
			lowerKey->speedMultiplier +
			(upperKey->speedMultiplier - lowerKey->speedMultiplier) * interpolation,
			0.0f);
	}

	int32_t FindActiveRailZone(const EditorComponent* railZone, float normalizedProgress) {
		if (railZone == nullptr || !railZone->isActive) {
			return -1;
		}

		const float progress = (std::clamp)(normalizedProgress, 0.0f, 1.0f);

		for (int32_t zoneIndex = 0;
			zoneIndex < static_cast<int32_t>(railZone->railZoneEntries.size());
			zoneIndex++) {
			const EditorRailZoneEntry& zoneEntry = railZone->railZoneEntries[static_cast<size_t>(zoneIndex)];
			const float zoneStart = (std::min)(zoneEntry.startNormalized, zoneEntry.endNormalized);
			const float zoneEnd = (std::max)(zoneEntry.startNormalized, zoneEntry.endNormalized);

			if (progress >= zoneStart && progress <= zoneEnd) {
				return zoneIndex;
			}
		}

		return -1;
	}

	bool WasRailMarkerCrossed(
		float previousProgress,
		float currentProgress,
		float markerProgress,
		bool isReversed) {
		if (!isReversed) {
			if (currentProgress >= previousProgress) {
				return markerProgress > previousProgress && markerProgress <= currentProgress;
			}

			return markerProgress > previousProgress || markerProgress <= currentProgress;
		}

		if (currentProgress <= previousProgress) {
			return markerProgress < previousProgress && markerProgress >= currentProgress;
		}

		return markerProgress < previousProgress || markerProgress >= currentProgress;
	}

	void AdvanceRailRuntimeState(
		EditorRailFollowerRuntimeState& runtimeState,
		const EditorComponent& railMovementComponent,
		float deltaTime) {
		if (runtimeState.isPaused) {
			return;
		}

		float targetSpeed = railMovementComponent.railSpeed * runtimeState.targetSpeedMultiplier;

		if (runtimeState.isReversed) {
			targetSpeed *= -1.0f;
		}

		const bool isChangingDirection = runtimeState.currentSpeed * targetSpeed < 0.0f;
		const bool isAccelerating =
			!isChangingDirection && std::abs(targetSpeed) > std::abs(runtimeState.currentSpeed);
		const float speedChangeRate = isAccelerating ?
			railMovementComponent.railAcceleration : railMovementComponent.railDeceleration;
		const float speedTarget = isChangingDirection ? 0.0f : targetSpeed;
		runtimeState.currentSpeed = MoveTowards(
			runtimeState.currentSpeed,
			speedTarget,
			speedChangeRate * deltaTime);

		const float requestedDistance =
			runtimeState.traveledDistance + runtimeState.currentSpeed * deltaTime;
		const bool hasCrossedEnd =
			requestedDistance < 0.0f || requestedDistance > runtimeState.totalDistance;
		runtimeState.traveledDistance = NormalizeRailDistance(
			requestedDistance,
			runtimeState.totalDistance,
			railMovementComponent.railLoop);

		if (hasCrossedEnd) {
			runtimeState.endReached = true;

			if (!railMovementComponent.railLoop && railMovementComponent.railStopAtEnd) {
				runtimeState.isPaused = true;
				runtimeState.currentSpeed = 0.0f;
			}
		}
	}

	Vector3 CalculateRailDirection(
		const EditorRailFollowerRuntimeState& runtimeState,
		const EditorComponent& railMovementComponent,
		float railDistance,
		const Vector3& railPosition) {
		const float lookAheadDistance = (std::max)(
			railMovementComponent.railLookAheadDistance,
			0.01f);
		const float forwardDistance = NormalizeRailDistance(
			railDistance + lookAheadDistance,
			runtimeState.totalDistance,
			railMovementComponent.railLoop);
		Vector3 forwardDirection = Subtract(
			SampleRailPosition(runtimeState.samples, forwardDistance),
			railPosition);

		if (Length(forwardDirection) <= kRailMinimumDistance) {
			const float backwardDistance = NormalizeRailDistance(
				railDistance - lookAheadDistance,
				runtimeState.totalDistance,
				railMovementComponent.railLoop);
			forwardDirection = Subtract(
				railPosition,
				SampleRailPosition(runtimeState.samples, backwardDistance));
		}

		const bool facesReverse = std::abs(runtimeState.currentSpeed) > kRailMinimumDistance ?
			runtimeState.currentSpeed < 0.0f :
			((railMovementComponent.railSpeed < 0.0f) != runtimeState.isReversed);

		if (facesReverse) {
			forwardDirection = Multiply(-1.0f, forwardDirection);
		}

		return Length(forwardDirection) > kRailMinimumDistance ?
			Normalize(forwardDirection) : Vector3{0.0f, 0.0f, 0.0f};
	}

	Vector3 ApplyRailOffset(
		const Vector3& railPosition,
		const Vector3& railDirection,
		const EditorScriptVector2& railOffset) {
		if (Length(railDirection) <= kRailMinimumDistance) {
			return railPosition;
		}

		Vector3 referenceUp{0.0f, 1.0f, 0.0f};

		if (std::abs(Dot(railDirection, referenceUp)) >= 0.98f) {
			referenceUp = {0.0f, 0.0f, 1.0f};
		}

		const Vector3 railRight = Normalize(Cross(referenceUp, railDirection));
		const Vector3 railUp = Normalize(Cross(railDirection, railRight));
		return Add(
			railPosition,
			Add(
				Multiply(railOffset.x, railRight),
				Multiply(railOffset.y, railUp)));
	}

	float MoveTowardsValue(float currentValue, float targetValue, float maximumDelta) {
		if (currentValue < targetValue) {
			return (std::min)(currentValue + maximumDelta, targetValue);
		}

		return (std::max)(currentValue - maximumDelta, targetValue);
	}

	void AdvanceMovementModifier(
		EditorRailFollowerRuntimeState& runtimeState,
		const EditorComponent* movementModifier,
		const EditorInputManager* inputManager,
		int32_t ownerGameObjectId,
		float deltaTime) {
		if (movementModifier == nullptr || !movementModifier->isActive) {
			runtimeState.modifierInputOffset = {0.0f, 0.0f};
			return;
		}

		float inputX = 0.0f;
		float inputY = 0.0f;
		const int32_t inputGameObjectId = movementModifier->movementModifierInputGameObjectId >= 0
			? movementModifier->movementModifierInputGameObjectId
			: ownerGameObjectId;

		if (inputManager != nullptr) {
			inputManager->TryGetActionVector2(
				inputGameObjectId,
				movementModifier->movementModifierActionMapName,
				movementModifier->movementModifierActionName,
				inputX,
				inputY);
		}

		const float inputLength = std::sqrt(inputX * inputX + inputY * inputY);

		if (inputLength > 1.0f) {
			inputX /= inputLength;
			inputY /= inputLength;
		}

		const float maximumDelta =
			(std::max)(movementModifier->movementModifierInputSpeed, 0.0f) * deltaTime;
		runtimeState.modifierInputOffset.x = MoveTowardsValue(
			runtimeState.modifierInputOffset.x,
			inputX * (std::max)(movementModifier->movementModifierInputRange.x, 0.0f),
			maximumDelta);
		runtimeState.modifierInputOffset.y = MoveTowardsValue(
			runtimeState.modifierInputOffset.y,
			inputY * (std::max)(movementModifier->movementModifierInputRange.y, 0.0f),
			maximumDelta);
	}

	Vector3 ApplyMovementModifierPosition(
		const Vector3& railPosition,
		const Vector3& railDirection,
		const EditorComponent* movementModifier,
		const EditorScriptVector2& modifierInputOffset) {
		if (movementModifier == nullptr || !movementModifier->isActive ||
			Length(railDirection) <= kRailMinimumDistance) {
			return railPosition;
		}

		Vector3 referenceUp{0.0f, 1.0f, 0.0f};

		if (std::abs(Dot(railDirection, referenceUp)) >= 0.98f) {
			referenceUp = {0.0f, 0.0f, 1.0f};
		}

		const Vector3 railRight = Normalize(Cross(referenceUp, railDirection));
		const Vector3 railUp = Normalize(Cross(railDirection, railRight));
		const Vector3 localOffset = movementModifier->movementModifierLocalPositionOffset;
		const float localX = (movementModifier->movementModifierAxisMask & 1) != 0
			? localOffset.x + modifierInputOffset.x
			: 0.0f;
		const float localY = (movementModifier->movementModifierAxisMask & 2) != 0
			? localOffset.y + modifierInputOffset.y
			: 0.0f;
		const float localZ = (movementModifier->movementModifierAxisMask & 4) != 0
			? localOffset.z
			: 0.0f;
		return Add(
			railPosition,
			Add(
				Multiply(localX, railRight),
				Add(Multiply(localY, railUp), Multiply(localZ, railDirection))));
	}

	void AdvanceRailOffset(
		EditorRailFollowerRuntimeState& runtimeState,
		const EditorComponent& railMovementComponent,
		const EditorScriptVector2& movementRange,
		const EditorInputManager* inputManager,
		int32_t gameObjectId,
		float deltaTime) {
		EditorScriptVector2 moveInput = runtimeState.moveInput;

		if (railMovementComponent.railUsePlayerInput && inputManager != nullptr) {
			float inputX = 0.0f;
			float inputY = 0.0f;

			if (inputManager->TryGetActionVector2(
					gameObjectId,
					railMovementComponent.railInputActionMapName,
					railMovementComponent.railInputActionName,
					inputX,
					inputY)) {
				moveInput = {inputX, inputY};
			}
		}

		const float inputLength = std::sqrt(moveInput.x * moveInput.x + moveInput.y * moveInput.y);

		if (inputLength > 1.0f) {
			moveInput.x /= inputLength;
			moveInput.y /= inputLength;
		}

		const float moveDistance =
			(std::max)(railMovementComponent.railOffsetMoveSpeed, 0.0f) * deltaTime;
		runtimeState.currentOffset.x = (std::clamp)(
			runtimeState.currentOffset.x + moveInput.x * moveDistance,
			-(std::max)(movementRange.x, 0.0f),
			(std::max)(movementRange.x, 0.0f));
		runtimeState.currentOffset.y = (std::clamp)(
			runtimeState.currentOffset.y + moveInput.y * moveDistance,
			-(std::max)(movementRange.y, 0.0f),
			(std::max)(movementRange.y, 0.0f));
		runtimeState.moveInput = {0.0f, 0.0f};
	}

	float NormalizeAngle(float angle) {
		constexpr float kPi = 3.14159265f;
		constexpr float kTwoPi = kPi * 2.0f;

		while (angle > kPi) {
			angle -= kTwoPi;
		}

		while (angle < -kPi) {
			angle += kTwoPi;
		}

		return angle;
	}

	Vector3 MultiplyComponents(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x * secondValue.x,
			firstValue.y * secondValue.y,
			firstValue.z * secondValue.z};
	}

	Vector3 ClampVectorLength(const Vector3& value, float maximumLength) {
		const float valueLength = Length(value);

		if (maximumLength <= 0.0f || valueLength <= maximumLength || valueLength <= kRailMinimumDistance) {
			return value;
		}

		return Multiply(maximumLength / valueLength, value);
	}

	Vector3 ResolveRailLocalForwardAxis(int32_t localForwardAxis) {
		switch ((std::clamp)(localForwardAxis, 0, 3)) {
		case 1:
			return {0.0f, 0.0f, -1.0f};
		case 2:
			return {1.0f, 0.0f, 0.0f};
		case 3:
			return {-1.0f, 0.0f, 0.0f};
		default:
			return {0.0f, 0.0f, 1.0f};
		}
	}

	float ResolveRailLocalForwardYawOffset(int32_t localForwardAxis) {
		constexpr float kPi = 3.14159265f;

		switch ((std::clamp)(localForwardAxis, 0, 3)) {
		case 1:
			return kPi;
		case 2:
			return kPi * 0.5f;
		case 3:
			return -kPi * 0.5f;
		default:
			return 0.0f;
		}
	}

	Vector3 ResolveRailWorldForward(
		const Vector3& worldRotation,
		int32_t localForwardAxis,
		bool usesHorizontalThrust) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			worldRotation,
			{0.0f, 0.0f, 0.0f});
		Vector3 worldForward = Transform(
			ResolveRailLocalForwardAxis(localForwardAxis),
			rotationMatrix);

		if (usesHorizontalThrust) {
			worldForward.y = 0.0f;
		}

		return Length(worldForward) > kRailMinimumDistance ?
			Normalize(worldForward) : Vector3{0.0f, 0.0f, 1.0f};
	}
}

void EditorRailMovementManager::Initialize(
	EditorScene* editorScene,
	EditorPhysicsManager* physicsManager,
	EditorInputManager* inputManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	inputManager_ = inputManager;
	scriptManager_ = scriptManager;
	isStarted_ = false;
}

void EditorRailMovementManager::Start() {
	std::unordered_map<int32_t, EditorRailFollowerRuntimeState> previousRuntimeStates =
		std::move(runtimeStates_);
	runtimeStates_.clear();

	if (editorScene_ != nullptr) {
		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::RailMovement);

			if (railMovementComponent == nullptr) {
				continue;
			}

			EditorRailFollowerRuntimeState runtimeState{};
			const auto previousRuntimeIterator = previousRuntimeStates.find(gameObject.id);

			if (previousRuntimeIterator != previousRuntimeStates.end()) {
				runtimeState = std::move(previousRuntimeIterator->second);
			}
			else {
				runtimeState.isPaused = railMovementComponent->railStartPaused;
				runtimeState.isReversed = railMovementComponent->railReverse;
				runtimeState.currentOffset = railMovementComponent->railStartOffset;
			}

			runtimeStates_.emplace(gameObject.id, std::move(runtimeState));

			EditorGameObject* mutableGameObject = editorScene_->FindGameObject(gameObject.id);
			EditorComponent* railZone = mutableGameObject != nullptr
				? EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::RailZone)
				: nullptr;

			if (railZone != nullptr) {
				railZone->railZoneActiveIndex = -1;
			}

			EditorComponent* railEventMarker = mutableGameObject != nullptr
				? EditorComponentUtility::FindComponent(*mutableGameObject, EditorComponentType::RailEventMarker)
				: nullptr;

			if (railEventMarker != nullptr) {
				railEventMarker->railEventMarkerRuntimeInitialized = false;
				railEventMarker->railEventMarkerPreviousProgress = 0.0f;

				for (EditorRailEventMarkerEntry& markerEntry : railEventMarker->railEventMarkerEntries) {
					markerEntry.runtimeTriggered = false;
				}
			}
		}
	}

	isStarted_ = true;
}

void EditorRailMovementManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailMovement);

		if (railMovementComponent == nullptr || !railMovementComponent->isActive) {
			continue;
		}

		const EditorGameObject* railPathGameObject = editorScene_->FindGameObject(
			railMovementComponent->railPathGameObjectId);

		if (railPathGameObject == nullptr ||
			!railPathGameObject->isActive ||
			railPathGameObject->id == gameObject.id) {
			continue;
		}

		auto [runtimeStateIterator, wasRuntimeStateInserted] = runtimeStates_.try_emplace(gameObject.id);
		EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;

		if (wasRuntimeStateInserted) {
			runtimeState.isPaused = railMovementComponent->railStartPaused;
			runtimeState.isReversed = railMovementComponent->railReverse;
			runtimeState.currentOffset = railMovementComponent->railStartOffset;
		}

		const size_t pathSignature = BuildRailPathSignature(*editorScene_, *railPathGameObject);
		const bool shouldRebuildSamples =
			runtimeState.samples.empty() ||
			runtimeState.cachedPathGameObjectId != railPathGameObject->id ||
			runtimeState.cachedLoop != railMovementComponent->railLoop ||
			runtimeState.cachedSmoothCurve != railMovementComponent->railUseSmoothCurve ||
			runtimeState.pathSignature != pathSignature;

		if (shouldRebuildSamples) {
			const float previousNormalized = runtimeState.totalDistance > kRailMinimumDistance ?
				runtimeState.traveledDistance / runtimeState.totalDistance :
				railMovementComponent->railStartNormalized;

			if (!BuildRailSamples(
					*editorScene_,
					*railPathGameObject,
					railMovementComponent->railLoop,
					railMovementComponent->railUseSmoothCurve,
					runtimeState.samples)) {
				runtimeState.samples.clear();
				runtimeState.totalDistance = 0.0f;
				continue;
			}

			runtimeState.totalDistance = runtimeState.samples.back().distance;
			runtimeState.cachedPathGameObjectId = railPathGameObject->id;
			runtimeState.cachedLoop = railMovementComponent->railLoop;
			runtimeState.cachedSmoothCurve = railMovementComponent->railUseSmoothCurve;
			runtimeState.pathSignature = pathSignature;

			const float requestedNormalized = runtimeState.hasPendingNormalized ?
				runtimeState.pendingNormalized : previousNormalized;
			runtimeState.traveledDistance =
				(std::clamp)(requestedNormalized, 0.0f, 1.0f) * runtimeState.totalDistance;
			runtimeState.isDistanceInitialized = true;
			runtimeState.hasPendingNormalized = false;
		}

		if (!runtimeState.isDistanceInitialized ||
			runtimeState.totalDistance <= kRailMinimumDistance) {
			continue;
		}

		const float normalizedProgress = (std::clamp)(
			runtimeState.traveledDistance / runtimeState.totalDistance,
			0.0f,
			1.0f);
		EditorComponent* railEventMarker = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailEventMarker);

		if (railEventMarker != nullptr && railEventMarker->isActive) {
			if (railEventMarker->railEventMarkerRuntimeInitialized) {
				for (EditorRailEventMarkerEntry& markerEntry : railEventMarker->railEventMarkerEntries) {
					const bool directionAllowed = markerEntry.directionMode == 0 ||
						(markerEntry.directionMode == 1 && !runtimeState.isReversed) ||
						(markerEntry.directionMode == 2 && runtimeState.isReversed);
					const bool canTrigger = directionAllowed &&
						(!markerEntry.triggerOnce || !markerEntry.runtimeTriggered);

					if (!canTrigger || !WasRailMarkerCrossed(
						railEventMarker->railEventMarkerPreviousProgress,
						normalizedProgress,
						(std::clamp)(markerEntry.normalizedProgress, 0.0f, 1.0f),
						runtimeState.isReversed)) {
						continue;
					}

					markerEntry.runtimeTriggered = true;

					if (scriptManager_ != nullptr && !markerEntry.actionName.empty()) {
						EditorScriptActionPayload payload{};
						payload.type = EditorScriptActionPayloadTypeString;
						strncpy_s(payload.stringValue, markerEntry.markerId.c_str(), _TRUNCATE);
						const int32_t actionTargetGameObjectId =
							railEventMarker->railEventMarkerActionTargetGameObjectId >= 0
							? railEventMarker->railEventMarkerActionTargetGameObjectId
							: gameObject.id;
						scriptManager_->QueueActionPayload(
							actionTargetGameObjectId,
							markerEntry.actionName,
							payload);
					}
				}
			}

			railEventMarker->railEventMarkerPreviousProgress = normalizedProgress;
			railEventMarker->railEventMarkerRuntimeInitialized = true;
		}
		const EditorComponent* speedProfile = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailSpeedProfile);
		EditorComponent* railZone = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailZone);
		const int32_t activeZoneIndex = FindActiveRailZone(railZone, normalizedProgress);
		EditorScriptVector2 activeMovementRange = railMovementComponent->railMovementRange;
		float zoneSpeedMultiplier = 1.0f;

		if (railZone != nullptr && railZone->isActive) {
			const int32_t previousZoneIndex = railZone->railZoneActiveIndex;

			auto queueZoneAction = [this, &gameObject, railZone](
				const EditorRailZoneEntry& zoneEntry,
				const std::string& actionName) {
				if (scriptManager_ == nullptr || actionName.empty()) {
					return;
				}

				EditorScriptActionPayload payload{};
				payload.type = EditorScriptActionPayloadTypeString;
				strncpy_s(payload.stringValue, zoneEntry.zoneId.c_str(), _TRUNCATE);
				const int32_t actionTargetGameObjectId = railZone->railZoneActionTargetGameObjectId >= 0
					? railZone->railZoneActionTargetGameObjectId
					: gameObject.id;
				scriptManager_->QueueActionPayload(actionTargetGameObjectId, actionName, payload);
			};

			if (previousZoneIndex != activeZoneIndex) {
				if (previousZoneIndex >= 0 &&
					previousZoneIndex < static_cast<int32_t>(railZone->railZoneEntries.size())) {
					const EditorRailZoneEntry& previousZone =
						railZone->railZoneEntries[static_cast<size_t>(previousZoneIndex)];
					queueZoneAction(previousZone, previousZone.exitedActionName);
				}

				if (activeZoneIndex >= 0 &&
					activeZoneIndex < static_cast<int32_t>(railZone->railZoneEntries.size())) {
					const EditorRailZoneEntry& activeZone =
						railZone->railZoneEntries[static_cast<size_t>(activeZoneIndex)];
					queueZoneAction(activeZone, activeZone.enteredActionName);
				}

				railZone->railZoneActiveIndex = activeZoneIndex;
			}

			if (activeZoneIndex >= 0 &&
				activeZoneIndex < static_cast<int32_t>(railZone->railZoneEntries.size())) {
				const EditorRailZoneEntry& activeZone =
					railZone->railZoneEntries[static_cast<size_t>(activeZoneIndex)];
				zoneSpeedMultiplier = (std::max)(activeZone.speedMultiplier, 0.0f);

				if (activeZone.overrideMovementRange) {
					activeMovementRange = activeZone.movementRange;
				}
			}
		}

		runtimeState.targetSpeedMultiplier =
			EvaluateRailSpeedProfile(speedProfile, normalizedProgress) * zoneSpeedMultiplier;

		AdvanceRailOffset(
			runtimeState,
			*railMovementComponent,
			activeMovementRange,
			inputManager_,
			gameObject.id,
			deltaTime);
		const EditorComponent* movementModifier = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MovementModifier);
		AdvanceMovementModifier(
			runtimeState,
			movementModifier,
			inputManager_,
			gameObject.id,
			deltaTime);

		const EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RigidBody);
		const bool canUseDynamicPhysics =
			railMovementComponent->railMovementMode != 0 &&
			physicsManager_ != nullptr &&
			rigidBodyComponent != nullptr &&
			rigidBodyComponent->isActive &&
			!rigidBodyComponent->isKinematic;

		// Railの時間進行は物理Bodyの有無に依存させない。
		// Dynamic RigidbodyはFixedUpdateで、この進んだ目標位置へForce追従する。
		AdvanceRailRuntimeState(runtimeState, *railMovementComponent, deltaTime);

		if (canUseDynamicPhysics) {
			railMovementComponent->velocity = rigidBodyComponent->velocity;
			continue;
		}

		const Vector3 previousWorldPosition = ResolveWorldPosition(*editorScene_, gameObject);
		const Vector3 railPosition = SampleRailPosition(
			runtimeState.samples,
			runtimeState.traveledDistance);
		const Vector3 railDirection = CalculateRailDirection(
			runtimeState,
			*railMovementComponent,
			runtimeState.traveledDistance,
			railPosition);
		const Vector3 railOffsetPosition = ApplyRailOffset(
			railPosition,
			railDirection,
			runtimeState.currentOffset);
		const Vector3 nextWorldPosition = ApplyMovementModifierPosition(
			railOffsetPosition,
			railDirection,
			movementModifier,
			runtimeState.modifierInputOffset);
		Vector3 worldScale = gameObject.scale;
		Vector3 worldRotation = gameObject.rotate;
		Vector3 currentWorldPosition = previousWorldPosition;
		editorScene_->GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			currentWorldPosition);
		(void)currentWorldPosition;
		railMovementComponent->velocity = runtimeState.isPaused ?
			Vector3{0.0f, 0.0f, 0.0f} :
			Multiply(1.0f / deltaTime, Subtract(nextWorldPosition, previousWorldPosition));

		if (railMovementComponent->railOrientToPath) {
			const Vector3 forwardDirection = railDirection;

			if (Length(forwardDirection) > kRailMinimumDistance) {
				worldRotation.x = -std::asin((std::clamp)(forwardDirection.y, -1.0f, 1.0f));
				worldRotation.y = std::atan2(forwardDirection.x, forwardDirection.z);
			}
		}

		if (movementModifier != nullptr && movementModifier->isActive) {
			constexpr float kDegreeToRadian = 0.0174532924f;
			worldRotation.x += movementModifier->movementModifierLocalRotationOffset.x * kDegreeToRadian;
			worldRotation.y += movementModifier->movementModifierLocalRotationOffset.y * kDegreeToRadian;
			worldRotation.z += movementModifier->movementModifierLocalRotationOffset.z * kDegreeToRadian;
		}

		editorScene_->SetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			nextWorldPosition);
	}
}

void EditorRailMovementManager::FixedUpdate(float fixedDeltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || physicsManager_ == nullptr || fixedDeltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailMovement);
		const EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RigidBody);

		if (railMovementComponent == nullptr ||
			!railMovementComponent->isActive ||
			railMovementComponent->railMovementMode == 0 ||
			railMovementComponent->railMovementMode > 2 ||
			rigidBodyComponent == nullptr ||
			!rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		const auto runtimeStateIterator = runtimeStates_.find(gameObject.id);

		if (runtimeStateIterator == runtimeStates_.end()) {
			continue;
		}

		EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;

		if (!runtimeState.isDistanceInitialized ||
			runtimeState.totalDistance <= kRailMinimumDistance ||
			runtimeState.samples.empty()) {
			continue;
		}

		const Vector3 railPosition = SampleRailPosition(
			runtimeState.samples,
			runtimeState.traveledDistance);
		const Vector3 railDirection = CalculateRailDirection(
			runtimeState,
			*railMovementComponent,
			runtimeState.traveledDistance,
			railPosition);
		const EditorComponent* movementModifier = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MovementModifier);
		const Vector3 currentPosition = ResolveWorldPosition(*editorScene_, gameObject);
		const Vector3 positionInfluence{
			(std::clamp)(railMovementComponent->railPositionInfluence.x, 0.0f, 1.0f),
			(std::clamp)(railMovementComponent->railPositionInfluence.y, 0.0f, 1.0f),
			(std::clamp)(railMovementComponent->railPositionInfluence.z, 0.0f, 1.0f)};

		// 曲がり区間では、追従位置への直線距離ベクトル（コード）が接線から大きくずれ、
		// 「コーナーを斜めに突っ切る方向」に力が掛かって正しく押せなくなる。
		// 前進分は先読みした追従点と接線方向へ、横ずれ分だけを軌道へ引き戻す力に分ける。
		const float railDirectionSign =
			std::abs(runtimeState.currentSpeed) > kRailMinimumDistance ?
				(runtimeState.currentSpeed < 0.0f ? -1.0f : 1.0f) :
				(((railMovementComponent->railSpeed < 0.0f) != runtimeState.isReversed) ? -1.0f : 1.0f);
		const float chaseLookAheadDistance = (std::max)(
			railMovementComponent->railLookAheadDistance,
			std::abs(runtimeState.currentSpeed) * 0.25f);
		const float chaseRailDistance = NormalizeRailDistance(
				runtimeState.traveledDistance + chaseLookAheadDistance * railDirectionSign,
				runtimeState.totalDistance,
				railMovementComponent->railLoop);
		const Vector3 chaseRailPosition = SampleRailPosition(
			runtimeState.samples,
			chaseRailDistance);
		const Vector3 chaseRailDirection = CalculateRailDirection(
			runtimeState,
			*railMovementComponent,
			chaseRailDistance,
			chaseRailPosition);
		const Vector3 chaseOffsetPosition = ApplyRailOffset(
			chaseRailPosition,
			chaseRailDirection,
			runtimeState.currentOffset);
		const Vector3 targetPosition = ApplyMovementModifierPosition(
			chaseOffsetPosition,
			chaseRailDirection,
			movementModifier,
			runtimeState.modifierInputOffset);
		const Vector3 targetVelocity = runtimeState.isPaused ?
			Vector3{0.0f, 0.0f, 0.0f} : Multiply(std::abs(runtimeState.currentSpeed), chaseRailDirection);
		const Vector3 positionError = MultiplyComponents(
			Subtract(targetPosition, currentPosition),
			positionInfluence);
		const Vector3 velocityError = Subtract(targetVelocity, rigidBodyComponent->velocity);
		const float forwardPositionError = Dot(positionError, chaseRailDirection);
		const float forwardVelocityError = Dot(velocityError, chaseRailDirection);
		const Vector3 lateralPositionError = Subtract(
			positionError,
			Multiply(forwardPositionError, chaseRailDirection));
		const Vector3 lateralVelocityError = Subtract(
			velocityError,
			Multiply(forwardVelocityError, chaseRailDirection));
		const float positionSpring = (std::max)(railMovementComponent->railPositionSpring, 0.0f);
		const float positionDamping = (std::max)(railMovementComponent->railPositionDamping, 0.0f);
		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		editorScene_->GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			worldPosition);
(void)worldScale;
		(void)worldPosition;
		Vector3 requestedAcceleration{};
		float debugTargetForwardSpeed = 0.0f;
		float debugCurrentForwardSpeed = 0.0f;
		float debugEngineAcceleration = 0.0f;

		if (railMovementComponent->railMovementMode == 2) {
			// 船体推進ではSpline接線へ直接押さず、船首ローカル軸へ推力を加える。
			// Railからの横ずれは操舵を主体とし、設定した割合だけ補助Forceで戻す。
			const Vector3 shipForward = ResolveRailWorldForward(
				worldRotation,
				railMovementComponent->railLocalForwardAxis,
				railMovementComponent->railShipHorizontalThrust);
			// 波に押し出されてレールから大きく離れた場合はレール側の権限を強める。
			// 横ずれが小さい内は lateralAssist の設定比率（ゆるさ）を保ち、
			// 離れるほど横補正を 100% へ、推力方向をレール接線方向へ寄せる。
			const float lateralErrorLength = Length(lateralPositionError);
			const float railAuthority = (std::clamp)(
				lateralErrorLength / kRailAuthorityRadius,
				0.0f,
				1.0f);
			Vector3 railForward = railMovementComponent->railShipHorizontalThrust
				? Vector3{chaseRailDirection.x, 0.0f, chaseRailDirection.z}
				: chaseRailDirection;
			const float railForwardLength = Length(railForward);
			if (railForwardLength > kRailMinimumDistance) {
				railForward = Multiply(1.0f / railForwardLength, railForward);
			}
			else {
				railForward = shipForward;
			}
			const Vector3 thrustForward = railAuthority <= 0.0f
				? shipForward
				: Normalize(Add(
					Multiply(1.0f - railAuthority, shipForward),
					Multiply(railAuthority, railForward)));
			const float targetForwardSpeed = runtimeState.isPaused ? 0.0f : std::abs(runtimeState.currentSpeed);
			const float currentForwardSpeed = Dot(rigidBodyComponent->velocity, thrustForward);
			debugTargetForwardSpeed = targetForwardSpeed;
			debugCurrentForwardSpeed = currentForwardSpeed;
			// 波の抗力などで目標速度まで出し切れない時も、沿線の遅れ分を引っ張って前進を保証する。
			const float engineAcceleration =
				positionDamping * kRailEngineResponseScale * (targetForwardSpeed - currentForwardSpeed) +
				positionSpring * kRailAlongPositionSpringScale * forwardPositionError;
			debugEngineAcceleration = engineAcceleration;
			const float lateralAssist = (std::clamp)(railMovementComponent->railShipLateralAssist, 0.0f, 1.0f);
			const float lateralCorrectionScale = lateralAssist + (1.0f - lateralAssist) * railAuthority;
			const Vector3 lateralAcceleration = Multiply(
				lateralCorrectionScale,
				Add(
					Multiply(positionSpring, lateralPositionError),
					Multiply(positionDamping, lateralVelocityError)));
			requestedAcceleration = Add(
				Multiply(engineAcceleration, thrustForward),
				lateralAcceleration);
		}
		else {
			const float engineThrust = positionDamping * kRailEngineResponseScale * forwardVelocityError;
			const float alongCorrection =
				positionSpring * kRailAlongPositionSpringScale * forwardPositionError;
			const Vector3 forwardAcceleration = Multiply(
				engineThrust + alongCorrection,
				chaseRailDirection);
			const Vector3 lateralAcceleration = Add(
				Multiply(positionSpring, lateralPositionError),
				Multiply(positionDamping, lateralVelocityError));
			requestedAcceleration = Add(forwardAcceleration, lateralAcceleration);
		}

		requestedAcceleration = ClampVectorLength(
			requestedAcceleration,
			(std::max)(railMovementComponent->railMaximumAcceleration, 0.0f));
		// Buoyancy等で実際のボディ質量が変わっている場合に対応するため、
		// コンポーネントの質量ではなく実際のJoltボディ質量を使用する。
		float actualBodyMass = rigidBodyComponent->mass;
		physicsManager_->GetBodyMass(gameObject.id, actualBodyMass);
		const float effectiveMass = (std::max)(actualBodyMass, 0.01f);
		const bool wasRailForceApplied = physicsManager_->AddForce(
			gameObject.id,
			Multiply(effectiveMass, requestedAcceleration));
		railMovementComponent->velocity = rigidBodyComponent->velocity;

		// TEMP DEBUG: 船体推進の実力を確認する診断ログ。調査完了後に削除する。
		{
			static uint32_t railDebugCounter = 0u;
			const bool isPlayerShipForDiagnostics =
				gameObject.name.find("PlayerShip") != std::string::npos;
			if (isPlayerShipForDiagnostics && (railDebugCounter++ % 60u) == 0u) {
				float bodyMass = 0.0f;
				physicsManager_->GetBodyMass(gameObject.id, bodyMass);
				const float appliedForceMagnitude = Length(Multiply(effectiveMass, requestedAcceleration));
				char debugBuffer[512];
				snprintf(
					debugBuffer,
					sizeof(debugBuffer),
					"[RailShip] %s mode=%d target=%.2f vAlong=%.2f engine=%.2f fwdErr=%.2f latErr=%.2f clamp=%.2f compMass=%.2f bodyMass=%.2f force=%.2f vel=(%.2f,%.2f,%.2f) angVel=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f) rot=(%.2f,%.2f,%.2f) traveled=%.1f/%.1f\n",
					gameObject.name.c_str(),
					railMovementComponent->railMovementMode,
					debugTargetForwardSpeed,
					debugCurrentForwardSpeed,
					debugEngineAcceleration,
					forwardPositionError,
					Length(lateralPositionError),
					railMovementComponent->railMaximumAcceleration,
					effectiveMass,
					bodyMass,
					appliedForceMagnitude,
					rigidBodyComponent->velocity.x,
					rigidBodyComponent->velocity.y,
					rigidBodyComponent->velocity.z,
					rigidBodyComponent->angularVelocity.x,
					rigidBodyComponent->angularVelocity.y,
					rigidBodyComponent->angularVelocity.z,
					worldPosition.x,
					worldPosition.y,
					worldPosition.z,
					gameObject.rotate.x,
					gameObject.rotate.y,
					gameObject.rotate.z,
					runtimeState.traveledDistance,
					runtimeState.totalDistance);
				OutputDebugStringA(debugBuffer);
			}
		}

		if (!wasRailForceApplied) {
			// Collider生成失敗などでJolt Bodyが存在しない場合でも、RailFollower全体を停止させない。
			// 物理Bodyが復旧したフレームからは通常のForce追従へ自動的に戻る。
			const Vector3 fallbackRailOffsetPosition = ApplyRailOffset(
				railPosition,
				railDirection,
				runtimeState.currentOffset);
			const Vector3 fallbackTargetPosition = ApplyMovementModifierPosition(
				fallbackRailOffsetPosition,
				railDirection,
				movementModifier,
				runtimeState.modifierInputOffset);
			Vector3 fallbackWorldScale = gameObject.scale;
			Vector3 fallbackWorldRotation = worldRotation;
			Vector3 fallbackWorldPosition = currentPosition;
			editorScene_->GetWorldTransform(
				gameObject.id,
				fallbackWorldScale,
				fallbackWorldRotation,
				fallbackWorldPosition);
			editorScene_->SetWorldTransform(
				gameObject.id,
				fallbackWorldScale,
				fallbackWorldRotation,
				fallbackTargetPosition);
			railMovementComponent->velocity = runtimeState.isPaused ?
				Vector3{0.0f, 0.0f, 0.0f} :
				Multiply(
					1.0f / fixedDeltaTime,
					Subtract(fallbackTargetPosition, fallbackWorldPosition));
			continue;
		}

		if (!railMovementComponent->railOrientToPath ||
			Length(railDirection) <= kRailMinimumDistance) {
			continue;
		}

		Vector3 targetRotation{
			-std::asin((std::clamp)(chaseRailDirection.y, -1.0f, 1.0f)),
			std::atan2(chaseRailDirection.x, chaseRailDirection.z) - ResolveRailLocalForwardYawOffset(
				railMovementComponent->railLocalForwardAxis),
			worldRotation.z};

		// ロール角の制限: 最大ロール角度内にクランプ
		const float maxRollAngleRad = railMovementComponent->railMaximumRollAngle * 0.0174532924f;
		if (maxRollAngleRad > 0.0f) {
			const float currentRoll = NormalizeAngle(worldRotation.z);
			const float clampedRoll = (std::clamp)(currentRoll, -maxRollAngleRad, maxRollAngleRad);
			targetRotation.z = clampedRoll;
		}

		// ピッチ角の制限: 最大ピッチ角度内にクランプ
		const float maxPitchAngleRad = railMovementComponent->railMaximumPitchAngle * 0.0174532924f;
		if (maxPitchAngleRad > 0.0f) {
			const float currentPitch = NormalizeAngle(worldRotation.x);
			const float clampedPitch = (std::clamp)(currentPitch, -maxPitchAngleRad, maxPitchAngleRad);
			targetRotation.x = clampedPitch;
		}

		// ヨー角の制限: 最大ヨー角度内にクランプ
		const float maxYawAngleRad = railMovementComponent->railMaximumYawAngle * 0.0174532924f;
		if (maxYawAngleRad > 0.0f) {
			const float currentYaw = NormalizeAngle(worldRotation.y);
			const float clampedYaw = (std::clamp)(currentYaw, -maxYawAngleRad, maxYawAngleRad);
			targetRotation.y = clampedYaw;
		}

		if (movementModifier != nullptr && movementModifier->isActive) {
			constexpr float kDegreeToRadian = 0.0174532924f;
			targetRotation.x += movementModifier->movementModifierLocalRotationOffset.x * kDegreeToRadian;
			targetRotation.y += movementModifier->movementModifierLocalRotationOffset.y * kDegreeToRadian;
			targetRotation.z += movementModifier->movementModifierLocalRotationOffset.z * kDegreeToRadian;
		}
		const Vector3 rotationInfluence{
			(std::clamp)(railMovementComponent->railRotationInfluence.x, 0.0f, 1.0f),
			(std::clamp)(railMovementComponent->railRotationInfluence.y, 0.0f, 1.0f),
			(std::clamp)(railMovementComponent->railRotationInfluence.z, 0.0f, 1.0f)};
		const Vector3 rotationError = MultiplyComponents(
			{
				NormalizeAngle(targetRotation.x - worldRotation.x),
				NormalizeAngle(targetRotation.y - worldRotation.y),
				NormalizeAngle(targetRotation.z - worldRotation.z)},
			rotationInfluence);
		const Vector3 angularVelocity = MultiplyComponents(
			rigidBodyComponent->angularVelocity,
			rotationInfluence);
		Vector3 requestedAngularAcceleration = Add(
			Multiply((std::max)(railMovementComponent->railRotationSpring, 0.0f), rotationError),
			Multiply(-(std::max)(railMovementComponent->railRotationDamping, 0.0f), angularVelocity));

		// ロール復元力とダンピングを追加
		const float rollRestorationStrength = (std::max)(railMovementComponent->railRollRestorationStrength, 0.0f);
		const float rollDamping = (std::max)(railMovementComponent->railRollDamping, 0.0f);
		if (rollRestorationStrength > 0.0f || rollDamping > 0.0f) {
			const float currentRoll = NormalizeAngle(worldRotation.z);
			const float rollError = NormalizeAngle(-currentRoll);  // 直立（0）に戻す
			const float rollVelocity = rigidBodyComponent->angularVelocity.z;
			const float rollTorque = rollRestorationStrength * rollError - rollDamping * rollVelocity;
			requestedAngularAcceleration.z += rollTorque;
		}

		// ピッチ復元力とダンピングを追加
		const float pitchRestorationStrength = (std::max)(railMovementComponent->railPitchRestorationStrength, 0.0f);
		const float pitchDamping = (std::max)(railMovementComponent->railPitchDamping, 0.0f);
		if (pitchRestorationStrength > 0.0f || pitchDamping > 0.0f) {
			const float currentPitch = NormalizeAngle(worldRotation.x);
			const float pitchError = NormalizeAngle(-currentPitch);  // 水平（0）に戻す
			const float pitchVelocity = rigidBodyComponent->angularVelocity.x;
			const float pitchTorque = pitchRestorationStrength * pitchError - pitchDamping * pitchVelocity;
			requestedAngularAcceleration.x += pitchTorque;
		}

		// ヨー復元力とダンピングを追加
		const float yawRestorationStrength = (std::max)(railMovementComponent->railYawRestorationStrength, 0.0f);
		const float yawDamping = (std::max)(railMovementComponent->railYawDamping, 0.0f);
		if (yawRestorationStrength > 0.0f || yawDamping > 0.0f) {
			const float currentYaw = NormalizeAngle(worldRotation.y);
			const float railYaw = std::atan2(chaseRailDirection.x, chaseRailDirection.z);
			const float adjustedRailYaw = railYaw - ResolveRailLocalForwardYawOffset(
				railMovementComponent->railLocalForwardAxis);
			const float yawError = NormalizeAngle(adjustedRailYaw - currentYaw);  // 進行方向に戻す
			const float yawVelocity = rigidBodyComponent->angularVelocity.y;
			const float yawTorque = yawRestorationStrength * yawError - yawDamping * yawVelocity;
			requestedAngularAcceleration.y += yawTorque;
		}

		requestedAngularAcceleration = ClampVectorLength(
			requestedAngularAcceleration,
			(std::max)(railMovementComponent->railMaximumAngularAcceleration, 0.0f));
		physicsManager_->AddTorque(
			gameObject.id,
			Multiply(effectiveMass, requestedAngularAcceleration));
	}
}

void EditorRailMovementManager::Draw() {
}

void EditorRailMovementManager::Stop() {
	isStarted_ = false;
}

void EditorRailMovementManager::ResetSessionState() {
	runtimeStates_.clear();
}

bool EditorRailMovementManager::SetPaused(int32_t gameObjectId, bool isPaused) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	EditorComponent* railMovementComponent = gameObject != nullptr ?
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) : nullptr;

	if (railMovementComponent == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	runtimeState.isPaused = isPaused;

	if (isPaused) {
		railMovementComponent->velocity = {0.0f, 0.0f, 0.0f};
	}

	return true;
}

bool EditorRailMovementManager::IsPaused(int32_t gameObjectId) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);
	return runtimeStateIterator != runtimeStates_.end() && runtimeStateIterator->second.isPaused;
}

bool EditorRailMovementManager::SetSpeed(int32_t gameObjectId, float speed) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	EditorComponent* railMovementComponent = gameObject != nullptr ?
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) : nullptr;

	if (railMovementComponent == nullptr) {
		return false;
	}

	railMovementComponent->railSpeed = speed;
	return true;
}

bool EditorRailMovementManager::SetReverse(int32_t gameObjectId, bool isReversed) {
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;

	if (!isStarted_ || gameObject == nullptr ||
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	runtimeState.isReversed = isReversed;
	return true;
}

bool EditorRailMovementManager::SetNormalizedProgress(
	int32_t gameObjectId,
	float normalizedProgress) {
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;

	if (!isStarted_ || gameObject == nullptr ||
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	const float clampedProgress = (std::clamp)(normalizedProgress, 0.0f, 1.0f);

	if (runtimeState.totalDistance > kRailMinimumDistance) {
		runtimeState.traveledDistance = clampedProgress * runtimeState.totalDistance;
		runtimeState.isDistanceInitialized = true;
	} else {
		runtimeState.pendingNormalized = clampedProgress;
		runtimeState.hasPendingNormalized = true;
	}

	runtimeState.endReached = false;
	return true;
}

bool EditorRailMovementManager::SetRailPath(
	int32_t gameObjectId,
	int32_t railPathGameObjectId,
	bool preservesProgress) {
	if (!isStarted_ || editorScene_ == nullptr || gameObjectId == railPathGameObjectId) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	const EditorGameObject* railPathGameObject = editorScene_->FindGameObject(railPathGameObjectId);
	EditorComponent* railMovementComponent = gameObject != nullptr ?
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) : nullptr;

	if (railMovementComponent == nullptr || railPathGameObject == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	float nextProgress = railMovementComponent->railStartNormalized;

	if (preservesProgress && runtimeState.totalDistance > kRailMinimumDistance) {
		nextProgress = runtimeState.traveledDistance / runtimeState.totalDistance;
	}

	railMovementComponent->railPathGameObjectId = railPathGameObjectId;
	runtimeState.samples.clear();
	runtimeState.totalDistance = 0.0f;
	runtimeState.isDistanceInitialized = false;
	runtimeState.pendingNormalized = (std::clamp)(nextProgress, 0.0f, 1.0f);
	runtimeState.hasPendingNormalized = true;
	runtimeState.endReached = false;
	return true;
}

bool EditorRailMovementManager::SetMoveInput(
	int32_t gameObjectId,
	const EditorScriptVector2& moveInput) {
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;

	if (!isStarted_ || gameObject == nullptr ||
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	runtimeState.moveInput = {
		(std::clamp)(moveInput.x, -1.0f, 1.0f),
		(std::clamp)(moveInput.y, -1.0f, 1.0f)};
	return true;
}

bool EditorRailMovementManager::SetOffset(
	int32_t gameObjectId,
	const EditorScriptVector2& offset) {
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;
	const EditorComponent* railMovementComponent = gameObject != nullptr ?
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) : nullptr;

	if (!isStarted_ || railMovementComponent == nullptr) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStates_[gameObjectId];
	runtimeState.currentOffset = {
		(std::clamp)(
			offset.x,
			-(std::max)(railMovementComponent->railMovementRange.x, 0.0f),
			(std::max)(railMovementComponent->railMovementRange.x, 0.0f)),
		(std::clamp)(
			offset.y,
			-(std::max)(railMovementComponent->railMovementRange.y, 0.0f),
			(std::max)(railMovementComponent->railMovementRange.y, 0.0f))};
	return true;
}

bool EditorRailMovementManager::GetOffset(
	int32_t gameObjectId,
	EditorScriptVector2& offset) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end()) {
		return false;
	}

	offset = runtimeStateIterator->second.currentOffset;
	return true;
}

bool EditorRailMovementManager::GetNormalizedProgress(
	int32_t gameObjectId,
	float& normalizedProgress) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	normalizedProgress = (std::clamp)(
		runtimeStateIterator->second.traveledDistance /
			runtimeStateIterator->second.totalDistance,
		0.0f,
		1.0f);
	return true;
}

bool EditorRailMovementManager::GetRailLength(int32_t gameObjectId, float& railLength) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	railLength = runtimeStateIterator->second.totalDistance;
	return true;
}

bool EditorRailMovementManager::GetRailPosition(
	int32_t gameObjectId,
	float normalizedProgress,
	Vector3& position) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.samples.empty() ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	const EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;
	const float distance =
		(std::clamp)(normalizedProgress, 0.0f, 1.0f) * runtimeState.totalDistance;
	position = SampleRailPosition(runtimeState.samples, distance);
	return true;
}

bool EditorRailMovementManager::GetRailDirection(
	int32_t gameObjectId,
	float normalizedProgress,
	Vector3& direction) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.samples.empty() ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	const EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;
	const float distance =
		(std::clamp)(normalizedProgress, 0.0f, 1.0f) * runtimeState.totalDistance;
	const float directionSampleDistance = (std::max)(runtimeState.totalDistance * 0.001f, 0.01f);
	const float forwardDistance = (std::min)(
		distance + directionSampleDistance,
		runtimeState.totalDistance);
	const float backwardDistance = (std::max)(distance - directionSampleDistance, 0.0f);
	direction = Subtract(
		SampleRailPosition(runtimeState.samples, forwardDistance),
		SampleRailPosition(runtimeState.samples, backwardDistance));

	if (Length(direction) <= kRailMinimumDistance) {
		return false;
	}

	direction = Normalize(direction);
	return true;
}

bool EditorRailMovementManager::SetDistance(int32_t gameObjectId, float distance) {
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (!isStarted_ || gameObject == nullptr ||
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) == nullptr ||
		runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;
	runtimeState.traveledDistance = (std::clamp)(distance, 0.0f, runtimeState.totalDistance);
	runtimeState.isDistanceInitialized = true;
	runtimeState.hasPendingNormalized = false;
	runtimeState.endReached = false;
	return true;
}

bool EditorRailMovementManager::GetState(
	int32_t gameObjectId,
	EditorScriptRailState& state) const {
	state = {};
	const EditorGameObject* gameObject = editorScene_ != nullptr ?
		editorScene_->FindGameObject(gameObjectId) : nullptr;
	const EditorComponent* railMovementComponent = gameObject != nullptr ?
		EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement) : nullptr;

	if (railMovementComponent == nullptr) {
		return false;
	}

	state.hasComponent = true;
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end()) {
		state.targetSpeed = railMovementComponent->railSpeed;
		return true;
	}

	const EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;
	state.isReady = runtimeState.isDistanceInitialized &&
		runtimeState.totalDistance > kRailMinimumDistance;
	state.isPaused = runtimeState.isPaused;
	state.isReversed = runtimeState.isReversed;
	state.endReached = runtimeState.endReached;
	state.traveledDistance = runtimeState.traveledDistance;
	state.totalDistance = runtimeState.totalDistance;
	state.currentSpeed = runtimeState.currentSpeed;
	const float targetSpeed =
		railMovementComponent->railSpeed * runtimeState.targetSpeedMultiplier;
	state.targetSpeed = runtimeState.isReversed ? -targetSpeed : targetSpeed;
	state.offset = runtimeState.currentOffset;

	if (runtimeState.totalDistance > kRailMinimumDistance) {
		state.normalizedProgress = (std::clamp)(
			runtimeState.traveledDistance / runtimeState.totalDistance,
			0.0f,
			1.0f);
	}

	return true;
}

bool EditorRailMovementManager::GetSpeedMultiplier(
	int32_t gameObjectId,
	float& speedMultiplier) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end()) {
		return false;
	}

	speedMultiplier = runtimeStateIterator->second.targetSpeedMultiplier;
	return true;
}

bool EditorRailMovementManager::RearmEventMarkers(
	int32_t gameObjectId,
	const std::string& markerId) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	EditorComponent* railEventMarker = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailEventMarker)
		: nullptr;

	if (railEventMarker == nullptr) {
		return false;
	}

	bool wasRearmed = false;

	for (EditorRailEventMarkerEntry& markerEntry : railEventMarker->railEventMarkerEntries) {
		if (!markerId.empty() && markerEntry.markerId != markerId) {
			continue;
		}

		markerEntry.runtimeTriggered = false;
		wasRearmed = true;
	}

	return wasRearmed;
}

bool EditorRailMovementManager::GetClosestNormalizedProgress(
	int32_t gameObjectId,
	const Vector3& worldPosition,
	float& normalizedProgress) const {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() ||
		runtimeStateIterator->second.samples.size() < 2u ||
		runtimeStateIterator->second.totalDistance <= kRailMinimumDistance) {
		return false;
	}

	const EditorRailFollowerRuntimeState& runtimeState = runtimeStateIterator->second;
	float closestDistanceSquared = (std::numeric_limits<float>::max)();
	float closestRailDistance = 0.0f;

	for (size_t sampleIndex = 1u; sampleIndex < runtimeState.samples.size(); sampleIndex++) {
		const EditorRailRuntimeSample& previousSample = runtimeState.samples[sampleIndex - 1u];
		const EditorRailRuntimeSample& currentSample = runtimeState.samples[sampleIndex];
		const Vector3 segment = Subtract(currentSample.position, previousSample.position);
		const float segmentLengthSquared = Dot(segment, segment);

		if (segmentLengthSquared <= kRailMinimumDistance * kRailMinimumDistance) {
			continue;
		}

		const float segmentRatio = (std::clamp)(
			Dot(Subtract(worldPosition, previousSample.position), segment) /
				segmentLengthSquared,
			0.0f,
			1.0f);
		const Vector3 closestPosition = Add(
			previousSample.position,
			Multiply(segmentRatio, segment));
		const Vector3 positionDifference = Subtract(worldPosition, closestPosition);
		const float distanceSquared = Dot(positionDifference, positionDifference);

		if (distanceSquared >= closestDistanceSquared) {
			continue;
		}

		closestDistanceSquared = distanceSquared;
		closestRailDistance = previousSample.distance +
			(currentSample.distance - previousSample.distance) * segmentRatio;
	}

	if (closestDistanceSquared == (std::numeric_limits<float>::max)()) {
		return false;
	}

	normalizedProgress = (std::clamp)(
		closestRailDistance / runtimeState.totalDistance,
		0.0f,
		1.0f);
	return true;
}

bool EditorRailMovementManager::GetRailFrame(
	int32_t gameObjectId,
	float normalizedProgress,
	EditorScriptRailFrame& frame) const {
	Vector3 position{};
	Vector3 forward{};

	if (!GetRailPosition(gameObjectId, normalizedProgress, position) ||
		!GetRailDirection(gameObjectId, normalizedProgress, forward)) {
		return false;
	}

	Vector3 referenceUp{0.0f, 1.0f, 0.0f};

	if (std::abs(Dot(forward, referenceUp)) >= 0.98f) {
		referenceUp = {0.0f, 0.0f, 1.0f};
	}

	const Vector3 right = Normalize(Cross(referenceUp, forward));
	const Vector3 up = Normalize(Cross(forward, right));
	frame.position = {position.x, position.y, position.z};
	frame.forward = {forward.x, forward.y, forward.z};
	frame.right = {right.x, right.y, right.z};
	frame.up = {up.x, up.y, up.z};
	return true;
}

bool EditorRailMovementManager::ConsumeEndReached(int32_t gameObjectId) {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() || !runtimeStateIterator->second.endReached) {
		return false;
	}

	runtimeStateIterator->second.endReached = false;
	return true;
}
