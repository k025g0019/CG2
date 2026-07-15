#pragma warning(disable : 4514)

#include "EditorInputManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace EditorSharedState;

namespace {
	constexpr float kEditorFloorY = 0.0f;  // ジャンプ可能判定で床として扱うワールド Y 座標
	struct PlayerInputActionDefinition {
		struct Vector2Binding {
			int32_t upKey = 0;  // 2DVector Composite の正方向 Y。
			int32_t downKey = 0;  // 2DVector Composite の負方向 Y。
			int32_t leftKey = 0;  // 2DVector Composite の負方向 X。
			int32_t rightKey = 0;  // 2DVector Composite の正方向 X。
			std::string bindingPath;  // Keyboard/2DVector(W,S,A,D) のような表示用 Path。
		};
		std::unordered_map<std::string, Vector2Binding> vector2Bindings;  // Move / Look / Steer など任意名の Vector2 Action。

		struct ButtonBinding {
			bool usesMouse = false;  // true ならキーではなくマウスボタンを使う。
			int32_t key = 0;  // キー入力の DirectInput 番号。
			int32_t mouseButton = -1;  // マウス入力のボタン番号。
			std::string bindingPath;  // Keyboard/Space または Mouse/LeftButton。
		};
		std::unordered_map<std::string, ButtonBinding> buttonBindings;  // Jump / Fire / Submit / LeftClick など任意の Button Action を保持する。
	};

	std::string MakeActionStateKey(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName) {
		return std::to_string(gameObjectId) + "|" + actionMapName + "|" + actionName;
	}

	std::vector<std::string> SplitByPipe(const std::string& line) {
		std::vector<std::string> elements;
		std::stringstream stream(line);
		std::string element;
		while (std::getline(stream, element, '|')) {
			elements.push_back(element);
		}

		return elements;
	}

