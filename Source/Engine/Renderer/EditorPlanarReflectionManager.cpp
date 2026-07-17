#include "EditorPlanarReflectionManager.h"
#include "EditorScene.h"
#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include <cmath>

using namespace EditorSharedState;

static Vector3 FromRot(const Vector3& rot, const Vector3& dir) {
    Matrix4x4 m = MakeAffineMatrix({1,1,1}, rot, {0,0,0});
    return Normalize(Transform(dir, m));
}

static Vector3 LocalMeshSize(const EditorSceneObject& o) {
    if (o.usesCustomMesh && Length(o.customMeshLocalBoundsSize) > 0.0001f)
        return o.customMeshLocalBoundsSize;
    switch (o.meshType) {
    case EditorModelMeshType::Plane: return {1,1,0};
    case EditorModelMeshType::Box:   return {1.6f,0.7f,1};
    default: return {1,1,1};
    }
}

void EditorPlanarReflectionManager::CollectProbes(const EditorScene& scene,
    const std::vector<EditorSceneObject>& sceneObjects)
{
    views_.clear();
    for (const auto& go : scene.GetGameObjects()) {
        if (!go.isActive) continue;
        auto* comp = EditorComponentUtility::FindComponent(go, EditorComponentType::ReflectionProbe);
        if (!comp || !comp->isActive || comp->assetPath != "Planar") continue;
        const EditorSceneObject* obj = nullptr;
        for (const auto& so : sceneObjects)
            if (so.gameObjectId == go.id) { obj = &so; break; }
        if (!obj) continue;
        ProbeView pv;
        pv.sourceId = go.id;
        pv.gameObject = &go;
        pv.sceneObject = obj;
        views_.push_back(pv);
    }
}

void EditorPlanarReflectionManager::UpdateCameras(const Matrix4x4& sw, const Matrix4x4& svp,
    const Matrix4x4& gw, const Matrix4x4& gvp)
{
    for (size_t i = 0; i < views_.size(); ++i) {
        auto& v = views_[i];
        const auto& go = *v.gameObject;
        const auto& obj = *v.sceneObject;

        Vector3 ls = LocalMeshSize(obj);
        Matrix4x4 wm = MakeAffineMatrix(go.scale, go.rotate, go.translate);
        Vector3 rc = Transform(Vector3{}, wm);
        Vector3 r = FromRot(go.rotate, {1,0,0});
        Vector3 u = FromRot(go.rotate, {0,1,0});
        Vector3 f = FromRot(go.rotate, {0,0,1});
        float sx = std::abs(ls.x*go.scale.x), sy = std::abs(ls.y*go.scale.y), sz = std::abs(ls.z*go.scale.z);
        int ax = (sx<sy&&sx<sz) ? 0 : (sz<sy&&sz<sx) ? 2 : 1;
        Vector3 n = ax==0 ? r : ax==2 ? f : u;
        Vector3 cp = {sw.matrix[3][0], sw.matrix[3][1], sw.matrix[3][2]};
        if (Dot(Subtract(cp, rc), n) < 0) n = Multiply(-1.f, n);
        float ht = ax==0 ? sx*0.5f : ax==2 ? sz*0.5f : sy*0.5f;
        Vector3 pt = Add(rc, Multiply(ht, n));
        n = Normalize(n); float d = Dot(pt, n);
        auto R = MakeIdentity4x4();
        R.matrix[0][0]=1-2*n.x*n.x; R.matrix[0][1]=-2*n.x*n.y; R.matrix[0][2]=-2*n.x*n.z;
        R.matrix[1][0]=-2*n.y*n.x; R.matrix[1][1]=1-2*n.y*n.y; R.matrix[1][2]=-2*n.y*n.z;
        R.matrix[2][0]=-2*n.z*n.x; R.matrix[2][1]=-2*n.z*n.y; R.matrix[2][2]=1-2*n.z*n.z;
        R.matrix[3][0]=2*d*n.x; R.matrix[3][1]=2*d*n.y; R.matrix[3][2]=2*d*n.z;

        v.sceneCam.viewProjection = Multiply(R, svp);
        v.sceneCam.inverseViewProjection = Inverse(v.sceneCam.viewProjection);
        v.sceneCam.position = Transform(cp, R);
        v.sceneCam.clipPlane = {n.x,n.y,n.z,-Dot(n,pt)+0.002f};

        cp = {gw.matrix[3][0], gw.matrix[3][1], gw.matrix[3][2]};
        if (Dot(Subtract(cp, rc), n) < 0) n = Multiply(-1.f, n);
        v.gameCam = v.sceneCam;
    }
}
