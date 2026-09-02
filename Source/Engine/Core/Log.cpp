#include "Log.h"

#include <Windows.h>
#include <ostream>

void Log(const std::string& /*message*/) {
	// VS出力ウィンドウは診断用の[GIZMO-DIAG]等だけを見たいという要望のため、
	// ここでの出力ウィンドウ送出は止める。ファイルログ(main.log等)には影響しない。
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;  // os は main.log などのファイル出力先。改行込みで 1 行のログとして保存する。
}
