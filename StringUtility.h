#pragma once

#include <string>

std::wstring ConvertString(const std::string& str);  // UTF-8 の std::string を Win32 API 用の UTF-16 std::wstring に変換する。
std::string ConvertString(const std::wstring& str);  // UTF-16 の std::wstring をログやファイルパス表示用の UTF-8 std::string に変換する。
