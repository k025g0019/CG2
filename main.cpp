#pragma warning(push, 0)
#include <sdkddkver.h>
#include <Windows.h>
#include <cstdint>
#pragma warning(pop)

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// クライアント領域のサイズ
	constexpr int clientWidth = 1280;
	constexpr int clientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる

	RECT wrc = { 0, 0, clientWidth, clientHeight };
	// クライアント領域をウィンドウサイズに変換する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

	// ウィンドウの生成

	HWND hwnd = CreateWindow(
		L"DirectXGameClass", // クラス名
		L"DirectX Game", // タイトルバーの文字
		WS_OVERLAPPEDWINDOW, // ウィンドウスタイル
		CW_USEDEFAULT, // 表示X座標
		CW_USEDEFAULT, // 表示Y座標
		wrc.right - wrc.left, // ウィンドウの幅
		wrc.bottom - wrc.top, // ウィンドウの高さ
		nullptr, // 親ウィンドウハンドル
		nullptr, // メニューハンドル
		instanceHandle, // インスタンスハンドル
		nullptr); // 追加パラメータ




	MSG msg{};
	while (msg.message != WM_QUIT) {

		if (PeekMessage(&msg,NULL,0,0,PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}else {


			// ウィンドウの表示
			ShowWindow(hwnd, SW_SHOW);

			WNDCLASS wc{};
			// ウィンドウプロシージャ
			wc.lpfnWndProc = WindowProc;
			// クラススタイル
			wc.lpszClassName = L"DirectXGameClass";
			// インスタンスハンドル
			wc.hInstance = GetModuleHandle(nullptr);
			// カーソル
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

			// ウィンドウクラスの登録
			RegisterClass(&wc);
		}
	}
	OutputDebugStringW(L"Hello, DirectX!\n");

	return 0;
}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
