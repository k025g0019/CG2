#include "EditorConstraintManager.h"
#include "EditorComponentUtility.h"
#include "Source/Engine/Core/Vector&Matrix.h"

namespace {
	constexpr float kConstraintMinDistance = 0.0001f;
	constexpr float kDegToRad = 3.14159265f / 180.0f;
	constexpr float kRadToDeg = 180.0f / 3.14159265f;

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

	float MoveTowardsAngle(float current, float target, float maximumDelta) {
		const float angleDelta = NormalizeAngle(target - current);
		return NormalizeAngle(current + Clamp(angleDelta, -maximumDelta, maximumDelta));
	}

	float ConstraintLength(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 NormalizeSafe(const Vector3& value, const Vector3& fallback) {
		const float length = ConstraintLength(value);
		return length > kConstraintMinDistance
			? Vector3{value.x / length, value.y / length, value.z / length}
			: fallback;
	}

	float DotVector(const Vector3& firstValue, const Vector3& secondValue) {
		return firstValue.x * secondValue.x + firstValue.y * secondValue.y + firstValue.z * secondValue.z;
	}

	Vector3 SubtractVector(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
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

		auto* turretAim = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::TurretAim);
		if (turretAim != nullptr && turretAim->isActive) SolveTurretAim(gameObject, *turretAim, deltaTime);

		auto* horizonStabilizer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraHorizonStabilizer);
		if (horizonStabilizer != nullptr && horizonStabilizer->isActive) {
			SolveCameraHorizonStabilizer(gameObject, *horizonStabilizer, deltaTime);
		}
	}
}

