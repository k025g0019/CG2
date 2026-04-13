#pragma warning(push, 0)
#include <sdkddkver.h>
#include <Windows.h>
#include <cstdint>
#pragma warning(pop)

namespace {

// ウィンドウ関連の固定値
constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
constexpr wchar_t kWindowTitle[] = L"CG2";
constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

// Windowsアプリケーションのエントリポイント
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

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
		return 1;
	}

	// クライアント領域のサイズ
	RECT windowRect{0, 0, kClientWidth, kClientHeight};
	// クライアント領域を実際のウィンドウサイズに変換する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		return 1;
	}

	// ウィンドウの生成
	HWND windowHandle = CreateWindow(
		windowClass.lpszClassName,           // 利用するクラス名
		kWindowTitle,                        // タイトルバーの文字
		WS_OVERLAPPEDWINDOW,                 // ウィンドウスタイル
		CW_USEDEFAULT,                       // 表示X座標
		CW_USEDEFAULT,                       // 表示Y座標
		windowRect.right - windowRect.left,  // ウィンドウの幅
		windowRect.bottom - windowRect.top,  // ウィンドウの高さ
		nullptr,                             // 親ウィンドウハンドル
		nullptr,                             // メニューハンドル
		windowClass.hInstance,               // インスタンスハンドル
		nullptr);                            // 追加パラメータ

	if (windowHandle == nullptr) {
		return 1;
	}

	// ウィンドウを表示する
	ShowWindow(windowHandle, SW_SHOW);
	UpdateWindow(windowHandle);

	// ウィンドウの×ボタンが押されるまでループ
	MSG message{};
	while (message.message != WM_QUIT) {
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		} else {
			// ゲームの処理
		}
	}

	return static_cast<int>(message.wParam);
}

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		// ウィンドウが破棄されたとき
	case WM_DESTROY:
		// OSにアプリケーションの終了を伝える
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
