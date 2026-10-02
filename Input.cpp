#include "Input.h"

using namespace Microsoft::WRL;
#include <cassert>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

void Input::Initialize(HINSTANCE instanceHandle, HWND windowHandle) {

    HRESULT result;
	// DirectInputのインスタンスを作る
    result = DirectInput8Create(
        instanceHandle,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()),
        nullptr);
    assert(SUCCEEDED(result));

    // キーボードデバイス生成
    result = directInput_->CreateDevice(
        GUID_SysKeyboard,
        keyboard_.GetAddressOf(),
        nullptr);
    assert(SUCCEEDED(result));
	// 入力データフォーマットの設定
    result = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));

    // 排他制御levelセット
    result = keyboard_->SetCooperativeLevel(
        windowHandle,
        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(result));
}

void Input::Update() {
    keyboard_->Acquire();
    // キーボードの状態を取得
    BYTE keyboardState[256];
    HRESULT result = keyboard_->GetDeviceState(sizeof(keyboardState), keyboardState);
    if (FAILED(result)) {
        // デバイスが失われた場合、再取得を試みる
        if ((result == DIERR_INPUTLOST) || (result == DIERR_NOTACQUIRED)) {
            keyboard_->Acquire();
        }
	}
}

