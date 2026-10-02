#include "Input.h"

using namespace Microsoft::WRL;
#include <cassert>
#define DIRECTINPUT_VERSION 0x0800
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

void Input::Initialize(HINSTANCE instanceHandle, HWND windowHandle) {

    HRESULT result;
	ComPtr<IDirectInput8> directInput_ = nullptr;
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
	//更新処理
}