void EditorConstraintManager::SolveTurretAim(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float deltaTime) {
	int32_t targetGameObjectId = component.turretTargetGameObjectId;

	if (targetGameObjectId < 0) {
		const int32_t selectorOwnerGameObjectId = component.turretTargetSelectorGameObjectId >= 0
			? component.turretTargetSelectorGameObjectId
			: gameObject.id;
		const EditorGameObject* selectorOwner = editorScene_->FindGameObject(selectorOwnerGameObjectId);
		const EditorComponent* selector = selectorOwner != nullptr
			? EditorComponentUtility::FindComponent(*selectorOwner, EditorComponentType::TargetSelector)
			: nullptr;
		targetGameObjectId = selector != nullptr
			? selector->targetSelectorCurrentTargetGameObjectId
			: -1;
	}

	EditorGameObject* target = FindTarget(targetGameObjectId);
	EditorGameObject* yawPivot = component.turretYawPivotGameObjectId >= 0
		? FindTarget(component.turretYawPivotGameObjectId)
		: &gameObject;
	EditorGameObject* pitchPivot = component.turretPitchPivotGameObjectId >= 0
		? FindTarget(component.turretPitchPivotGameObjectId)
		: yawPivot;
	component.turretCurrentTargetGameObjectId = targetGameObjectId;
	component.turretCanReachTarget = false;
	component.turretIsAimed = false;

	if (target == nullptr || yawPivot == nullptr || pitchPivot == nullptr) {
		return;
	}

	Vector3 targetScale = target->scale;
	Vector3 targetRotation = target->rotate;
	Vector3 targetPosition = target->translate;
	editorScene_->GetWorldTransform(target->id, targetScale, targetRotation, targetPosition);
	(void)targetScale;
	(void)targetRotation;
	const EditorGameObject* velocityOwner = target;
	const EditorComponent* targetRigidBody = nullptr;

	while (velocityOwner != nullptr && targetRigidBody == nullptr) {
		targetRigidBody = EditorComponentUtility::FindComponent(
			*velocityOwner,
			EditorComponentType::RigidBody);
		velocityOwner = targetRigidBody == nullptr
			? editorScene_->FindGameObject(velocityOwner->parentId)
			: velocityOwner;
	}

	if (targetRigidBody != nullptr) {
		const float predictionSeconds = (std::max)(component.turretPredictionSeconds, 0.0f);
		targetPosition.x += targetRigidBody->velocity.x * predictionSeconds;
		targetPosition.y += targetRigidBody->velocity.y * predictionSeconds;
		targetPosition.z += targetRigidBody->velocity.z * predictionSeconds;
	}

	Vector3 yawScale = yawPivot->scale;
	Vector3 yawWorldRotation = yawPivot->rotate;
	Vector3 yawPosition = yawPivot->translate;
	editorScene_->GetWorldTransform(yawPivot->id, yawScale, yawWorldRotation, yawPosition);
	(void)yawScale;
	(void)yawWorldRotation;
	const Vector3 targetDirection = NormalizeSafe(
		SubtractVector(targetPosition, yawPosition),
		{0.0f, 0.0f, 1.0f});
	Vector3 yawParentRotation{};

	if (const EditorGameObject* yawParent = editorScene_->FindGameObject(yawPivot->parentId)) {
		Vector3 parentScale = yawParent->scale;
		Vector3 parentPosition = yawParent->translate;
		yawParentRotation = yawParent->rotate;
		editorScene_->GetWorldTransform(yawParent->id, parentScale, yawParentRotation, parentPosition);
	}

	Vector3 pitchParentRotation{};

	if (const EditorGameObject* pitchParent = editorScene_->FindGameObject(pitchPivot->parentId)) {
		Vector3 parentScale = pitchParent->scale;
		Vector3 parentPosition = pitchParent->translate;
		pitchParentRotation = pitchParent->rotate;
		editorScene_->GetWorldTransform(pitchParent->id, parentScale, pitchParentRotation, parentPosition);
	}

	const float desiredYawDegrees = NormalizeAngle(
		std::atan2(targetDirection.x, targetDirection.z) - yawParentRotation.y) / kDegToRad;
	const float desiredPitchDegrees = NormalizeAngle(
		-std::asin(Clamp(targetDirection.y, -1.0f, 1.0f)) - pitchParentRotation.x) / kDegToRad;
	const float minimumYaw = (std::min)(component.turretYawMinimumDegrees, component.turretYawMaximumDegrees);
	const float maximumYaw = (std::max)(component.turretYawMinimumDegrees, component.turretYawMaximumDegrees);
	const float minimumPitch = (std::min)(component.turretPitchMinimumDegrees, component.turretPitchMaximumDegrees);
	const float maximumPitch = (std::max)(component.turretPitchMinimumDegrees, component.turretPitchMaximumDegrees);
	const float targetYaw = Clamp(desiredYawDegrees, minimumYaw, maximumYaw) * kDegToRad;
	const float targetPitch = Clamp(desiredPitchDegrees, minimumPitch, maximumPitch) * kDegToRad;
	component.turretCanReachTarget = desiredYawDegrees >= minimumYaw && desiredYawDegrees <= maximumYaw &&
		desiredPitchDegrees >= minimumPitch && desiredPitchDegrees <= maximumPitch;
	yawPivot->rotate.y = MoveTowardsAngle(
		yawPivot->rotate.y,
		targetYaw,
		(std::max)(component.turretYawSpeedDegrees, 0.0f) * kDegToRad * (std::max)(deltaTime, 0.0f));
	pitchPivot->rotate.x = MoveTowardsAngle(
		pitchPivot->rotate.x,
		targetPitch,
		(std::max)(component.turretPitchSpeedDegrees, 0.0f) * kDegToRad * (std::max)(deltaTime, 0.0f));
	component.turretYawErrorDegrees = std::abs(NormalizeAngle(targetYaw - yawPivot->rotate.y)) / kDegToRad;
	component.turretPitchErrorDegrees = std::abs(NormalizeAngle(targetPitch - pitchPivot->rotate.x)) / kDegToRad;
	component.turretIsAimed = component.turretCanReachTarget &&
		component.turretYawErrorDegrees <= (std::max)(component.turretAimToleranceDegrees, 0.0f) &&
		component.turretPitchErrorDegrees <= (std::max)(component.turretAimToleranceDegrees, 0.0f);
}

