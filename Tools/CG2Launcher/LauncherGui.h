#pragma once

#include <Windows.h>

#include <filesystem>

class LauncherGui {
public:
	static int Run(HINSTANCE instance, const std::filesystem::path& initialInvite);
};
