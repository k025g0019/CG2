#include "Input.h"
#include "WinApp.h"
using namespace Microsoft::WRL;
#include <cassert>
#include <cstring>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

void Input::Initialize(WinApp* winApp) {
	assert(winApp != nullptr);

	// 受け取ったWinAppをメンバーへ保存する
	this->winApp = winApp;

	HRESULT result = DirectInput8Create(
		this->winApp->GetHInstance(),
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(directInput_.GetAddressOf()),
		nullptr);
	assert(SUCCEEDED(result));

	result = directInput_->CreateDevice(
		GUID_SysKeyboard,
		keyboard_.GetAddressOf(),
		nullptr);
	assert(SUCCEEDED(result));

	result = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(result));

	result = keyboard_->SetCooperativeLevel(
		this->winApp->GetHwnd(),
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(result));
}

void Input::Update() {
	// 更新前のキー状態を保存する
	memcpy(preKey_, key_, sizeof(key_));

	// キーボードを取得する
	keyboard_->Acquire();

	// 現在のキー状態をメンバー変数へ保存する
	HRESULT result =
		keyboard_->GetDeviceState(sizeof(key_), key_);

	// ウィンドウ切替などで入力を失った場合は再取得する
	if (FAILED(result)) {
		keyboard_->Acquire();

		// 再取得した後、キー状態をもう一度読み込む
		result = keyboard_->GetDeviceState(sizeof(key_), key_);
	}
}

bool Input::PushKey(BYTE keyNumber) {
	// キーが押されたかどうかを判定
	if (key_[keyNumber]) {
		return true;
	}
	return false;
}

bool Input::TriggerKey(BYTE keyNumber) {
	// キーが押された瞬間かどうかを判定
	if (key_[keyNumber] && !preKey_[keyNumber]) {
		return true;
	}
	return false;
}
