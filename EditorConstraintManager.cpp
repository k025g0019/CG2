#include "EditorConstraintManager.h"
#include "EditorComponentUtility.h"

namespace {
	constexpr float kConstraintMinDistance = 0.0001f;
	constexpr float kDegToRad = 3.14159265f / 180.0f;

	float Clamp(float v, float min, float max) {
		return (std::max)(min, (std::min)(v, max));
	}

	float Lerp(float a, float b, float t) {
		return a + (b - a) * t;
	}

	Vector3 LerpVec(const Vector3& a, const Vector3& b, float t) {
		return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
	}

	float NormalizeAngle(float rad) {
		while (rad > 3.14159265f) rad -= 2.0f * 3.14159265f;
		while (rad < -3.14159265f) rad += 2.0f * 3.14159265f;
		return rad;
	}

	Vector3 LerpAngle(const Vector3& a, const Vector3& b, float t) {
		return {
			NormalizeAngle(Lerp(a.x, NormalizeAngle(b.x - a.x) + a.x, t)),
			NormalizeAngle(Lerp(a.y, NormalizeAngle(b.y - a.y) + a.y, t)),
			NormalizeAngle(Lerp(a.z, NormalizeAngle(b.z - a.z) + a.z, t))};
	}

	void GetAxes(int32_t axis, Vector3& forward, Vector3& up, Vector3& right) {
		switch (axis) {
		case 0: forward = {1,0,0}; up = {0,1,0}; right = {0,0,-1}; break;
		case 1: forward = {-1,0,0}; up = {0,1,0}; right = {0,0,1}; break;
		case 2: forward = {0,1,0}; up = {0,0,-1}; right = {1,0,0}; break;
		case 3: forward = {0,-1,0}; up = {0,0,1}; right = {1,0,0}; break;
		case 4: forward = {0,0,1}; up = {0,1,0}; right = {-1,0,0}; break;
		case 5: forward = {0,0,-1}; up = {0,1,0}; right = {1,0,0}; break;
		default: forward = {0,1,0}; up = {0,0,-1}; right = {1,0,0}; break;
		}
	}
}

void EditorConstraintManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
}

void EditorConstraintManager::Update(float deltaTime) {
	(void)deltaTime;
	if (editorScene_ == nullptr) return;

	for (auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;

		auto* aim = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::AimConstraint);
		if (aim != nullptr && aim->isActive) SolveAimConstraint(gameObject, *aim);

		auto* lookAt = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::LookAtConstraint);
		if (lookAt != nullptr && lookAt->isActive) SolveLookAtConstraint(gameObject, *lookAt);

		auto* rotation = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RotationConstraint);
		if (rotation != nullptr && rotation->isActive) SolveRotationConstraint(gameObject, *rotation);

		auto* scale = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ScaleConstraint);
		if (scale != nullptr && scale->isActive) SolveScaleConstraint(gameObject, *scale);

		auto* position = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::PositionConstraint);
		if (position != nullptr && position->isActive) SolvePositionConstraint(gameObject, *position);

		auto* parent = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ParentConstraint);
		if (parent != nullptr && parent->isActive) SolveParentConstraint(gameObject, *parent);
	}
}

EditorGameObject* EditorConstraintManager::FindTarget(int32_t targetId) {
	if (editorScene_ == nullptr || targetId < 0) return nullptr;
	return editorScene_->FindGameObject(targetId);
}

void EditorConstraintManager::SolveAimConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	Vector3 dir = {target->translate.x - gameObject.translate.x,
	               target->translate.y - gameObject.translate.y,
	               target->translate.z - gameObject.translate.z};
	float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len < kConstraintMinDistance) return;
	dir = {dir.x / len, dir.y / len, dir.z / len};

	Vector3 localForward, localUp, localRight;
	GetAxes(component.constraintAimAxis, localForward, localUp, localRight);

	Vector3 desiredForward = dir;

	Vector3 worldUp = {0, 1, 0};
	Vector3 desiredRight = Cross(desiredForward, worldUp);
	float rightLen = std::sqrt(desiredRight.x * desiredRight.x + desiredRight.y * desiredRight.y + desiredRight.z * desiredRight.z);
	if (rightLen < kConstraintMinDistance) {
		desiredRight = {1, 0, 0};
	} else {
		desiredRight = {desiredRight.x / rightLen, desiredRight.y / rightLen, desiredRight.z / rightLen};
	}
	Vector3 desiredUp = Cross(desiredRight, desiredForward);

	float rotY = std::atan2(desiredForward.x, desiredForward.z);
	float rotX = -std::asin(Clamp(desiredForward.y, -1.0f, 1.0f));
	float rotZ = std::atan2(-desiredRight.y, desiredUp.y);

	Vector3 targetRot = {rotX, rotY, rotZ};
	gameObject.rotate = LerpAngle(gameObject.rotate, targetRot, component.constraintWeight);
}

