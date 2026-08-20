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
	constexpr float kRailRadianToDegree = 57.29577951f;
	constexpr float kRailDegreeToRadian = 0.0174532924f;

	// Yaw Safety Assist: Rail見出しからのYaw偏差が stage2→stage4 の範囲で
	// 二次イーズイン(立ち上がりが緩やか→終盤で急激に絞る)で 1.0→minSpeedScale へ落ちる。
	// 「20-30度はわずかに、30-45度は大きく絞る」という段階的挙動を単一の滑らかな曲線で表す。
	float ResolveYawSafetySpeedScale(
		float yawErrorAbsDegrees,
		float stage2Degrees,
		float stage4Degrees,
		float minSpeedScale) {
		const float rampStart = (std::max)(stage2Degrees, 0.0f);
		const float rampEnd = (std::max)(stage4Degrees, rampStart + 0.001f);
		const float t = (std::clamp)(
			(yawErrorAbsDegrees - rampStart) / (rampEnd - rampStart), 0.0f, 1.0f);
		const float easeInQuad = t * t;
		const float clampedMinSpeedScale = (std::clamp)(minSpeedScale, 0.0f, 1.0f);
		return 1.0f + (clampedMinSpeedScale - 1.0f) * easeInQuad;
	}

	// Yaw Safety Assist: stage1→stage4 の範囲でYaw復元強度を 1.0→maxRestorationScale へ
	// 線形に強める。速度側とは異なりRampの開始点が早い(10度から)ため、
	// 「10-20度はまず復元だけ強める」という段階を再現する。
	float ResolveYawSafetyRestorationScale(
		float yawErrorAbsDegrees,
		float stage1Degrees,
		float stage4Degrees,
		float maxRestorationScale) {
		const float rampStart = (std::max)(stage1Degrees, 0.0f);
		const float rampEnd = (std::max)(stage4Degrees, rampStart + 0.001f);
		const float t = (std::clamp)(
			(yawErrorAbsDegrees - rampStart) / (rampEnd - rampStart), 0.0f, 1.0f);
		const float clampedMaxRestorationScale = (std::max)(maxRestorationScale, 1.0f);
		return 1.0f + (clampedMaxRestorationScale - 1.0f) * t;
	}

	// 直交基底(right/up/forward、いずれも単位ベクトル)から、MakeAffineMatrixが再現できるEuler(x,y,z)へ
	// 変換する。EditorScene.cpp の DecomposeTransformMatrix と同じ式(scale=1相当)を使うことで、
	// MakeAffineMatrix(結果) が必ず元の基底を再現することを保証する。Pitch/Roll絶対角度制限の
	// Hard Clampだけが使う変換であり、値をEuler角として意味づけて読むためのものではない
	// (以前のPitch/Roll≈±180度バグはEuler成分を直接「物理角度」として読んだことが原因であり、
	// ここでは逆方向=基底→Eulerへの機械的な変換にのみ使うため同じ問題は起きない)。
	Vector3 DecomposeOrientationVectorsToEuler(
		const Vector3& right,
		const Vector3& up,
		const Vector3& forward) {
		constexpr float kTransformEpsilon = 0.00001f;
		Vector3 rotation{};
		const float sineY = (std::clamp)(-right.z, -1.0f, 1.0f);
		rotation.y = std::asin(sineY);
		const float cosineY = std::cos(rotation.y);

		if (std::fabs(cosineY) > kTransformEpsilon) {
			rotation.x = std::atan2(up.z, forward.z);
			rotation.z = std::atan2(right.y, right.x);
		}
		else {
			rotation.x = std::atan2(-forward.y, up.y);
			rotation.z = 0.0f;
		}

		return rotation;
	}

	// Roll/Pitch Safety Envelope: freeDegrees以下では0、emergencyDegreesで1へ飽和する危険度[0,1]。
	// freeDegrees未満は完全に0を返すので、波による通常の揺れには一切介入しない(Dead Zone)。
	// 呼び出し側でこれを二乗(t^2)して使うことで、free付近では緩やかに、emergencyへ近づくほど
	// 急激に介入を強める(急激なカクつきを避けつつ危険域では確実に効かせる)。
	float ResolveAttitudeSafetyFactor(
		float angleAbsDegrees,
		float freeDegrees,
		float emergencyDegrees) {
		const float rampStart = (std::max)(freeDegrees, 0.0f);
		const float rampEnd = (std::max)(emergencyDegrees, rampStart + 0.001f);
		return (std::clamp)(
			(angleAbsDegrees - rampStart) / (rampEnd - rampStart), 0.0f, 1.0f);
	}

	// Yaw Safety / Attitude Safetyで共通利用する「危険度t(0..1)に応じて1.0からminScaleへ
	// 二次イーズインで落ちるスケール」。tは呼び出し側でResolveAttitudeSafetyFactor等から渡す。
	float ResolveSafetyForwardScale(float dangerT, float minScale) {
		const float clampedMinScale = (std::clamp)(minScale, 0.0f, 1.0f);
		const float easeInQuad = dangerT * dangerT;
		return 1.0f + (clampedMinScale - 1.0f) * easeInQuad;
	}

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

	float SmoothStep01(float edge0, float edge1, float value) {
		const float span = edge1 - edge0;
		const float t = (std::clamp)(
			span > kRailMinimumDistance ? (value - edge0) / span : (value >= edge1 ? 1.0f : 0.0f),
			0.0f,
			1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Mode 2オートパイロット用: 船体の現在位置に最も近いRail上の距離を求める。
	// Rail全体を毎フレーム総当たりせず、前フレームの距離付近(searchWindowDistance)だけを探索する。
	// ウィンドウ内にサンプルが無い(テレポート・初期スポーン等で大きく外れた)場合のみ全域探索へ落ちる。
	// サンプル「点」ではなく、隣接サンプル間のSegment(線分)へ船体位置を射影して最近傍距離を求める。
	// 点だけを比較すると、サンプル間隔(直線区間で最大kSmoothSamplesPerSegmentごと)単位でしか
	// 距離が変化できず、1 Physics Frameで数十m飛ぶ不連続な結果を生む。線分射影により、
	// 実際の船体位置に応じて連続的(サンプル間を補間した値)に変化する距離を返す。
	float FindClosestRailDistance(
		const std::vector<EditorRailRuntimeSample>& samples,
		const Vector3& shipPosition,
		float previousDistance,
		float totalDistance,
		bool isLooping,
		float searchWindowDistance) {
		const size_t sampleCount = samples.size();
		if (sampleCount == 0u) {
			return 0.0f;
		}
		if (sampleCount == 1u) {
			return samples.front().distance;
		}

		float bestDistance = (std::clamp)(previousDistance, 0.0f, totalDistance);
		float bestSquaredLength = 0.0f;
		bool hasBest = false;

		// Segment[index, index+1]を船体位置Pへ射影し、線分上の最近傍点とそれに対応する
		// 連続的なdistance(2端点のdistanceをtで補間した値)を候補に加える。
		const auto evaluateSegment = [&](size_t index) {
			const EditorRailRuntimeSample& first = samples[index];
			const EditorRailRuntimeSample& second = samples[index + 1u];
			const Vector3 segment = Subtract(second.position, first.position);
			const float segmentLengthSquared = Dot(segment, segment);
			float t = 0.0f;
			if (segmentLengthSquared > kRailMinimumDistance * kRailMinimumDistance) {
				t = (std::clamp)(
					Dot(Subtract(shipPosition, first.position), segment) / segmentLengthSquared,
					0.0f,
					1.0f);
			}
			const Vector3 closestPoint = Add(first.position, Multiply(t, segment));
			const Vector3 offset = Subtract(closestPoint, shipPosition);
			const float squaredLength = Dot(offset, offset);
			if (!hasBest || squaredLength < bestSquaredLength) {
				hasBest = true;
				bestSquaredLength = squaredLength;
				bestDistance = first.distance + t * (second.distance - first.distance);
			}
		};

		const auto scanRange = [&](float rangeStart, float rangeEnd) {
			auto beginIterator = std::lower_bound(
				samples.begin(), samples.end(), rangeStart,
				[](const EditorRailRuntimeSample& sample, float value) { return sample.distance < value; });
			size_t beginIndex = static_cast<size_t>(beginIterator - samples.begin());
			if (beginIndex > 0u) {
				// 区間開始直前のSegment(前サンプル→区間内の最初のサンプル)も評価対象へ含める。
				--beginIndex;
			}
			const auto endIterator = std::upper_bound(
				samples.begin(), samples.end(), rangeEnd,
				[](float value, const EditorRailRuntimeSample& sample) { return value < sample.distance; });
			size_t endIndex = static_cast<size_t>(endIterator - samples.begin());
			endIndex = (std::min)(endIndex, sampleCount - 1u);

			for (size_t index = beginIndex; index < endIndex; ++index) {
				evaluateSegment(index);
			}
		};

		if (!isLooping) {
			scanRange(
				(std::max)(previousDistance - searchWindowDistance, 0.0f),
				(std::min)(previousDistance + searchWindowDistance, totalDistance));
		}
		else {
			const float normalizedPrevious = NormalizeRailDistance(previousDistance, totalDistance, true);
			const float rangeStart = normalizedPrevious - searchWindowDistance;
			const float rangeEnd = normalizedPrevious + searchWindowDistance;

			if (rangeStart < 0.0f) {
				scanRange(0.0f, rangeEnd);
				scanRange(totalDistance + rangeStart, totalDistance);
			}
			else if (rangeEnd > totalDistance) {
				scanRange(rangeStart, totalDistance);
				scanRange(0.0f, rangeEnd - totalDistance);
			}
			else {
				scanRange(rangeStart, rangeEnd);
			}
		}

		if (!hasBest) {
			// ウィンドウ内に候補が無かった場合(初期化・テレポート・局所探索で見失った場合)だけ
			// 全域をSegment単位で探索する。通常フレームでは発生しない。
			scanRange(0.0f, totalDistance);
		}

		return bestDistance;
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

	// この船の「ローカル船首軸」をrailLocalForwardAxisから求める。0=+Z、1=-Z、2=+X、3=-X。
	// Mode 2のshipForward/Yaw基準はすべてこの軸を経由し、Vector3(0,0,1)を独自にハードコードしない。
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

	// この船の「ローカル右舷軸」。船首軸と同じ水平面内で90度右にした軸を返す
	// (船首軸に対してRoll成分を持たない、船首軸選択と一貫した右方向)。
	Vector3 ResolveRailLocalRightAxis(int32_t localForwardAxis) {
		switch ((std::clamp)(localForwardAxis, 0, 3)) {
		case 1:
			return {-1.0f, 0.0f, 0.0f};
		case 2:
			return {0.0f, 0.0f, -1.0f};
		case 3:
			return {0.0f, 0.0f, 1.0f};
		default:
			return {1.0f, 0.0f, 0.0f};
		}
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
			// Path切り替え・初期構築はGameplay進行の不連続なジャンプなので、Physical Rail Progressも
			// 同じ位置へ同期する(古い経路のbacklogを引き継いで暴走させないため)。
			runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
			runtimeState.isPhysicalDistanceInitialized = true;
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

		// 診断値は毎ステップ0から積み直す。途中でcontinueした軸が前ステップの値を
		// 残したままにならないようにする。
		railMovementComponent->railDebugFollowForce = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugFollowTorque = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugPositionError = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugYawError = 0.0f;
		railMovementComponent->railDebugYawSafetySpeedScale = 1.0f;
		railMovementComponent->railDebugYawSafetyRestorationScale = 1.0f;
		railMovementComponent->railDebugForwardPositionScale = 1.0f;
		railMovementComponent->railDebugForwardPositionError = 0.0f;
		railMovementComponent->railDebugLateralPositionError = 0.0f;
		railMovementComponent->railDebugForwardCorrectionForce = 0.0f;
		railMovementComponent->railDebugLateralCorrectionForce = 0.0f;
		railMovementComponent->railDebugBoatPitchDegrees = 0.0f;
		railMovementComponent->railDebugBoatRollDegrees = 0.0f;
		railMovementComponent->railDebugPitchSafetyFactor = 0.0f;
		railMovementComponent->railDebugRollSafetyFactor = 0.0f;
		railMovementComponent->railDebugAttitudeRecoveryTorque = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugGameplayRailProgress = 0.0f;
		railMovementComponent->railDebugPhysicalRailProgress = 0.0f;
		railMovementComponent->railDebugPhysicalTargetSpeed = 0.0f;
		railMovementComponent->railDebugPitchAngleLimited = 0.0f;
		railMovementComponent->railDebugRollAngleLimited = 0.0f;
		railMovementComponent->railDebugClosestRailDistance = 0.0f;
		railMovementComponent->railDebugClosestRailDistanceDelta = 0.0f;
		railMovementComponent->railDebugSteeringLookAheadDistance = 0.0f;
		railMovementComponent->railDebugSteeringYawErrorDegrees = 0.0f;
		railMovementComponent->railDebugEngineAcceleration = 0.0f;
		railMovementComponent->railDebugLateralAssistAcceleration = 0.0f;
		railMovementComponent->railDebugLateralAssistScale = 0.0f;
		railMovementComponent->railDebugYawAngularVelocity = 0.0f;
		railMovementComponent->railDebugShipForward = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugShipRight = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugForwardVelocitySlipAngleDegrees = 0.0f;
		railMovementComponent->railDebugLateralSpeed = 0.0f;
		railMovementComponent->railDebugHullLateralGripAcceleration = 0.0f;
		railMovementComponent->railDebugHullLateralGripScale = 0.0f;
		railMovementComponent->railDebugHullLateralGripSpeedFactor = 0.0f;
		railMovementComponent->railDebugHullLateralGripSlipFactor = 0.0f;
		railMovementComponent->railDebugHorizontalSpeed = 0.0f;
		railMovementComponent->railDebugPreClampAcceleration = 0.0f;
		railMovementComponent->railDebugPostClampAcceleration = 0.0f;
		railMovementComponent->railDebugMode2ClampScale = 1.0f;
		railMovementComponent->railDebugRailRidePosition = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugRailRideActualPosition = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugRailRidePositionErrorXZ = 0.0f;
		railMovementComponent->railDebugRailRideForward = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugRailRideTargetYawDegrees = 0.0f;
		railMovementComponent->railDebugRailRideFinalYawDegrees = 0.0f;
		railMovementComponent->railDebugRailRideYawErrorDegrees = 0.0f;
		railMovementComponent->railDebugRailRideVelocityXZ = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugRailRideActualVelocityXZ = {0.0f, 0.0f, 0.0f};
		railMovementComponent->railDebugRailRideVelocityDirectionErrorDegrees = 0.0f;
		railMovementComponent->railDebugRailRidePhysicsY = 0.0f;
		railMovementComponent->railDebugRailRideFinalY = 0.0f;
		railMovementComponent->railDebugRailRidePhysicsPitchDegrees = 0.0f;
		railMovementComponent->railDebugRailRideFinalPitchDegrees = 0.0f;
		railMovementComponent->railDebugRailRidePhysicsRollDegrees = 0.0f;
		railMovementComponent->railDebugRailRideFinalRollDegrees = 0.0f;
		// Sceneの保存値がそのまま効いているかを確認するため、実際に使う値をそのまま出す。
		railMovementComponent->railDebugAppliedPositionInfluence =
			railMovementComponent->railPositionInfluence;
		railMovementComponent->railDebugAppliedRotationInfluence =
			railMovementComponent->railRotationInfluence;

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

		// Mode 2 Rail Ride: RailがXZ/Yaw/Progress/速度を完全所有するレールシューティング専用方式。
		// Pure Pursuit・Engine・Hull Grip・Rail Assist等の物理追従(Force/Torque)は一切使わない
		// (PostFixedUpdateで最終Poseを直接構築する)。Boat Autopilot(既定・SmallBoat/MissileBoat)や
		// Mode 1には一切影響しない。
		if (railMovementComponent->railMovementMode == 2 &&
			railMovementComponent->railMode2MovementStyle == 1) {
			continue;
		}

		if (!runtimeState.isPhysicalDistanceInitialized) {
			runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
			runtimeState.isPhysicalDistanceInitialized = true;
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
		// Position PD・操舵方向はGameplay Rail Progress(traveledDistance)ではなく
		// Physical Rail Progress(physicalTraveledDistance)から作る。Safetyで物理速度を
		// 落としてもGameplay進行だけが先へ逃げ続けず、目標位置が実船の近くに留まる。
		const float chaseRailDistance = NormalizeRailDistance(
				runtimeState.physicalTraveledDistance + chaseLookAheadDistance * railDirectionSign,
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

		// 船体の実際の軸ベクトル。worldRotation.x/zをEuler角として直接使わない
		// (Yaw(worldRotation.y)が180°付近になると水平姿勢でもEuler分解上のX/Zが
		// ±180°側へ飛ぶGimbal特有の多重表現があるため)。Roll/Pitch Safety Envelopeと
		// Mode 2オートパイロットの両方がこのベクトルを共通で使う。
		const Matrix4x4 boatOrientationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f}, worldRotation, {0.0f, 0.0f, 0.0f});
		const Vector3 boatRightForAttitude = Normalize(Transform({1.0f, 0.0f, 0.0f}, boatOrientationMatrix));
		const Vector3 boatUpForAttitude = Normalize(Transform({0.0f, 1.0f, 0.0f}, boatOrientationMatrix));
		const Vector3 boatForwardForAttitude = Normalize(Transform({0.0f, 0.0f, 1.0f}, boatOrientationMatrix));
		const Vector3 worldUpForAttitude{0.0f, 1.0f, 0.0f};
		// worldRotation.x = -asin(forward.y)(Update()のRail向き計算と同じ規約)に合わせる。
		const float boatPitchRad = -std::asin(
			(std::clamp)(Dot(boatForwardForAttitude, worldUpForAttitude), -1.0f, 1.0f));
		// 水平ならboatUp・WorldUpが一致(dot=1)し、boatRightは水平(dot=0)になるのでatan2(0,1)=0。
		const float boatRollRad = std::atan2(
			Dot(boatRightForAttitude, worldUpForAttitude),
			Dot(boatUpForAttitude, worldUpForAttitude));
		const float boatPitchDegrees = boatPitchRad * kRailRadianToDegree;
		const float boatRollDegrees = boatRollRad * kRailRadianToDegree;

		// ==============================================================
		// Mode 2 オートパイロット: 「RailへPD拘束される物体」ではなく「Railを航路として
		// 見ながら自力で航走する船」にする。Railは航路・少し先の目標地点・目標速度だけを与え、
		// 実際の移動は船首方向のエンジン推力とYaw操舵で発生させる。Mode 1には一切影響しない。
		// ==============================================================
		Vector3 mode2ShipForwardXZ{0.0f, 0.0f, 1.0f};
		Vector3 mode2ShipRightXZ{1.0f, 0.0f, 0.0f};
		Vector3 mode2LateralOffsetVector{};
		Vector3 mode2RailRightAtClosest{1.0f, 0.0f, 0.0f};
		float mode2LateralOffsetLength = 0.0f;
		float mode2SteeringYawErrorRad = 0.0f;

		if (railMovementComponent->railMovementMode == 2) {
			// この船の「ローカル船首軸」をrailLocalForwardAxisから求め、Mode 2の全計算
			// (shipForward・実前進速度・エンジン推力方向・Yaw計算・Yaw Safety・Hard ClampのYaw基準)で
			// 同じ軸を使う。Vector3(0,0,1)を独自にハードコードしない
			// (Roll/Pitch Safety Envelope用のboatForwardForAttitudeは既存仕様のまま+Z固定で変更しない)。
			const Vector3 shipForwardWorld = Normalize(Transform(
				ResolveRailLocalForwardAxis(railMovementComponent->railLocalForwardAxis),
				boatOrientationMatrix));
			const Vector3 shipRightWorld = Normalize(Transform(
				ResolveRailLocalRightAxis(railMovementComponent->railLocalForwardAxis),
				boatOrientationMatrix));
			railMovementComponent->railDebugShipForward = shipForwardWorld;
			railMovementComponent->railDebugShipRight = shipRightWorld;

			const Vector3 shipForwardXZRaw{shipForwardWorld.x, 0.0f, shipForwardWorld.z};
			const float shipForwardXZLength = Length(shipForwardXZRaw);
			mode2ShipForwardXZ = shipForwardXZLength > kRailMinimumDistance
				? Multiply(1.0f / shipForwardXZLength, shipForwardXZRaw)
				: Vector3{0.0f, 0.0f, 1.0f};
			// shipRightXZはshipRightWorldを独立にXZ射影しない。Pitch/Rollが付くと3D上で直交していた
			// Forward/RightがそれぞれのXZ射影後には厳密に直交しなくなり、lateralSpeed(Dot積)へ
			// 前進速度成分が漏れ込む。水平面上でshipForwardXZから90度回した向きとして作ることで、
			// Dot(shipForwardXZ, shipRightXZ)が常に0になることを保証する
			// (worldUpとのCrossの符号はResolveRailLocalRightAxisの各軸定義と一致することを確認済み)。
			const Vector3 worldUpForShipAxes{0.0f, 1.0f, 0.0f};
			mode2ShipRightXZ = Normalize(Cross(worldUpForShipAxes, mode2ShipForwardXZ));

			// 船首-移動方向差(度): 船首軸設定が90度ずれているのか(A)、船首は合っているが
			// 物理的に横滑りしているのか(B)を切り分けるための一時診断。制御には使わない。
			const Vector3 velocityXZRaw{
				rigidBodyComponent->velocity.x, 0.0f, rigidBodyComponent->velocity.z};
			const float velocityXZLength = Length(velocityXZRaw);
			constexpr float kMinimumSlipSpeed = 0.5f;
			if (velocityXZLength > kMinimumSlipSpeed) {
				const Vector3 velocityDirectionXZ = Multiply(1.0f / velocityXZLength, velocityXZRaw);
				const float slipAngleRad = NormalizeAngle(
					std::atan2(velocityDirectionXZ.x, velocityDirectionXZ.z) -
					std::atan2(mode2ShipForwardXZ.x, mode2ShipForwardXZ.z));
				railMovementComponent->railDebugForwardVelocitySlipAngleDegrees =
					slipAngleRad * kRailRadianToDegree;
			}
			else {
				railMovementComponent->railDebugForwardVelocitySlipAngleDegrees = 0.0f;
			}

			// 船体の現在位置からRail上の最近傍点を求める(全域総当たりではなく前フレーム位置付近だけ探索)。
			// 探索Windowの外には出ないため、空間的に近いだけの遠い別セグメントへ突然ジャンプすることは
			// ない(前フレーム周辺にサンプルが1つも無い初期化/テレポート時だけ全域探索へfallbackする)。
			const float previousPhysicalDistance = runtimeState.physicalTraveledDistance;
			const float searchWindowDistance = (std::max)(
				60.0f, std::abs(runtimeState.currentSpeed) * 2.0f + 10.0f);
			const float closestRailDistance = FindClosestRailDistance(
				runtimeState.samples,
				currentPosition,
				previousPhysicalDistance,
				runtimeState.totalDistance,
				railMovementComponent->railLoop,
				searchWindowDistance);
			runtimeState.physicalTraveledDistance = closestRailDistance;

			// ジャンプ検出診断: 前フレームからの距離変化量。ループ時はWrapを考慮して
			// [-totalDistance/2, totalDistance/2]へ正規化する(符号は進行方向へは+、逆行は-)。
			float closestRailDistanceDelta = closestRailDistance - previousPhysicalDistance;
			if (railMovementComponent->railLoop && runtimeState.totalDistance > kRailMinimumDistance) {
				const float halfTotalDistance = runtimeState.totalDistance * 0.5f;
				if (closestRailDistanceDelta > halfTotalDistance) {
					closestRailDistanceDelta -= runtimeState.totalDistance;
				}
				else if (closestRailDistanceDelta < -halfTotalDistance) {
					closestRailDistanceDelta += runtimeState.totalDistance;
				}
			}
			railMovementComponent->railDebugClosestRailDistanceDelta = closestRailDistanceDelta;

			const Vector3 closestRailPosition = SampleRailPosition(runtimeState.samples, closestRailDistance);
			const Vector3 closestRailDirectionAtShip = CalculateRailDirection(
				runtimeState, *railMovementComponent, closestRailDistance, closestRailPosition);
			const Vector3 offsetClosestPosition = ApplyRailOffset(
				closestRailPosition, closestRailDirectionAtShip, runtimeState.currentOffset);

			mode2LateralOffsetVector = Subtract(currentPosition, offsetClosestPosition);
			Vector3 referenceUpForRight{0.0f, 1.0f, 0.0f};
			if (std::abs(Dot(closestRailDirectionAtShip, referenceUpForRight)) >= 0.98f) {
				referenceUpForRight = {0.0f, 0.0f, 1.0f};
			}
			mode2RailRightAtClosest = Normalize(Cross(referenceUpForRight, closestRailDirectionAtShip));
			mode2LateralOffsetLength = Length(mode2LateralOffsetVector);

			// 少し先の航路点(速度に応じて先読み距離を伸ばす)を操舵目標にする。
			const float horizontalSpeed = Length(
				Vector3{rigidBodyComponent->velocity.x, 0.0f, rigidBodyComponent->velocity.z});
			const float lookAheadDistance = (std::max)(
				railMovementComponent->railSteeringBaseLookAheadDistance, 0.0f) +
				(std::max)(railMovementComponent->railSteeringLookAheadTime, 0.0f) * horizontalSpeed;
			const float steeringTargetDistance = NormalizeRailDistance(
				closestRailDistance + lookAheadDistance * railDirectionSign,
				runtimeState.totalDistance,
				railMovementComponent->railLoop);
			const Vector3 steeringTargetRailPosition = SampleRailPosition(
				runtimeState.samples, steeringTargetDistance);
			const Vector3 steeringTargetRailDirection = CalculateRailDirection(
				runtimeState, *railMovementComponent, steeringTargetDistance, steeringTargetRailPosition);
			const Vector3 offsetSteeringTargetPosition = ApplyMovementModifierPosition(
				ApplyRailOffset(
					steeringTargetRailPosition, steeringTargetRailDirection, runtimeState.currentOffset),
				steeringTargetRailDirection,
				movementModifier,
				runtimeState.modifierInputOffset);

			// Pure Pursuit方式: 少し先の航路点(オフセット込み)を直接狙う。狙う点自体が中心線上に
			// あるため、船体が横へずれているほど狙う方向が自然に中心へ寄る
			// (「右へずれた→少し左へ船首を向ける」を別式なしで実現する)。
			const Vector3 steeringDirectionRaw{
				offsetSteeringTargetPosition.x - currentPosition.x,
				0.0f,
				offsetSteeringTargetPosition.z - currentPosition.z};
			const float steeringDirectionLength = Length(steeringDirectionRaw);
			const Vector3 desiredSteeringDirectionXZ = steeringDirectionLength > kRailMinimumDistance
				? Multiply(1.0f / steeringDirectionLength, steeringDirectionRaw)
				: mode2ShipForwardXZ;

			// steeringHeadingYawRad/currentHeadingYawRadは共にrailLocalForwardAxisを反映した
			// mode2ShipForwardXZベースで測るため、worldRotation.y基準のオフセット補正は不要
			// (両者を同じ世界空間の方位角として直接比較できる)。
			const float steeringHeadingYawRad = std::atan2(
				desiredSteeringDirectionXZ.x, desiredSteeringDirectionXZ.z);
			// 現在のYawもworldRotation.yを直接読まず、船首ベクトルから求める(Euler多重表現を避ける)。
			const float currentHeadingYawRad = std::atan2(
				mode2ShipForwardXZ.x, mode2ShipForwardXZ.z);
			mode2SteeringYawErrorRad = NormalizeAngle(steeringHeadingYawRad - currentHeadingYawRad);

			railMovementComponent->railDebugClosestRailDistance = closestRailDistance;
			railMovementComponent->railDebugSteeringLookAheadDistance = lookAheadDistance;
			railMovementComponent->railDebugSteeringYawErrorDegrees =
				mode2SteeringYawErrorRad * kRailRadianToDegree;
			railMovementComponent->railDebugLateralPositionError = mode2LateralOffsetLength;
		}

		// Yaw Safety Assist: Yaw偏差が大きいほど、物理追従の目標前進速度を非線形に落とし、
		// Yaw復元強度を非線形に強める。Rail進行のs(runtimeState.traveledDistance / currentSpeed)
		// そのものは変更しない。Mode 2では操舵目標とのYaw誤差(mode2SteeringYawErrorRad)を使い、
		// 通常のカーブ追従はYaw操舵側が担うため、Yaw Safetyは非常用としてのみ働く。
		const float railHeadingYawRad = std::atan2(chaseRailDirection.x, chaseRailDirection.z);
		const float adjustedRailHeadingYawRad = railHeadingYawRad - ResolveRailLocalForwardYawOffset(
			railMovementComponent->railLocalForwardAxis);
		const float mode1RailHeadingYawErrorRad = NormalizeAngle(
			adjustedRailHeadingYawRad - NormalizeAngle(worldRotation.y));
		const float railHeadingYawErrorRad = railMovementComponent->railMovementMode == 2
			? mode2SteeringYawErrorRad
			: mode1RailHeadingYawErrorRad;
		const float yawErrorAbsDegrees = std::abs(railHeadingYawErrorRad) * kRailRadianToDegree;

		float yawSafetySpeedScale = 1.0f;
		float yawSafetyRestorationScale = 1.0f;
		if (railMovementComponent->railYawSafetyAssistEnabled) {
			yawSafetySpeedScale = ResolveYawSafetySpeedScale(
				yawErrorAbsDegrees,
				railMovementComponent->railYawSafetyStage2Degrees,
				railMovementComponent->railYawSafetyStage4Degrees,
				railMovementComponent->railYawSafetyMinSpeedScale);
			yawSafetyRestorationScale = ResolveYawSafetyRestorationScale(
				yawErrorAbsDegrees,
				railMovementComponent->railYawSafetyStage1Degrees,
				railMovementComponent->railYawSafetyStage4Degrees,
				railMovementComponent->railYawSafetyMaxRestorationScale);
		}
		railMovementComponent->railDebugYawSafetySpeedScale = yawSafetySpeedScale;
		railMovementComponent->railDebugYawSafetyRestorationScale = yawSafetyRestorationScale;

		// Roll/Pitch Safety Envelope: 波による通常の揺れ(free角度以下)には一切介入しないが、
		// 危険域(emergency角度)へ近づくほど、Attitude Recovery TorqueとForward Safety Scaleの
		// 両方を非線形に強める。Buoyancyの姿勢制御そのものは変更せず、Railは加算的にTorqueを足すだけ。
		float pitchDangerT = 0.0f;
		float rollDangerT = 0.0f;
		if (railMovementComponent->railAttitudeSafetyAssistEnabled) {
			pitchDangerT = ResolveAttitudeSafetyFactor(
				std::abs(boatPitchDegrees),
				railMovementComponent->railPitchFreeDegrees,
				railMovementComponent->railPitchEmergencyDegrees);
			rollDangerT = ResolveAttitudeSafetyFactor(
				std::abs(boatRollDegrees),
				railMovementComponent->railRollFreeDegrees,
				railMovementComponent->railRollEmergencyDegrees);
		}
		const float pitchSafetyAssist = pitchDangerT * pitchDangerT;
		const float rollSafetyAssist = rollDangerT * rollDangerT;
		const float pitchForwardScale = ResolveSafetyForwardScale(
			pitchDangerT, railMovementComponent->railAttitudeSafetyMinForwardScale);
		const float rollForwardScale = ResolveSafetyForwardScale(
			rollDangerT, railMovementComponent->railAttitudeSafetyMinForwardScale);
		// Yaw/Pitch/Rollのうち最も危険な軸がForward方向の推力・位置補正を支配する。
		// 「船が横向き/縦回転しかけているのにRailだけ全速で前へ引っ張らない」ため。
		const float combinedForwardSafetyScale = (std::min)(
			yawSafetySpeedScale, (std::min)(pitchForwardScale, rollForwardScale));
		railMovementComponent->railDebugBoatPitchDegrees = boatPitchDegrees;
		railMovementComponent->railDebugBoatRollDegrees = boatRollDegrees;
		railMovementComponent->railDebugPitchSafetyFactor = pitchSafetyAssist;
		railMovementComponent->railDebugRollSafetyFactor = rollSafetyAssist;
		railMovementComponent->railDebugForwardPositionScale = combinedForwardSafetyScale;

		// Physical Rail Progressの目標速度。Safetyで抑えるが、Gameplay Rail Progress(traveledDistance)
		// に対してbacklogがある間はrailPhysicalCatchupSpeedMultiplierの範囲で追いつき速度を追加する。
		// 危険度が高い(combinedForwardSafetyScaleが小さい)間はcatchupもそれに比例して抑える。
		// これにより「Safety解除直後に巨大なPosition Errorで最大Forceを出して瞬間的に追いつく」ことを防ぐ。
		const float baseRailSpeedAbs = std::abs(runtimeState.currentSpeed);
		const float safetyScaledSpeedAbs = baseRailSpeedAbs * combinedForwardSafetyScale;
		const float physicalBacklogDistance =
			railDirectionSign * (runtimeState.traveledDistance - runtimeState.physicalTraveledDistance);
		float physicalTargetSpeedAbs = safetyScaledSpeedAbs;
		if (physicalBacklogDistance > kRailMinimumDistance) {
			const float catchupMultiplier = (std::max)(
				railMovementComponent->railPhysicalCatchupSpeedMultiplier, 1.0f);
			const float maxCatchupSpeedAbs = baseRailSpeedAbs * catchupMultiplier;
			const float catchupHeadroomAbs = maxCatchupSpeedAbs - baseRailSpeedAbs;
			physicalTargetSpeedAbs = (std::min)(
				safetyScaledSpeedAbs + catchupHeadroomAbs * combinedForwardSafetyScale,
				maxCatchupSpeedAbs);
		}
		railMovementComponent->railDebugPhysicalTargetSpeed = physicalTargetSpeedAbs;
		railMovementComponent->railDebugGameplayRailProgress = runtimeState.traveledDistance;
		railMovementComponent->railDebugPhysicalRailProgress = runtimeState.physicalTraveledDistance;

		const Vector3 targetVelocity = runtimeState.isPaused ?
			Vector3{0.0f, 0.0f, 0.0f} :
			Multiply(physicalTargetSpeedAbs, chaseRailDirection);
		const Vector3 positionError = MultiplyComponents(
			Subtract(targetPosition, currentPosition),
			positionInfluence);
		// 速度誤差も位置誤差と同じ追従軸マスクを通す。
		// マスクしないと railPositionInfluence.y = 0(浮力併用)でも鉛直方向の速度誤差が
		// 減衰力として残り、Buoyancyが作るHeaveを打ち消してしまう。
		const Vector3 velocityError = MultiplyComponents(
			Subtract(targetVelocity, rigidBodyComponent->velocity),
			positionInfluence);
		const float forwardPositionError = Dot(positionError, chaseRailDirection);
		const float forwardVelocityError = Dot(velocityError, chaseRailDirection);
		const Vector3 lateralPositionError = Subtract(
			positionError,
			Multiply(forwardPositionError, chaseRailDirection));
		const Vector3 lateralVelocityError = Subtract(
			velocityError,
			Multiply(forwardVelocityError, chaseRailDirection));
		// Rail進行(s)が物理追従より先へ進み続けると、forward position errorが際限なく
		// 増大しうる(Safetyで速度だけ落としても、位置誤差項が最大加速度Clampへ張り付いて
		// Safetyの意図を打ち消してしまう問題への対策)。まず絶対値を上限で切り、
		// さらに危険域では combinedForwardSafetyScale で追加的に弱める。横方向Errorは対象外。
		const float clampedForwardPositionError = (std::clamp)(
			forwardPositionError,
			-(std::max)(railMovementComponent->railMaxForwardRecoveryError, 0.0f),
			(std::max)(railMovementComponent->railMaxForwardRecoveryError, 0.0f));
		const float forwardPositionErrorForForce = clampedForwardPositionError * combinedForwardSafetyScale;
		railMovementComponent->railDebugForwardPositionError = forwardPositionError;
		if (railMovementComponent->railMovementMode != 2) {
			// Mode 2は既にmode2LateralOffsetLength(Rail最近傍点からの横ずれ)で書き込み済み。
			railMovementComponent->railDebugLateralPositionError = Length(lateralPositionError);
		}
		const float positionSpring = (std::max)(railMovementComponent->railPositionSpring, 0.0f);
		const float positionDamping = (std::max)(railMovementComponent->railPositionDamping, 0.0f);
		Vector3 requestedAcceleration{};
		float forwardAccelerationForDebug = 0.0f;
		Vector3 lateralAccelerationForDebug{};

		if (railMovementComponent->railMovementMode == 2) {
			// Mode 2 オートパイロット: Forward Position Errorは使わない。目標前進速度(physicalTargetSpeedAbs、
			// Yaw/Attitude Safety・Catchupを反映済み)と船首方向速度の差だけでエンジン加速度を決める。
			const float shipForwardSpeed = Dot(rigidBodyComponent->velocity, mode2ShipForwardXZ);
			const float speedError = physicalTargetSpeedAbs - shipForwardSpeed;
			const float speedGain = (std::max)(railMovementComponent->railEngineSpeedGain, 0.0f);
			float desiredEngineAcceleration = 0.0f;
			if (speedError > 0.0f) {
				desiredEngineAcceleration = speedGain * speedError;
			}
			else {
				// 目標超過時は巨大な逆Forceを出さない。エンジン推力をごく弱く落とすだけにし、
				// 実際の減速は水抵抗(Buoyancy側)に任せる。
				constexpr float kOverspeedBrakeFactor = 0.15f;
				desiredEngineAcceleration = speedGain * speedError * kOverspeedBrakeFactor;
			}
			// エンジン出力に立ち上がり・立ち下がりの応答を持たせる(毎フレーム即座に切り替えない)。
			const float engineResponseRate = desiredEngineAcceleration >= runtimeState.currentEngineAcceleration
				? (std::max)(railMovementComponent->railEngineAccelResponse, 0.0f)
				: (std::max)(railMovementComponent->railEngineDecelResponse, 0.0f);
			runtimeState.currentEngineAcceleration = MoveTowards(
				runtimeState.currentEngineAcceleration,
				desiredEngineAcceleration,
				engineResponseRate * fixedDeltaTime);
			const float engineMaxAcceleration = (std::max)(railMovementComponent->railEngineMaxAcceleration, 0.0f);
			const float engineAcceleration = (std::clamp)(
				runtimeState.currentEngineAcceleration, -engineMaxAcceleration, engineMaxAcceleration);

			// 横補助(補助輪): Dead Zone内は完全に0で操舵のみに任せる。Soft Radiusまでは弱く、
			// Soft Radius~Emergency Radiusで最大倍率まで強め、Emergency Radius以上はクランプする。
			const float deadZone = (std::max)(railMovementComponent->railLateralAssistDeadZone, 0.0f);
			const float softRadius = (std::max)(railMovementComponent->railLateralAssistSoftRadius, deadZone);
			const float emergencyRadius = (std::max)(
				railMovementComponent->railLateralAssistEmergencyRadius, softRadius);
			const float maxMultiplier = (std::max)(railMovementComponent->railLateralAssistMaxMultiplier, 0.0f);
			float lateralAssistScale = 0.0f;
			if (mode2LateralOffsetLength > deadZone) {
				if (mode2LateralOffsetLength <= softRadius) {
					lateralAssistScale = maxMultiplier * 0.25f *
						SmoothStep01(deadZone, softRadius, mode2LateralOffsetLength);
				}
				else {
					lateralAssistScale = maxMultiplier * (0.25f + 0.75f *
						SmoothStep01(softRadius, emergencyRadius, mode2LateralOffsetLength));
				}
			}
			const float signedLateralOffset = Dot(mode2LateralOffsetVector, mode2RailRightAtClosest);
			const float lateralVelocityAlongRailRight = Dot(
				rigidBodyComponent->velocity, mode2RailRightAtClosest);
			const Vector3 lateralAssistAcceleration = lateralAssistScale > 0.0f
				? Multiply(
					-lateralAssistScale * (
						positionSpring * signedLateralOffset + positionDamping * lateralVelocityAlongRailRight),
					mode2RailRightAtClosest)
				: Vector3{0.0f, 0.0f, 0.0f};

			// 船体横滑り抑制(Hull Lateral Grip): Rail横補助とは完全に別物。Rail位置は一切見ず、
			// 船体基準の横方向速度(lateralSpeed)だけを、船体自身が水を横から受けて減衰していく
			// 挙動として再現する。船首が先に曲がり、速度ベクトルが遅れて追従する高速艇らしい
			// 旋回を作るためのMode 2専用ゲームプレイ補助で、既存Buoyancy等の水力モデルは変更しない。
			const Vector3 velocityXZForGrip{
				rigidBodyComponent->velocity.x, 0.0f, rigidBodyComponent->velocity.z};
			const float lateralSpeed = Dot(velocityXZForGrip, mode2ShipRightXZ);
			const float horizontalSpeedForGrip = Length(velocityXZForGrip);
			railMovementComponent->railDebugLateralSpeed = lateralSpeed;
			railMovementComponent->railDebugHorizontalSpeed = horizontalSpeedForGrip;

			Vector3 hullLateralGripAcceleration{0.0f, 0.0f, 0.0f};
			float hullLateralGripScale = 0.0f;
			float hullLateralGripSpeedFactor = 0.0f;
			float hullLateralGripSlipFactor = 0.0f;
			if (railMovementComponent->railHullLateralGripEnabled) {
				// 停止・低速では強制的に横速度を消さない。「船体が水平面内で水に対してどれだけ
				// 高速で移動しているか」(horizontalSpeed)を基準にする。shipForwardSpeedだと、
				// 真横近くまで横滑りしている(前進速度成分がほぼ0になる)最も抑制が必要な状態で
				// 逆にグリップが消えてしまうため使わない。
				const float gripMinSpeed = (std::max)(railMovementComponent->railHullLateralGripMinSpeed, 0.0f);
				const float gripFullSpeed = (std::max)(
					railMovementComponent->railHullLateralGripFullSpeed, gripMinSpeed + kRailMinimumDistance);
				const float speedFactor = SmoothStep01(gripMinSpeed, gripFullSpeed, horizontalSpeedForGrip);

				// 横滑り角が小さい(自然な滑り)うちはほぼ介入せず、角度が大きいほど強く抑える。
				const float slipAngleDegForGrip = railMovementComponent->railDebugForwardVelocitySlipAngleDegrees;
				const float slipStartDeg = (std::max)(railMovementComponent->railHullLateralGripSlipStartDegrees, 0.0f);
				const float slipFullDeg = (std::max)(
					railMovementComponent->railHullLateralGripSlipFullDegrees, slipStartDeg + 0.01f);
				const float slipFactor = SmoothStep01(
					slipStartDeg, slipFullDeg, std::abs(slipAngleDegForGrip));

				// 小さな横滑り速度は残す(Rail車両のように吸い付かせない)。
				const float deadZoneSpeed = (std::max)(
					railMovementComponent->railHullLateralGripDeadZoneSpeed, 0.01f);
				const float deadZoneFactor = SmoothStep01(0.0f, deadZoneSpeed, std::abs(lateralSpeed));

				const float gripStrength = (std::max)(railMovementComponent->railHullLateralGripStrength, 0.0f);
				const float effectiveGrip = gripStrength * speedFactor * slipFactor * deadZoneFactor;
				const float maxGripAcceleration = (std::max)(
					railMovementComponent->railHullLateralGripMaxAcceleration, 0.0f);
				const float gripAccelerationMagnitude = (std::clamp)(
					std::abs(lateralSpeed) * effectiveGrip, 0.0f, maxGripAcceleration);

				if (gripAccelerationMagnitude > 0.0f) {
					const float gripSign = lateralSpeed >= 0.0f ? 1.0f : -1.0f;
					hullLateralGripAcceleration = Multiply(
						-gripSign * gripAccelerationMagnitude, mode2ShipRightXZ);
				}
				hullLateralGripScale = effectiveGrip;
				hullLateralGripSpeedFactor = speedFactor;
				hullLateralGripSlipFactor = slipFactor;
			}
			railMovementComponent->railDebugHullLateralGripAcceleration = Length(hullLateralGripAcceleration);
			railMovementComponent->railDebugHullLateralGripScale = hullLateralGripScale;
			railMovementComponent->railDebugHullLateralGripSpeedFactor = hullLateralGripSpeedFactor;
			railMovementComponent->railDebugHullLateralGripSlipFactor = hullLateralGripSlipFactor;

			// requestedAcceleration = engine*shipForward + hullLateralGrip(船体横滑り抑制) +
			// railEmergencyLateralAssist(Railから大きく逸脱した時だけのゲーム補助)。通常カーブでは
			// engine+hullLateralGripが主役で、Rail Emergency Assistはできるだけ出ないのが理想。
			// 各成分は既に個別にClamp済みなので、ここではまだ最終Clampを掛けない
			// (最終ClampはMode 1/2共通コードで行う。Mode 2は専用の合成上限を使う→後述)。
			requestedAcceleration = Add(
				Add(Multiply(engineAcceleration, mode2ShipForwardXZ), hullLateralGripAcceleration),
				lateralAssistAcceleration);
			forwardAccelerationForDebug = engineAcceleration;
			lateralAccelerationForDebug = Add(hullLateralGripAcceleration, lateralAssistAcceleration);

			railMovementComponent->railDebugEngineAcceleration = engineAcceleration;
			railMovementComponent->railDebugLateralAssistAcceleration = Length(lateralAssistAcceleration);
			railMovementComponent->railDebugLateralAssistScale = lateralAssistScale;
		}
		else {
			const float engineThrust = positionDamping * kRailEngineResponseScale * forwardVelocityError;
			const float alongCorrection =
				positionSpring * kRailAlongPositionSpringScale * forwardPositionErrorForForce;
			const Vector3 forwardAcceleration = Multiply(
				engineThrust + alongCorrection,
				chaseRailDirection);
			const Vector3 lateralAcceleration = Add(
				Multiply(positionSpring, lateralPositionError),
				Multiply(positionDamping, lateralVelocityError));
			requestedAcceleration = Add(forwardAcceleration, lateralAcceleration);
			forwardAccelerationForDebug = engineThrust + alongCorrection;
			lateralAccelerationForDebug = lateralAcceleration;
		}

		// railMaximumAccelerationはMode 1(Spline位置追従PD)由来の最終上限で、PlayerShipのScene保存値は
		// 12m/s²しかない。Mode 2はEngine/Hull Grip/Rail Assistを既に個別Clamp済みなので、ここで
		// 同じ低い上限を掛けると通常走行でもEngine・Hull Gripが一緒に潰れてしまう。Mode 2は専用の
		// 合成上限(railMode2MaxCombinedAcceleration、既定は各成分の最大値合計より十分大きい)を使う。
		const float finalAccelerationClamp = railMovementComponent->railMovementMode == 2
			? (std::max)(railMovementComponent->railMode2MaxCombinedAcceleration, 0.0f)
			: (std::max)(railMovementComponent->railMaximumAcceleration, 0.0f);
		const float preClampAccelerationMagnitude = Length(requestedAcceleration);
		requestedAcceleration = ClampVectorLength(requestedAcceleration, finalAccelerationClamp);
		if (railMovementComponent->railMovementMode == 2) {
			const float postClampAccelerationMagnitude = Length(requestedAcceleration);
			railMovementComponent->railDebugPreClampAcceleration = preClampAccelerationMagnitude;
			railMovementComponent->railDebugPostClampAcceleration = postClampAccelerationMagnitude;
			railMovementComponent->railDebugMode2ClampScale = preClampAccelerationMagnitude > kRailMinimumDistance
				? postClampAccelerationMagnitude / preClampAccelerationMagnitude
				: 1.0f;
		}
		// Buoyancy等で実際のボディ質量が変わっている場合に対応するため、
		// コンポーネントの質量ではなく実際のJoltボディ質量を使用する。
		float actualBodyMass = rigidBodyComponent->mass;
		physicsManager_->GetBodyMass(gameObject.id, actualBodyMass);
		const float effectiveMass = (std::max)(actualBodyMass, 0.01f);
		const bool wasRailForceApplied = physicsManager_->AddForce(
			gameObject.id,
			Multiply(effectiveMass, requestedAcceleration));
		railMovementComponent->velocity = rigidBodyComponent->velocity;

		// Physical Rail Progressを今回計算したphysicalTargetSpeedAbsで前進させる(Mode 1のみ)。
		// Gameplay Rail Progress(traveledDistance)を追い越さないよう、backlogが1ステップで
		// 解消する場合はGameplayへ同期して止める(行き過ぎを防ぐ)。
		// Mode 2ではphysicalTraveledDistanceは既にFindClosestRailDistanceで直接上書き済みのため、
		// ここで速度積分による前進はしない(2重に進めてしまうため)。
		if (railMovementComponent->railMovementMode != 2 && !runtimeState.isPaused) {
			const float physicalAdvanceStep = physicalTargetSpeedAbs * fixedDeltaTime;
			if (physicalBacklogDistance <= physicalAdvanceStep) {
				runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
			}
			else {
				runtimeState.physicalTraveledDistance = NormalizeRailDistance(
					runtimeState.physicalTraveledDistance + physicalAdvanceStep * railDirectionSign,
					runtimeState.totalDistance,
					railMovementComponent->railLoop);
			}
		}

		// Mode 2 操舵Yaw Torque: 既存のtargetRotation/Yaw復元Torque経路(Mode 1用)は使わず、
		// 操舵目標(mode2SteeringYawErrorRad)から直接Yaw角加速度を作る独立した系統。
		// Yaw Safetyの復元強化(yawSafetyRestorationScale)だけは非常用として引き続き反映する。
		if (railMovementComponent->railMovementMode == 2) {
			const float steeringYawGain = (std::max)(railMovementComponent->railSteeringYawGain, 0.0f);
			const float steeringYawDamping = (std::max)(railMovementComponent->railSteeringYawDamping, 0.0f);
			const float steeringMaxYawAngularAcceleration =
				(std::max)(railMovementComponent->railSteeringMaxYawAngularAcceleration, 0.0f);
			float steeringYawAngularAcceleration = yawSafetyRestorationScale * (
				steeringYawGain * mode2SteeringYawErrorRad -
				steeringYawDamping * rigidBodyComponent->angularVelocity.y);
			steeringYawAngularAcceleration = (std::clamp)(
				steeringYawAngularAcceleration,
				-steeringMaxYawAngularAcceleration,
				steeringMaxYawAngularAcceleration);
			const Vector3 steeringAngularAccelerationVector{0.0f, steeringYawAngularAcceleration, 0.0f};
			physicsManager_->AddTorque(
				gameObject.id,
				Multiply(effectiveMass, steeringAngularAccelerationVector));
			railMovementComponent->railDebugFollowTorque = Multiply(effectiveMass, steeringAngularAccelerationVector);
			railMovementComponent->railDebugYawError = mode2SteeringYawErrorRad;
		}
		railMovementComponent->railDebugYawAngularVelocity = rigidBodyComponent->angularVelocity.y;

		// Roll/Pitch Attitude Recovery Torque: railRotationInfluenceのマスクとは無関係に、
		// 危険域(pitchSafetyAssist/rollSafetyAssist > 0)でのみ加算的にTorqueを加える。
		// 通常域(free角度以下)ではpitchSafetyAssist=rollSafetyAssist=0のため、
		// Buoyancyが作るPitch/Rollの自然な揺れには一切影響しない。
		Vector3 attitudeRecoveryAngularAcceleration{};
		if (railMovementComponent->railAttitudeSafetyAssistEnabled) {
			const float attitudeStrength = (std::max)(railMovementComponent->railAttitudeSafetyStrength, 0.0f);
			const float attitudeDamping = (std::max)(railMovementComponent->railAttitudeSafetyDamping, 0.0f);
			attitudeRecoveryAngularAcceleration.x = pitchSafetyAssist * (
				-attitudeStrength * boatPitchRad - attitudeDamping * rigidBodyComponent->angularVelocity.x);
			attitudeRecoveryAngularAcceleration.z = rollSafetyAssist * (
				-attitudeStrength * boatRollRad - attitudeDamping * rigidBodyComponent->angularVelocity.z);
			attitudeRecoveryAngularAcceleration = ClampVectorLength(
				attitudeRecoveryAngularAcceleration,
				(std::max)(railMovementComponent->railAttitudeSafetyMaxTorque, 0.0f));
		}
		physicsManager_->AddTorque(
			gameObject.id,
			Multiply(effectiveMass, attitudeRecoveryAngularAcceleration));
		railMovementComponent->railDebugAttitudeRecoveryTorque =
			Multiply(effectiveMass, attitudeRecoveryAngularAcceleration);

		// Pitch/Roll 角度ソフト制限: Torqueによる押し戻し方式。上のAttitude Safety(段階的復元Torque)や
		// 下のPostFixedUpdateで行う「絶対角度制限」(Hard Clamp、角度を直接書き換える別機能)とは
		// 完全に独立した第3の系統。限界角度(railAttitudeAngleLimitMaxPitch/RollDegreesを共有)を
		// 超えた分だけTorqueで押し戻す。角度は書き換えない。既定OFF。
		if (railMovementComponent->railAttitudeAngleSoftLimitEnabled) {
			Vector3 limitedAngularVelocity = rigidBodyComponent->angularVelocity;
			bool wasVelocityLimited = false;
			Vector3 angleLimitCorrectiveAngularAcceleration{};

			const float limitStrength = (std::max)(railMovementComponent->railAttitudeAngleSoftLimitStrength, 0.0f);
			const float limitDamping = (std::max)(railMovementComponent->railAttitudeAngleSoftLimitDamping, 0.0f);
			const float limitMaxAngularAcceleration =
				(std::max)(railMovementComponent->railAttitudeAngleSoftLimitMaxTorque, 0.0f);

			const float maxPitchRad =
				railMovementComponent->railAttitudeAngleLimitMaxPitchDegrees * kRailDegreeToRadian;
			if (maxPitchRad > 0.0f) {
				if ((boatPitchRad >= maxPitchRad && limitedAngularVelocity.x > 0.0f) ||
					(boatPitchRad <= -maxPitchRad && limitedAngularVelocity.x < 0.0f)) {
					limitedAngularVelocity.x = 0.0f;
					wasVelocityLimited = true;
				}
				const float pitchOvershoot = (std::max)(std::abs(boatPitchRad) - maxPitchRad, 0.0f);
				if (pitchOvershoot > 0.0f) {
					const float pitchSign = boatPitchRad >= 0.0f ? 1.0f : -1.0f;
					angleLimitCorrectiveAngularAcceleration.x = -pitchSign *
						(limitStrength * pitchOvershoot) - limitDamping * rigidBodyComponent->angularVelocity.x;
				}
			}

			const float maxRollRad =
				railMovementComponent->railAttitudeAngleLimitMaxRollDegrees * kRailDegreeToRadian;
			if (maxRollRad > 0.0f) {
				if ((boatRollRad >= maxRollRad && limitedAngularVelocity.z > 0.0f) ||
					(boatRollRad <= -maxRollRad && limitedAngularVelocity.z < 0.0f)) {
					limitedAngularVelocity.z = 0.0f;
					wasVelocityLimited = true;
				}
				const float rollOvershoot = (std::max)(std::abs(boatRollRad) - maxRollRad, 0.0f);
				if (rollOvershoot > 0.0f) {
					const float rollSign = boatRollRad >= 0.0f ? 1.0f : -1.0f;
					angleLimitCorrectiveAngularAcceleration.z = -rollSign *
						(limitStrength * rollOvershoot) - limitDamping * rigidBodyComponent->angularVelocity.z;
				}
			}

			if (Length(angleLimitCorrectiveAngularAcceleration) > 0.0f) {
				angleLimitCorrectiveAngularAcceleration = ClampVectorLength(
					angleLimitCorrectiveAngularAcceleration, limitMaxAngularAcceleration);
				physicsManager_->AddTorque(
					gameObject.id,
					Multiply(effectiveMass, angleLimitCorrectiveAngularAcceleration));
			}

			if (wasVelocityLimited) {
				physicsManager_->SetAngularVelocity(gameObject.id, limitedAngularVelocity);
			}
		}

		// Rail追従力をRuntime診断値へ残す。Buoyancyの水力と force 比を比べるために使う。
		// 追従軸マスクが効いていれば、浮力併用設定では Y 成分が 0 になる。
		railMovementComponent->railDebugFollowForce = Multiply(effectiveMass, requestedAcceleration);
		railMovementComponent->railDebugPositionError = positionError;
		railMovementComponent->railDebugForwardCorrectionForce = effectiveMass * forwardAccelerationForDebug;
		railMovementComponent->railDebugLateralCorrectionForce = effectiveMass * Length(lateralAccelerationForDebug);
		// Rail指令速度と実際のRigidbody前進速度。水力の速度依存(V²則)を判定するために両方残す。
		railMovementComponent->railDebugCurrentSpeed = runtimeState.currentSpeed;
		// Mode 2はrailLocalForwardAxisを反映した船首方向(mode2ShipForwardXZ、エンジンの
		// 速度誤差計算と同じ軸)で実前進速度を測る。Mode 1は従来通りRail接線方向のまま。
		railMovementComponent->railDebugActualForwardSpeed = Dot(
			rigidBodyComponent->velocity,
			railMovementComponent->railMovementMode == 2 ? mode2ShipForwardXZ : chaseRailDirection);

		if (!wasRailForceApplied) {
			// Collider生成失敗などでJolt Bodyが存在しない場合でも、RailFollower全体を停止させない。
			// 物理Bodyが復旧したフレームからは通常のForce追従へ自動的に戻る。
			// Transform直接制御の間はPhysical Rail ProgressにbacklogをためないようGameplayへ同期する。
			runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
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

		// Mode 2は操舵Yaw Torque(mode2SteeringYawErrorRad由来)を既に上で独立に加えているため、
		// 以下のSpline接線ベースのtargetRotation/Yaw復元Torque経路(Mode 1専用)は使わない。
		if (railMovementComponent->railMovementMode == 2 ||
			!railMovementComponent->railOrientToPath ||
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

		// 以下の軸別復元は回転追従軸(railRotationInfluence)を必ず通す。
		// マスクしないと浮力併用設定(0,1,0)でもRoll/Pitchを水平へ戻すTorqueが残り、
		// 波によるRoll/Pitchを打ち消してしまう。Railが担うのはYawだけにする。

		// ロール復元力とダンピングを追加
		const float rollRestorationStrength = (std::max)(railMovementComponent->railRollRestorationStrength, 0.0f);
		const float rollDamping = (std::max)(railMovementComponent->railRollDamping, 0.0f);
		if (rotationInfluence.z > 0.0f && (rollRestorationStrength > 0.0f || rollDamping > 0.0f)) {
			const float currentRoll = NormalizeAngle(worldRotation.z);
			const float rollError = NormalizeAngle(-currentRoll);  // 直立（0）に戻す
			const float rollVelocity = rigidBodyComponent->angularVelocity.z;
			const float rollTorque = rollRestorationStrength * rollError - rollDamping * rollVelocity;
			requestedAngularAcceleration.z += rotationInfluence.z * rollTorque;
		}

		// ピッチ復元力とダンピングを追加
		const float pitchRestorationStrength = (std::max)(railMovementComponent->railPitchRestorationStrength, 0.0f);
		const float pitchDamping = (std::max)(railMovementComponent->railPitchDamping, 0.0f);
		if (rotationInfluence.x > 0.0f && (pitchRestorationStrength > 0.0f || pitchDamping > 0.0f)) {
			const float currentPitch = NormalizeAngle(worldRotation.x);
			const float pitchError = NormalizeAngle(-currentPitch);  // 水平（0）に戻す
			const float pitchVelocity = rigidBodyComponent->angularVelocity.x;
			const float pitchTorque = pitchRestorationStrength * pitchError - pitchDamping * pitchVelocity;
			requestedAngularAcceleration.x += rotationInfluence.x * pitchTorque;
		}

		// ヨー復元力とダンピングを追加。Yaw Safety Assistにより、Rail見出しからの
		// 偏差(railHeadingYawErrorRad、上で計算済み)が大きいほど非線形に復元強度を強める。
		const float yawRestorationStrength = (std::max)(railMovementComponent->railYawRestorationStrength, 0.0f);
		const float yawDamping = (std::max)(railMovementComponent->railYawDamping, 0.0f);
		if (rotationInfluence.y > 0.0f && (yawRestorationStrength > 0.0f || yawDamping > 0.0f)) {
			const float yawVelocity = rigidBodyComponent->angularVelocity.y;
			const float yawTorque = yawSafetyRestorationScale *
				(yawRestorationStrength * railHeadingYawErrorRad - yawDamping * yawVelocity);
			requestedAngularAcceleration.y += rotationInfluence.y * yawTorque;
		}

		requestedAngularAcceleration = ClampVectorLength(
			requestedAngularAcceleration,
			(std::max)(railMovementComponent->railMaximumAngularAcceleration, 0.0f));
		// Rail追従Torqueを診断値へ残す。回転追従軸マスクが効いていれば、
		// 浮力併用設定ではPitch(X)とRoll(Z)成分が0になり、Yaw(Y)だけが残る。
		railMovementComponent->railDebugFollowTorque =
			Multiply(effectiveMass, requestedAngularAcceleration);
		railMovementComponent->railDebugYawError = rotationError.y;
		physicsManager_->AddTorque(
			gameObject.id,
			Multiply(effectiveMass, requestedAngularAcceleration));
	}
}

void EditorRailMovementManager::PostFixedUpdate(float fixedDeltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || physicsManager_ == nullptr || fixedDeltaTime <= 0.0f) {
		return;
	}

	// Pitch/Roll 絶対角度制限(Hard Clamp): Joltの積分・最終姿勢確定が終わった直後(PostFixedStepCallback)
	// でのみ実行する。PreFixedStep内で角度を書き換えても、この後に続くBuoyancyのTorqueとJolt積分で
	// 再び限界角度を超えてしまうため、ここでの実行が必須。
	// この機能に限り、ユーザーの明示的な許可によりRigidbody回転を直接書き換える
	// (他のRail制御・Buoyancy等では引き続きForce/Torqueのみで、Transform直接書き換えは禁止のまま)。
	// Yawは一切変更せず、Pitch/Rollだけをそれぞれ独立に許容範囲へ収める。
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

		// Mode 2 Rail Rideはこのループでは処理しない(下のRail Ride専用ループが
		// Pitch/Roll Hard Clampを含めて最終Poseを一括構築するため、二重にSetGameObjectTransformしない)。
		const bool isRailRideMode2 = railMovementComponent != nullptr &&
			railMovementComponent->railMovementMode == 2 &&
			railMovementComponent->railMode2MovementStyle == 1;

		if (railMovementComponent == nullptr ||
			!railMovementComponent->isActive ||
			!railMovementComponent->railAttitudeAngleLimitEnabled ||
			railMovementComponent->railMovementMode == 0 ||
			railMovementComponent->railMovementMode > 2 ||
			isRailRideMode2 ||
			rigidBodyComponent == nullptr ||
			!rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		const float maxPitchRad =
			railMovementComponent->railAttitudeAngleLimitMaxPitchDegrees * kRailDegreeToRadian;
		const float maxRollRad =
			railMovementComponent->railAttitudeAngleLimitMaxRollDegrees * kRailDegreeToRadian;

		if (maxPitchRad <= 0.0f && maxRollRad <= 0.0f) {
			continue;
		}

		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		if (!editorScene_->GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition)) {
			continue;
		}

		// 現在の実姿勢をベクトル方式(Euler直読みしない)で取得する。以前の「正常姿勢なのにPitch/Roll≈±180度」
		// バグの再発防止のため、必ずこの方式を使う。
		const Matrix4x4 currentOrientationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f}, worldRotation, {0.0f, 0.0f, 0.0f});
		const Vector3 boatRight = Normalize(Transform({1.0f, 0.0f, 0.0f}, currentOrientationMatrix));
		const Vector3 boatUp = Normalize(Transform({0.0f, 1.0f, 0.0f}, currentOrientationMatrix));
		const Vector3 boatForward = Normalize(Transform({0.0f, 0.0f, 1.0f}, currentOrientationMatrix));
		const Vector3 worldUp{0.0f, 1.0f, 0.0f};

		const float currentPitchRad = -std::asin(
			(std::clamp)(Dot(boatForward, worldUp), -1.0f, 1.0f));
		const float currentRollRad = std::atan2(
			Dot(boatRight, worldUp), Dot(boatUp, worldUp));

		const bool pitchExceeded = maxPitchRad > 0.0f && std::abs(currentPitchRad) > maxPitchRad;
		const bool rollExceeded = maxRollRad > 0.0f && std::abs(currentRollRad) > maxRollRad;
		railMovementComponent->railDebugPitchAngleLimited = pitchExceeded ? 1.0f : 0.0f;
		railMovementComponent->railDebugRollAngleLimited = rollExceeded ? 1.0f : 0.0f;

		if (!pitchExceeded && !rollExceeded) {
			continue;
		}

		const float clampedPitchRad = maxPitchRad > 0.0f
			? (std::clamp)(currentPitchRad, -maxPitchRad, maxPitchRad)
			: currentPitchRad;
		const float clampedRollRad = maxRollRad > 0.0f
			? (std::clamp)(currentRollRad, -maxRollRad, maxRollRad)
			: currentRollRad;

		// YawはboatForwardの水平成分から求め、保持する(worldRotation.yを直接は読まない。
		// atan2は全域で連続なので、以前のEuler直読みバグと同じ問題は起きない)。
		const float currentYawRad = std::atan2(boatForward.x, boatForward.z);

		// (Yaw, ClampedPitch, ClampedRoll)から直交基底を直接組み立てる。Euler合成を経由しないため、
		// Gimbal付近での多重表現(以前のPitch/Roll≈±180度バグと同種の問題)が原理的に発生しない。
		const Vector3 desiredForward{
			std::sin(currentYawRad) * std::cos(clampedPitchRad),
			-std::sin(clampedPitchRad),
			std::cos(currentYawRad) * std::cos(clampedPitchRad)};
		Vector3 referenceRight = Cross(worldUp, desiredForward);
		if (Length(referenceRight) <= kRailMinimumDistance) {
			// Pitch制限角(通常90度未満)では実質発生しない縮退ケースへの保険。
			referenceRight = boatRight;
		}
		referenceRight = Normalize(referenceRight);
		const Vector3 referenceUp = Normalize(Cross(desiredForward, referenceRight));
		const float cosRoll = std::cos(clampedRollRad);
		const float sinRoll = std::sin(clampedRollRad);
		const Vector3 desiredRight = Add(
			Multiply(cosRoll, referenceRight), Multiply(sinRoll, referenceUp));
		const Vector3 desiredUp = Add(
			Multiply(-sinRoll, referenceRight), Multiply(cosRoll, referenceUp));

		// 求めた直交基底をそのままRigidbodyへ書き込む(この機能に限り直接回転変更を許可)。
		physicsManager_->SetGameObjectTransform(
			gameObject.id,
			worldPosition,
			DecomposeOrientationVectorsToEuler(desiredRight, desiredUp, desiredForward));

		// 限界角度に達し、さらに外側へ向かう角速度成分だけを止める。内側へ戻る角速度は残す。
		Vector3 limitedAngularVelocity = rigidBodyComponent->angularVelocity;
		bool wasVelocityLimited = false;
		if (pitchExceeded &&
			((currentPitchRad >= maxPitchRad && limitedAngularVelocity.x > 0.0f) ||
			 (currentPitchRad <= -maxPitchRad && limitedAngularVelocity.x < 0.0f))) {
			limitedAngularVelocity.x = 0.0f;
			wasVelocityLimited = true;
		}
		if (rollExceeded &&
			((currentRollRad >= maxRollRad && limitedAngularVelocity.z > 0.0f) ||
			 (currentRollRad <= -maxRollRad && limitedAngularVelocity.z < 0.0f))) {
			limitedAngularVelocity.z = 0.0f;
			wasVelocityLimited = true;
		}
		if (wasVelocityLimited) {
			physicsManager_->SetAngularVelocity(gameObject.id, limitedAngularVelocity);
		}
	}

	// ==================================================================
	// Mode 2 Rail Ride: 「物理船体がRailを追いかける」のではなく「RailがPlayerShipを運ぶ」方式。
	// Railが XZ位置・Yaw・Progress・前進速度を完全所有する。Y/Pitch/RollだけBuoyancy等の
	// 物理結果(Hard Clamp適用後)を残す。Pure Pursuit・Engine・Hull Grip・Rail Assistは
	// 使わない(FixedUpdate側で既にRail Ride対象をスキップ済み)。Buoyancy/Planing/Slamming/
	// Added Mass等の水力モデル自体は変更しない(この後の最終Poseで XZ/Yawだけ上書きする)。
	// ==================================================================
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
			railMovementComponent->railMovementMode != 2 ||
			railMovementComponent->railMode2MovementStyle != 1 ||
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

		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		if (!editorScene_->GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition)) {
			continue;
		}

		// --- Y/Pitch/Roll: 物理(Buoyancy等)の結果をベクトル方式で取得する ---
		const Matrix4x4 physicsOrientationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f}, worldRotation, {0.0f, 0.0f, 0.0f});
		const Vector3 physicsBoatRight = Normalize(Transform({1.0f, 0.0f, 0.0f}, physicsOrientationMatrix));
		const Vector3 physicsBoatUp = Normalize(Transform({0.0f, 1.0f, 0.0f}, physicsOrientationMatrix));
		const Vector3 physicsBoatForward = Normalize(Transform({0.0f, 0.0f, 1.0f}, physicsOrientationMatrix));
		const Vector3 worldUpForRide{0.0f, 1.0f, 0.0f};
		const float physicsPitchRad = -std::asin(
			(std::clamp)(Dot(physicsBoatForward, worldUpForRide), -1.0f, 1.0f));
		const float physicsRollRad = std::atan2(
			Dot(physicsBoatRight, worldUpForRide), Dot(physicsBoatUp, worldUpForRide));

		float finalPitchRad = physicsPitchRad;
		float finalRollRad = physicsRollRad;
		if (railMovementComponent->railAttitudeAngleLimitEnabled) {
			const float maxPitchRad =
				railMovementComponent->railAttitudeAngleLimitMaxPitchDegrees * kRailDegreeToRadian;
			const float maxRollRad =
				railMovementComponent->railAttitudeAngleLimitMaxRollDegrees * kRailDegreeToRadian;
			if (maxPitchRad > 0.0f) {
				finalPitchRad = (std::clamp)(physicsPitchRad, -maxPitchRad, maxPitchRad);
			}
			if (maxRollRad > 0.0f) {
				finalRollRad = (std::clamp)(physicsRollRad, -maxRollRad, maxRollRad);
			}
		}

		// --- XZ/Yaw: RailのGameplay Progress(traveledDistance)から直接決定する ---
		// Mode 2 Rail Rideでは唯一の進行座標。Physical Progress/closestRailDistance/backlog/
		// catchupは使わない。加速度・減速度・速度Zone・速度Profile等は既存のAdvanceRailRuntimeState
		// (Update側)がそのまま適用するため、ここでは進行速度・進行距離のロジックを重複させない。
		const float railProgress = runtimeState.traveledDistance;
		const Vector3 railProgressPosition = SampleRailPosition(runtimeState.samples, railProgress);
		const Vector3 railProgressDirection = CalculateRailDirection(
			runtimeState, *railMovementComponent, railProgress, railProgressPosition);
		const Vector3 offsetRailPosition = ApplyRailOffset(
			railProgressPosition, railProgressDirection, runtimeState.currentOffset);
		const EditorComponent* movementModifier = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MovementModifier);
		const Vector3 finalRailXZPosition = ApplyMovementModifierPosition(
			offsetRailPosition,
			railProgressDirection,
			movementModifier,
			runtimeState.modifierInputOffset);

		// Yawは中央差分で求める(前方差分1点よりRailの折り返し・急カーブに対して安定)。
		// 非ループ端では片側差分へ安全にフォールバックする。ループはdistance wrapを考慮する。
		const float yawSampleDistance = (std::max)(
			railMovementComponent->railRideYawSampleDistance, kRailMinimumDistance);
		float yawBackwardDistance = railProgress - yawSampleDistance;
		float yawForwardDistance = railProgress + yawSampleDistance;
		if (railMovementComponent->railLoop) {
			yawBackwardDistance = NormalizeRailDistance(
				yawBackwardDistance, runtimeState.totalDistance, true);
			yawForwardDistance = NormalizeRailDistance(
				yawForwardDistance, runtimeState.totalDistance, true);
		}
		else {
			if (yawBackwardDistance < 0.0f) {
				yawBackwardDistance = railProgress;
			}
			if (yawForwardDistance > runtimeState.totalDistance) {
				yawForwardDistance = railProgress;
			}
		}
		const Vector3 yawBackwardPosition = SampleRailPosition(runtimeState.samples, yawBackwardDistance);
		const Vector3 yawForwardPosition = SampleRailPosition(runtimeState.samples, yawForwardDistance);
		Vector3 railYawTangent{
			yawForwardPosition.x - yawBackwardPosition.x,
			0.0f,
			yawForwardPosition.z - yawBackwardPosition.z};
		const float railYawTangentLength = Length(railYawTangent);
		const Vector3 railForwardXZForYaw = railYawTangentLength > kRailMinimumDistance
			? Multiply(1.0f / railYawTangentLength, railYawTangent)
			: Vector3{railProgressDirection.x, 0.0f, railProgressDirection.z};

		// railLocalForwardAxisを必ず経由する。「指定されたローカル船首軸」がRail接線を向く
		// Yawを求める(Rail接線=World+Zとハードコードしない)。Mode 1のtargetRotation.y計算
		// (atan2(tangent) - offset(axis))と同じ規約。
		const float railTangentYawRad = std::atan2(railForwardXZForYaw.x, railForwardXZForYaw.z);
		const float targetLocalPlusZYawRad = railTangentYawRad - ResolveRailLocalForwardYawOffset(
			railMovementComponent->railLocalForwardAxis);

		// (Yaw, ClampedPitch, ClampedRoll)から直交基底を直接組み立てる。Hard Clampと同じ方式を
		// 使い、Euler合成を経由しないため、以前のPitch/Roll≈±180度バグと同種の問題は起きない。
		const Vector3 desiredForward{
			std::sin(targetLocalPlusZYawRad) * std::cos(finalPitchRad),
			-std::sin(finalPitchRad),
			std::cos(targetLocalPlusZYawRad) * std::cos(finalPitchRad)};
		Vector3 referenceRight = Cross(worldUpForRide, desiredForward);
		if (Length(referenceRight) <= kRailMinimumDistance) {
			referenceRight = physicsBoatRight;
		}
		referenceRight = Normalize(referenceRight);
		const Vector3 referenceUp = Normalize(Cross(desiredForward, referenceRight));
		const float cosFinalRoll = std::cos(finalRollRad);
		const float sinFinalRoll = std::sin(finalRollRad);
		const Vector3 desiredRight = Add(
			Multiply(cosFinalRoll, referenceRight), Multiply(sinFinalRoll, referenceUp));
		const Vector3 desiredUp = Add(
			Multiply(-sinFinalRoll, referenceRight), Multiply(cosFinalRoll, referenceUp));

		const Vector3 finalPosition{finalRailXZPosition.x, worldPosition.y, finalRailXZPosition.z};
		const Vector3 finalRotation = DecomposeOrientationVectorsToEuler(desiredRight, desiredUp, desiredForward);

		// 診断値はSetGameObjectTransform適用前(=物理結果とRail目標のズレ)を記録する。
		const Vector3 positionErrorVectorXZ{
			finalRailXZPosition.x - worldPosition.x, 0.0f, finalRailXZPosition.z - worldPosition.z};
		const float currentYawRadForDebug = std::atan2(physicsBoatForward.x, physicsBoatForward.z);
		const Vector3 velocityXZForDebug{
			rigidBodyComponent->velocity.x, 0.0f, rigidBodyComponent->velocity.z};
		const float velocityXZLengthForDebug = Length(velocityXZForDebug);

		railMovementComponent->railDebugRailRidePosition = finalRailXZPosition;
		railMovementComponent->railDebugRailRideActualPosition = worldPosition;
		railMovementComponent->railDebugRailRidePositionErrorXZ = Length(positionErrorVectorXZ);
		railMovementComponent->railDebugRailRideForward = railForwardXZForYaw;
		railMovementComponent->railDebugRailRideTargetYawDegrees = targetLocalPlusZYawRad * kRailRadianToDegree;
		railMovementComponent->railDebugRailRideFinalYawDegrees =
			NormalizeAngle(finalRotation.y) * kRailRadianToDegree;
		railMovementComponent->railDebugRailRideYawErrorDegrees =
			NormalizeAngle(targetLocalPlusZYawRad - currentYawRadForDebug) * kRailRadianToDegree;
		railMovementComponent->railDebugRailRidePhysicsY = worldPosition.y;
		railMovementComponent->railDebugRailRideFinalY = finalPosition.y;
		railMovementComponent->railDebugRailRidePhysicsPitchDegrees = physicsPitchRad * kRailRadianToDegree;
		railMovementComponent->railDebugRailRideFinalPitchDegrees = finalPitchRad * kRailRadianToDegree;
		railMovementComponent->railDebugRailRidePhysicsRollDegrees = physicsRollRad * kRailRadianToDegree;
		railMovementComponent->railDebugRailRideFinalRollDegrees = finalRollRad * kRailRadianToDegree;

		// この機能に限り、ユーザーの明示的な許可によりRigidbody姿勢を直接書き換える
		// (他のRail制御・Buoyancy等では引き続きForce/Torqueのみ)。
		physicsManager_->SetGameObjectTransform(gameObject.id, finalPosition, finalRotation);

		// --- XZ Linear Velocity: Railの実際の移動速度(接線方向*速度)へ整合させる ---
		// これによりカーブ中もVelocity DirectionがRail接線へ自動的に沿い、Buoyancy等の水力計算も
		// 実際にRail方向へ動いている速度を参照できる。Y速度は物理の結果をそのまま残す。
		const Vector3 railVelocityXZ = Multiply(runtimeState.currentSpeed, railForwardXZForYaw);
		const Vector3 finalVelocity{railVelocityXZ.x, rigidBodyComponent->velocity.y, railVelocityXZ.z};
		physicsManager_->SetVelocity(gameObject.id, finalVelocity);

		railMovementComponent->railDebugRailRideVelocityXZ = railVelocityXZ;
		railMovementComponent->railDebugRailRideActualVelocityXZ = velocityXZForDebug;
		if (velocityXZLengthForDebug > 0.5f && Length(railVelocityXZ) > kRailMinimumDistance) {
			const float velocityDirectionErrorRad = NormalizeAngle(
				std::atan2(velocityXZForDebug.x, velocityXZForDebug.z) -
				std::atan2(railVelocityXZ.x, railVelocityXZ.z));
			railMovementComponent->railDebugRailRideVelocityDirectionErrorDegrees =
				velocityDirectionErrorRad * kRailRadianToDegree;
		}
		else {
			railMovementComponent->railDebugRailRideVelocityDirectionErrorDegrees = 0.0f;
		}

		// --- Yaw Angular Velocity: 自由なYaw回転成分を除去する(毎ステップRailへ書き戻すため、
		// 残っていても最終的には上書きされるが、Buoyancy等の次ステップ計算に不要な回転運動量を
		// 持ち越さないため0にする)。Pitch/Roll Angular Velocityは物理の結果として残す。
		const Vector3 finalAngularVelocity{
			rigidBodyComponent->angularVelocity.x, 0.0f, rigidBodyComponent->angularVelocity.z};
		physicsManager_->SetAngularVelocity(gameObject.id, finalAngularVelocity);
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
		// Scriptからの明示的なワープなのでPhysical Rail Progressも即座に同期する。
		runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
		runtimeState.isPhysicalDistanceInitialized = true;
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
	// Scriptからの明示的なワープなのでPhysical Rail Progressも即座に同期する。
	runtimeState.physicalTraveledDistance = runtimeState.traveledDistance;
	runtimeState.isPhysicalDistanceInitialized = true;
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
