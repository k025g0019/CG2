#include "WinApp.h"

#include <commdlg.h>
#include <filesystem>

#include "EditorSharedState.h"
#include "Log.h"
#include "ProjectSettings.h"

#pragma comment(lib, "comdlg32.lib")

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#include "ThirdParty/imgui-docking/imgui-docking/backends/imgui_impl_win32.h"
#pragma warning(pop)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace {
	//----------------------------------------
	// Window タイトルの保持
	//----------------------------------------

	// Player のタイトルは Window を作る前に決まり、以降変わらない。
	// Instance へ持たせると SetStandaloneWindowTitle を静的にできないため、
	// この翻訳単位に閉じた値として持つ。
	std::wstring g_standaloneWindowTitle = WinApp::kDefaultGameWindowTitle;

	//----------------------------------------
	// 文字コード変換
	//----------------------------------------

	// Engine 内部は UTF-8、Windows API は UTF-16 なので境界で変換する。
	std::string WideToUtf8(const std::wstring& text) {
		if (text.empty()) {
			return {};
		}

		const int convertedSize = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string convertedText(convertedSize, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, convertedText.data(), convertedSize, nullptr, nullptr);

		if (!convertedText.empty() && convertedText.back() == '\0') {
			convertedText.pop_back();
		}

		return convertedText;
	}

	std::wstring Utf8ToWide(const std::string& text) {
		if (text.empty()) {
			return {};
		}

		const int convertedSize = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

		if (convertedSize <= 0) {
			return {};
		}

		std::wstring convertedText(static_cast<size_t>(convertedSize), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, convertedText.data(), convertedSize);

		if (!convertedText.empty() && convertedText.back() == L'\0') {
			convertedText.pop_back();
		}

		return convertedText;
	}
}  // namespace

//========================================
// 静的メンバ関数
//========================================

void WinApp::SetStandaloneWindowTitle(const std::string& windowTitle) {
	const std::wstring convertedTitle = Utf8ToWide(windowTitle);
	g_standaloneWindowTitle = convertedTitle.empty() ? kDefaultGameWindowTitle : convertedTitle;
}

//========================================
// 初期化処理
//========================================

bool WinApp::Initialize(HINSTANCE instanceHandle, std::ostream& logStream) {
	instanceHandle_ = instanceHandle;

	//----------------------------------------
	// Window Class の登録
	//----------------------------------------

	// Window の作成ルールを Windows へ登録する。lpfnWndProc は静的メンバ関数を指す。
	WNDCLASS windowClass{};
	windowClass.lpfnWndProc = WindowProc;  // Message を処理する関数
	windowClass.lpszClassName = kWindowClassName;  // クラス名
	windowClass.hInstance = instanceHandle;  // インスタンスハンドル
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);  // 標準の矢印カーソル

	if (RegisterClass(&windowClass) == 0) {
		Log(logStream, "RegisterClass failed");
		return false;
	}

	isWindowClassRegistered_ = true;
	Log(logStream, "window class registered");

	//----------------------------------------
	// Window サイズと Style の決定
	//----------------------------------------

	// Project Settings の解像度 / Window Mode をここで実際に反映する。
	// SwapChain はこの Client 領域から作られるため、ここが描画解像度そのものになる。
	const ProjectSettingsData& projectSettings = ProjectSettings::Get().GetData();
	const bool isBorderlessFullscreen =
		projectSettings.windowMode == ProjectWindowMode::BorderlessFullscreen;
	const int32_t requestedWidth = projectSettings.gameWidth > 0 ? projectSettings.gameWidth : kClientWidth;
	const int32_t requestedHeight = projectSettings.gameHeight > 0 ? projectSettings.gameHeight : kClientHeight;

	int32_t windowPositionX = CW_USEDEFAULT;
	int32_t windowPositionY = CW_USEDEFAULT;
	int32_t windowWidth = requestedWidth;
	int32_t windowHeight = requestedHeight;
	DWORD windowStyle = WS_OVERLAPPEDWINDOW;

	if (isBorderlessFullscreen) {
		// 枠なしでモニター全域へ広げる。排他 Fullscreen より Alt+Tab や Editor 併用が安定する。
		windowStyle = WS_POPUP;
		windowPositionX = 0;
		windowPositionY = 0;
		windowWidth = GetSystemMetrics(SM_CXSCREEN);
		windowHeight = GetSystemMetrics(SM_CYSCREEN);
	}
	else {
		// 描画したい Client 領域の左上と右下
		RECT windowRect{0, 0, requestedWidth, requestedHeight};

		// タイトルバーなどを含めた実際の Window サイズへ調整する
		if (AdjustWindowRect(&windowRect, windowStyle, FALSE) == 0) {
			Log(logStream, "AdjustWindowRect failed");
			return false;
		}

		Log(logStream, "window rect adjusted");
		windowWidth = windowRect.right - windowRect.left;
		windowHeight = windowRect.bottom - windowRect.top;
	}

	//----------------------------------------
	// Window の生成と表示
	//----------------------------------------

	// 調整後のサイズを使い、描画領域が Project Settings の解像度になるように作る
	windowHandle_ = CreateWindow(
		windowClass.lpszClassName,
		EditorSharedState::g_isStandaloneGame ? g_standaloneWindowTitle.c_str() : kWindowTitle,
		windowStyle,
		windowPositionX,
		windowPositionY,
		windowWidth,
		windowHeight,
		nullptr,
		nullptr,
		windowClass.hInstance,
		nullptr);

	if (windowHandle_ == nullptr) {
		Log(logStream, "CreateWindow failed");
		return false;
	}

	Log(logStream, "window created");

	ShowWindow(windowHandle_, SW_SHOW);  // 生成した Window を画面へ表示する
	UpdateWindow(windowHandle_);
	Log(logStream, "window shown");

	return true;
}

