#pragma once

#include <Windows.h>

#include <filesystem>

class PublisherGui {
public:
	static void Open(HINSTANCE instance, HWND owner, const std::filesystem::path& installRoot);
};
