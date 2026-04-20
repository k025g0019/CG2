#pragma warning(push, 0)
#include <sdkddkver.h>
#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#pragma warning(pop)

namespace {

	// ウィンドウ関連の固定値
	constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
	constexpr wchar_t kWindowTitle[] = L"CG2";
	constexpr int32_t kClientWidth = 1280;
	constexpr int32_t kClientHeight = 720;

}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);


void log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());

}
// Windowsアプリケーションのエントリポイント
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// ログのディレクトリを用意
	std::filesystem::create_directory("logs");
	// 現在時刻を取得
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	std::chrono::time_point < std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	//日本時間
	std::chrono::zoned_time localTime{ std::chrono::current_zone(),nowSeconds };

	//formatを使って
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	//時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/" + dateString + ".log");
	//ファイルを使って書き込む
	std::ofstream logStream(logFilePath);
	if (!logStream) {
		return 1;
	}

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
		log(logStream, "RegisterClass failed");
		return 1;
	}
	log(logStream, "window class registered");

	// クライアント領域のサイズ
	RECT windowRect{ 0, 0, kClientWidth, kClientHeight };
	// クライアント領域を実際のウィンドウサイズに変換する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		log(logStream, "AdjustWindowRect failed");
		return 1;
	}
	log(logStream, "window rect adjusted");

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
		log(logStream, "CreateWindow failed");
		return 1;
	}
	log(logStream, "window created");

	// ウィンドウを表示する
	ShowWindow(windowHandle, SW_SHOW);
	UpdateWindow(windowHandle);
	log(logStream, "window shown");

	// ウィンドウの×ボタンが押されるまでループ
	MSG message{};
	log(logStream, "main loop started");
	while (message.message != WM_QUIT) {
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		else {
			// ゲームの処理
		}
	}

	log(logStream, "application finished");
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
