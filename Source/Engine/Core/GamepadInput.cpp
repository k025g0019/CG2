#include "GamepadInput.h"

#include "ProjectSettings.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <Xinput.h>
#pragma warning(pop)

#include <algorithm>
#include <chrono>
#include <cmath>

#pragma comment(lib, "xinput.lib")

namespace {
	// 未接続Slotの再検出間隔。XInputGetStateは未接続Slotに対して重いため、毎フレームは叩かない。
	constexpr float kDisconnectedPollIntervalSeconds = 1.0f;

	// Stick の生値(-32768..32767)を、Dead Zone を考慮した -1..1 へ変換する。
	// 円形Dead Zoneで、境界のすぐ外側が 0 から滑らかに立ち上がるようにする。
	void ApplyStickDeadZone(float rawX, float rawY, float deadZone, float sensitivity, float& outX, float& outY) {
		constexpr float kStickRange = 32767.0f;
		float normalizedX = (std::clamp)(rawX / kStickRange, -1.0f, 1.0f);
		float normalizedY = (std::clamp)(rawY / kStickRange, -1.0f, 1.0f);
		const float magnitude = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);

		if (magnitude <= deadZone || magnitude <= 0.0001f) {
			outX = 0.0f;
			outY = 0.0f;
			return;
		}

		// Dead Zone の外側を 0..1 へ引き伸ばす。
		const float scaledMagnitude =
			(std::min)((magnitude - deadZone) / (1.0f - deadZone), 1.0f) * sensitivity;
		outX = (normalizedX / magnitude) * scaledMagnitude;
		outY = (normalizedY / magnitude) * scaledMagnitude;
	}

	std::uint16_t GetButtonMask(GamepadButton button) {
		switch (button) {
		case GamepadButton::A: return XINPUT_GAMEPAD_A;
		case GamepadButton::B: return XINPUT_GAMEPAD_B;
		case GamepadButton::X: return XINPUT_GAMEPAD_X;
		case GamepadButton::Y: return XINPUT_GAMEPAD_Y;
		case GamepadButton::LeftShoulder: return XINPUT_GAMEPAD_LEFT_SHOULDER;
		case GamepadButton::RightShoulder: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
		case GamepadButton::Back: return XINPUT_GAMEPAD_BACK;
		case GamepadButton::Start: return XINPUT_GAMEPAD_START;
		case GamepadButton::LeftStickButton: return XINPUT_GAMEPAD_LEFT_THUMB;
		case GamepadButton::RightStickButton: return XINPUT_GAMEPAD_RIGHT_THUMB;
		case GamepadButton::DPadUp: return XINPUT_GAMEPAD_DPAD_UP;
		case GamepadButton::DPadDown: return XINPUT_GAMEPAD_DPAD_DOWN;
		case GamepadButton::DPadLeft: return XINPUT_GAMEPAD_DPAD_LEFT;
		case GamepadButton::DPadRight: return XINPUT_GAMEPAD_DPAD_RIGHT;
		default: return 0u;
		}
	}
}

GamepadInput& GamepadInput::Get() {
	static GamepadInput instance;
	return instance;
}

void GamepadInput::Update() {
	const ProjectSettingsData& projectSettings = ProjectSettings::Get().GetData();
	const float deadZone = (std::clamp)(projectSettings.gamepadStickDeadZone, 0.0f, 0.9f);
	const float sensitivity = (std::clamp)(projectSettings.gamepadLookSensitivity, 0.05f, 10.0f);

	const std::int64_t currentTick = std::chrono::steady_clock::now().time_since_epoch().count();
	const bool shouldPollDisconnected = currentTick >= nextDisconnectedPollTick_;

	if (shouldPollDisconnected) {
		nextDisconnectedPollTick_ = currentTick +
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<float>(kDisconnectedPollIntervalSeconds)).count();
	}

	for (int32_t gamepadIndex = 0; gamepadIndex < kMaxGamepadCount; ++gamepadIndex) {
		GamepadState& state = gamepads_[gamepadIndex];
		state.previousButtons = state.buttons;

		// 未接続Slotは毎フレーム問い合わせない(XInputGetStateが未接続時に重いため)。
		if (!state.isConnected && !shouldPollDisconnected) {
			continue;
		}

		XINPUT_STATE xinputState{};
		const DWORD result = XInputGetState(static_cast<DWORD>(gamepadIndex), &xinputState);

		if (result != ERROR_SUCCESS) {
			// 抜かれた瞬間も含め、未接続Padは必ず中立値へ戻す。
			// これにより「抜けた瞬間に入力が入りっぱなしになる」事故を防ぐ。
			const bool wasConnected = state.isConnected;
			state = GamepadState{};
			state.previousButtons = wasConnected ? state.previousButtons : 0u;
			continue;
		}

		state.isConnected = true;
		state.buttons = xinputState.Gamepad.wButtons;
		state.leftTrigger = static_cast<float>(xinputState.Gamepad.bLeftTrigger) / 255.0f;
		state.rightTrigger = static_cast<float>(xinputState.Gamepad.bRightTrigger) / 255.0f;
		ApplyStickDeadZone(
			static_cast<float>(xinputState.Gamepad.sThumbLX),
			static_cast<float>(xinputState.Gamepad.sThumbLY),
			deadZone,
			sensitivity,
			state.leftStickX,
			state.leftStickY);
		ApplyStickDeadZone(
			static_cast<float>(xinputState.Gamepad.sThumbRX),
			static_cast<float>(xinputState.Gamepad.sThumbRY),
			deadZone,
			sensitivity,
			state.rightStickX,
			state.rightStickY);
	}
}

