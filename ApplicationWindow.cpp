#include "ApplicationWindow.h"

#include "Log.h"

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#pragma warning(pop)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

HWND CreateMainWindow(HINSTANCE instanceHandle, std::ostream& logStream) {
	// ウィンドウクラスを登録する
	WNDCLASS windowClass{};
	// メッセージを処理する関数
	windowClass.lpfnWndProc = WindowProc;
	// クラス名
	windowClass.lpszClassName = kWindowClassName;
	// インスタンスハンドル
	windowClass.hInstance = instanceHandle;
	// 標準の矢印カーソル
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを OS へ登録する
	if (RegisterClass(&windowClass) == 0) {
		Log(logStream, "RegisterClass failed");
		return nullptr;
	}
	Log(logStream, "window class registered");

	// クライアント領域の希望サイズ
	RECT windowRect{0, 0, kClientWidth, kClientHeight};
	// タイトルバーなどを含めた実際のウィンドウサイズへ調整する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		Log(logStream, "AdjustWindowRect failed");
		return nullptr;
	}
	Log(logStream, "window rect adjusted");

	// ウィンドウ本体を生成する
	HWND windowHandle = CreateWindow(
		windowClass.lpszClassName,
		kWindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		windowClass.hInstance,
		nullptr);

	if (windowHandle == nullptr) {
		Log(logStream, "CreateWindow failed");
		return nullptr;
	}
	Log(logStream, "window created");

	// 生成したウィンドウを画面へ表示する
	ShowWindow(windowHandle, SW_SHOW);
	UpdateWindow(windowHandle);
	Log(logStream, "window shown");

	return windowHandle;
}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
	// ImGui が処理したメッセージはここで打ち切る
	if (ImGui_ImplWin32_WndProcHandler(windowHandle, message, wParam, lParam)) {
		return true;
	}
#endif

	switch (message) {
	case WM_DESTROY:
		// ウィンドウが閉じられたらアプリケーション終了を通知する
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