	int32_t DikCodeFromName(const std::string& keyName) {
		static const std::unordered_map<std::string, int32_t> kDikCodes{
			{"Escape", 0x01}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05},
			{"5", 0x06}, {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B},
			{"Backspace", 0x0E}, {"Tab", 0x0F},
			{"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13}, {"T", 0x14}, {"Y", 0x15},
			{"U", 0x16}, {"I", 0x17}, {"O", 0x18}, {"P", 0x19},
			{"Enter", 0x1C}, {"Return", 0x1C}, {"LeftCtrl", 0x1D},
			{"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21}, {"G", 0x22}, {"H", 0x23},
			{"J", 0x24}, {"K", 0x25}, {"L", 0x26}, {"LeftShift", 0x2A},
			{"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E}, {"V", 0x2F}, {"B", 0x30},
			{"N", 0x31}, {"M", 0x32}, {"RightShift", 0x36}, {"LeftAlt", 0x38}, {"Space", 0x39},
			{"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E}, {"F5", 0x3F},
			{"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42}, {"F9", 0x43}, {"F10", 0x44},
			{"F11", 0x57}, {"F12", 0x58}, {"RightCtrl", 0x9D}, {"RightAlt", 0xB8},
			{"Home", 0xC7}, {"Up", 0xC8}, {"UpArrow", 0xC8}, {"PageUp", 0xC9},
			{"Left", 0xCB}, {"LeftArrow", 0xCB}, {"Right", 0xCD}, {"RightArrow", 0xCD},
			{"End", 0xCF}, {"Down", 0xD0}, {"DownArrow", 0xD0}, {"PageDown", 0xD1},
			{"Insert", 0xD2}, {"Delete", 0xD3}};
		const auto keyIterator = kDikCodes.find(keyName);

		return keyIterator != kDikCodes.end() ? keyIterator->second : 0;
	}

	int32_t MouseButtonFromName(const std::string& buttonName) {
		if (buttonName == "LeftButton") { return 0; }
		if (buttonName == "RightButton") { return 1; }
		if (buttonName == "MiddleButton") { return 2; }
		return -1;
	}

	bool IsMouseButtonPressed(int32_t mouseButtonIndex) {
		if (mouseButtonIndex < 0 || mouseButtonIndex >= 4) {
			return false;
		}

		return (g_mouseState.rgbButtons[mouseButtonIndex] & 0x80) != 0;
	}

	bool TryLoadPlayerInputActionsAsset(
		const std::string& filePath,
		const std::string& actionMapName,
		PlayerInputActionDefinition& actionDefinition) {
		std::ifstream file(filePath);
		if (!file.is_open()) {
			return false;
		}

		actionDefinition = {};
		std::string line;
		while (std::getline(file, line)) {
			if (line.empty() || line[0] == '#') {
				continue;
			}

			std::vector<std::string> elements = SplitByPipe(line);
			if (elements.size() < 6 || elements[0] != "Action" || elements[1] != actionMapName) {
				continue;
			}

			const std::string& actionName = elements[2];
			const std::string& valueType = elements[3];
			const std::string& bindingType = elements[4];

			if (valueType == "Vector2" && bindingType == "2DVector" && elements.size() >= 9) {
				PlayerInputActionDefinition::Vector2Binding vector2Binding{};
				vector2Binding.upKey = DikCodeFromName(elements[5]);
				vector2Binding.downKey = DikCodeFromName(elements[6]);
				vector2Binding.leftKey = DikCodeFromName(elements[7]);
				vector2Binding.rightKey = DikCodeFromName(elements[8]);
				vector2Binding.bindingPath =
					"Keyboard/2DVector(" + elements[5] + "," + elements[6] + "," + elements[7] + "," + elements[8] + ")";
				actionDefinition.vector2Bindings[actionName] = vector2Binding;
			}
			else if (valueType == "Button") {
				PlayerInputActionDefinition::ButtonBinding buttonBinding{};
				if (bindingType == "Key" && elements.size() >= 6) {
					buttonBinding.key = DikCodeFromName(elements[5]);
					buttonBinding.bindingPath = "Keyboard/" + elements[5];
				}
				else if (bindingType == "Mouse" && elements.size() >= 6) {
					buttonBinding.usesMouse = true;
					buttonBinding.mouseButton = MouseButtonFromName(elements[5]);
					buttonBinding.bindingPath = "Mouse/" + elements[5];
				}

				if (buttonBinding.key != 0 || buttonBinding.mouseButton >= 0) {
					actionDefinition.buttonBindings[actionName] = buttonBinding;
				}
			}
		}

		return !actionDefinition.vector2Bindings.empty() || !actionDefinition.buttonBindings.empty();
	}
}

void EditorInputManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Update で Input Component を探すため Scene を保持する
	consoleMessages_ = consoleMessages;  // PlayerInput の Fire などを Console へ流せるようにする
	actionPressedStates_.clear();
	actionVector2States_.clear();
	actionButtonStates_.clear();
}

void EditorInputManager::Update(const uint8_t* keyState, float deltaTime) {
	if (editorScene_ == nullptr) {
		return;
	}

	actionVector2States_.clear();  // Script 参照用の Action 値は毎フレーム再構築する。
	actionButtonStates_.clear();

	// カメラ相対移動のための方向ベクトル
	const auto& cam = g_cameraTransform;
	float pitchCos = std::cos(cam.rotate.x);
	Vector3 cameraForward{
		std::sin(cam.rotate.y) * pitchCos,
		0.0f,
		std::cos(cam.rotate.y) * pitchCos
	};
	float fwdLen = std::sqrt(cameraForward.x * cameraForward.x + cameraForward.z * cameraForward.z);
	if (fwdLen > 0.0f) {
		cameraForward.x /= fwdLen;
		cameraForward.z /= fwdLen;
	}
	Vector3 cameraRight{
		std::cos(cam.rotate.y),
		0.0f,
		-std::sin(cam.rotate.y)
	};

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		// 非アクティブな GameObject は入力で動かさない
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* playerInput =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::PlayerInput);
		EditorComponent* input =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Input);

		EditorComponent* rigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		bool canDriveRigidBody =
			rigidBody != nullptr &&
			rigidBody->isActive &&
			!rigidBody->isKinematic;  // Rigidbody が有効な時は Transform 直書きではなく速度で Jolt に渡す
		EditorComponent* characterController =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CharacterController);
		bool canDriveCharacterController =
			!canDriveRigidBody &&
			characterController != nullptr &&
			characterController->isActive;  // Rigidbody なし CharacterController は Jolt CharacterVirtual の速度を更新する

