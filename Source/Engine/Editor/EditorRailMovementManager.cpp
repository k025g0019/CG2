#include "EditorRailMovementManager.h"

#include "EditorComponentUtility.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace {
	constexpr int32_t kSmoothSamplesPerSegment = 16;
	constexpr float kRailMinimumDistance = 0.0001f;

	Vector3 ResolveWorldPosition(const EditorScene& editorScene, const EditorGameObject& gameObject) {
		Vector3 worldPosition = gameObject.translate;
		const EditorGameObject* parentGameObject = editorScene.FindGameObject(gameObject.parentId);

		// 現在の描画系と同じく、Hierarchy の親 Transform は位置を順に加算する。
		while (parentGameObject != nullptr) {
			worldPosition = Add(worldPosition, parentGameObject->translate);
			parentGameObject = editorScene.FindGameObject(parentGameObject->parentId);
		}

		return worldPosition;
	}

	Vector3 ConvertWorldToLocalPosition(
		const EditorScene& editorScene,
		const EditorGameObject& gameObject,
		const Vector3& worldPosition) {
		const EditorGameObject* parentGameObject = editorScene.FindGameObject(gameObject.parentId);

		if (parentGameObject == nullptr) {
			return worldPosition;
		}

		return Subtract(worldPosition, ResolveWorldPosition(editorScene, *parentGameObject));
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
}

void EditorRailMovementManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
	runtimeStates_.clear();
	isStarted_ = false;
}

void EditorRailMovementManager::Start() {
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
			runtimeState.isPaused = railMovementComponent->railStartPaused;
			runtimeState.isReversed = railMovementComponent->railReverse;
			runtimeStates_.emplace(gameObject.id, std::move(runtimeState));
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

		if (!runtimeState.isPaused) {
			float targetSpeed = railMovementComponent->railSpeed;

			if (runtimeState.isReversed) {
				targetSpeed *= -1.0f;
			}

			const bool isChangingDirection = runtimeState.currentSpeed * targetSpeed < 0.0f;
			const bool isAccelerating =
				!isChangingDirection && std::abs(targetSpeed) > std::abs(runtimeState.currentSpeed);
			const float speedChangeRate = isAccelerating ?
				railMovementComponent->railAcceleration : railMovementComponent->railDeceleration;
			const float speedTarget = isChangingDirection ? 0.0f : targetSpeed;
			runtimeState.currentSpeed = MoveTowards(
				runtimeState.currentSpeed,
				speedTarget,
				speedChangeRate * deltaTime);

			const float previousDistance = runtimeState.traveledDistance;
			const float requestedDistance =
				previousDistance + runtimeState.currentSpeed * deltaTime;
			const bool hasCrossedEnd =
				requestedDistance < 0.0f || requestedDistance > runtimeState.totalDistance;
			runtimeState.traveledDistance = NormalizeRailDistance(
				requestedDistance,
				runtimeState.totalDistance,
				railMovementComponent->railLoop);

			if (hasCrossedEnd) {
				runtimeState.endReached = true;

				if (!railMovementComponent->railLoop && railMovementComponent->railStopAtEnd) {
					runtimeState.isPaused = true;
					runtimeState.currentSpeed = 0.0f;
				}
			}
		}

		const Vector3 previousWorldPosition = ResolveWorldPosition(*editorScene_, gameObject);
		const Vector3 nextWorldPosition = SampleRailPosition(
			runtimeState.samples,
			runtimeState.traveledDistance);
		gameObject.translate = ConvertWorldToLocalPosition(
			*editorScene_,
			gameObject,
			nextWorldPosition);
		railMovementComponent->velocity = runtimeState.isPaused ?
			Vector3{0.0f, 0.0f, 0.0f} :
			Multiply(1.0f / deltaTime, Subtract(nextWorldPosition, previousWorldPosition));

		if (!railMovementComponent->railOrientToPath) {
			continue;
		}

		const float lookAheadDistance = (std::max)(
			railMovementComponent->railLookAheadDistance,
			0.01f);
		const float forwardDistance = NormalizeRailDistance(
			runtimeState.traveledDistance + lookAheadDistance,
			runtimeState.totalDistance,
			railMovementComponent->railLoop);
		Vector3 forwardDirection = Subtract(
			SampleRailPosition(runtimeState.samples, forwardDistance),
			nextWorldPosition);

		if (Length(forwardDirection) <= kRailMinimumDistance) {
			const float backwardDistance = NormalizeRailDistance(
				runtimeState.traveledDistance - lookAheadDistance,
				runtimeState.totalDistance,
				railMovementComponent->railLoop);
			forwardDirection = Subtract(
				nextWorldPosition,
				SampleRailPosition(runtimeState.samples, backwardDistance));
		}

		const bool facesReverse = std::abs(runtimeState.currentSpeed) > kRailMinimumDistance ?
			runtimeState.currentSpeed < 0.0f :
			((railMovementComponent->railSpeed < 0.0f) != runtimeState.isReversed);

		if (facesReverse) {
			forwardDirection = Multiply(-1.0f, forwardDirection);
		}

		if (Length(forwardDirection) > kRailMinimumDistance) {
			forwardDirection = Normalize(forwardDirection);
			gameObject.rotate.x = -std::asin((std::clamp)(forwardDirection.y, -1.0f, 1.0f));
			gameObject.rotate.y = std::atan2(forwardDirection.x, forwardDirection.z);
		}
	}
}

void EditorRailMovementManager::Draw() {
}

void EditorRailMovementManager::Stop() {
	runtimeStates_.clear();
	isStarted_ = false;
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

bool EditorRailMovementManager::ConsumeEndReached(int32_t gameObjectId) {
	const auto runtimeStateIterator = runtimeStates_.find(gameObjectId);

	if (runtimeStateIterator == runtimeStates_.end() || !runtimeStateIterator->second.endReached) {
		return false;
	}

	runtimeStateIterator->second.endReached = false;
	return true;
}
