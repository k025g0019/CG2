#include "BoxCollision.h"

#include "EditorComponentUtility.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	constexpr float kEditorFloorY = 0.0f;  // エディター物理で床として扱うワールド Y 座標
	constexpr float kMinimumColliderSize = 0.001f;  // 0 サイズ Collider で押し戻し計算が壊れないようにする下限

	struct BoxCollisionBody {
		EditorGameObject* gameObject;  // 押し戻しで座標を書き換える対象 GameObject
		EditorComponent* collider;  // 中心とサイズを持つ BoxCollider
		EditorComponent* rigidBody;  // 速度や Kinematic を持つ RigidBody。nullptr なら静的物体扱い
		Vector3 center;  // Collider 中心のワールド座標
		Vector3 halfSize;  // Collider のワールド半サイズ
	};

	float GetScaledSize(float colliderSize, float objectScale) {
		// Transform の Scale も Collider サイズへ反映する
		float scaledSize = colliderSize * std::fabs(objectScale);
		return (std::max)(scaledSize, kMinimumColliderSize);
	}

	bool IsDynamicBody(const EditorComponent* rigidBody) {
		// RigidBody があり、有効で、Kinematic でないものだけ物理で動かす
		return rigidBody != nullptr && rigidBody->isActive && !rigidBody->isKinematic;
	}

	bool TryCreateBoxBody(EditorGameObject& gameObject, BoxCollisionBody& body) {
		if (!gameObject.isActive) {
			return false;
		}

		EditorComponent* boxCollider =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::BoxCollider);
		if (boxCollider == nullptr || !boxCollider->isActive || boxCollider->isTrigger) {
			return false;
		}

		body.gameObject = &gameObject;
		body.collider = boxCollider;
		body.rigidBody = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		body.center = {
			gameObject.translate.x + boxCollider->colliderCenter.x,
			gameObject.translate.y + boxCollider->colliderCenter.y,
			gameObject.translate.z + boxCollider->colliderCenter.z};
		body.halfSize = {
			GetScaledSize(boxCollider->colliderSize.x, gameObject.scale.x) * 0.5f,
			GetScaledSize(boxCollider->colliderSize.y, gameObject.scale.y) * 0.5f,
			GetScaledSize(boxCollider->colliderSize.z, gameObject.scale.z) * 0.5f};

		return true;
	}

	void ApplyAxisVelocityResponse(EditorComponent* rigidBody, float axisDirection, float& velocityAxis) {
		if (!IsDynamicBody(rigidBody)) {
			return;
		}

		// 押し戻し方向と逆向きに進んでいる時だけ、めり込み続けないよう速度を止める
		if (velocityAxis * axisDirection < 0.0f) {
			velocityAxis = -velocityAxis * rigidBody->bounciness;
		}
	}

	void ResolveBoxPair(BoxCollisionBody& firstBody, BoxCollisionBody& secondBody) {
		float distanceX = secondBody.center.x - firstBody.center.x;  // 2 つの中心差。押し戻し方向の判定に使う
		float distanceY = secondBody.center.y - firstBody.center.y;
		float distanceZ = secondBody.center.z - firstBody.center.z;
		float overlapX = firstBody.halfSize.x + secondBody.halfSize.x - std::fabs(distanceX);  // X 軸のめり込み量
		float overlapY = firstBody.halfSize.y + secondBody.halfSize.y - std::fabs(distanceY);  // Y 軸のめり込み量
		float overlapZ = firstBody.halfSize.z + secondBody.halfSize.z - std::fabs(distanceZ);  // Z 軸のめり込み量

		// どれか 1 軸でも重なっていなければ AABB は接触していない
		if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f) {
			return;
		}

		bool isFirstDynamic = IsDynamicBody(firstBody.rigidBody);  // 動的な側だけ Transform を押し戻す
		bool isSecondDynamic = IsDynamicBody(secondBody.rigidBody);
		if (!isFirstDynamic && !isSecondDynamic) {
			return;
		}

		float firstMoveRate = 0.0f;  // 1.0f なら firstBody が全めり込み量を戻す
		float secondMoveRate = 0.0f;  // 1.0f なら secondBody が全めり込み量を戻す
		if (isFirstDynamic && isSecondDynamic) {
			firstMoveRate = 0.5f;
			secondMoveRate = 0.5f;
		}
		else if (isFirstDynamic) {
			firstMoveRate = 1.0f;
		}
		else if (isSecondDynamic) {
			secondMoveRate = 1.0f;
		}

		// 一番浅い軸だけ戻すことで、箱同士が斜めに不自然に飛ばないようにする
		if (overlapX <= overlapY && overlapX <= overlapZ) {
			float direction = distanceX >= 0.0f ? -1.0f : 1.0f;  // firstBody を押し戻す X 方向
			firstBody.gameObject->translate.x += direction * overlapX * firstMoveRate;
			secondBody.gameObject->translate.x -= direction * overlapX * secondMoveRate;
			ApplyAxisVelocityResponse(firstBody.rigidBody, direction, firstBody.rigidBody->velocity.x);
			ApplyAxisVelocityResponse(secondBody.rigidBody, -direction, secondBody.rigidBody->velocity.x);
		}
		else if (overlapY <= overlapX && overlapY <= overlapZ) {
			float direction = distanceY >= 0.0f ? -1.0f : 1.0f;  // firstBody を押し戻す Y 方向
			firstBody.gameObject->translate.y += direction * overlapY * firstMoveRate;
			secondBody.gameObject->translate.y -= direction * overlapY * secondMoveRate;
			ApplyAxisVelocityResponse(firstBody.rigidBody, direction, firstBody.rigidBody->velocity.y);
			ApplyAxisVelocityResponse(secondBody.rigidBody, -direction, secondBody.rigidBody->velocity.y);
		}
		else {
			float direction = distanceZ >= 0.0f ? -1.0f : 1.0f;  // firstBody を押し戻す Z 方向
			firstBody.gameObject->translate.z += direction * overlapZ * firstMoveRate;
			secondBody.gameObject->translate.z -= direction * overlapZ * secondMoveRate;
			ApplyAxisVelocityResponse(firstBody.rigidBody, direction, firstBody.rigidBody->velocity.z);
			ApplyAxisVelocityResponse(secondBody.rigidBody, -direction, secondBody.rigidBody->velocity.z);
		}
	}
}

