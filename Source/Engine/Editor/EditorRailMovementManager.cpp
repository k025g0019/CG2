#include "EditorRailMovementManager.h"

#include "EditorComponentUtility.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	constexpr int32_t kSmoothSamplesPerSegment = 16;
	constexpr float kRailMinimumDistance = 0.0001f;

	struct RailSample {
		Vector3 position{0.0f, 0.0f, 0.0f};
		float distance = 0.0f;
	};

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
		std::vector<RailSample>& railSamples) {
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

	Vector3 SampleRailPosition(const std::vector<RailSample>& railSamples, float distance) {
		const auto upperSample = std::lower_bound(
			railSamples.begin(),
			railSamples.end(),
			distance,
			[](const RailSample& railSample, float targetDistance) {
				return railSample.distance < targetDistance;
			});

		if (upperSample == railSamples.begin()) {
			return upperSample->position;
		}

		if (upperSample == railSamples.end()) {
			return railSamples.back().position;
		}

		const RailSample& previousSample = *(upperSample - 1);
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
}

void EditorRailMovementManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
	traveledDistanceByGameObjectId_.clear();
	totalDistanceByGameObjectId_.clear();
	isStarted_ = false;
}

void EditorRailMovementManager::Start() {
	traveledDistanceByGameObjectId_.clear();
	totalDistanceByGameObjectId_.clear();
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

		std::vector<RailSample> railSamples;

		if (!BuildRailSamples(
				*editorScene_,
				*railPathGameObject,
				railMovementComponent->railLoop,
				railMovementComponent->railUseSmoothCurve,
				railSamples)) {
			continue;
		}

		const float totalDistance = railSamples.back().distance;
		totalDistanceByGameObjectId_[gameObject.id] = totalDistance;
		auto [distanceIterator, wasInserted] = traveledDistanceByGameObjectId_.try_emplace(
			gameObject.id,
			(std::clamp)(railMovementComponent->railStartNormalized, 0.0f, 1.0f) * totalDistance);

		if (!wasInserted) {
			distanceIterator->second += railMovementComponent->railSpeed * deltaTime;
		}

		distanceIterator->second = NormalizeRailDistance(
			distanceIterator->second,
			totalDistance,
			railMovementComponent->railLoop);
		const Vector3 previousWorldPosition = ResolveWorldPosition(*editorScene_, gameObject);
		const Vector3 nextWorldPosition = SampleRailPosition(
			railSamples,
			distanceIterator->second);
		gameObject.translate = ConvertWorldToLocalPosition(
			*editorScene_,
			gameObject,
			nextWorldPosition);
		railMovementComponent->velocity = Multiply(
			1.0f / deltaTime,
			Subtract(nextWorldPosition, previousWorldPosition));

		if (!railMovementComponent->railOrientToPath) {
			continue;
		}

		const float lookAheadDistance = (std::max)(
			railMovementComponent->railLookAheadDistance,
			0.01f);
		const float forwardDistance = NormalizeRailDistance(
			distanceIterator->second + lookAheadDistance,
			totalDistance,
			railMovementComponent->railLoop);
		Vector3 forwardDirection = Subtract(
			SampleRailPosition(railSamples, forwardDistance),
			nextWorldPosition);

		if (Length(forwardDirection) <= kRailMinimumDistance) {
			const float backwardDistance = NormalizeRailDistance(
				distanceIterator->second - lookAheadDistance,
				totalDistance,
				railMovementComponent->railLoop);
			forwardDirection = Subtract(
				nextWorldPosition,
				SampleRailPosition(railSamples, backwardDistance));
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
	traveledDistanceByGameObjectId_.clear();
	totalDistanceByGameObjectId_.clear();
	isStarted_ = false;
}

bool EditorRailMovementManager::GetNormalizedProgress(
	int32_t gameObjectId,
	float& normalizedProgress) const {
	const auto traveledDistanceIterator = traveledDistanceByGameObjectId_.find(gameObjectId);
	const auto totalDistanceIterator = totalDistanceByGameObjectId_.find(gameObjectId);

	if (traveledDistanceIterator == traveledDistanceByGameObjectId_.end() ||
		totalDistanceIterator == totalDistanceByGameObjectId_.end() ||
		totalDistanceIterator->second <= kRailMinimumDistance) {
		return false;
	}

	normalizedProgress = (std::clamp)(
		traveledDistanceIterator->second / totalDistanceIterator->second,
		0.0f,
		1.0f);
	return true;
}
