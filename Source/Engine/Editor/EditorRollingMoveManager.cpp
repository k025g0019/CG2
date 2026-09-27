#include "EditorRollingMoveManager.h"

#include "EditorComponentUtility.h"
#include "Vector&Matrix.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kRollingMinimumDirectionLength = 0.0001f;  // 0 ベクトルに近い進行方向では回転軸を作れない
	constexpr float kRollingMinimumTorque = 0.0001f;  // 0 トルクでは駆動しない
	constexpr float kRollingHorsepowerToWatt = 745.7f;  // 馬力を角速度上限計算へ使うため W に変換する
	constexpr float kRollingAngularSpeedEpsilon = 0.05f;  // 上限付近で毎フレーム張り付いて震えないよう少し余裕を見る
	constexpr float kRollingParallelThreshold = 0.98f;  // 基準軸と進行方向がほぼ平行なら別軸へ切り替える

	Vector3 MakeLocalDirectionToWorld(const EditorGameObject& gameObject, const Vector3& localDirection) {
		const Vector3 movementRotate = {
			0.0f,
			gameObject.rotate.y,
			0.0f};  // 球やタイヤは転がりで X/Z 軸が上下を向くので、進行方向は見た目回転すべてではなく Yaw だけを使う。
		const Matrix4x4 rotateMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			movementRotate,
			{0.0f, 0.0f, 0.0f});  // 平行移動を含まない回転だけの行列で、ローカル軸方向をワールド方向へ変換する。
		return Transform(localDirection, rotateMatrix);
	}

	float DotVector3(const Vector3& firstVector, const Vector3& secondVector) {
		return
			(firstVector.x * secondVector.x) +
			(firstVector.y * secondVector.y) +
			(firstVector.z * secondVector.z);
	}

	Vector3 MakeRollingReferenceAxis(const EditorGameObject& gameObject, const Vector3& worldDirection) {
		const Matrix4x4 rotateMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			gameObject.rotate,
			{0.0f, 0.0f, 0.0f});  // 進行方向と同じローカル基準で回転軸を作るため、姿勢全体の回転を使う。

		Vector3 referenceAxis = Normalize(Transform({0.0f, 1.0f, 0.0f}, rotateMatrix));  // まずはローカル上方向を基準軸として使う。
		if (std::fabs(DotVector3(referenceAxis, worldDirection)) >= kRollingParallelThreshold) {
			referenceAxis = Normalize(Transform({1.0f, 0.0f, 0.0f}, rotateMatrix));  // 上方向と平行に近い時はローカル右方向へ切り替える。
		}

		if (std::fabs(DotVector3(referenceAxis, worldDirection)) >= kRollingParallelThreshold) {
			referenceAxis = Normalize(Transform({0.0f, 0.0f, 1.0f}, rotateMatrix));  // まだ平行ならローカル前方向を使う。
		}

		return referenceAxis;
	}

	Vector3 MakeRollingAxis(const EditorGameObject& gameObject, const Vector3& worldDirection) {
		const Vector3 referenceAxis = MakeRollingReferenceAxis(gameObject, worldDirection);
		return Normalize(Cross(referenceAxis, worldDirection));  // 進行方向へ接線速度を作れる直交軸を、外積から求める。
	}

	Vector3 SubtractVector3(const Vector3& firstVector, const Vector3& secondVector) {
		return {
			firstVector.x - secondVector.x,
			firstVector.y - secondVector.y,
			firstVector.z - secondVector.z};
	}

	float GetRollingRadius(const EditorGameObject& gameObject, const EditorComponent& rollingMoveComponent) {
		const float configuredRadius = rollingMoveComponent.colliderRadius;
		if (configuredRadius > 0.001f) {
			return configuredRadius;
		}

		const EditorComponent* sphereCollider =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SphereCollider);
		if (sphereCollider != nullptr && sphereCollider->isActive && sphereCollider->colliderRadius > 0.001f) {
			return sphereCollider->colliderRadius;
		}

		const float fallbackRadius = (std::max)(0.5f, (std::max)(std::fabs(gameObject.scale.x), std::fabs(gameObject.scale.z)) * 0.5f);
		return fallbackRadius;
	}
}

