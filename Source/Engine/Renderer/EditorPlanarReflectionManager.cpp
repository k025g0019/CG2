#include "EditorPlanarReflectionManager.h"

#include "EditorComponentUtility.h"
#include "EditorScene.h"
#include "EditorSharedState.h"

#include <cmath>
#include <limits>

using namespace EditorSharedState;

namespace {
	Vector3 TransformDirectionByRotation(const Vector3& rotation, const Vector3& direction) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		return Normalize(Transform(direction, rotationMatrix));
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
}

void EditorPlanarReflectionManager::UpdateCameras(
	const Matrix4x4& sceneCameraWorld,
	const Matrix4x4& sceneViewMatrix,
	const Matrix4x4& sceneProjectionMatrix,
	const Matrix4x4& gameCameraWorld,
	const Matrix4x4& gameViewMatrix,
	const Matrix4x4& gameProjectionMatrix) {
	for (ProbeView& probeView : views_) {
		const EditorGameObject& gameObject = *probeView.gameObject;
		const EditorComponent& component = *probeView.component;
		const Matrix4x4 reflectorWorld = MakeAffineMatrix(
			gameObject.scale,
			gameObject.rotate,
			gameObject.translate);

		// Reflection Probe のローカル Y 面を反射面とする。
		// メッシュの最薄軸から推測すると、FBX の余白や非一様スケールで反射位置が変わる。
		const Vector3 reflectorCenter = Transform(component.colliderCenter, reflectorWorld);
		const Vector3 localUp = TransformDirectionByRotation(gameObject.rotate, {0.0f, 1.0f, 0.0f});
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
		if (probeView.gameObject == nullptr || probeView.component == nullptr) {
			continue;
		}

		const Matrix4x4 reflectorWorld = MakeAffineMatrix(
			probeView.gameObject->scale,
			probeView.gameObject->rotate,
			probeView.gameObject->translate);
		const Vector3 reflectorCenter = Transform(
			probeView.component->colliderCenter,
			reflectorWorld);
		const Vector3 cameraOffset = Subtract(cameraPosition, reflectorCenter);
		const float distanceSquared = Dot(cameraOffset, cameraOffset);

		if (distanceSquared >= nearestDistanceSquared) {
			continue;
		}

		nearestDistanceSquared = distanceSquared;
		nearestView = &probeView;
	}

	return nearestView;
}
