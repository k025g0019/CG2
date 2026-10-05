#pragma once

#include <wrl.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "WinApp.h"

class Input {
public:
    // namespace省略
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:



    // 初期化
    void Initialize(HINSTANCE hInstance,HWND hwnd);

    // 更新
    void Update();

	// キーが押されたかどうかを判定する関数
    bool PushKey(BYTE keyNumber);

	// キーが押された瞬間かどうかを判定する関数
	bool TriggerKey(BYTE keyNumber);
private:
    BYTE key_[256]{};
    BYTE preKey_[256]{};
    // キーボードのデバイス
	ComPtr<IDirectInputDevice8> keyboard_;
    // DirectInput本体
    ComPtr<IDirectInput8> directInput_;

    //WindowsApi
	WinApp* winApp = nullptr;

};