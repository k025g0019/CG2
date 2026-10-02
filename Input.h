#pragma once

#include <wrl.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

class Input {
public:
    // 初期化
    void Initialize(HINSTANCE instanceHandle, HWND windowHandle);

    // 更新
    void Update();

private:
    // DirectInput本体
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;

    // キーボード入力デバイス
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
};