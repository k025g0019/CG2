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
	constexpr size_t kMaxInputConsoleMessages = 240;  // Console を入力ログで無制限に増やさない上限

	struct PlayerInputActionDefinition {
		bool hasMove = false;  // Move Action が定義されているなら true
		int32_t moveUpKey = 0;  // 2DVector Composite の Up
		int32_t moveDownKey = 0;  // 2DVector Composite の Down
		int32_t moveLeftKey = 0;  // 2DVector Composite の Left
		int32_t moveRightKey = 0;  // 2DVector Composite の Right
		struct ButtonBinding {
			bool usesMouse = false;  // true ならキーではなくマウスボタンを使う。
			int32_t key = 0;  // キー入力の DirectInput 番号。
			int32_t mouseButton = -1;  // マウス入力のボタン番号。
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
		if (keyName == "W") { return 0x11; }
		if (keyName == "A") { return 0x1E; }
		if (keyName == "S") { return 0x1F; }
		if (keyName == "D") { return 0x20; }
		if (keyName == "Q") { return 0x10; }
		if (keyName == "E") { return 0x12; }
		if (keyName == "R") { return 0x13; }
		if (keyName == "F") { return 0x21; }
		if (keyName == "Space") { return 0x39; }
		if (keyName == "LeftShift") { return 0x2A; }
		if (keyName == "RightShift") { return 0x36; }
		if (keyName == "LeftCtrl") { return 0x1D; }
		if (keyName == "UpArrow") { return 0xC8; }
		if (keyName == "DownArrow") { return 0xD0; }
		if (keyName == "LeftArrow") { return 0xCB; }
		if (keyName == "RightArrow") { return 0xCD; }
		return 0;
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

			if (actionName == "Move" && valueType == "Vector2" && bindingType == "2DVector" && elements.size() >= 9) {
				actionDefinition.hasMove = true;
				actionDefinition.moveUpKey = DikCodeFromName(elements[5]);
				actionDefinition.moveDownKey = DikCodeFromName(elements[6]);
				actionDefinition.moveLeftKey = DikCodeFromName(elements[7]);
				actionDefinition.moveRightKey = DikCodeFromName(elements[8]);
			}
			else if (valueType == "Button") {
				PlayerInputActionDefinition::ButtonBinding buttonBinding{};
				if (bindingType == "Key" && elements.size() >= 6) {
					buttonBinding.key = DikCodeFromName(elements[5]);
				}
				else if (bindingType == "Mouse" && elements.size() >= 6) {
					buttonBinding.usesMouse = true;
					buttonBinding.mouseButton = MouseButtonFromName(elements[5]);
				}

				if (buttonBinding.key != 0 || buttonBinding.mouseButton >= 0) {
					actionDefinition.buttonBindings[actionName] = buttonBinding;
				}
			}
		}

		return actionDefinition.hasMove || !actionDefinition.buttonBindings.empty();
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
		Vector3 moveDirection{0.0f, 0.0f, 0.0f};  // 実際に移動へ使う入力方向
		bool isJumpPressed = false;  // Jump Action または旧 Input Jump が押されていれば true
		bool isFirePressed = false;  // Fire Action が押されていれば true

		if (playerInput != nullptr && playerInput->isActive && !playerInput->assetPath.empty()) {
			PlayerInputActionDefinition actionDefinition{};
			if (TryLoadPlayerInputActionsAsset(playerInput->assetPath, playerInput->inputActionMapName, actionDefinition)) {
				hasRuntimeInput = true;

				if (actionDefinition.hasMove && !playerInput->inputMoveEventName.empty()) {
					float moveInputX = 0.0f;  // DLL Script から参照する Unity 風 Move Vector2 の X。
					float moveInputY = 0.0f;  // DLL Script から参照する Unity 風 Move Vector2 の Y。
					if (IsKeyPressed(keyState, actionDefinition.moveUpKey)) {
						moveDirection.x += cameraForward.x;
						moveDirection.z += cameraForward.z;
						moveInputY += 1.0f;
					}
					if (IsKeyPressed(keyState, actionDefinition.moveDownKey)) {
						moveDirection.x -= cameraForward.x;
						moveDirection.z -= cameraForward.z;
						moveInputY -= 1.0f;
					}
					if (IsKeyPressed(keyState, actionDefinition.moveLeftKey)) {
						moveDirection.x -= cameraRight.x;
						moveDirection.z -= cameraRight.z;
						moveInputX -= 1.0f;
					}
					if (IsKeyPressed(keyState, actionDefinition.moveRightKey)) {
						moveDirection.x += cameraRight.x;
						moveDirection.z += cameraRight.z;
						moveInputX += 1.0f;
					}

					ActionVector2State moveState{};
					moveState.x = moveInputX;
					moveState.y = moveInputY;
					moveState.isActive = true;
					actionVector2States_[MakeActionStateKey(gameObject.id, playerInput->inputActionMapName, "Move")] = moveState;
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
					buttonState.isActive = true;
					actionButtonStates_[actionStateKey] = buttonState;
					actionPressedStates_[actionStateKey] = isPressed;

					if (actionName == "Jump" && !playerInput->inputJumpEventName.empty()) {
						isJumpPressed = isPressed;
					}

					if (actionName == "Fire" && !playerInput->inputFireEventName.empty()) {
						isFirePressed = isPressed;
					}
				}

				if (WasActionJustPressed(gameObject.id, playerInput->inputActionMapName, "Fire")) {
					PushConsoleMessage("入力: " + gameObject.name + " が " + playerInput->inputFireEventName + " を実行");
				}
			}
		}

		if (!hasRuntimeInput && input != nullptr && input->isActive) {
			hasRuntimeInput = true;

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

		if (!hasRuntimeInput) {
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

void EditorInputManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ == nullptr) {
		return;
	}

	consoleMessages_->push_back(message);
	if (consoleMessages_->size() > kMaxInputConsoleMessages) {
		consoleMessages_->erase(consoleMessages_->begin());
	}
}
