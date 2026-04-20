#pragma warning(push, 0)
#include <sdkddkver.h>
#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#pragma warning(pop)

namespace {

// ウィンドウ関連の固定値
constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
constexpr wchar_t kWindowTitle[] = L"CG2";
constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

// string->wstring
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (sizeNeeded == 0) {
		return {};
	}

	std::wstring result(static_cast<size_t>(sizeNeeded), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded);
	result.pop_back();
	return result;
}

// wstring->string
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (sizeNeeded == 0) {
		return {};
	}

	std::string result(static_cast<size_t>(sizeNeeded), '\0');
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded, nullptr, nullptr);
	result.pop_back();
	return result;
}

// 出力ウィンドウに文字を出す
void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

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
	Log("window class registered\n");

	//文字列を格納する
	std::string str0{ "STRING!!!" };
	Log(str0 + "\n");

	// 整数を文字列にする
	std::string str1{ std::to_string(10) };
	Log(str1 + "\n");

	// wstringをstringに変換してログに出す
	Log(ConvertString(std::wstring{ L"WSTRING!!!\n" }));

	// formatを使ってログに出す
	Log(std::format("client size: {} x {}\n", kClientWidth, kClientHeight));

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
	Log("window created\n");

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
