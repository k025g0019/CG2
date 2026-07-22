#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "EditorCommonTypes.h"

struct EditorGameObject;
struct EditorComponent;
struct EditorSceneObject;
class EditorScene;

struct PlanarReflectionCamera {
	Matrix4x4 viewMatrix;  // 平面で反転したカメラの View 行列。
	Matrix4x4 projectionMatrix;  // 元 View と同じ Projection 行列。
	Matrix4x4 viewProjection;
	Matrix4x4 inverseViewProjection;
	Vector3 position;
	Vector4 clipPlane;
};

class EditorPlanarReflectionManager {
public:
	void CollectProbes(
		const EditorScene& scene,
		const std::vector<EditorSceneObject>& sceneObjects);  // 有効な平面反射コンポーネントを集める。
	void UpdateCameras(
		const Matrix4x4& sceneCameraWorld,
		const Matrix4x4& sceneViewMatrix,
		const Matrix4x4& sceneProjectionMatrix,
		const Matrix4x4& gameCameraWorld,
		const Matrix4x4& gameViewMatrix,
		const Matrix4x4& gameProjectionMatrix);  // Scene / Game 用の反射カメラを個別に作る。

	struct ProbeView {
		int32_t sourceId = -1;
		const EditorGameObject* gameObject = nullptr;
		const EditorSceneObject* sceneObject = nullptr;
		PlanarReflectionCamera sceneCam;
		PlanarReflectionCamera gameCam;
	};
	const std::vector<ProbeView>& GetViews() const { return views_; }
	bool HasProbes() const { return !views_.empty(); }

private:
	std::vector<ProbeView> views_;
};