//========================================
// 毎フレーム処理
//========================================

bool WinApp::ProcessMessage() {
	MSG message{};

	// 溜まっている Message をすべて捌く。1 件ずつだと入力が遅れる。
	while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
		if (message.message == WM_QUIT) {
			exitCode_ = static_cast<int>(message.wParam);
			return true;  // 終了要求
		}

		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	return false;
}

void WinApp::RequestQuit() {
	if (windowHandle_ != nullptr) {
		// 直接壊さず WM_CLOSE を積む。Scene の保存確認を WindowProc が担当しているため。
		PostMessageW(windowHandle_, WM_CLOSE, 0u, 0);
	}
}

//========================================
// 終了処理
//========================================

void WinApp::Finalize() {
	if (windowHandle_ != nullptr) {
		CloseWindow(windowHandle_);
		windowHandle_ = nullptr;
	}

	if (isWindowClassRegistered_ && instanceHandle_ != nullptr) {
		UnregisterClass(kWindowClassName, instanceHandle_);
		isWindowClassRegistered_ = false;
	}

	instanceHandle_ = nullptr;
}

//========================================
// getter
//========================================

bool WinApp::GetClientSize(uint32_t& clientWidth, uint32_t& clientHeight) const {
	if (windowHandle_ == nullptr) {
		return false;
	}

	// clientRect は外枠やタイトルバーを含まない、描画に使う領域だけを返す。
	RECT clientRect{};

	if (GetClientRect(windowHandle_, &clientRect) == 0) {
		return false;
	}

	// 最小化中は 0 になるが、サイズ 0 の SwapChain は作れないので 1px へ丸める。
	clientWidth = (clientRect.right - clientRect.left) > 0
		? static_cast<uint32_t>(clientRect.right - clientRect.left)
		: 1u;
	clientHeight = (clientRect.bottom - clientRect.top) > 0
		? static_cast<uint32_t>(clientRect.bottom - clientRect.top)
		: 1u;
	return true;
}

//========================================
// Window Procedure
//========================================

LRESULT CALLBACK WinApp::WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
	// ImGui の入力欄や Docking が使用するマウス / キーボード Message を先に処理する
	if (ImGui_ImplWin32_WndProcHandler(windowHandle, message, wParam, lParam)) {
		return true;
	}
#endif

	switch (message) {
	case WM_ERASEBKGND:
		// DirectX 側で毎フレーム全面を塗るため、Windows 既定の白塗り潰しは無効にする。
		return 1;
	case WM_CLOSE: {
		using namespace EditorSharedState;

		// Play 中に閉じると、保存される g_editorScene が Runtime で動いた後の状態
		// (船の位置など)になってしまい、次回変な場所から始まる原因になる。
		// 閉じる前に必ず Stop して、Play 開始前の Backup へ復元してから保存する。
		if (g_editorRuntimeManager.IsPlaying()) {
			g_editorRuntimeManager.TogglePlay();
		}

		std::string savePath = g_currentScenePath;
		bool shouldSaveScene = false;

		if (g_isEditorSceneInitialized && !g_isStandaloneGame) {
			// Scene Path の有無に関係なく確認する。Launcher から作った Project は
			// 最初から Scene Path があるため、未保存 Scene だけを対象にすると確認が出ない。
			const int answer = MessageBoxW(
				windowHandle,
				L"シーンを保存して終了しますか？",
				L"CG2Engine Editor",
				MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);

			if (answer == IDCANCEL) {
				return 0;  // キャンセル: 閉じない
			}

			shouldSaveScene = answer == IDYES;

			if (shouldSaveScene && savePath.empty()) {
				// 保存先を選ばせる
				wchar_t fileBuffer[MAX_PATH] = L"NewScene.scene";
				OPENFILENAMEW ofn{};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = windowHandle;
				ofn.lpstrFilter = L"シーンファイル (*.scene)\0*.scene\0すべてのファイル (*.*)\0*.*\0";
				ofn.lpstrFile = fileBuffer;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrDefExt = L"scene";
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

				if (GetSaveFileNameW(&ofn) == 0) {
					return 0;  // 保存先選択をキャンセルしたので閉じない
				}

				savePath = WideToUtf8(fileBuffer);

				// 拡張子が無ければ .scene を付ける
				std::filesystem::path scenePath(savePath);
				if (scenePath.extension().empty()) {
					scenePath += ".scene";
				}
				savePath = scenePath.generic_string();
			}
		}

		if (shouldSaveScene && !savePath.empty()) {
			const std::filesystem::path parentPath = std::filesystem::path(savePath).parent_path();
			if (!parentPath.empty()) {
				std::filesystem::create_directories(parentPath);
			}

			if (!g_editorScene.SaveScene(savePath)) {
				MessageBoxW(windowHandle, L"シーンの保存に失敗しました。", L"CG2Engine Editor", MB_OK | MB_ICONERROR);
				return 0;  // 保存失敗時は閉じない
			}

			g_currentScenePath = savePath;
		}

		DestroyWindow(windowHandle);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);  // main ループ側の IsEndRequested を成立させるため、WM_QUIT を Message Queue へ積む
		return 0;
	default:
		// このアプリで処理しない Message は Windows 標準処理に渡す
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
