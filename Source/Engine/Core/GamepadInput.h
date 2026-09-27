#pragma once

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

// .inputactions から参照する Gamepad ボタン名。XInput のボタン配置に対応する。
enum class GamepadButton : int32_t {
	None = 0,
	A,
	B,
	X,
	Y,
	LeftShoulder,
	RightShoulder,
	Back,
	Start,
	LeftStickButton,
	RightStickButton,
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	LeftTrigger,   // アナログ値をしきい値でボタン化する。
	RightTrigger,
};

enum class GamepadStick : int32_t {
	Left = 0,
	Right,
	DPad,  // Stick と同じ Vector2 として扱えるようにする。
};

// XInput の Gamepad を最大4台まで読む。Editor と Player の両方から使う。
// 未接続のPadを毎フレーム問い合わせると XInputGetState が重くなるため、
// 未接続のSlotだけ一定間隔を空けて再検出する(接続中のPadは毎フレーム読む)。
class GamepadInput {
public:
	static constexpr int32_t kMaxGamepadCount = 4;

	static GamepadInput& Get();

	GamepadInput(const GamepadInput&) = delete;
	GamepadInput& operator=(const GamepadInput&) = delete;

	// 毎フレーム先頭で1回だけ呼ぶ。切断されたPadは自動的に中立値へ戻る。
	// 再検出間隔の計測は内部の時計で行うため、呼び出し側はdeltaTimeを渡さなくてよい。
	void Update();

	bool IsConnected(int32_t gamepadIndex) const;
	int32_t GetConnectedCount() const;

	// 押下中 / 押した瞬間 / 離した瞬間。gamepadIndex が負なら接続中の全Padを OR で見る。
	bool IsButtonPressed(GamepadButton button, int32_t gamepadIndex = -1) const;
	bool WasButtonJustPressed(GamepadButton button, int32_t gamepadIndex = -1) const;
	bool WasButtonJustReleased(GamepadButton button, int32_t gamepadIndex = -1) const;

	// Dead Zone と Sensitivity を適用済みの Stick 値(-1..1)。gamepadIndex が負なら
	// 最初に入力が入っているPadの値を返す。
	void GetStick(GamepadStick stick, int32_t gamepadIndex, float& outX, float& outY) const;

	float GetTrigger(bool isRightTrigger, int32_t gamepadIndex = -1) const;

	static GamepadButton ParseButtonName(const std::string& buttonName);
	static GamepadStick ParseStickName(const std::string& stickName);
	static const char* GetButtonName(GamepadButton button);

private:
	struct GamepadState {
		std::uint16_t buttons = 0u;        // XINPUT_GAMEPAD_* のビットフラグ。
		std::uint16_t previousButtons = 0u;
		float leftTrigger = 0.0f;
		float rightTrigger = 0.0f;
		float leftStickX = 0.0f;
		float leftStickY = 0.0f;
		float rightStickX = 0.0f;
		float rightStickY = 0.0f;
		bool isConnected = false;
	};

	GamepadInput() = default;

	bool IsButtonDownInState(const GamepadState& state, GamepadButton button, bool usePrevious) const;

	GamepadState gamepads_[kMaxGamepadCount]{};
	std::int64_t nextDisconnectedPollTick_ = 0;  // 未接続Slotを次に再検出する時刻(steady_clock tick)。
};

#pragma warning(pop)
