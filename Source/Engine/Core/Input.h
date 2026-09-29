#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wrl/client.h>
#pragma warning(pop)

#include <cstdint>

class WinApp;

#pragma warning(push)
#pragma warning(disable : 4820)

//========================================
// 入力のクラス化
//========================================

// DirectInput の Keyboard と Mouse を 1 つのクラスへまとめる。
//
// 持つものは Device 3 つと、今フレーム / 前フレームの状態だけ。
// 「押した瞬間」は 2 フレーム分の状態を比べて決めるので、両方をこのクラスが持たないと
// 呼び出し側が前フレーム状態を自前で覚えることになり、取り違えの原因になる。
//
// Device は ComPtr で持つため Release を書かない。Unacquire だけは
// 解放前に明示する必要があるので Finalize で行う。
//
// 実体は動的に確保する。Initialize 前と Finalize 後に入力を読もうとした場合は
// Pointer が無い状態になるので、呼び出し側が誤って古い値を読み続けることがない。
class Input {
public:
	//----------------------------------------
	// クラスの定数
	//----------------------------------------

	static constexpr int32_t kKeyCount = 256;  // DirectInput が返す DIK_* の総数
	static constexpr int32_t kMouseButtonCount = 4;  // DIMOUSESTATE が持つボタン数
	static constexpr uint8_t kPressedBit = 0x80u;  // DirectInput が押下を示すビット

	//----------------------------------------
	// 初期化・毎フレーム・終了
	//----------------------------------------

	// DirectInput 本体と Keyboard / Mouse Device を作る。
	// 協調 Level の設定に Window Handle が要るため、WinApp のポインタを受け取る。
	bool Initialize(HINSTANCE instanceHandle, WinApp* winApp);

	// 1 フレーム分の入力を取り込む。前フレーム状態の退避もここで行うので、
	// フレーム内で 2 回呼ぶと「押した瞬間」が消える。呼ぶのは 1 回だけ。
	void Update();

	// Device を Unacquire してから手放す。ComPtr なので Release は書かない。
	void Finalize();

	//----------------------------------------
	// 条件判定
	//----------------------------------------

	bool PushKey(int32_t dikCode) const;  // 押され続けているか
	bool TriggerKey(int32_t dikCode) const;  // このフレームで押した瞬間か
	bool ReleaseKey(int32_t dikCode) const;  // このフレームで離した瞬間か
	bool PushMouseButton(int32_t mouseButton) const;  // Mouse ボタンが押され続けているか
	bool TriggerMouseButton(int32_t mouseButton) const;  // Mouse ボタンを押した瞬間か
	bool ReleaseMouseButton(int32_t mouseButton) const;  // Mouse ボタンを離した瞬間か
	bool IsInitialized() const { return keyboardDevice_ != nullptr; }  // Device を作れているか

	//----------------------------------------
	// getter
	//----------------------------------------

	// 既存の Manager が 256 バイト配列をそのまま受け取る形なので、読み取り専用で公開する。
	const BYTE* GetKeyStates() const { return keyStates_; }
	const BYTE* GetPreviousKeyStates() const { return previousKeyStates_; }
	const DIMOUSESTATE& GetMouseState() const { return mouseState_; }
	const DIMOUSESTATE& GetPreviousMouseState() const { return previousMouseState_; }
	float GetMouseMoveX() const { return static_cast<float>(mouseState_.lX); }  // 今フレームの水平移動量
	float GetMouseMoveY() const { return static_cast<float>(mouseState_.lY); }  // 今フレームの垂直移動量
	float GetMouseWheel() const { return static_cast<float>(mouseState_.lZ); }  // Wheel の回転量。1 目盛 120

private:
	//----------------------------------------
	// DirectInput の Device
	//----------------------------------------

	Microsoft::WRL::ComPtr<IDirectInput8> directInput_;  // Device を作る本体
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboardDevice_;  // Keyboard。Initialize に失敗すると null
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouseDevice_;  // Mouse。Mouse 無しの環境では null になりうる

	//----------------------------------------
	// 今フレームと前フレームの状態
	//----------------------------------------

	BYTE keyStates_[kKeyCount]{};  // 今フレームの DIK_* ごとの押下状態
	BYTE previousKeyStates_[kKeyCount]{};  // 前フレーム。差分で押した瞬間を判定する
	DIMOUSESTATE mouseState_{};  // 今フレームの移動量とボタン
	DIMOUSESTATE previousMouseState_{};  // 前フレーム

	//----------------------------------------
	// 内部処理
	//----------------------------------------

	bool IsValidKeyCode(int32_t dikCode) const;  // 配列外参照を防ぐ範囲確認
	bool IsValidMouseButton(int32_t mouseButton) const;
	void AcquireAndReadKeyboard();  // Acquire 込みで Keyboard を読む
	void AcquireAndReadMouse();  // Acquire 込みで Mouse を読む
};

#pragma warning(pop)
