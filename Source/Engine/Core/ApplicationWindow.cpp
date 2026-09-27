#include "ApplicationWindow.h"

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
std::wstring standaloneWindowTitle = kDefaultGameWindowTitle;

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

void SetStandaloneWindowTitle(const std::string& windowTitle) {
	const std::wstring convertedTitle = Utf8ToWide(windowTitle);
	standaloneWindowTitle = convertedTitle.empty() ? kDefaultGameWindowTitle : convertedTitle;
}

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

	// Project Settings の解像度 / Window Mode をここで実際に反映する。
	// SwapChain はこのクライアント領域から作られるため、ここが描画解像度そのものになる。
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
		// 枠なしでモニター全域へ広げる。排他Fullscreenより Alt+Tab や Editor 併用が安定する。
		windowStyle = WS_POPUP;
		windowPositionX = 0;
		windowPositionY = 0;
		windowWidth = GetSystemMetrics(SM_CXSCREEN);
		windowHeight = GetSystemMetrics(SM_CYSCREEN);
	}
	else {
		// 描画したいクライアント領域の左上と右下
		RECT windowRect{0, 0, requestedWidth, requestedHeight};
		// タイトルバーなどを含めた実際のウィンドウサイズへ調整する
		if (AdjustWindowRect(&windowRect, windowStyle, FALSE) == 0) {
			Log(logStream, "AdjustWindowRect failed");
			return nullptr;
		}
		Log(logStream, "window rect adjusted");
		windowWidth = windowRect.right - windowRect.left;
		windowHeight = windowRect.bottom - windowRect.top;
	}

	// 調整後のサイズを使い、描画領域が Project Settings の解像度になるように作る
	HWND windowHandle = CreateWindow(
		windowClass.lpszClassName,
		EditorSharedState::g_isStandaloneGame ? standaloneWindowTitle.c_str() : kWindowTitle,
		windowStyle,
		windowPositionX,
		windowPositionY,
		windowWidth,
		windowHeight,
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
	case WM_CLOSE: {
		using namespace EditorSharedState;

		// Play中に閉じると、保存されるg_editorSceneがRuntimeで動いた後の状態
		// (船の位置など)になってしまい、次回変な場所から始まる原因になる。
		// 閉じる前に必ずStopして、Play開始前のバックアップへ復元してから保存する。
		if (g_editorRuntimeManager.IsPlaying()) {
			g_editorRuntimeManager.TogglePlay();
		}

		std::string savePath = g_currentScenePath;
		bool shouldSaveScene = false;

		if (g_isEditorSceneInitialized && !g_isStandaloneGame) {
			// Scene Path の有無に関係なく確認する。Launcher から作った Project は
			// 最初から Scene Path があるため、未保存Sceneだけを対象にすると確認が出ない。
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
		PostQuitMessage(0);  // メインループ側の IsEndRequested を成立させるため、WM_QUIT をメッセージキューへ積む
		return 0;
	default:
		// このアプリで処理しないメッセージは Windows 標準処理に渡す
		return DefWindowProcW(windowHandle, message, wParam, lParam);
	}
}