void BoxCollision::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;  // Update で全 GameObject を調べるため Scene 参照を保持する
}

void BoxCollision::Update() {
	if (editorScene_ == nullptr) {
		return;
	}

	std::vector<BoxCollisionBody> boxBodies;  // Box 同士の接触解決に使う一時配列

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		// 床判定に必要な BoxCollider
		EditorComponent* boxCollider =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::BoxCollider);
		// 反発速度を書き戻すための RigidBody
		EditorComponent* rigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);

		// Collider / Rigidbody がない、無効、Trigger の場合は床衝突を行わない
		if (boxCollider == nullptr || rigidBody == nullptr || !boxCollider->isActive || boxCollider->isTrigger) {
			continue;
		}

		float bottomY = gameObject.translate.y + boxCollider->colliderCenter.y - boxCollider->colliderSize.y * 0.5f;  // Box の下端。中心座標から高さの半分を引く

		// 下端が床を抜けた分だけ、GameObject を上へ戻す
		if (bottomY < kEditorFloorY) {
			gameObject.translate.y += kEditorFloorY - bottomY;

			// 落下中だけ Y 速度を反転し、bounciness で反発量を調整する
			if (rigidBody->velocity.y < 0.0f) {
				rigidBody->velocity.y = -rigidBody->velocity.y * rigidBody->bounciness;
			}
		}

		BoxCollisionBody body{};
		if (TryCreateBoxBody(gameObject, body)) {
			boxBodies.push_back(body);
		}
	}

	for (int32_t firstIndex = 0; firstIndex < static_cast<int32_t>(boxBodies.size()); firstIndex++) {
		for (int32_t secondIndex = firstIndex + 1; secondIndex < static_cast<int32_t>(boxBodies.size()); secondIndex++) {
			ResolveBoxPair(
				boxBodies[static_cast<size_t>(firstIndex)],
				boxBodies[static_cast<size_t>(secondIndex)]);
		}
	}
}

void BoxCollision::Draw() {
}
