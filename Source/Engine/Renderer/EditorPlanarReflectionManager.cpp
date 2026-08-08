#include "EditorPlanarReflectionManager.h"

#include "EditorComponentUtility.h"
#include "EditorScene.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace EditorSharedState;

namespace {
	Vector3 ResolveWorldUp(const Matrix4x4& worldMatrix) {
		const Vector3 worldUp{
			worldMatrix.matrix[1][0],
			worldMatrix.matrix[1][1],
			worldMatrix.matrix[1][2]};
		const float lengthSquared = Dot(worldUp, worldUp);

		if (lengthSquared <= 0.000001f) {
			return {0.0f, 1.0f, 0.0f};
		}

		return Normalize(worldUp);
	}

	Matrix4x4 MakeReflectionMatrix(const Vector3& planePoint, const Vector3& planeNormal) {
		const Vector3 normal = Normalize(planeNormal);
		const float planeDistance = Dot(planePoint, normal);
		Matrix4x4 reflectionMatrix = MakeIdentity4x4();
		reflectionMatrix.matrix[0][0] = 1.0f - 2.0f * normal.x * normal.x;
		reflectionMatrix.matrix[0][1] = -2.0f * normal.x * normal.y;
		reflectionMatrix.matrix[0][2] = -2.0f * normal.x * normal.z;
		reflectionMatrix.matrix[1][0] = -2.0f * normal.y * normal.x;
		reflectionMatrix.matrix[1][1] = 1.0f - 2.0f * normal.y * normal.y;
		reflectionMatrix.matrix[1][2] = -2.0f * normal.y * normal.z;
		reflectionMatrix.matrix[2][0] = -2.0f * normal.z * normal.x;
		reflectionMatrix.matrix[2][1] = -2.0f * normal.z * normal.y;
		reflectionMatrix.matrix[2][2] = 1.0f - 2.0f * normal.z * normal.z;
		reflectionMatrix.matrix[3][0] = 2.0f * planeDistance * normal.x;
		reflectionMatrix.matrix[3][1] = 2.0f * planeDistance * normal.y;
		reflectionMatrix.matrix[3][2] = 2.0f * planeDistance * normal.z;
		return reflectionMatrix;
	}

	PlanarReflectionCamera BuildReflectionCamera(
		const Matrix4x4& sourceCameraWorld,
		const Matrix4x4& sourceViewMatrix,
		const Matrix4x4& sourceProjectionMatrix,
		const Vector3& reflectorCenter,
		const Vector3& unsignedPlaneNormal,
		float halfThickness) {
		const Vector3 sourceCameraPosition{
			sourceCameraWorld.matrix[3][0],
			sourceCameraWorld.matrix[3][1],
			sourceCameraWorld.matrix[3][2]};
		Vector3 planeNormal = Normalize(unsignedPlaneNormal);

		// 各 View のカメラ側を鏡面の表面として選ぶ。Scene と Game で法線を共有しない。
		if (Dot(Subtract(sourceCameraPosition, reflectorCenter), planeNormal) < 0.0f) {
			planeNormal = Multiply(-1.0f, planeNormal);
		}

		const Vector3 planePoint = Add(reflectorCenter, Multiply(halfThickness, planeNormal));
		const Matrix4x4 reflectionMatrix = MakeReflectionMatrix(planePoint, planeNormal);

		PlanarReflectionCamera reflectionCamera{};
		reflectionCamera.viewMatrix = Multiply(reflectionMatrix, sourceViewMatrix);
		reflectionCamera.projectionMatrix = sourceProjectionMatrix;
		reflectionCamera.viewProjection = Multiply(
			reflectionCamera.viewMatrix,
			reflectionCamera.projectionMatrix);
		reflectionCamera.inverseViewProjection = Inverse(reflectionCamera.viewProjection);
		reflectionCamera.position = Transform(sourceCameraPosition, reflectionMatrix);
		reflectionCamera.clipPlane = {
			planeNormal.x,
			planeNormal.y,
			planeNormal.z,
			-Dot(planeNormal, planePoint) + 0.002f};
		return reflectionCamera;
	}
}

