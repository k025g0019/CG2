#pragma once

#include <Windows.h>
#include <cstdint>


class WinApp {
public:
	static constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
	static constexpr wchar_t kWindowTitle[] = L"CG2";

	static constexpr int32_t kClientWidth = 1280;
	static constexpr int32_t kClientHeight = 720;


	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	void Initialize();
	HWND GetHwnd() const { return hwnd; };
	HINSTANCE GetHInstance() const { return windowClass.hInstance; };
	void Finalize();
	//メッセージの処理
	bool ProcessMessage();

private:
	WNDCLASS windowClass{};

	HWND hwnd = nullptr;
};
