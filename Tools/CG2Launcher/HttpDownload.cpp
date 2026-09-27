#include "HttpDownload.h"

#include <Windows.h>
#include <winhttp.h>

#include <cwchar>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {
	std::wstring ToWide(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (count <= 0) return {};
		std::wstring result(static_cast<std::size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	struct InternetHandle {
		HINTERNET value = nullptr;
		~InternetHandle() { if (value) WinHttpCloseHandle(value); }
	};

	bool EndsWithCaseInsensitive(const std::wstring& value, const wchar_t* suffix) {
		const std::size_t suffixLength = std::wcslen(suffix);
		if (value.size() < suffixLength) {
			return false;
		}
		return CompareStringOrdinal(
			value.data() + value.size() - suffixLength,
			static_cast<int>(suffixLength),
			suffix,
			static_cast<int>(suffixLength),
			TRUE) == CSTR_EQUAL;
	}

	bool ShouldBypassAutomaticProxy(const std::wstring& host) {
		// Tailscale MagicDNSはOS内の仮想Networkへ直接問い合わせる必要がある。
		// LAN側の自動Proxy/PACへ.ts.netを渡すと、同一TailnetでもProxy側で接続に失敗する。
		return EndsWithCaseInsensitive(host, L".ts.net");
	}

	// 番号だけでは原因が分からないため、よく出るWinHTTPエラーには日本語の理由を添える。
	std::string DescribeWinHttpError(DWORD code) {
		const char* reason = nullptr;
		switch (code) {
		case ERROR_WINHTTP_NAME_NOT_RESOLVED: reason = "ホスト名を解決できません（TailscaleなどのVPN接続とDNSを確認してください）"; break;
		case ERROR_WINHTTP_CANNOT_CONNECT: reason = "接続を拒否されました（配布サーバーが停止しているか、ポートが塞がれています）"; break;
		case ERROR_WINHTTP_CONNECTION_ERROR: reason = "接続が切断されました"; break;
		case ERROR_WINHTTP_TIMEOUT: reason = "応答がありません（タイムアウト）"; break;
		case ERROR_WINHTTP_SECURE_FAILURE: reason = "TLS接続に失敗しました"; break;
		case ERROR_WINHTTP_INVALID_URL: reason = "URLの形式が正しくありません"; break;
		case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: reason = "http/https以外のURLです"; break;
		default: break;
		}
		std::string text = "Win32 " + std::to_string(code);
		if (reason != nullptr) text += " / " + std::string(reason);
		return text;
	}
}

bool DownloadHttpFile(const std::string& url, const std::filesystem::path& destination, std::string& error) {
	error.clear();
	const std::wstring wideUrl = ToWide(url);
	if (wideUrl.empty()) { error = "URLがUTF-8ではありません"; return false; }
	URL_COMPONENTSW parts{}; parts.dwStructSize = sizeof(parts);
	parts.dwSchemeLength = static_cast<DWORD>(-1); parts.dwHostNameLength = static_cast<DWORD>(-1);
	parts.dwUrlPathLength = static_cast<DWORD>(-1); parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wideUrl.c_str(), 0U, 0U, &parts) || parts.dwHostNameLength == 0U) {
		error = "URLを解析できません: " + DescribeWinHttpError(GetLastError()); return false;
	}
	const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	std::wstring requestPath(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.dwExtraInfoLength > 0U) requestPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
	if (requestPath.empty()) requestPath = L"/";
	const DWORD accessType = ShouldBypassAutomaticProxy(host)
		? WINHTTP_ACCESS_TYPE_NO_PROXY
		: WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
	InternetHandle session{WinHttpOpen(L"ManoLauncher/1.0", accessType,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0U)};
	if (!session.value) { error = "HTTP Sessionを作成できません"; return false; }
	WinHttpSetTimeouts(session.value, 5000, 5000, 15000, 30000);
	InternetHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0U)};
	if (!connection.value) { error = "Hubへ接続できません: " + DescribeWinHttpError(GetLastError()); return false; }
	const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0U;
	InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", requestPath.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
	if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0U,
		WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U) || !WinHttpReceiveResponse(request.value, nullptr)) {
		error = "HTTP requestに失敗: " + DescribeWinHttpError(GetLastError()); return false;
	}
	DWORD status = 0U; DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status < 200U || status >= 300U) {
		error = "Hub HTTP status: " + std::to_string(status);
		if (status == 404U) error += " / 配布サーバーに該当ファイルがありません（未公開の可能性）";
		return false;
	}
	std::error_code ec; std::filesystem::create_directories(destination.parent_path(), ec);
	auto temporary = destination; temporary += ".download";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) { error = "Download先を作成できません"; return false; }
	for (;;) {
		DWORD available = 0U;
		if (!WinHttpQueryDataAvailable(request.value, &available)) { error = "HTTP受信量を取得できません"; break; }
		if (available == 0U) break;
		std::vector<char> buffer(available); DWORD received = 0U;
		if (!WinHttpReadData(request.value, buffer.data(), available, &received)) { error = "HTTP受信に失敗"; break; }
		output.write(buffer.data(), received); if (!output.good()) { error = "Download File保存に失敗"; break; }
	}
	output.close();
	if (!error.empty()) { std::filesystem::remove(temporary, ec); return false; }
	if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		error = "Download Fileの確定に失敗: Win32 " + std::to_string(GetLastError()); std::filesystem::remove(temporary, ec); return false;
	}
	return true;
}
