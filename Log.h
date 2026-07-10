#pragma once

#include <iosfwd>
#include <string>

void Log(const std::string& message);  // Visual Studio の出力ウィンドウへデバッグ文字列を出す。
void Log(std::ostream& os, const std::string& message);  // ファイルストリームと Visual Studio の出力ウィンドウへ同じログを出す。