bool GamepadInput::IsConnected(int32_t gamepadIndex) const {
	if (gamepadIndex < 0 || gamepadIndex >= kMaxGamepadCount) {
		return false;
	}

	return gamepads_[gamepadIndex].isConnected;
}

int32_t GamepadInput::GetConnectedCount() const {
	int32_t connectedCount = 0;

	for (const GamepadState& state : gamepads_) {
		if (state.isConnected) {
			connectedCount++;
		}
	}

	return connectedCount;
}

bool GamepadInput::IsButtonDownInState(
	const GamepadState& state,
	GamepadButton button,
	bool usePrevious) const {
	if (!state.isConnected) {
		return false;
	}

	if (button == GamepadButton::LeftTrigger || button == GamepadButton::RightTrigger) {
		// Trigger は前フレーム値を保持していないため、押下中判定のみ扱う。
		if (usePrevious) {
			return false;
		}

		const float threshold = ProjectSettings::Get().GetData().gamepadTriggerThreshold;
		return button == GamepadButton::LeftTrigger
			? state.leftTrigger >= threshold
			: state.rightTrigger >= threshold;
	}

	const std::uint16_t mask = GetButtonMask(button);

	if (mask == 0u) {
		return false;
	}

	return ((usePrevious ? state.previousButtons : state.buttons) & mask) != 0u;
}

bool GamepadInput::IsButtonPressed(GamepadButton button, int32_t gamepadIndex) const {
	if (gamepadIndex >= 0) {
		return gamepadIndex < kMaxGamepadCount &&
			IsButtonDownInState(gamepads_[gamepadIndex], button, false);
	}

	for (const GamepadState& state : gamepads_) {
		if (IsButtonDownInState(state, button, false)) {
			return true;
		}
	}

	return false;
}

bool GamepadInput::WasButtonJustPressed(GamepadButton button, int32_t gamepadIndex) const {
	if (gamepadIndex >= 0) {
		if (gamepadIndex >= kMaxGamepadCount) {
			return false;
		}

		const GamepadState& state = gamepads_[gamepadIndex];
		return IsButtonDownInState(state, button, false) && !IsButtonDownInState(state, button, true);
	}

	for (const GamepadState& state : gamepads_) {
		if (IsButtonDownInState(state, button, false) && !IsButtonDownInState(state, button, true)) {
			return true;
		}
	}

	return false;
}

bool GamepadInput::WasButtonJustReleased(GamepadButton button, int32_t gamepadIndex) const {
	if (gamepadIndex >= 0) {
		if (gamepadIndex >= kMaxGamepadCount) {
			return false;
		}

		const GamepadState& state = gamepads_[gamepadIndex];
		return !IsButtonDownInState(state, button, false) && IsButtonDownInState(state, button, true);
	}

	for (const GamepadState& state : gamepads_) {
		if (!IsButtonDownInState(state, button, false) && IsButtonDownInState(state, button, true)) {
			return true;
		}
	}

	return false;
}

