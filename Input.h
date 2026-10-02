#pragma once

#include <wrl.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

class Input {
public:
    // namespace省略
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
   
    
    // 初期化
    void Initialize(HINSTANCE instanceHandle, HWND windowHandle);

    // 更新
    void Update();

private:
    BYTE key_[256]{};
    BYTE preKey_[256]{};
    // キーボードのデバイス
	ComPtr<IDirectInputDevice8> keyboard_;
    // DirectInput本体
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;

};