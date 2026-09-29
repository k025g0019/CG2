#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include <cstdint>
#include <iosfwd>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//========================================
// Windows API のクラス化
//========================================

// Window Class の登録、Window の生成、Message の振り分けを 1 つのクラスへまとめる。
//
// このクラスが Windows との境界をすべて受け持ち、HWND を外へ配らない。
// HWND が要るもの（DirectX の SwapChain、DirectInput の協調 Level、
// ScreenToClient など）は GetHwnd() を通して受け取る。
//
// 生成と破棄は Initialize / Finalize で明示する。実体は動的に確保して、
// Window を作る前と壊した後の時間帯を Pointer の有無で表せるようにしている。
class WinApp {
public:
	//----------------------------------------
	// クラスの定数
	//----------------------------------------

	static constexpr wchar_t kWindowClassName[] = L"CG2EngineWindowClass";  // OS へ登録する Window Class 名
	static constexpr wchar_t kWindowTitle[] = L"CG2Engine Editor";  // Editor 起動時のタイトルバー
	static constexpr wchar_t kDefaultGameWindowTitle[] = L"CG2Engine Game";  // Product Name が空のときの Player 名
	static constexpr int32_t kClientWidth = 1920;  // Project Settings に解像度が無いときの既定幅
	static constexpr int32_t kClientHeight = 1080;  // 同じく既定高さ

	//----------------------------------------
	// 静的メンバ関数
	//----------------------------------------

	// Windows から直接呼ばれる Callback。呼び出し規約の都合で this を受け取れないため
	// 静的メンバ関数にしている。Instance 固有の状態は触らず、ImGui と終了処理へ振り分ける。
	static LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

	// Build Settings の Product Name を Player のタイトルへ反映する。
	// Window を作る前に決める値なので、Instance を持たずに呼べる静的メンバ関数にしている。
	static void SetStandaloneWindowTitle(const std::string& windowTitle);

	//----------------------------------------
	// 通常のメンバ関数
	//----------------------------------------

	bool Initialize(HINSTANCE instanceHandle, std::ostream& logStream);  // Window Class 登録と Window 生成。失敗したら false
	bool ProcessMessage();  // 溜まった Message を処理する。WM_QUIT を受け取ったら true（終了要求）
	void Finalize();  // Window を壊し、Window Class の登録を外す
	void RequestQuit();  // Window を閉じる要求を積む。Menu の「終了」から使う

	//----------------------------------------
	// getter
	//----------------------------------------

	HWND GetHwnd() const { return windowHandle_; }  // DirectX / DirectInput へ渡す Window Handle
	HINSTANCE GetHInstance() const { return instanceHandle_; }  // Device 生成などに使う実体 Handle
	bool HasWindow() const { return windowHandle_ != nullptr; }  // Window を作れているか
	bool GetClientSize(uint32_t& clientWidth, uint32_t& clientHeight) const;  // Client 領域の現在サイズ。取れなければ false
	int GetExitCode() const { return exitCode_; }  // WM_QUIT が運んできた終了コード

private:
	HWND windowHandle_ = nullptr;  // 生成した Window。Finalize まで保持する
	HINSTANCE instanceHandle_ = nullptr;  // WinMain が受け取ったアプリの実体
	bool isWindowClassRegistered_ = false;  // Finalize で UnregisterClass すべきかの判定
	int exitCode_ = 0;  // WM_QUIT の wParam。main の戻り値になる
};

#pragma warning(pop)
