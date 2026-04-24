#pragma once

#include <Windows.h>
#include <cstdint>
#include <iosfwd>

constexpr wchar_t kWindowClassName[] = L"CG2WindowClass";
constexpr wchar_t kWindowTitle[] = L"CG2";
constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

HWND CreateMainWindow(HINSTANCE instanceHandle, std::ostream& logStream);
LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);
