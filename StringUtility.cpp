#include "StringUtility.h"

#include <Windows.h>

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (sizeNeeded == 0) {
		return {};
	}

	std::wstring result(static_cast<size_t>(sizeNeeded), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded);
	result.pop_back();
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return {};
	}

	const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (sizeNeeded == 0) {
		return {};
	}

	std::string result(static_cast<size_t>(sizeNeeded), '\0');
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, result.data(), sizeNeeded, nullptr, nullptr);
	result.pop_back();
	return result;
}
