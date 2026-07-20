#pragma once

#include <Windows.h>
#include <cstdint>
#include <iosfwd>

constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";  // Windows に登録するウィンドウクラス名
constexpr wchar_t kWindowTitle[] = L"CG2";  // タイトルバーに表示するアプリ名
constexpr int32_t kClientWidth = 1920;  // DirectX の初期レンダーターゲット幅
constexpr int32_t kClientHeight = 1080;  // DirectX の初期レンダーターゲット高さ
HWND CreateMainWindow(HINSTANCE instanceHandle, std::ostream& logStream);  // Win32 ウィンドウを生成して、作成済み HWND を返す
LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);  // Windows から届くメッセージを ImGui とアプリ終了処理へ振り分ける