		bool hasRuntimeInput = false;  // 旧 Input または新 PlayerInput のどちらかが動作していれば true
		bool appliesBuiltInMovement = false;  // 旧 Input Component の互換移動を実行する場合だけ true。
		Vector3 moveDirection{0.0f, 0.0f, 0.0f};  // 実際に移動へ使う入力方向
		bool isJumpPressed = false;  // Jump Action または旧 Input Jump が押されていれば true

		if (playerInput != nullptr && playerInput->isActive && !playerInput->assetPath.empty()) {
			PlayerInputActionDefinition actionDefinition{};
			if (TryLoadPlayerInputActionsAsset(playerInput->assetPath, playerInput->inputActionMapName, actionDefinition)) {
				hasRuntimeInput = true;

				for (const auto& vector2BindingPair : actionDefinition.vector2Bindings) {
					const std::string& actionName = vector2BindingPair.first;
					const PlayerInputActionDefinition::Vector2Binding& vector2Binding = vector2BindingPair.second;
					float moveInputX = 0.0f;  // DLL Script から参照する Unity 風 Move Vector2 の X。
					float moveInputY = 0.0f;  // DLL Script から参照する Unity 風 Move Vector2 の Y。

					if (IsKeyPressed(keyState, vector2Binding.upKey)) {
						moveInputY += 1.0f;
					}

					if (IsKeyPressed(keyState, vector2Binding.downKey)) {
						moveInputY -= 1.0f;
					}

					if (IsKeyPressed(keyState, vector2Binding.leftKey)) {
						moveInputX -= 1.0f;
					}

					if (IsKeyPressed(keyState, vector2Binding.rightKey)) {
						moveInputX += 1.0f;
					}

					ActionVector2State moveState{};
					moveState.x = moveInputX;
					moveState.y = moveInputY;
					moveState.isActive = true;
					moveState.bindingPath = vector2Binding.bindingPath;
					actionVector2States_[MakeActionStateKey(gameObject.id, playerInput->inputActionMapName, actionName)] = moveState;
				}

				for (const auto& buttonBindingPair : actionDefinition.buttonBindings) {
					const std::string& actionName = buttonBindingPair.first;
					const PlayerInputActionDefinition::ButtonBinding& buttonBinding = buttonBindingPair.second;
					const bool isPressed = buttonBinding.usesMouse
						                       ? IsMouseButtonPressed(buttonBinding.mouseButton)
						                       : IsKeyPressed(keyState, buttonBinding.key);
					const std::string actionStateKey =
						MakeActionStateKey(gameObject.id, playerInput->inputActionMapName, actionName);
					const bool wasPressed = actionPressedStates_[actionStateKey];

					ActionButtonState buttonState{};
					buttonState.isPressed = isPressed;
					buttonState.wasJustPressed = isPressed && !wasPressed;
					buttonState.wasJustReleased = !isPressed && wasPressed;
					buttonState.isActive = true;
					buttonState.bindingPath = buttonBinding.bindingPath;
					actionButtonStates_[actionStateKey] = buttonState;
					actionPressedStates_[actionStateKey] = isPressed;

				}
			}
		}

