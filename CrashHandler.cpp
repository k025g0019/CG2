#include "CrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>
#pragma warning(push)
#pragma warning(disable : 5045)
#include <strsafe.h>
#pragma warning(pop)

#pragma comment(lib, "dbghelp.lib")

LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept {
	SYSTEMTIME time;  // Dump ファイル名に使う現在時刻
	GetLocalTime(&time);

	// 出力先の相対パス。例: ./Dumps/20260628_164300.dmp
	wchar_t filePath[MAX_PATH] = {0};
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(
		filePath,
		MAX_PATH,
		L"./Dumps/%04d%02d%02d_%02d%02d%02d.dmp",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute,
		time.wSecond);

	// Dump を書き込むためのファイルハンドル
	HANDLE dumpFileHandle = CreateFileW(
		filePath,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		0,
		nullptr);

	if (dumpFileHandle == INVALID_HANDLE_VALUE) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD processId = GetCurrentProcessId();  // Dump に紐づけるプロセス ID と例外発生スレッド ID
	DWORD threadId = GetCurrentThreadId();

	// MiniDumpWriteDump へ渡す例外の詳細情報
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{};
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	// 実際に .dmp を出力する
	MiniDumpWriteDump(
		GetCurrentProcess(),
		processId,
		dumpFileHandle,
		MiniDumpNormal,
		&minidumpInformation,
		nullptr,
		nullptr);
	CloseHandle(dumpFileHandle);
	return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() noexcept {
	SetUnhandledExceptionFilter(ExportDump);  // C++ 側で捕まらなかった例外を ExportDump に流す
}
