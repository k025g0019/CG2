#include "Input.h"

#include "EditorHrCheck.h"
#include "WinApp.h"

#include <cstring>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

//========================================
// 初期化処理
//========================================

bool Input::Initialize(HINSTANCE instanceHandle, WinApp* winApp) {
	if (winApp == nullptr || !winApp->HasWindow()) {
		return false;  // 協調 Level に Window Handle が要るので、Window より後に呼ぶ
	}

	const HWND windowHandle = winApp->GetHwnd();

	//----------------------------------------
	// DirectInput 本体
	//----------------------------------------

	HRESULT hr = DirectInput8Create(
		instanceHandle,
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(directInput_.GetAddressOf()),
		nullptr);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || directInput_ == nullptr) {
		return false;
	}

	//----------------------------------------
	// Keyboard Device
	//----------------------------------------

	hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboardDevice_.GetAddressOf(), nullptr);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || keyboardDevice_ == nullptr) {
		directInput_.Reset();
		return false;
	}

	hr = keyboardDevice_->SetDataFormat(&c_dfDIKeyboard);
	EDITOR_HR_VERIFY(hr);

	// FOREGROUND: 前面のときだけ取る。NONEXCLUSIVE: 他アプリと共有する。
	// NOWINKEY: Windows キーで Editor から抜けてしまうのを防ぐ。
	hr = keyboardDevice_->SetCooperativeLevel(
		windowHandle,
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	EDITOR_HR_VERIFY(hr);

	//----------------------------------------
	// Mouse Device
	//----------------------------------------

	hr = directInput_->CreateDevice(GUID_SysMouse, mouseDevice_.GetAddressOf(), nullptr);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || mouseDevice_ == nullptr) {
		// Mouse が無くても Keyboard だけで Editor は操作できるため、ここでは失敗にしない。
		mouseDevice_.Reset();
		return true;
	}

	hr = mouseDevice_->SetDataFormat(&c_dfDIMouse);
	EDITOR_HR_VERIFY(hr);

	hr = mouseDevice_->SetCooperativeLevel(
		windowHandle,
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	EDITOR_HR_VERIFY(hr);

	return true;
}

//========================================
// 毎フレーム処理
//========================================

void Input::Update() {
	//----------------------------------------
	// 前フレーム状態の退避
	//----------------------------------------

	// 退避してから読む。順番を逆にすると「押した瞬間」が毎フレーム成立してしまう。
	std::memcpy(previousKeyStates_, keyStates_, sizeof(keyStates_));
	previousMouseState_ = mouseState_;

	AcquireAndReadKeyboard();
	AcquireAndReadMouse();
}

void Input::AcquireAndReadKeyboard() {
	if (keyboardDevice_ == nullptr) {
		return;
	}

	// Acquire はフォーカス復帰後に入力取得を再開するために必要。
	keyboardDevice_->Acquire();
	HRESULT hr = keyboardDevice_->GetDeviceState(sizeof(keyStates_), keyStates_);

	// 取得に失敗した場合は Device を取り直し、同じフレーム内で 1 回だけ再取得する。
	if (FAILED(hr)) {
		keyboardDevice_->Acquire();
		hr = keyboardDevice_->GetDeviceState(sizeof(keyStates_), keyStates_);
	}

	// 2 回とも失敗したら、押しっぱなし扱いが残らないよう中立へ倒す。
	if (FAILED(hr)) {
		std::memset(keyStates_, 0, sizeof(keyStates_));
	}
}

void Input::AcquireAndReadMouse() {
	if (mouseDevice_ == nullptr) {
		return;
	}

	mouseDevice_->Acquire();
	HRESULT hr = mouseDevice_->GetDeviceState(sizeof(mouseState_), &mouseState_);

	if (FAILED(hr)) {
		mouseDevice_->Acquire();
		hr = mouseDevice_->GetDeviceState(sizeof(mouseState_), &mouseState_);
	}

	if (FAILED(hr)) {
		mouseState_ = DIMOUSESTATE{};
	}
}

//========================================
// 終了処理
//========================================

void Input::Finalize() {
	// Unacquire は明示しないと、解放後も OS 側が Device を掴んだままになりうる。
	if (keyboardDevice_ != nullptr) {
		keyboardDevice_->Unacquire();
	}

	if (mouseDevice_ != nullptr) {
		mouseDevice_->Unacquire();
	}

	// ComPtr なので Release は書かない。Reset で参照を手放すだけ。
	mouseDevice_.Reset();
	keyboardDevice_.Reset();
	directInput_.Reset();
}

//========================================
// 条件判定
//========================================

bool Input::IsValidKeyCode(int32_t dikCode) const {
	return dikCode >= 0 && dikCode < kKeyCount;
}

bool Input::IsValidMouseButton(int32_t mouseButton) const {
	return mouseButton >= 0 && mouseButton < kMouseButtonCount;
}

bool Input::PushKey(int32_t dikCode) const {
	if (!IsValidKeyCode(dikCode)) {
		return false;
	}

	return (keyStates_[dikCode] & kPressedBit) != 0u;
}

bool Input::TriggerKey(int32_t dikCode) const {
	if (!IsValidKeyCode(dikCode)) {
		return false;
	}

	// 今フレームは押していて、前フレームは押していない。
	return (keyStates_[dikCode] & kPressedBit) != 0u
		&& (previousKeyStates_[dikCode] & kPressedBit) == 0u;
}

bool Input::ReleaseKey(int32_t dikCode) const {
	if (!IsValidKeyCode(dikCode)) {
		return false;
	}

	return (keyStates_[dikCode] & kPressedBit) == 0u
		&& (previousKeyStates_[dikCode] & kPressedBit) != 0u;
}

bool Input::PushMouseButton(int32_t mouseButton) const {
	if (!IsValidMouseButton(mouseButton)) {
		return false;
	}

	return (mouseState_.rgbButtons[mouseButton] & kPressedBit) != 0u;
}

bool Input::TriggerMouseButton(int32_t mouseButton) const {
	if (!IsValidMouseButton(mouseButton)) {
		return false;
	}

	return (mouseState_.rgbButtons[mouseButton] & kPressedBit) != 0u
		&& (previousMouseState_.rgbButtons[mouseButton] & kPressedBit) == 0u;
}

bool Input::ReleaseMouseButton(int32_t mouseButton) const {
	if (!IsValidMouseButton(mouseButton)) {
		return false;
	}

	return (mouseState_.rgbButtons[mouseButton] & kPressedBit) == 0u
		&& (previousMouseState_.rgbButtons[mouseButton] & kPressedBit) != 0u;
}
