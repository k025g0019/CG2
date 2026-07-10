#include "SphereCollision.h"

#include "EditorComponentUtility.h"

namespace {
	constexpr float kEditorFloorY = 0.0f;  // エディター物理で床として扱うワールド Y 座標
}

void SphereCollision::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;  // Update で全 GameObject を調べるため Scene 参照を保持する
}

void SphereCollision::Update() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		// 床判定に必要な SphereCollider
		EditorComponent* sphereCollider =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SphereCollider);
		// 反発速度を書き戻すための RigidBody
		EditorComponent* rigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);

		// Collider / Rigidbody がない、無効、Trigger の場合は床衝突を行わない
		if (sphereCollider == nullptr ||
			rigidBody == nullptr ||
			!sphereCollider->isActive ||
			sphereCollider->isTrigger) {
			continue;
		}

		float bottomY = gameObject.translate.y + sphereCollider->colliderCenter.y - sphereCollider->colliderRadius;  // Sphere の下端。中心座標から半径を引く

		// 下端が床を抜けた分だけ、GameObject を上へ戻す
		if (bottomY < kEditorFloorY) {
			gameObject.translate.y += kEditorFloorY - bottomY;

			// 落下中だけ Y 速度を反転し、bounciness で反発量を調整する
			if (rigidBody->velocity.y < 0.0f) {
				rigidBody->velocity.y = -rigidBody->velocity.y * rigidBody->bounciness;
			}
		}
	}
}

void SphereCollision::Draw() {
}
