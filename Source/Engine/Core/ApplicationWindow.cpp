#include "ApplicationWindow.h"

#include <commdlg.h>
#include <filesystem>

#include "EditorSharedState.h"
#include "Log.h"

#pragma comment(lib, "comdlg32.lib")

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#include "ThirdParty/imgui-docking/imgui-docking/backends/imgui_impl_win32.h"
#pragma warning(pop)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace {
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
}  // namespace

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
	case WM_CLOSE: {
		using namespace EditorSharedState;
		std::string savePath = g_currentScenePath;

		if (g_isEditorSceneInitialized && savePath.empty()) {
			// 未保存シーン: 保存するか確認する
			const int answer = MessageBoxW(
				windowHandle,
				L"未保存のシーンがあります。保存しますか？",
				L"CG2 Editor",
				MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);

			if (answer == IDCANCEL) {
				return 0;  // キャンセル: 閉じない
			}

			if (answer == IDYES) {
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

		if (g_isEditorSceneInitialized && !savePath.empty()) {
			const std::filesystem::path parentPath = std::filesystem::path(savePath).parent_path();
			if (!parentPath.empty()) {
				std::filesystem::create_directories(parentPath);
			}

			if (!g_editorScene.SaveScene(savePath)) {
				MessageBoxW(windowHandle, L"シーンの保存に失敗しました。", L"CG2 Editor", MB_OK | MB_ICONERROR);
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
