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
    Matrix4x4 viewProjection;
    Matrix4x4 inverseViewProjection;
    Vector3 position;
    Vector4 clipPlane;
};

class EditorPlanarReflectionManager {
public:
    void CollectProbes(const EditorScene& scene, const std::vector<EditorSceneObject>& sceneObjects);
    void UpdateCameras(const Matrix4x4& sw, const Matrix4x4& svp, const Matrix4x4& gw, const Matrix4x4& gvp);

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
