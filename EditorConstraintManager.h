#pragma once

#include "EditorScene.h"

#include <cmath>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorConstraintManager {
public:
	EditorConstraintManager() = default;
	~EditorConstraintManager() = default;
	EditorConstraintManager(const EditorConstraintManager&) = delete;
	EditorConstraintManager& operator=(const EditorConstraintManager&) = delete;
	EditorConstraintManager(EditorConstraintManager&&) = delete;
	EditorConstraintManager& operator=(EditorConstraintManager&&) = delete;

	void Initialize(EditorScene* editorScene);
	void Update(float deltaTime);

private:
	EditorScene* editorScene_ = nullptr;

	void SolveAimConstraint(EditorGameObject& gameObject, EditorComponent& component);
	void SolveLookAtConstraint(EditorGameObject& gameObject, EditorComponent& component);
	void SolveParentConstraint(EditorGameObject& gameObject, EditorComponent& component);
	void SolvePositionConstraint(EditorGameObject& gameObject, EditorComponent& component);
	void SolveRotationConstraint(EditorGameObject& gameObject, EditorComponent& component);
	void SolveScaleConstraint(EditorGameObject& gameObject, EditorComponent& component);
	EditorGameObject* FindTarget(int32_t targetId);
	Vector3 GetAimDirection(int32_t aimAxis, const Vector3& targetDir) const;
	Vector3 BuildLookAtRotation(const Vector3& from, const Vector3& target, int32_t upAxis, float rollDeg) const;
};

#pragma warning(pop)
