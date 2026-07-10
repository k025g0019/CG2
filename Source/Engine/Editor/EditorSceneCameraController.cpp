#include "EditorSceneCameraController.h"

#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>

#pragma warning(disable : 5045)

void EditorSceneCameraController::Initialize() {
}

void EditorSceneCameraController::UpdateKeyboard(
	const uint8_t* keyState,
	const uint8_t* previousKeyState,
	bool isRuntimePlaying,
	Transforms& cameraTransform,
	Transforms& uvTransform,
	float moveSpeed,
	float rotateSpeed,
	float fastRate) {
	if (keyState == nullptr || previousKeyState == nullptr || isRuntimePlaying) {
		return;
	}

	const ImGuiIO& imguiIo = ImGui::GetIO(); // Inspector の数値入力やメニュー操作中は、Scene カメラ用ショートカットを止める。
	const bool isImGuiKeyboardEditing =
		imguiIo.WantTextInput ||
		imguiIo.WantCaptureKeyboard ||
		ImGui::IsAnyItemActive();
	if (isImGuiKeyboardEditing) {
		return;
	}

	Vector3 cameraForward = CalculateForward(cameraTransform);
	Vector3 cameraRight = CalculateRight(cameraTransform);
	float currentCameraMoveSpeed = moveSpeed;
	bool isCameraFastMove = keyState[DIK_LSHIFT] != 0 || keyState[DIK_RSHIFT] != 0;
	if (isCameraFastMove) {
		currentCameraMoveSpeed *= fastRate;
	}

	if (keyState[DIK_LEFT]) {
		cameraTransform.rotate.y -= rotateSpeed * 3.0f;
	}

	if (keyState[DIK_RIGHT]) {
		cameraTransform.rotate.y += rotateSpeed * 3.0f;
	}

	if (keyState[DIK_UP]) {
		cameraTransform.rotate.x -= rotateSpeed * 3.0f;
	}

	if (keyState[DIK_DOWN]) {
		cameraTransform.rotate.x += rotateSpeed * 3.0f;
	}
	cameraTransform.rotate.x = (std::clamp)(cameraTransform.rotate.x, -1.5f, 1.5f);

	if (keyState[DIK_A]) {
		cameraTransform.translate.x -= cameraRight.x * currentCameraMoveSpeed;
		cameraTransform.translate.z -= cameraRight.z * currentCameraMoveSpeed;
	}

	if (keyState[DIK_D]) {
		cameraTransform.translate.x += cameraRight.x * currentCameraMoveSpeed;
		cameraTransform.translate.z += cameraRight.z * currentCameraMoveSpeed;
	}

	if (keyState[DIK_Q]) {
		cameraTransform.translate.y += currentCameraMoveSpeed;
	}

	if (keyState[DIK_E]) {
		cameraTransform.translate.y -= currentCameraMoveSpeed;
	}

	if (keyState[DIK_W]) {
		cameraTransform.translate.x += cameraForward.x * currentCameraMoveSpeed;
		cameraTransform.translate.y += cameraForward.y * currentCameraMoveSpeed;
		cameraTransform.translate.z += cameraForward.z * currentCameraMoveSpeed;
	}

	if (keyState[DIK_S]) {
		cameraTransform.translate.x -= cameraForward.x * currentCameraMoveSpeed;
		cameraTransform.translate.y -= cameraForward.y * currentCameraMoveSpeed;
		cameraTransform.translate.z -= cameraForward.z * currentCameraMoveSpeed;
	}

	bool isReturnTrigger =
		((keyState[DIK_RETURN] != 0) && (previousKeyState[DIK_RETURN] == 0));
	if (isReturnTrigger) {
		uvTransform.translate = {0.0f, 0.0f, 0.0f};
		cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
		cameraTransform.translate = {0.0f, 0.0f, -5.0f};
	}
}

void EditorSceneCameraController::UpdateMouse(
	bool isSceneHovered,
	bool& isMiddleCameraDragging,
	bool& isRightCameraDragging,
	Transforms& cameraTransform,
	float rotateSpeed,
	float panSpeed,
	float wheelMoveSpeed) {
	if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
		isMiddleCameraDragging = true;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
		isMiddleCameraDragging = false;
	}

	if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		isRightCameraDragging = true;
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		isRightCameraDragging = false;
	}

	Vector3 cameraForward = CalculateForward(cameraTransform);
	Vector3 cameraRight = CalculateRight(cameraTransform);
	Vector3 cameraUp{
		cameraForward.y * cameraRight.z - cameraForward.z * cameraRight.y,
		cameraForward.z * cameraRight.x - cameraForward.x * cameraRight.z,
		cameraForward.x * cameraRight.y - cameraForward.y * cameraRight.x
	};

	if (isMiddleCameraDragging) {
		ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
		cameraTransform.translate.x -= cameraRight.x * mouseDelta.x * panSpeed;
		cameraTransform.translate.y -= cameraRight.y * mouseDelta.x * panSpeed;
		cameraTransform.translate.z -= cameraRight.z * mouseDelta.x * panSpeed;
		cameraTransform.translate.x += cameraUp.x * mouseDelta.y * panSpeed;
		cameraTransform.translate.y += cameraUp.y * mouseDelta.y * panSpeed;
		cameraTransform.translate.z += cameraUp.z * mouseDelta.y * panSpeed;
	}

	if (isRightCameraDragging) {
		ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
		cameraTransform.rotate.y += mouseDelta.x * rotateSpeed;
		cameraTransform.rotate.x += mouseDelta.y * rotateSpeed;
		cameraTransform.rotate.x = (std::clamp)(cameraTransform.rotate.x, -1.5f, 1.5f);
	}

	if (isSceneHovered && ImGui::GetIO().MouseWheel != 0.0f) {
		cameraForward = CalculateForward(cameraTransform);
		cameraTransform.translate.x += cameraForward.x * ImGui::GetIO().MouseWheel * wheelMoveSpeed;
		cameraTransform.translate.y += cameraForward.y * ImGui::GetIO().MouseWheel * wheelMoveSpeed;
		cameraTransform.translate.z += cameraForward.z * ImGui::GetIO().MouseWheel * wheelMoveSpeed;
	}
}

void EditorSceneCameraController::Draw() {
}

Vector3 EditorSceneCameraController::CalculateForward(const Transforms& cameraTransform) const {
	float pitchCos = std::cos(cameraTransform.rotate.x);
	return Vector3{
		std::sin(cameraTransform.rotate.y) * pitchCos,
		-std::sin(cameraTransform.rotate.x),
		std::cos(cameraTransform.rotate.y) * pitchCos
	};
}

Vector3 EditorSceneCameraController::CalculateRight(const Transforms& cameraTransform) const {
	return Vector3{
		std::cos(cameraTransform.rotate.y),
		0.0f,
		-std::sin(cameraTransform.rotate.y)
	};
}
