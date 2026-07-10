#include "ApplicationWindow.h"

#include "Log.h"

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_impl_win32.h"
#pragma warning(pop)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

HWND CreateMainWindow(HINSTANCE instanceHandle, std::ostream& logStream) {
	// ウィンドウの作成ルールを Windows に登録するための設定
	WNDCLASS windowClass{};
	windowClass.lpfnWndProc = WindowProc;  // メッセージを処理する関数
	windowClass.lpszClassName = kWindowClassName;  // クラス名
	windowClass.hInstance = instanceHandle;  // インスタンスハンドル
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);  // 標準の矢印カーソル

	// ウィンドウクラスを OS へ登録する
	if (RegisterClass(&windowClass) == 0) {
		Log(logStream, "RegisterClass failed");
		return nullptr;
	}
	Log(logStream, "window class registered");

	// 描画したいクライアント領域の左上と右下
	RECT windowRect{0, 0, kClientWidth, kClientHeight};
	// タイトルバーなどを含めた実際のウィンドウサイズへ調整する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		Log(logStream, "AdjustWindowRect failed");
		return nullptr;
	}
	Log(logStream, "window rect adjusted");

	// AdjustWindowRect 後のサイズを使い、描画領域が kClientWidth / kClientHeight になるように作る
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

	ShowWindow(windowHandle, SW_SHOW);  // 生成したウィンドウを画面へ表示する
	UpdateWindow(windowHandle);
	Log(logStream, "window shown");

	return windowHandle;
}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
	// ImGui の入力欄や Docking が使用するマウス / キーボードメッセージを先に処理する
	if (ImGui_ImplWin32_WndProcHandler(windowHandle, message, wParam, lParam)) {
		return true;
	}
#endif

	switch (message) {
	case WM_ERASEBKGND:
		// DirectX 側で毎フレーム全面を塗るため、Windows 既定の白塗り潰しは無効にする。
		return 1;
	case WM_DESTROY:
		PostQuitMessage(0);  // メインループ側の IsEndRequested を成立させるため、WM_QUIT をメッセージキューへ積む
		return 0;
	default:
		// このアプリで処理しないメッセージは Windows 標準処理に渡す
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
