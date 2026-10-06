#include "WinApp.h"

#include <cassert>

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#pragma warning(pop)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

void WinApp::Initialize() {
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// メッセージを処理する関数
	windowClass.lpfnWndProc = WindowProc;
	// クラス名
	windowClass.lpszClassName = kWindowClassName;
	// インスタンスハンドル
	windowClass.hInstance = GetModuleHandle(nullptr);
	// 標準の矢印カーソル
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	// ウィンドウクラスを OS へ登録する

	RegisterClass(&windowClass);


	// クライアント領域の希望サイズ
	RECT windowRect{0, 0, kClientWidth, kClientHeight};
	// タイトルバーなどを含めた実際のウィンドウサイズへ調整する
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	//------------------------------
	// ウィンドウ生成
	//------------------------------
	hwnd = CreateWindowEx(
		0,
		kWindowClassName,
		kWindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		windowClass.hInstance,
		nullptr
	);

	if (hwnd == nullptr) {
		return;
	}

	// 生成したウィンドウを画面へ表示する
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
}


LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
	// ImGui が処理したメッセージはここで打ち切る
	if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam)) {
		return true;
	}
#endif

	switch (message) {
	case WM_DESTROY:
		// ウィンドウが閉じられたらアプリケーション終了を通知する
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

void WinApp::Finalize() {
	CloseWindow(hwnd);
	CoUninitialize();
}

bool WinApp::ProcessMessage() {
	MSG msg{};
	if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (msg.message == WM_QUIT) {
		return true;
	}
	return false;
}
