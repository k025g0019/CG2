#pragma once

#include <Windows.h>
#include <cstdint>
#include <iosfwd>

constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
constexpr wchar_t kWindowTitle[] = L"CG2";

constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

class WinApp {
public:
	WNDCLASS windowClass{};
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	void Initialize();
	HWND GetHwnd() const { return hwnd; };
	HINSTANCE GetHInstance() const { return windowClass.hInstance; };
	void Finalize();
	//メッセージの処理
	bool ProcessMessage();

private:
	HWND hwnd = nullptr;
};
