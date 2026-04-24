#include "CrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>
#pragma warning(push)
#pragma warning(disable : 5045)
#include <strsafe.h>
#pragma warning(pop)

#pragma comment(lib, "dbghelp.lib")

LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept {
	// ダンプファイルのパスを決定する
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d%02d%02d_%02d%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

	// ダンプファイルを作成する
	HANDLE dumpFileHandle = CreateFileW(
		filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	if (dumpFileHandle == INVALID_HANDLE_VALUE) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	MiniDumpWriteDump(
		GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	CloseHandle(dumpFileHandle);
	return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() noexcept {
	SetUnhandledExceptionFilter(ExportDump);
}
