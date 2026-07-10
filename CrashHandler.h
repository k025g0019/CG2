#pragma once

#include <Windows.h>

void InstallCrashHandler() noexcept;  // 未処理例外が起きたときに Dump ファイルを書き出す関数を登録する
LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept;  // 例外情報を MiniDumpWriteDump へ渡し、Dumps フォルダに .dmp を保存する