void EditorConstraintManager::SolveLookAtConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	Vector3 dir = {target->translate.x - gameObject.translate.x,
	               target->translate.y - gameObject.translate.y,
	               target->translate.z - gameObject.translate.z};
	float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len < kConstraintMinDistance) return;
	dir = {dir.x / len, dir.y / len, dir.z / len};

	Vector3 worldUp = {0, 1, 0};
	Vector3 desiredRight = Cross(dir, worldUp);
	float rightLen = std::sqrt(desiredRight.x * desiredRight.x + desiredRight.y * desiredRight.y + desiredRight.z * desiredRight.z);
	if (rightLen < kConstraintMinDistance) {
		desiredRight = {1, 0, 0};
	} else {
		desiredRight = {desiredRight.x / rightLen, desiredRight.y / rightLen, desiredRight.z / rightLen};
	}

	float rollRad = component.constraintRoll * kDegToRad;
	float cosR = std::cos(rollRad);
	float sinR = std::sin(rollRad);
	Vector3 rolledRight = {
		desiredRight.x * cosR - worldUp.x * sinR,
		desiredRight.y * cosR - worldUp.y * sinR,
		desiredRight.z * cosR - worldUp.z * sinR};
	Vector3 desiredUp = Cross(rolledRight, dir);

	float rotY = std::atan2(dir.x, dir.z);
	float rotX = -std::asin(Clamp(dir.y, -1.0f, 1.0f));
	float rotZ = std::atan2(-rolledRight.y, desiredUp.y);

	Vector3 targetRot = {rotX, rotY, rotZ};
	gameObject.rotate = LerpAngle(gameObject.rotate, targetRot, component.constraintWeight);
}

void EditorConstraintManager::SolveParentConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	float w = component.constraintWeight;
	gameObject.translate = LerpVec(gameObject.translate, {
		target->translate.x + component.constraintPositionOffset.x,
		target->translate.y + component.constraintPositionOffset.y,
		target->translate.z + component.constraintPositionOffset.z}, w);
	gameObject.rotate = LerpAngle(gameObject.rotate, {
		target->rotate.x + component.constraintRotationOffset.x,
		target->rotate.y + component.constraintRotationOffset.y,
		target->rotate.z + component.constraintRotationOffset.z}, w);
	gameObject.scale = LerpVec(gameObject.scale, target->scale, w);
}

void EditorConstraintManager::SolvePositionConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	gameObject.translate = LerpVec(gameObject.translate, {
		target->translate.x + component.constraintPositionOffset.x,
		target->translate.y + component.constraintPositionOffset.y,
		target->translate.z + component.constraintPositionOffset.z},
		component.constraintWeight);
}

void EditorConstraintManager::SolveRotationConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	gameObject.rotate = LerpAngle(gameObject.rotate, {
		target->rotate.x + component.constraintRotationOffset.x,
		target->rotate.y + component.constraintRotationOffset.y,
		target->rotate.z + component.constraintRotationOffset.z},
		component.constraintWeight);
}

void EditorConstraintManager::SolveScaleConstraint(EditorGameObject& gameObject, EditorComponent& component) {
	EditorGameObject* target = FindTarget(component.connectedGameObjectId);
	if (target == nullptr) return;

	Vector3 desired = target->scale;
	if (component.constraintFreezeAxisX) desired.x = gameObject.scale.x;
	if (component.constraintFreezeAxisY) desired.y = gameObject.scale.y;
	if (component.constraintFreezeAxisZ) desired.z = gameObject.scale.z;

	gameObject.scale = LerpVec(gameObject.scale, desired, component.constraintWeight);
}
