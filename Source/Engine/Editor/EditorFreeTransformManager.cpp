#include "EditorFreeTransformManager.h"
#include "EditorComponentUtility.h"
#include "Vector&Matrix.h"

#include <cmath>

void EditorFreeTransformManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
	isStarted_ = false;
}

void EditorFreeTransformManager::Start() {
	isStarted_ = true;
}

void EditorFreeTransformManager::Update(float deltaTime, const uint8_t* keyState) {
	(void)keyState;
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;

		auto* ft = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::FreeTransform);
		if (ft == nullptr || !ft->isActive) continue;
		if (ft->freeMoveSpeed <= 0.0f && ft->freeRotateSpeed <= 0.0f) continue;

		Vector3 moveInput = ft->velocity;
		float moveSpeed = ft->freeMoveSpeed;
		float rotateSpeed = ft->freeRotateSpeed;
		Vector3 rotInput = ft->freeRotationInput;
		int32_t moveAxes = ft->freeMoveAxes;
		int32_t rotAxes = ft->freeRotateAxes;
		bool localSpace = ft->freeUseLocalSpace;

		Vector3 moveDelta = Multiply(moveSpeed * deltaTime, moveInput);
		Vector3 rotDelta = Multiply(rotateSpeed * deltaTime * (3.14159265f / 180.0f), rotInput);

		if (localSpace) {
			Matrix4x4 rotMat = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, gameObject.rotate, {0.0f, 0.0f, 0.0f});
			moveDelta = Transform(moveDelta, rotMat);
		}

		if (moveAxes & 1) gameObject.translate.x += moveDelta.x;
		if (moveAxes & 2) gameObject.translate.y += moveDelta.y;
		if (moveAxes & 4) gameObject.translate.z += moveDelta.z;

		if (rotAxes & 1) gameObject.rotate.x += rotDelta.x;
		if (rotAxes & 2) gameObject.rotate.y += rotDelta.y;
		if (rotAxes & 4) gameObject.rotate.z += rotDelta.z;
	}
}

void EditorFreeTransformManager::Stop() {
	isStarted_ = false;
}