		if (!hasRuntimeInput && input != nullptr && input->isActive) {
			hasRuntimeInput = true;
			appliesBuiltInMovement = true;

			// 旧 Input Component は互換維持のため残し、PlayerInput がない場合だけ使う
			if (IsKeyPressed(keyState, input->inputForwardKey)) {
				moveDirection.x += cameraForward.x;
				moveDirection.z += cameraForward.z;
			}
			if (IsKeyPressed(keyState, input->inputBackKey)) {
				moveDirection.x -= cameraForward.x;
				moveDirection.z -= cameraForward.z;
			}
			if (IsKeyPressed(keyState, input->inputLeftKey)) {
				moveDirection.x -= cameraRight.x;
				moveDirection.z -= cameraRight.z;
			}
			if (IsKeyPressed(keyState, input->inputRightKey)) {
				moveDirection.x += cameraRight.x;
				moveDirection.z += cameraRight.z;
			}
			isJumpPressed = IsKeyPressed(keyState, input->inputJumpKey);

			if ((g_mouseState.rgbButtons[1] & 0x80) != 0) {
				float sens = input->inputMouseSensitivity * 0.003f;
				float invert = input->inputInvertY ? -1.0f : 1.0f;
				gameObject.rotate.y += static_cast<float>(g_mouseState.lX) * sens;
				gameObject.rotate.x += static_cast<float>(g_mouseState.lY) * sens * invert;
				constexpr float kPitchLimit = 1.553f;
				if (gameObject.rotate.x > kPitchLimit) gameObject.rotate.x = kPitchLimit;
				if (gameObject.rotate.x < -kPitchLimit) gameObject.rotate.x = -kPitchLimit;
			}
		}

		if (!hasRuntimeInput || !appliesBuiltInMovement) {
			continue;
		}

		float moveLength = std::sqrt(moveDirection.x * moveDirection.x + moveDirection.z * moveDirection.z);
		if (moveLength > 0.0f) {
			const float configuredMoveSpeed =
				playerInput != nullptr && playerInput->isActive ? playerInput->inputMoveSpeed :
				(input != nullptr ? input->inputMoveSpeed : 0.0f);
			float normalizedSpeed = configuredMoveSpeed / moveLength;  // 斜め移動でも速度が増えないよう方向ベクトルを正規化する係数
			if (canDriveRigidBody) {
				rigidBody->velocity.x = moveDirection.x * normalizedSpeed;
				rigidBody->velocity.z = moveDirection.z * normalizedSpeed;
			}
			else if (canDriveCharacterController) {
				characterController->velocity.x = moveDirection.x * normalizedSpeed;
				characterController->velocity.z = moveDirection.z * normalizedSpeed;
			}
			else {
				float moveScale = normalizedSpeed * deltaTime;  // 物理を使わない GameObject は従来通り Transform を直接動かす
				gameObject.translate.x += moveDirection.x * moveScale;
				gameObject.translate.z += moveDirection.z * moveScale;
			}
		}
		else if (canDriveRigidBody) {
			rigidBody->velocity.x = 0.0f;  // 入力がない時は操作用 Rigidbody の水平移動だけ止め、Y 速度は重力やジャンプに任せる
			rigidBody->velocity.z = 0.0f;
		}
		else if (canDriveCharacterController) {
			characterController->velocity.x = 0.0f;  // CharacterVirtual も水平入力がない時は停止し、落下速度だけ維持する
			characterController->velocity.z = 0.0f;
		}

		// Jump 入力は RigidBody の Y 速度へ反映する
		if (canDriveRigidBody && isJumpPressed) {
			float bottomY = gameObject.translate.y + GetColliderBottomOffset(gameObject);

			// 床付近にいる時だけジャンプ速度を与える
			if (bottomY <= kEditorFloorY + 0.01f) {
				rigidBody->velocity.y = 5.0f;
			}
		}
		else if (canDriveCharacterController && isJumpPressed) {
			float bottomY = gameObject.translate.y + GetColliderBottomOffset(gameObject);

			// CharacterVirtual も床付近だけ上向き速度を与える
			if (bottomY <= kEditorFloorY + 0.01f) {
				characterController->velocity.y = 5.0f;
			}
		}
	}
}

void EditorInputManager::Draw() {
}

bool EditorInputManager::TryGetActionVector2(
	int32_t gameObjectId,
	const std::string& actionMapName,
	const std::string& actionName,
	float& x,
	float& y) const {
	const auto actionStateIt = actionVector2States_.find(MakeActionStateKey(gameObjectId, actionMapName, actionName));
	if (actionStateIt == actionVector2States_.end() || !actionStateIt->second.isActive) {
		x = 0.0f;
		y = 0.0f;
		return false;
	}

	x = actionStateIt->second.x;
	y = actionStateIt->second.y;
	return true;
}

