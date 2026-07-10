#include "EditorLocalMoveManager.h"

#include "EditorComponentUtility.h"
#include "Vector&Matrix.h"

#include <cmath>

namespace {
	Vector3 MakeLocalDirectionToWorld(const EditorGameObject& gameObject, const Vector3& localDirection) {
		const Matrix4x4 rotateMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			gameObject.rotate,
			{0.0f, 0.0f, 0.0f});  // 平行移動を含まない回転だけの行列で、ローカル軸方向をワールド方向へ変換する。
		return Transform(localDirection, rotateMatrix);
	}

	Vector3 MakeHorizontalDirection(const Vector3& direction) {
		Vector3 horizontalDirection = direction;  // 重力で床へ押し付ける移動にならないよう、水平移動だけに使う向きを作る。
		horizontalDirection.y = 0.0f;
		return Normalize(horizontalDirection);
	}
}

void EditorLocalMoveManager::Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager) {
	editorScene_ = editorScene;  // Play 中に Component を検索する Scene を保持する。
	physicsManager_ = physicsManager;  // Dynamic Rigidbody の速度を Jolt へ渡す先を保持する。
	isStarted_ = false;
}

void EditorLocalMoveManager::Start() {
	isStarted_ = true;
}

void EditorLocalMoveManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* localMoveComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::LocalMove);
		if (localMoveComponent == nullptr || !localMoveComponent->isActive) {
			continue;
		}

		const Vector3 normalizedLocalDirection = Normalize(localMoveComponent->velocity);  // Inspector で設定したローカル方向を単位ベクトルへそろえる。
		if (Length(normalizedLocalDirection) <= 0.0f) {
			continue;
		}

		Vector3 worldDirection = MakeLocalDirectionToWorld(gameObject, normalizedLocalDirection);
		if (std::fabs(normalizedLocalDirection.y) <= 0.0f) {
			const Vector3 horizontalDirection = MakeHorizontalDirection(worldDirection);
			if (Length(horizontalDirection) > 0.0f) {
				worldDirection = horizontalDirection;  // ローカル Y を使わない移動は、重力と干渉しないよう水平面上の向きに補正する。
			}
		}

		const Vector3 worldVelocity = Multiply(localMoveComponent->inputMoveSpeed, worldDirection);  // 速度は既存の inputMoveSpeed を再利用する。

		EditorComponent* rigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		const bool canDriveRigidBody =
			rigidBody != nullptr &&
			rigidBody->isActive &&
			!rigidBody->isKinematic &&
			physicsManager_ != nullptr;

		if (canDriveRigidBody) {
			rigidBody->velocity.x = worldVelocity.x;  // ローカル X/Z の移動は、回転後のワールド速度をそのまま水平速度として反映する。
			rigidBody->velocity.z = worldVelocity.z;

			if (std::fabs(normalizedLocalDirection.y) > 0.0f) {
				rigidBody->velocity.y = worldVelocity.y;  // ローカル Y を使う時だけ、上下速度も上書きする。
			}

			physicsManager_->SetVelocity(gameObject.id, rigidBody->velocity);
			continue;
		}

		gameObject.translate.x += worldVelocity.x * deltaTime;
		gameObject.translate.y += worldVelocity.y * deltaTime;
		gameObject.translate.z += worldVelocity.z * deltaTime;
	}
}

void EditorLocalMoveManager::Draw() {
}

void EditorLocalMoveManager::Stop() {
	isStarted_ = false;
}
