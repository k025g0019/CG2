#pragma once

#include "EditorScene.h"

class EditorComponentUtility {
public:
	static EditorComponent* FindComponent(EditorGameObject& gameObject, EditorComponentType type);  // 編集可能な GameObject から指定種類の Component を探す
	static const EditorComponent* FindComponent(const EditorGameObject& gameObject, EditorComponentType type);  // 読み取り専用 GameObject から指定種類の Component を探す
	static const EditorComponent* FindInheritedComponent(
		const EditorScene& editorScene,
		int32_t gameObjectId,
		EditorComponentType type,
		int32_t* componentOwnerGameObjectId = nullptr);  // 所有Objectから親方向へ最初の有効Componentを探す
	static Vector3 ResolveInheritedRigidBodyVelocity(
		const EditorScene& editorScene,
		int32_t sourceGameObjectId,
		bool useParentRigidBody,
		const Vector3& sampleWorldPosition,
		float linearInheritance,
		float angularInheritance);  // 並進速度と角速度による作用点速度を同じWorld速度へまとめる
};