void EditorConstraintManager::SolveCameraHorizonStabilizer(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float deltaTime) {
	// 旧SceneはCameraの接続先だけを設定し、Stabilizer Sourceが未設定だった。
	// 現在のCameraローカルOffsetをそのまま移行し、見た目を飛ばさず有効化する。
	if (component.horizonSourceGameObjectId < 0) {
		EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Camera);

		if (cameraComponent != nullptr &&
			cameraComponent->isActive &&
			cameraComponent->connectedGameObjectId >= 0) {
			component.horizonSourceGameObjectId = cameraComponent->connectedGameObjectId;
			component.horizonLocalPositionOffset = gameObject.translate;
			component.horizonRotationOffsetDegrees = {
				gameObject.rotate.x * kRadToDeg,
				gameObject.rotate.y * kRadToDeg,
				gameObject.rotate.z * kRadToDeg};
		}
	}

	EditorGameObject* source = FindTarget(component.horizonSourceGameObjectId);

	if (source == nullptr || source->id == gameObject.id) {
		return;
	}

	const Matrix4x4 sourceMatrix = editorScene_->GetWorldMatrix(source->id);
	const Vector3 sourcePosition = Transform(
		{0.0f, 0.0f, 0.0f},
		sourceMatrix);
	const Vector3 sourceForward = NormalizeSafe(
		SubtractVector(Transform({0.0f, 0.0f, 1.0f}, sourceMatrix), sourcePosition),
		{0.0f, 0.0f, 1.0f});
	const Vector3 sourceUp = NormalizeSafe(
		SubtractVector(Transform({0.0f, 1.0f, 0.0f}, sourceMatrix), sourcePosition),
		{0.0f, 1.0f, 0.0f});
	const Vector3 worldUp = NormalizeSafe(component.horizonWorldUp, {0.0f, 1.0f, 0.0f});
	const float worldUpForwardDot = DotVector(worldUp, sourceForward);
	const Vector3 projectedWorldUp = NormalizeSafe(
		SubtractVector(worldUp, {
			sourceForward.x * worldUpForwardDot,
			sourceForward.y * worldUpForwardDot,
			sourceForward.z * worldUpForwardDot}),
		{0.0f, 1.0f, 0.0f});
	const Vector3 upCross = Cross(projectedWorldUp, sourceUp);
	const float sourceRoll = std::atan2(
		DotVector(upCross, sourceForward),
		Clamp(DotVector(projectedWorldUp, sourceUp), -1.0f, 1.0f));
	// World行列のEuler分解値は、同じ姿勢でもPitchが±PI側の等価表現へ切り替わる。
	// Cameraへ継承するPitch/YawはForward方向から一意に作り、周期的な反転を防ぐ。
	const float sourceHorizontalLength = std::sqrt(
		sourceForward.x * sourceForward.x +
		sourceForward.z * sourceForward.z);
	const float sourcePitch = -std::atan2(
		sourceForward.y,
		(std::max)(sourceHorizontalLength, kConstraintMinDistance));
	const float sourceYaw = std::atan2(
		sourceForward.x,
		sourceForward.z);
	Vector3 cameraScale = gameObject.scale;
	Vector3 cameraRotation = gameObject.rotate;
	Vector3 cameraPosition = gameObject.translate;
	editorScene_->GetWorldTransform(gameObject.id, cameraScale, cameraRotation, cameraPosition);
	Vector3 targetPosition = cameraPosition;

	if (component.horizonFollowPosition) {
		targetPosition = Transform(component.horizonLocalPositionOffset, sourceMatrix);
	}

	const float maximumRoll = (std::max)(component.horizonMaximumRollDegrees, 0.0f) * kDegToRad;
	const Vector3 rotationOffset = {
		component.horizonRotationOffsetDegrees.x * kDegToRad,
		component.horizonRotationOffsetDegrees.y * kDegToRad,
		component.horizonRotationOffsetDegrees.z * kDegToRad};
	const Vector3 targetRotation = {
		sourcePitch * Clamp(component.horizonPitchInheritance, 0.0f, 1.0f) + rotationOffset.x,
		sourceYaw * Clamp(component.horizonYawInheritance, 0.0f, 1.0f) + rotationOffset.y,
		Clamp(
			sourceRoll * Clamp(component.horizonRollInheritance, 0.0f, 1.0f) + rotationOffset.z,
			-maximumRoll,
			maximumRoll)};
	const float dampingBlend = component.horizonDamping <= 0.0f
		? 1.0f
		: 1.0f - std::exp(-(std::max)(component.horizonDamping, 0.0f) * (std::max)(deltaTime, 0.0f));
	cameraPosition = LerpVec(cameraPosition, targetPosition, dampingBlend);
	cameraRotation = LerpAngle(cameraRotation, targetRotation, dampingBlend);
	editorScene_->SetWorldTransform(
		gameObject.id,
		cameraScale,
		cameraRotation,
		cameraPosition);
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
