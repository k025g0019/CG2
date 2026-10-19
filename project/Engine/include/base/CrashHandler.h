#pragma once

#include <Windows.h>

void InstallCrashHandler() noexcept;
LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept;