void EditorPlanarReflectionManager::CollectProbes(
	const EditorScene& scene,
	const std::vector<EditorSceneObject>& sceneObjects) {
	views_.clear();

	for (const EditorGameObject& gameObject : scene.GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* component = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ReflectionProbe);
		if (component == nullptr || !component->isActive || component->assetPath != "Planar") {
			continue;
		}

		const EditorSceneObject* sceneObject = nullptr;
		for (const EditorSceneObject& candidate : sceneObjects) {
			if (candidate.gameObjectId == gameObject.id) {
				sceneObject = &candidate;
				break;
			}
		}

		if (sceneObject == nullptr) {
			continue;
		}

		ProbeView probeView{};
		probeView.sourceId = gameObject.id;
		probeView.gameObject = &gameObject;
		probeView.component = component;
		probeView.sceneObject = sceneObject;
		views_.push_back(probeView);
	}

	// 明示的な鏡面 Probe がない Scene では、Ocean を反射 Capture 面の候補として使う。
	// 既存 Probe がある場合は従来の選択と全画面合成を優先し、挙動を変えない。
	if (!views_.empty()) {
		return;
	}

	for (const EditorGameObject& gameObject : scene.GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* oceanComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Ocean);

		if (oceanComponent == nullptr || !oceanComponent->isActive) {
			continue;
		}

		const EditorSceneObject* sceneObject = nullptr;

		for (const EditorSceneObject& candidate : sceneObjects) {
			if (candidate.gameObjectId == gameObject.id && candidate.ocean.isEnabled) {
				sceneObject = &candidate;
				break;
			}
		}

		if (sceneObject == nullptr || sceneObject->ocean.reflectionStrength <= 0.001f) {
			continue;
		}

		ProbeView oceanView{};
		oceanView.sourceId = gameObject.id;
		oceanView.isOceanSurface = true;
		oceanView.gameObject = &gameObject;
		oceanView.component = oceanComponent;
		oceanView.sceneObject = sceneObject;
		views_.push_back(oceanView);
	}
}

void EditorPlanarReflectionManager::UpdateCameras(
	const Matrix4x4& sceneCameraWorld,
	const Matrix4x4& sceneViewMatrix,
	const Matrix4x4& sceneProjectionMatrix,
	const Matrix4x4& gameCameraWorld,
	const Matrix4x4& gameViewMatrix,
	const Matrix4x4& gameProjectionMatrix) {
	for (ProbeView& probeView : views_) {
		if (probeView.gameObject == nullptr ||
			probeView.component == nullptr ||
			probeView.sceneObject == nullptr) {
			continue;
		}

		const EditorComponent& component = *probeView.component;
		const Matrix4x4& reflectorWorld = probeView.sceneObject->worldMatrix;

		// Reflection Probe のローカル Y 面を反射面とする。
		// Ocean は GameObject 原点、明示 Probe は Collider Center を面上の基準点にする。
		const Vector3 localCenter = probeView.isOceanSurface
			? Vector3{0.0f, 0.0f, 0.0f}
			: component.colliderCenter;
		const Vector3 reflectorCenter = Transform(localCenter, reflectorWorld);
		const Vector3 localUp = ResolveWorldUp(reflectorWorld);
		const float halfThickness = 0.0f;  // Center を反射面そのものとし、Probe 範囲の厚みで位置をずらさない。

		probeView.sceneCam = BuildReflectionCamera(
			sceneCameraWorld,
			sceneViewMatrix,
			sceneProjectionMatrix,
			reflectorCenter,
			localUp,
			halfThickness);
		probeView.gameCam = BuildReflectionCamera(
			gameCameraWorld,
			gameViewMatrix,
			gameProjectionMatrix,
			reflectorCenter,
			localUp,
			halfThickness);
	}
}

const EditorPlanarReflectionManager::ProbeView*
EditorPlanarReflectionManager::FindNearestView(const Vector3& cameraPosition) const {
	const ProbeView* nearestView = nullptr;
	float nearestDistanceSquared = (std::numeric_limits<float>::max)();

	for (const ProbeView& probeView : views_) {
		if (probeView.gameObject == nullptr ||
			probeView.component == nullptr ||
			probeView.sceneObject == nullptr) {
			continue;
		}

		const Matrix4x4& reflectorWorld = probeView.sceneObject->worldMatrix;
		const Vector3 localCenter = probeView.isOceanSurface
			? Vector3{0.0f, 0.0f, 0.0f}
			: probeView.component->colliderCenter;
		const Vector3 reflectorCenter = Transform(
			localCenter,
			reflectorWorld);
		const Vector3 cameraOffset = Subtract(cameraPosition, reflectorCenter);
		float distanceSquared = Dot(cameraOffset, cameraOffset);

		if (probeView.isOceanSurface) {
			const Matrix4x4 reflectorWorldToLocal = Inverse(reflectorWorld);
			const Vector3 localCameraPosition = Transform(
				cameraPosition,
				reflectorWorldToLocal);
			const float localHalfExtent =
				(std::max)(probeView.sceneObject->ocean.size, 1.0f) * 4.0f;
			const float overflowX = (std::max)(
				std::abs(localCameraPosition.x) - localHalfExtent,
				0.0f);
			const float overflowZ = (std::max)(
				std::abs(localCameraPosition.z) - localHalfExtent,
				0.0f);
			distanceSquared =
				(overflowX * overflowX + overflowZ * overflowZ) * 1024.0f +
				localCameraPosition.y * localCameraPosition.y;
		}

		if (distanceSquared >= nearestDistanceSquared) {
			continue;
		}

		nearestDistanceSquared = distanceSquared;
		nearestView = &probeView;
	}

	return nearestView;
}

bool EditorPlanarReflectionManager::HasCompositeProbes() const {
	for (const ProbeView& probeView : views_) {
		if (!probeView.isOceanSurface) {
			return true;
		}
	}

	return false;
}