void GamepadInput::GetStick(GamepadStick stick, int32_t gamepadIndex, float& outX, float& outY) const {
	outX = 0.0f;
	outY = 0.0f;

	const auto readStick = [stick](const GamepadState& state, float& x, float& y) {
		if (!state.isConnected) {
			return false;
		}

		if (stick == GamepadStick::Left) {
			x = state.leftStickX;
			y = state.leftStickY;
		}
		else if (stick == GamepadStick::Right) {
			x = state.rightStickX;
			y = state.rightStickY;
		}
		else {
			// DPad は Stick と同じ Vector2 として扱えるよう、押下状態を -1 / 0 / 1 へ変換する。
			x = 0.0f;
			y = 0.0f;

			if ((state.buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0u) { x -= 1.0f; }
			if ((state.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0u) { x += 1.0f; }
			if ((state.buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0u) { y -= 1.0f; }
			if ((state.buttons & XINPUT_GAMEPAD_DPAD_UP) != 0u) { y += 1.0f; }
		}

		return std::fabs(x) > 0.0001f || std::fabs(y) > 0.0001f;
	};

	if (gamepadIndex >= 0) {
		if (gamepadIndex < kMaxGamepadCount) {
			readStick(gamepads_[gamepadIndex], outX, outY);
		}

		return;
	}

	// Pad 指定なしの場合は、実際に入力が入っている最初のPadを採用する。
	for (const GamepadState& state : gamepads_) {
		float stickX = 0.0f;
		float stickY = 0.0f;

		if (readStick(state, stickX, stickY)) {
			outX = stickX;
			outY = stickY;
			return;
		}
	}
}

float GamepadInput::GetTrigger(bool isRightTrigger, int32_t gamepadIndex) const {
	if (gamepadIndex >= 0) {
		if (gamepadIndex >= kMaxGamepadCount || !gamepads_[gamepadIndex].isConnected) {
			return 0.0f;
		}

		return isRightTrigger ? gamepads_[gamepadIndex].rightTrigger : gamepads_[gamepadIndex].leftTrigger;
	}

	float maximumTrigger = 0.0f;

	for (const GamepadState& state : gamepads_) {
		if (!state.isConnected) {
			continue;
		}

		maximumTrigger = (std::max)(
			maximumTrigger,
			isRightTrigger ? state.rightTrigger : state.leftTrigger);
	}

	return maximumTrigger;
}

GamepadButton GamepadInput::ParseButtonName(const std::string& buttonName) {
	if (buttonName == "A") { return GamepadButton::A; }
	if (buttonName == "B") { return GamepadButton::B; }
	if (buttonName == "X") { return GamepadButton::X; }
	if (buttonName == "Y") { return GamepadButton::Y; }
	if (buttonName == "LeftShoulder" || buttonName == "LB") { return GamepadButton::LeftShoulder; }
	if (buttonName == "RightShoulder" || buttonName == "RB") { return GamepadButton::RightShoulder; }
	if (buttonName == "Back" || buttonName == "Select") { return GamepadButton::Back; }
	if (buttonName == "Start") { return GamepadButton::Start; }
	if (buttonName == "LeftStickButton" || buttonName == "L3") { return GamepadButton::LeftStickButton; }
	if (buttonName == "RightStickButton" || buttonName == "R3") { return GamepadButton::RightStickButton; }
	if (buttonName == "DPadUp") { return GamepadButton::DPadUp; }
	if (buttonName == "DPadDown") { return GamepadButton::DPadDown; }
	if (buttonName == "DPadLeft") { return GamepadButton::DPadLeft; }
	if (buttonName == "DPadRight") { return GamepadButton::DPadRight; }
	if (buttonName == "LeftTrigger" || buttonName == "LT") { return GamepadButton::LeftTrigger; }
	if (buttonName == "RightTrigger" || buttonName == "RT") { return GamepadButton::RightTrigger; }
	return GamepadButton::None;
}

GamepadStick GamepadInput::ParseStickName(const std::string& stickName) {
	if (stickName == "Right" || stickName == "RightStick") { return GamepadStick::Right; }
	if (stickName == "DPad") { return GamepadStick::DPad; }
	return GamepadStick::Left;
}

const char* GamepadInput::GetButtonName(GamepadButton button) {
	switch (button) {
	case GamepadButton::A: return "A";
	case GamepadButton::B: return "B";
	case GamepadButton::X: return "X";
	case GamepadButton::Y: return "Y";
	case GamepadButton::LeftShoulder: return "LeftShoulder";
	case GamepadButton::RightShoulder: return "RightShoulder";
	case GamepadButton::Back: return "Back";
	case GamepadButton::Start: return "Start";
	case GamepadButton::LeftStickButton: return "LeftStickButton";
	case GamepadButton::RightStickButton: return "RightStickButton";
	case GamepadButton::DPadUp: return "DPadUp";
	case GamepadButton::DPadDown: return "DPadDown";
	case GamepadButton::DPadLeft: return "DPadLeft";
	case GamepadButton::DPadRight: return "DPadRight";
	case GamepadButton::LeftTrigger: return "LeftTrigger";
	case GamepadButton::RightTrigger: return "RightTrigger";
	case GamepadButton::None:
	default:
		return "None";
	}
}
