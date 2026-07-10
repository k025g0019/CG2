#include "Log.h"

#include <Windows.h>
#include <ostream>

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());  // OutputDebugStringA は Visual Studio の「出力」ウィンドウへ narrow 文字列を送る。
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;  // os は main.log などのファイル出力先。改行込みで 1 行のログとして保存する。
	Log(message + "\n");  // デバッガ上でも同じ内容を見られるよう、末尾改行付きで出力ウィンドウへ流す。
}
