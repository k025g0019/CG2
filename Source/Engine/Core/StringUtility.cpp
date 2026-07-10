#include "StringUtility.h"

#include <Windows.h>

std::wstring ConvertString(const std::string& str) {
	// 空文字は Win32 変換 API に渡さず、そのまま空の wide 文字列として返す。
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);  // sizeNeeded は終端 null を含む UTF-16 文字数。

	// 0 は変換失敗。失敗時に未初期化文字列を返さない。
	if (sizeNeeded == 0) {
		return {};
	}

	std::wstring result(static_cast<size_t>(sizeNeeded), L'\0');  // result は終端 null の分も確保してから Win32 API に書き込ませる。
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded);

	result.pop_back();  // C++ 側の文字列には終端 null を含めないため、最後の 1 文字を削る。
	return result;
}

std::string ConvertString(const std::wstring& str) {
	// 空文字は Win32 変換 API に渡さず、そのまま空の UTF-8 文字列として返す。
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);  // sizeNeeded は終端 null を含む UTF-8 バイト数。

	// 0 は変換失敗。失敗時に未初期化バッファを返さない。
	if (sizeNeeded == 0) {
		return {};
	}

	std::string result(static_cast<size_t>(sizeNeeded), '\0');  // result は終端 null の分も確保してから Win32 API に書き込ませる。
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded, nullptr, nullptr);

	result.pop_back();  // C++ 側の文字列には終端 null を含めないため、最後の 1 バイトを削る。
	return result;
}