bool EditorInputManager::IsActionPressed(
	int32_t gameObjectId,
	const std::string& actionMapName,
	const std::string& actionName) const {
	const auto actionStateIt = actionButtonStates_.find(MakeActionStateKey(gameObjectId, actionMapName, actionName));
	if (actionStateIt == actionButtonStates_.end() || !actionStateIt->second.isActive) {
		return false;
	}

	return actionStateIt->second.isPressed;
}

bool EditorInputManager::WasActionJustPressed(
	int32_t gameObjectId,
	const std::string& actionMapName,
	const std::string& actionName) const {
	const auto actionStateIt = actionButtonStates_.find(MakeActionStateKey(gameObjectId, actionMapName, actionName));
	if (actionStateIt == actionButtonStates_.end() || !actionStateIt->second.isActive) {
		return false;
	}

	return actionStateIt->second.wasJustPressed;
}

bool EditorInputManager::WasActionJustReleased(
	int32_t gameObjectId,
	const std::string& actionMapName,
	const std::string& actionName) const {
	const auto actionStateIt = actionButtonStates_.find(MakeActionStateKey(gameObjectId, actionMapName, actionName));
	if (actionStateIt == actionButtonStates_.end() || !actionStateIt->second.isActive) {
		return false;
	}

	return actionStateIt->second.wasJustReleased;
}

std::string EditorInputManager::GetActionBindingPath(
	int32_t gameObjectId,
	const std::string& actionMapName,
	const std::string& actionName) const {
	const std::string actionStateKey = MakeActionStateKey(gameObjectId, actionMapName, actionName);
	const auto buttonStateIt = actionButtonStates_.find(actionStateKey);
	if (buttonStateIt != actionButtonStates_.end() && buttonStateIt->second.isActive) {
		return buttonStateIt->second.bindingPath;
	}

	const auto vectorStateIt = actionVector2States_.find(actionStateKey);
	if (vectorStateIt != actionVector2States_.end() && vectorStateIt->second.isActive) {
		return vectorStateIt->second.bindingPath;
	}

	return "";
}

bool EditorInputManager::IsKeyPressed(const uint8_t* keyState, int32_t keyIndex) const {
	// DirectInput のキー配列は 0 から 255 までなので範囲外は未入力扱い
	if (keyState == nullptr || keyIndex < 0 || keyIndex >= 256) {
		return false;
	}

	// DirectInput は 0 以外ならキー押下中
	return keyState[keyIndex] != 0;
}

float EditorInputManager::GetColliderBottomOffset(const EditorGameObject& gameObject) const {
	// BoxCollider の下端は中心 Y から高さの半分を引いた位置
	const EditorComponent* boxCollider =
		EditorComponentUtility::FindComponent(gameObject, EditorComponentType::BoxCollider);
	if (boxCollider != nullptr && boxCollider->isActive) {
		return boxCollider->colliderCenter.y - boxCollider->colliderSize.y * 0.5f;
	}

	// SphereCollider の下端は中心 Y から半径を引いた位置
	const EditorComponent* sphereCollider =
		EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SphereCollider);
	if (sphereCollider != nullptr && sphereCollider->isActive) {
		return sphereCollider->colliderCenter.y - sphereCollider->colliderRadius;
	}

	const EditorComponent* capsuleCollider =
		EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CapsuleCollider);
	if (capsuleCollider != nullptr && capsuleCollider->isActive) {
		return capsuleCollider->colliderCenter.y - capsuleCollider->colliderSize.y * 0.5f;
	}

	const EditorComponent* characterController =
		EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CharacterController);
	if (characterController != nullptr && characterController->isActive) {
		return characterController->colliderCenter.y - characterController->colliderSize.y * 0.5f;
	}

	// Collider がない場合は仮の半身分として -0.5f を返す
	return -0.5f;
}
