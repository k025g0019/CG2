#pragma warning(push, 0)
#include <sdkddkver.h>
#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <format>
#include <dbghelp.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <strsafe.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma warning(pop)

namespace {

	// ウィンドウ関連の固定値
	constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
	constexpr wchar_t kWindowTitle[] = L"CG2";
	constexpr int32_t kClientWidth = 1280;
	constexpr int32_t kClientHeight = 720;

}

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);


static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	// ダンプファイルのパスを決定する
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = {0};
	CreateDirectory(L"./Dump", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dump/%04d%02d%02d_%02d%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	// ダンプファイルを作成する
	HANDLE dumpFileHandle = CreateFileW(filePath, GENERIC_READ|GENERIC_WRITE,FILE_SHARE_WRITE|FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);

	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{0};
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpWithFullMemory, &minidumpInformation, nullptr, nullptr);
	return EXCEPTION_EXECUTE_HANDLER;
}
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

// ログファイルと出力ウィンドウの両方に文字を出す
void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	Log(message + "\n");
}
// Windowsアプリケーションのエントリポイント
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	SetUnhandledExceptionFilter(ExportDump);

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
	std::string logFilePath = std::string("logs/" + dateString + ".Log");
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
		Log(logStream, "RegisterClass failed");
		return 1;
	}
	Log(logStream, "window class registered");

	// クライアント領域のサイズ
	RECT windowRect{ 0, 0, kClientWidth, kClientHeight };
	// クライアント領域を実際のウィンドウサイズに変換する
	if (AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE) == 0) {
		Log(logStream, "AdjustWindowRect failed");
		return 1;
	}
	Log(logStream, "window rect adjusted");

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
		Log(logStream, "CreateWindow failed");
		return 1;
	}
	Log(logStream, "window created");

	// ウィンドウを表示する
	ShowWindow(windowHandle, SW_SHOW);
	UpdateWindow(windowHandle);
	Log(logStream, "window shown");

	// ウィンドウの×ボタンが押されるまでループ
	MSG message{};
	Log(logStream, "main loop started");

	// DXGI Factoryを作成する
	IDXGIFactory7 * dxgiFactory = nullptr;

	// HRESULTはWindows系のエラーコードであり
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定すること
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、土井にもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));


	// 使用するアダプタ用の変数、最初にnullptrを入れておく
	IDXGIAdapter4* useAdapter = nullptr;
	// いいもの順にアダプタを頼む
	for (UINT adapterIndex = 0; ; ++adapterIndex) {
		IDXGIAdapter4* candidateAdapter = nullptr;
		if (dxgiFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidateAdapter)) == DXGI_ERROR_NOT_FOUND) {
			break;
		}
		
		//アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));
		//ソフトウェアアダプタはスキップする
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			candidateAdapter->Release();
			continue;
		}

		useAdapter = candidateAdapter;
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);
	
	ID3D12Device* device = nullptr;

	hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
	if (SUCCEEDED(hr)) {
		Log(logStream, "FeatureLevel:12.2");
	} else {
		hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device));
		if (SUCCEEDED(hr)) {
			Log(logStream, "FeatureLevel:12.1");
		} else {
			hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
			if (SUCCEEDED(hr)) {
				Log(logStream, "FeatureLevel:12.0");
			}
		}
	}

	assert(device != nullptr);
	Log(logStream, "Complete create D3D12Device!!!");


	//uint32_t* p = nullptr;
	//*p = 100;

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

	Log(logStream, "application finished");
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