void EditorRollingMoveManager::Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager) {
	editorScene_ = editorScene;  // Play 中に Component を検索する Scene を保持する。
	physicsManager_ = physicsManager;  // Dynamic Rigidbody の速度を Jolt へ渡す先を保持する。
	isStarted_ = false;
}

void EditorRollingMoveManager::Start() {
	isStarted_ = true;
}

void EditorRollingMoveManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* rollingMoveComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RollingMove);
		if (rollingMoveComponent == nullptr || !rollingMoveComponent->isActive) {
			continue;
		}

		const Vector3 normalizedLocalDirection = Normalize(rollingMoveComponent->velocity);  // Inspector で設定した転がり方向を単位ベクトルへそろえる。
		if (Length(normalizedLocalDirection) <= kRollingMinimumDirectionLength) {
			continue;
		}

		Vector3 worldDirection = MakeLocalDirectionToWorld(gameObject, normalizedLocalDirection);
		worldDirection = Normalize(worldDirection);  // RollingMove の進行方向は Inspector の Vector3 をそのまま 3 次元で使う。
		if (Length(worldDirection) <= kRollingMinimumDirectionLength) {
			continue;
		}

		EditorComponent* rigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		const bool canDriveRigidBody =
			rigidBody != nullptr &&
			rigidBody->isActive &&
			!rigidBody->isKinematic &&
			physicsManager_ != nullptr;

		if (!canDriveRigidBody) {
			continue;
		}

		const float driveTorque = (std::max)(rollingMoveComponent->rollingTorque, 0.0f);  // タイヤや球を回す駆動トルク
		if (driveTorque <= kRollingMinimumTorque) {
			continue;
		}

		const float rollingRadius = GetRollingRadius(gameObject, *rollingMoveComponent);  // 半径は将来の速度表示や調整に使うためここで確定する
		if (rollingRadius <= 0.001f) {
			continue;
		}

		const Vector3 rollingAxis = MakeRollingAxis(gameObject, worldDirection);  // Y 成分を含む進行方向でも、外積から正しい回転軸を作る。
		if (Length(rollingAxis) <= kRollingMinimumDirectionLength) {
			continue;
		}

		const float horsepower = (std::max)(rollingMoveComponent->rollingHorsepower, 0.0f);  // 馬力はトルクを何 rad/s まで維持できるかの上限へ変換する
		const float maxAngularSpeed =
			(horsepower > 0.0f && driveTorque > kRollingMinimumTorque)
				? ((horsepower * kRollingHorsepowerToWatt) / driveTorque)
				: 0.0f;

		const float currentAxisAngularSpeed = DotVector3(rigidBody->angularVelocity, rollingAxis);  // 回転軸方向へどれだけ回っているかだけを見る
		if (maxAngularSpeed > 0.0f && std::fabs(currentAxisAngularSpeed) >= (maxAngularSpeed - kRollingAngularSpeedEpsilon)) {
			const Vector3 axisAngularVelocity = Multiply(currentAxisAngularSpeed, rollingAxis);
			const Vector3 otherAngularVelocity = SubtractVector3(rigidBody->angularVelocity, axisAngularVelocity);
			const float clampedAxisAngularSpeed = currentAxisAngularSpeed >= 0.0f ? maxAngularSpeed : -maxAngularSpeed;
			const Vector3 clampedAngularVelocity = Add(otherAngularVelocity, Multiply(clampedAxisAngularSpeed, rollingAxis));

			rigidBody->angularVelocity = clampedAngularVelocity;
			physicsManager_->SetAngularVelocity(gameObject.id, clampedAngularVelocity);
			continue;
		}

		const Vector3 driveTorqueVector = Multiply(driveTorque, rollingAxis);  // 線速度は入れず、回転トルクだけを与えて摩擦で前へ進ませる
		physicsManager_->AddTorque(gameObject.id, driveTorqueVector);
	}
}

void EditorRollingMoveManager::Draw() {
}

void EditorRollingMoveManager::Stop() {
	isStarted_ = false;
}
