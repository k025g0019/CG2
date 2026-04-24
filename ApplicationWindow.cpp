#include "ApplicationWindow.h"

#include "Log.h"

HWND CreateMainWindow(HINSTANCE instanceHandle, std::ostream& logStream) {
	// ウィンドウクラスを登録する
	WNDCLASS windowClass{};
	// ウィンドウプロシージャ
	windowClass.lpfnWndProc = WindowProc;
	// クラス名
	windowClass.lpszClassName = kWindowClassName;
	// インスタンスハンドル
	windowClass.hInstance = instanceHandle;
	// カーソル
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスの登録
	if (RegisterClass(&windowClass) == 0) {
		Log(logStream, "RegisterClass failed");
		return nullptr;
	}
	Log(logStream, "window class registered");

	// クライアント領域のサイズ
	RECT windowRect{ 0, 0, kClientWidth, kClientHeight };
	// クライアント領域を元に実際のウィンドウサイズに調整する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		Log(logStream, "AdjustWindowRect failed");
		return nullptr;
	}
	Log(logStream, "window rect adjusted");

	// ウィンドウの生成
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

	// ウィンドウを表示する
	ShowWindow(windowHandle, SW_SHOW);
	UpdateWindow(windowHandle);
	Log(logStream, "window shown");

	return windowHandle;
}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_DESTROY:
		// OSにアプリケーションの終了を伝える
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
