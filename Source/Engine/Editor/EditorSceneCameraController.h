#pragma once

#include "EditorCommonTypes.h"

#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSceneCameraController {
public:
	void Initialize();
	void UpdateKeyboard(
		const uint8_t* keyState,
		const uint8_t* previousKeyState,
		bool isRuntimePlaying,
		Transforms& cameraTransform,
		Transforms& uvTransform,
		float moveSpeed,
		float rotateSpeed,
		float fastRate);
	void UpdateMouse(
		bool isSceneHovered,
		bool& isMiddleCameraDragging,
		bool& isRightCameraDragging,
		Transforms& cameraTransform,
		float rotateSpeed,
		float panSpeed,
		float wheelMoveSpeed);
	void Draw();

private:
	Vector3 CalculateForward(const Transforms& cameraTransform) const;
	Vector3 CalculateRight(const Transforms& cameraTransform) const;
};

#pragma warning(pop)
