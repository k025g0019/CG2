#include "LauncherExperience.h"

#include "HttpDownload.h"
#include "PublisherService.h"

#include "Source/Engine/Core/EngineEnvironmentCheck.h"
#include "Source/Engine/Core/ProjectVersionManager.h"

#include <Windows.h>
#include <urlmon.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <objbase.h>
#include <sstream>
#include <system_error>

#pragma comment(lib, "urlmon.lib")

namespace {
	constexpr unsigned char kBom[] = {0xEFU, 0xBBU, 0xBFU};

	std::string ReadText(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		std::ostringstream text; text << file.rdbuf();
		std::string value = text.str();
		if (value.size() >= 3U && static_cast<unsigned char>(value[0]) == kBom[0] &&
			static_cast<unsigned char>(value[1]) == kBom[1] && static_cast<unsigned char>(value[2]) == kBom[2]) value.erase(0U, 3U);
		return value;
	}

	bool WriteText(const std::filesystem::path& path, const std::string& value, std::string& error) {
		std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) { error = ec.message(); return false; }
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) { error = "Fileを作成できません: " + path.generic_string(); return false; }
		file.write(reinterpret_cast<const char*>(kBom), sizeof(kBom)); file << value;
		if (!file.good()) { error = "File保存に失敗しました: " + path.generic_string(); return false; }
		return true;
	}

	std::string JsonString(const std::string& json, const std::string& key, std::size_t start = 0U) {
		const std::string marker = "\"" + key + "\"";
		const auto keyPos = json.find(marker, start); if (keyPos == std::string::npos) return {};
		const auto colon = json.find(':', keyPos + marker.size()); if (colon == std::string::npos) return {};
		const auto quote = json.find('"', colon + 1U); if (quote == std::string::npos) return {};
		std::string value; bool escaped = false;
		for (std::size_t i = quote + 1U; i < json.size(); ++i) {
			const char c = json[i];
			if (escaped) { value.push_back(c == 'n' ? '\n' : c); escaped = false; }
			else if (c == '\\') escaped = true;
			else if (c == '"') return value;
			else value.push_back(c);
		}
		return {};
	}

	std::uint32_t JsonUInt(const std::string& json, const std::string& key) {
		const std::string marker = "\"" + key + "\"";
		const auto keyPos = json.find(marker); if (keyPos == std::string::npos) return 0U;
		const auto colon = json.find(':', keyPos + marker.size()); if (colon == std::string::npos) return 0U;
		try { return static_cast<std::uint32_t>(std::stoul(json.substr(colon + 1U))); } catch (...) { return 0U; }
	}

	std::string EscapeJson(const std::string& value) {
		std::string out; out.reserve(value.size());
		for (const char c : value) {
			if (c == '\\' || c == '"') out.push_back('\\');
			if (c == '\n') out += "\\n"; else out.push_back(c);
		}
		return out;
	}

	std::wstring ToWide(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (count <= 0) return {};
		std::wstring result(static_cast<std::size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	std::string PathToUtf8(const std::filesystem::path& path) {
		const std::wstring value = path.generic_wstring();
		if (value.empty()) return {};
		const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (count <= 0) return {};
		std::string result(static_cast<std::size_t>(count), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
			result.data(), count, nullptr, nullptr);
		return result;
	}

	std::string EncodeUrlPath(const std::filesystem::path& path) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		const std::string utf8 = PathToUtf8(path);
		std::string encoded;
		encoded.reserve(utf8.size());
		for (const unsigned char byte : utf8) {
			if (std::isalnum(byte) != 0 || byte == '-' || byte == '_' || byte == '.' || byte == '~' || byte == '/') {
				encoded.push_back(static_cast<char>(byte));
			}
			else {
				encoded.push_back('%');
				encoded.push_back(kHex[(byte >> 4U) & 0x0FU]);
				encoded.push_back(kHex[byte & 0x0FU]);
			}
		}
		return encoded;
	}

	bool IsAbsoluteUrl(const std::string& value) { return value.starts_with("http://") || value.starts_with("https://"); }
	bool IsSafeProjectId(const std::string& value) {
		return !value.empty() && value != "." && value != ".." &&
			std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
	}

	std::string HubBase(std::string host, bool secure) {
		while (!host.empty() && host.back() == '/') host.pop_back();
		if (IsAbsoluteUrl(host)) return host;
		return std::string(secure ? "https://" : "http://") + host;
	}

	std::string JoinEndpoint(const std::string& base, const std::string& endpoint) {
		if (IsAbsoluteUrl(endpoint)) return endpoint;
		if (endpoint.empty()) return base;
		return base + (endpoint.front() == '/' ? "" : "/") + endpoint;
	}

	bool RebaseEngineManifest(const std::filesystem::path& manifestPath, const std::string& hubHost,
		EngineUpdateManifest& manifest, std::string& error) {
		if (hubHost.empty()) return true;
		const std::string baseUrl = JoinEndpoint(HubBase(hubHost, true),
			"/engines/" + manifest.version.ToString());
		std::string text = ReadText(manifestPath);
		const std::size_t field = text.find("BaseUrl|");
		if (field == std::string::npos || (field != 0U && text[field - 1U] != '\n')) {
			error = "Engine ManifestにBaseUrlがありません";
			return false;
		}
		const std::size_t lineEnd = text.find_first_of("\r\n", field);
		text.replace(field, lineEnd == std::string::npos ? std::string::npos : lineEnd - field,
			"BaseUrl|" + baseUrl);
		if (!WriteText(manifestPath, text, error)) return false;
		manifest.baseUrl = baseUrl;
		return true;
	}

	bool Download(const std::string& url, const std::filesystem::path& destination, std::string& error) {
		return DownloadHttpFile(url, destination, error);
	}

	// detailには最後に試したURLの失敗理由を入れる。URLだけ出しても原因が分からず切り分けできないため。
	bool DownloadHubFile(const std::string& hub, const std::string& endpoint,
		const std::filesystem::path& destination, std::string& resolvedUrl, std::string* detail = nullptr) {
		std::error_code ec; std::filesystem::create_directories(destination.parent_path(), ec);
		std::string error;
		const auto finish = [&](bool succeeded) { if (detail) *detail = succeeded ? std::string{} : error; return succeeded; };
		if (IsAbsoluteUrl(endpoint)) { resolvedUrl = endpoint; return finish(Download(endpoint, destination, error)); }
		if (IsAbsoluteUrl(hub)) { resolvedUrl = JoinEndpoint(HubBase(hub, true), endpoint); return finish(Download(resolvedUrl, destination, error)); }
		resolvedUrl = JoinEndpoint(HubBase(hub, true), endpoint);
		if (Download(resolvedUrl, destination, error)) return finish(true);
		resolvedUrl = JoinEndpoint(HubBase(hub, false), endpoint);
		return finish(Download(resolvedUrl, destination, error));
	}

	std::string WithReason(const std::string& message, const std::string& detail) {
		if (detail.empty()) return message;
		return message + "\r\n理由: " + detail;
	}

	// 入力されたCodeの揺れを吸収する。区切りや大文字小文字を無視し、見間違えやすい文字を寄せる。
	std::string NormalizeJoinCode(const std::string& text) {
		std::string normalized;
		for (const unsigned char character : text) {
			if (std::isalnum(character) == 0) continue;
			char upper = static_cast<char>(std::toupper(character));
			if (upper == 'I' || upper == 'L') upper = '1';
			else if (upper == 'O') upper = '0';
			else if (upper == 'U') upper = 'V';
			normalized.push_back(upper);
		}
		return normalized;
	}

	std::vector<std::string> Split(const std::string& line, char separator) {
		std::vector<std::string> values; std::size_t begin = 0U;
		for (;;) { const auto end = line.find(separator, begin); values.push_back(line.substr(begin, end - begin)); if (end == std::string::npos) return values; begin = end + 1U; }
	}

	std::string CreateLauncherUuid() {
		GUID guid{};
		if (CoCreateGuid(&guid) != S_OK) return std::to_string(GetTickCount64());
		char value[37]{};
		std::snprintf(value, sizeof(value), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
			guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
			guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
		return value;
	}

	std::string CurrentWindowsUserName() {
		wchar_t value[256]{};
		DWORD count = static_cast<DWORD>(std::size(value));
		if (!GetUserNameW(value, &count) || count <= 1U) return "owner";
		return PathToUtf8(std::filesystem::path(std::wstring(value, count - 1U)));
	}

	std::filesystem::path TarExecutable() {
		wchar_t systemDirectory[32768]{};
		const UINT length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
		if (length == 0U || length >= std::size(systemDirectory)) return {};
		return std::filesystem::path(systemDirectory) / "tar.exe";
	}

	std::wstring QuoteArgument(const std::filesystem::path& value) {
		return L"\"" + value.wstring() + L"\"";
	}

	bool RunProcessAndWait(const std::filesystem::path& executable, const std::wstring& arguments,
		const std::filesystem::path& workingDirectory, const std::filesystem::path& outputPath,
		DWORD& exitCode, std::string& error) {
		std::wstring command = QuoteArgument(executable) + L" " + arguments;
		std::vector<wchar_t> writable(command.begin(), command.end());
		writable.push_back(L'\0');
		HANDLE output = INVALID_HANDLE_VALUE;
		SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
		if (!outputPath.empty()) {
			output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
				CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
			if (output == INVALID_HANDLE_VALUE) { error = "一時出力を作成できません"; return false; }
		}
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		if (output != INVALID_HANDLE_VALUE) {
			startup.dwFlags |= STARTF_USESTDHANDLES;
			startup.hStdOutput = output;
			startup.hStdError = output;
			startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		}
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr,
			output != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
		if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
		if (!created) { error = "外部処理を開始できません (Windows " + std::to_string(GetLastError()) + ")"; return false; }
		WaitForSingleObject(process.hProcess, INFINITE);
		const bool readExit = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		if (!readExit) { error = "ZIP処理の終了コードを取得できません"; return false; }
		return true;
	}

	bool IsSafeArchiveEntry(const std::string& entry) {
		if (entry.empty() || entry.front() == '/' || entry.front() == '\\') return false;
		const std::filesystem::path path(entry);
		if (path.is_absolute() || path.has_root_path()) return false;
		for (const auto& part : path) if (part == "..") return false;
		return entry.find(':') == std::string::npos;
	}

	bool ExtractZipSafely(const std::filesystem::path& archivePath,
		const std::filesystem::path& destination, std::string& error) {
		const auto tar = TarExecutable();
		if (tar.empty() || !std::filesystem::exists(tar)) { error = "Windows標準のtar.exeが見つかりません"; return false; }
		std::error_code ec;
		std::filesystem::create_directories(destination, ec);
		const auto listing = destination.parent_path() / (destination.filename().wstring() + L".entries.txt");
		DWORD exitCode = 0U;
		if (!RunProcessAndWait(tar, L"-tf " + QuoteArgument(archivePath), {}, listing, exitCode, error) || exitCode != 0U) {
			std::filesystem::remove(listing, ec);
			if (error.empty()) error = "ZIPの内容一覧を読めません";
			return false;
		}
		std::istringstream entries(ReadText(listing));
		std::string entry;
		bool hasEntry = false;
		while (std::getline(entries, entry)) {
			if (!entry.empty() && entry.back() == '\r') entry.pop_back();
			if (!IsSafeArchiveEntry(entry)) {
				std::filesystem::remove(listing, ec);
				error = "ZIPに安全でないPathが含まれています: " + entry;
				return false;
			}
			hasEntry = true;
		}
		std::filesystem::remove(listing, ec);
		if (!hasEntry) { error = "ZIPが空です"; return false; }
		const auto types = destination.parent_path() / (destination.filename().wstring() + L".types.txt");
		if (!RunProcessAndWait(tar, L"-tvf " + QuoteArgument(archivePath), {}, types, exitCode, error) || exitCode != 0U) {
			std::filesystem::remove(types, ec);
			if (error.empty()) error = "ZIPのEntry種別を確認できません";
			return false;
		}
		std::istringstream typeLines(ReadText(types));
		while (std::getline(typeLines, entry)) {
			// symlink / hardlinkを許可すると、展開先の外へfileを書けるため取込前に拒否する。
			if (!entry.empty() && (entry.front() == 'l' || entry.front() == 'h')) {
				std::filesystem::remove(types, ec);
				error = "ZIPにLink Entryが含まれているため安全に展開できません";
				return false;
			}
		}
		std::filesystem::remove(types, ec);
		if (!RunProcessAndWait(tar, L"-xf " + QuoteArgument(archivePath) + L" -C " + QuoteArgument(destination),
			{}, {}, exitCode, error) || exitCode != 0U) {
			if (error.empty()) error = "ZIPを展開できません";
			return false;
		}
		return true;
	}

	bool CreateZip(const std::filesystem::path& sourceParent, const std::filesystem::path& sourceName,
		const std::filesystem::path& outputZip, std::string& error) {
		const auto tar = TarExecutable();
		if (tar.empty() || !std::filesystem::exists(tar)) { error = "Windows標準のtar.exeが見つかりません"; return false; }
		std::error_code ec;
		if (!outputZip.parent_path().empty()) std::filesystem::create_directories(outputZip.parent_path(), ec);
		if (ec) { error = "ZIP保存先を作成できません: " + ec.message(); return false; }
		DWORD exitCode = 0U;
		const std::wstring arguments = L"-a -cf " + QuoteArgument(outputZip) + L" -C " +
			QuoteArgument(sourceParent) + L" " + QuoteArgument(sourceName);
		if (!RunProcessAndWait(tar, arguments, {}, {}, exitCode, error) || exitCode != 0U) {
			if (error.empty()) error = "ZIPを作成できません";
			return false;
		}
		return true;
	}

	bool ShouldSkipProjectEntry(const std::filesystem::path& relative) {
		if (relative.empty()) return false;
		const std::string first = relative.begin()->string();
		return first == ".git" || first == ".team" || first == "Library" ||
			first == "Builds" || first == "BuildLogs" || first == "logs";
	}

	bool CopyProjectSnapshot(const std::filesystem::path& source,
		const std::filesystem::path& destination, std::string& error) {
		std::error_code ec;
		std::filesystem::create_directories(destination, ec);
		for (std::filesystem::recursive_directory_iterator iterator(source,
			std::filesystem::directory_options::skip_permission_denied, ec), end; iterator != end; iterator.increment(ec)) {
			if (ec) { error = ec.message(); return false; }
			const auto& entry = *iterator;
			const auto relative = std::filesystem::relative(entry.path(), source, ec);
			if (ShouldSkipProjectEntry(relative)) {
				if (entry.is_directory(ec)) iterator.disable_recursion_pending();
				continue;
			}
			const auto target = destination / relative;
			if (entry.is_directory(ec)) std::filesystem::create_directories(target, ec);
			else if (entry.is_regular_file(ec)) {
				std::filesystem::create_directories(target.parent_path(), ec);
				std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
			}
			if (ec) { error = "Project ZIP用copyに失敗しました: " + entry.path().generic_string(); return false; }
		}
		return true;
	}

	bool WriteProjectRecoveryFiles(const std::filesystem::path& projectRoot, std::string& error) {
		static constexpr const char* kGitIgnore =
			"# ManoEngine generated/cache\r\nLibrary/\r\nBuilds/\r\nBuildLogs/\r\nlogs/\r\n.team/\r\n.vs/\r\n"
			"**/obj/\r\n**/x64/Debug/\r\n*.pdb\r\n*.ilk\r\n*.exp\r\n*.lib\r\n*.user\r\n*.suo\r\n";
		std::error_code ec;
		if (!std::filesystem::exists(projectRoot / ".gitignore", ec) &&
			!WriteText(projectRoot / ".gitignore", kGitIgnore, error)) return false;
		static constexpr const char* kStartScript =
			"$ErrorActionPreference = 'Stop'\r\n"
			"$project = $PSScriptRoot\r\n"
			"$versionFile = Join-Path $project 'ProjectSettings\\ProjectVersion.cg2'\r\n"
			"$versionLine = Get-Content -LiteralPath $versionFile | Where-Object { $_ -like 'RequiredEngineVersion|*' } | Select-Object -First 1\r\n"
			"if (-not $versionLine) { throw 'ProjectのRequiredEngineVersionを読めません。' }\r\n"
			"$version = ($versionLine -split '\\|', 2)[1]\r\n"
			"$candidates = @(\r\n"
			"  (Join-Path $project ('PortableEngine\\' + $version + '\\CG2.exe')),\r\n"
			"  (Join-Path $project 'PortableEngine\\CG2.exe'),\r\n"
			"  (Join-Path $env:LOCALAPPDATA ('ManoEngine\\Engines\\' + $version + '\\CG2.exe')),\r\n"
			"  ('C:\\ManoHub\\Engines\\' + $version + '\\CG2.exe')\r\n"
			")\r\n"
			"if ($env:MANOENGINE_INSTALL_ROOT) { $candidates += (Join-Path $env:MANOENGINE_INSTALL_ROOT ('Engines\\' + $version + '\\CG2.exe')) }\r\n"
			"$engine = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1\r\n"
			"if (-not $engine) { throw ('Engine ' + $version + ' が見つかりません。Portable Engine ZIPを展開するかOffline導入してください。') }\r\n"
			"Start-Process -FilePath $engine -ArgumentList @('--project', $project) -WorkingDirectory (Split-Path -Parent $engine)\r\n";
		if (!std::filesystem::exists(projectRoot / "OpenManoProject.ps1", ec) &&
			!WriteText(projectRoot / "OpenManoProject.ps1", kStartScript, error)) return false;
		static constexpr const char* kGuide =
			"# Launcherが使えない場合\r\n\r\n"
			"- `OpenManoProject.ps1` をPowerShellで実行すると、Project指定のEngineを直接起動します。\r\n"
			"- Hubへ接続できない場合は、Portable Engine ZIPをOffline導入できます。\r\n"
			"- 共同制作サーバーが使えない場合は、Launcherの「Git共有へ切替」を使い、Gitでcommit/pull/pushしてください。\r\n"
			"- Git切替は自動接続を停止します。commitやpushは勝手に実行しません。\r\n";
		return std::filesystem::exists(projectRoot / "COLLABORATION_FALLBACK.md", ec) ||
			WriteText(projectRoot / "COLLABORATION_FALLBACK.md", kGuide, error);
	}

	bool UpdateTeamFallback(const std::filesystem::path& projectRoot, bool enabled, std::string& error) {
		const auto path = projectRoot / "ProjectSettings" / "TeamCollaboration.settings";
		const std::string source = ReadText(path);
		std::istringstream inspect(source);
		std::string line;
		std::string currentAutoConnect = "0";
		std::string currentAutoStart = "0";
		std::string previousAutoConnect;
		std::string previousAutoStart;
		std::string currentMode;
		while (std::getline(inspect, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto values = Split(line, '|');
			if (values.size() != 2U) continue;
			if (values[0] == "AutoConnect") currentAutoConnect = values[1];
			else if (values[0] == "AutoStartServer") currentAutoStart = values[1];
			else if (values[0] == "FallbackPreviousAutoConnect") previousAutoConnect = values[1];
			else if (values[0] == "FallbackPreviousAutoStart") previousAutoStart = values[1];
			else if (values[0] == "FallbackMode") currentMode = values[1];
		}
		if (enabled && currentMode != "Git") {
			previousAutoConnect = currentAutoConnect;
			previousAutoStart = currentAutoStart;
		}
		const std::string wantedAutoConnect = enabled ? "0" : (previousAutoConnect.empty() ? "1" : previousAutoConnect);
		const std::string wantedAutoStart = enabled ? "0" : (previousAutoStart.empty() ? currentAutoStart : previousAutoStart);

		std::istringstream input(source);
		std::ostringstream output;
		bool wroteAutoConnect = false;
		bool wroteAutoStart = false;
		bool wroteMode = false;
		bool wrotePreviousConnect = false;
		bool wrotePreviousStart = false;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.starts_with("AutoConnect|")) { output << "AutoConnect|" << wantedAutoConnect << "\r\n"; wroteAutoConnect = true; }
			else if (line.starts_with("AutoStartServer|")) { output << "AutoStartServer|" << wantedAutoStart << "\r\n"; wroteAutoStart = true; }
			else if (line.starts_with("FallbackMode|")) { output << "FallbackMode|" << (enabled ? "Git" : "Realtime") << "\r\n"; wroteMode = true; }
			else if (line.starts_with("FallbackPreviousAutoConnect|")) { output << "FallbackPreviousAutoConnect|" << previousAutoConnect << "\r\n"; wrotePreviousConnect = true; }
			else if (line.starts_with("FallbackPreviousAutoStart|")) { output << "FallbackPreviousAutoStart|" << previousAutoStart << "\r\n"; wrotePreviousStart = true; }
			else if (!line.empty()) output << line << "\r\n";
		}
		if (!wroteAutoConnect) output << "AutoConnect|" << wantedAutoConnect << "\r\n";
		if (!wroteAutoStart) output << "AutoStartServer|" << wantedAutoStart << "\r\n";
		if (!wroteMode) output << "FallbackMode|" << (enabled ? "Git" : "Realtime") << "\r\n";
		if (!wrotePreviousConnect) output << "FallbackPreviousAutoConnect|" << previousAutoConnect << "\r\n";
		if (!wrotePreviousStart) output << "FallbackPreviousAutoStart|" << previousAutoStart << "\r\n";
		return WriteText(path, output.str(), error);
	}

	bool WriteProjectCollaborationMetadata(const std::filesystem::path& projectRoot,
		const std::string& projectId, const std::string& projectName,
		const std::string& collaborationId, const std::string& ownerId,
		std::uint64_t lastSyncedRevision, std::string& error) {
		std::ostringstream metadata;
		metadata << "ManoProjectCollaboration|1\r\n"
			<< "ProjectId|" << projectId << "\r\n"
			<< "ProjectName|" << projectName << "\r\n"
			<< "CollaborationId|" << collaborationId << "\r\n"
			<< "OwnerId|" << ownerId << "\r\n"
			<< "LastSyncedRevision|" << lastSyncedRevision << "\r\n";
		return WriteText(projectRoot / "ProjectSettings" / "ProjectCollaboration.cg2",
			metadata.str(), error);
	}

	void RefreshProjectCollaborationMetadata(RegisteredProject& project) {
		if (project.projectRoot.empty()) return;
		std::istringstream input(ReadText(project.projectRoot / "ProjectSettings" /
			"ProjectCollaboration.cg2"));
		std::string line;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto values = Split(line, '|');
			if (values.size() != 2U) continue;
			if (values[0] == "ProjectId") project.projectId = values[1];
			else if (values[0] == "ProjectName") project.projectName = values[1];
			else if (values[0] == "CollaborationId") project.collaborationId = values[1];
			else if (values[0] == "OwnerId") project.ownerId = values[1];
			else if (values[0] == "LastSyncedRevision") {
				try { project.lastSyncedRevision = std::stoull(values[1]); } catch (...) {}
			}
		}
	}

	// ProjectのEngine設定はProject内のProjectVersion.cg2が正で、registry行はその写しである。
	// Editorの「現在Engine Versionへ固定」やGit/共同制作での取得はregistryを更新しないため、
	// 読み込み時に読み直さないとLauncherだけが古いEngineを起動し続ける。
	void RefreshProjectEngineVersion(
		RegisteredProject& project,
		const std::filesystem::path& installRoot) {
		if (project.projectRoot.empty()) return;
		ProjectVersionSettings settings{}; std::string error;
		if (!ProjectVersionManager::Load(project.projectRoot, settings, error)) return;
		const std::string version = settings.requiredEngineVersion.ToString();
		if (version == project.requiredEngineVersion && settings.updateChannel == project.updateChannel) return;
		project.requiredEngineVersion = version;
		project.updateChannel = settings.updateChannel;
		// 導入状態はEngine Versionごとに変わるので、Engine有無から決まる状態だけ作り直す。
		if (project.status == "Ready" || project.status == "Needs Install") {
			std::error_code ec;
			project.status = std::filesystem::exists(
				LauncherUpdate::GetEnginesDirectory(installRoot) / version / "CG2.exe", ec)
				? "Ready" : "Needs Install";
		}
	}

	struct ProjectSnapshotManifest {
		std::string projectId;
		std::string projectName;
		std::string requiredEngineVersion;
		std::uint32_t requiredScriptApiVersion = 0U;
		std::uint32_t projectFormatVersion = 0U;
		std::uint64_t snapshotRevision = 0U;
		std::string collaborationId;
		std::string ownerId;
		EngineUpdateChannel channel = EngineUpdateChannel::Stable;
		std::string baseUrl;
		std::string collaborationHost;
		std::uint16_t collaborationPort = 48000U;
		std::vector<EngineManifestFile> files;
	};

	bool IsSafeRelativeProjectPath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
		const std::string normalized = PathToUtf8(path.lexically_normal());
		return !normalized.empty() && normalized != "." && !normalized.starts_with("../") &&
			normalized.find('|') == std::string::npos;
	}

	bool LoadProjectSnapshotManifest(const std::filesystem::path& path,
		ProjectSnapshotManifest& manifest, std::string& error) {
		ProjectSnapshotManifest loaded{}; bool header = false;
		std::istringstream input(ReadText(path)); std::string line;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto values = Split(line, '|'); if (values.empty()) continue;
			if (values[0] == "ManoProjectManifest" && values.size() == 2U && values[1] == "1") header = true;
			else if (values.size() == 2U && values[0] == "ProjectId") loaded.projectId = values[1];
			else if (values.size() == 2U && values[0] == "ProjectName") loaded.projectName = values[1];
			else if (values.size() == 2U && values[0] == "RequiredEngineVersion") loaded.requiredEngineVersion = values[1];
			else if (values.size() == 2U && values[0] == "RequiredScriptApiVersion") {
				try { loaded.requiredScriptApiVersion = static_cast<std::uint32_t>(std::stoul(values[1])); } catch (...) { loaded.requiredScriptApiVersion = 0U; }
			}
			else if (values.size() == 2U && values[0] == "ProjectFormatVersion") {
				try { loaded.projectFormatVersion = static_cast<std::uint32_t>(std::stoul(values[1])); } catch (...) { loaded.projectFormatVersion = 0U; }
			}
			else if (values.size() == 2U && values[0] == "SnapshotRevision") {
				try { loaded.snapshotRevision = std::stoull(values[1]); } catch (...) { loaded.snapshotRevision = 0U; }
			}
			else if (values.size() == 2U && values[0] == "CollaborationId") loaded.collaborationId = values[1];
			else if (values.size() == 2U && values[0] == "OwnerId") loaded.ownerId = values[1];
			else if (values.size() == 2U && values[0] == "Channel") TryParseEngineUpdateChannel(values[1], loaded.channel);
			else if (values.size() == 2U && values[0] == "BaseUrl") loaded.baseUrl = values[1];
			else if (values.size() == 2U && values[0] == "CollaborationHost") loaded.collaborationHost = values[1];
			else if (values.size() == 2U && values[0] == "CollaborationPort") {
				try { loaded.collaborationPort = static_cast<std::uint16_t>(std::stoul(values[1])); } catch (...) { loaded.collaborationPort = 0U; }
			}
			else if (values.size() == 4U && values[0] == "File") {
				EngineManifestFile file{}; file.relativePath = ToWide(values[1]);
				try { file.size = std::stoull(values[2]); } catch (...) { error = "Project Manifestのサイズが不正です"; return false; }
				file.sha256 = values[3];
				if (!IsSafeRelativeProjectPath(file.relativePath) || file.sha256.size() != 64U) {
					error = "Project Manifestに危険または不正なファイルがあります"; return false;
				}
				loaded.files.push_back(std::move(file));
			}
		}
		if (!header || !IsSafeProjectId(loaded.projectId) || loaded.projectName.empty() ||
			loaded.requiredEngineVersion.empty() || loaded.requiredScriptApiVersion == 0U ||
			loaded.projectFormatVersion == 0U || loaded.collaborationId.empty() || loaded.ownerId.empty() ||
			loaded.baseUrl.empty() || loaded.files.empty()) {
			error = "Project Manifestの必須情報が不足しています"; return false;
		}
		manifest = std::move(loaded); return true;
	}

	std::filesystem::path DefaultProjectsDirectory() {
		wchar_t profile[32768]{};
		const DWORD count = GetEnvironmentVariableW(L"USERPROFILE", profile, _countof(profile));
		const std::filesystem::path root = count > 0U && count < _countof(profile)
			? std::filesystem::path(profile) / "Documents" : std::filesystem::current_path();
		return root / "ManoEngine Projects";
	}

	std::wstring SafeProjectFolderName(const std::string& name) {
		std::wstring value = ToWide(name);
		for (wchar_t& character : value) {
			if (std::wstring(L"<>:\"/\\|?*").find(character) != std::wstring::npos) character = L'_';
		}
		while (!value.empty() && (value.back() == L'.' || value.back() == L' ')) value.pop_back();
		return value.empty() ? L"ManoProject" : value;
	}

	std::filesystem::path FindAvailableProjectDirectory(const std::string& projectName) {
		const auto parent = DefaultProjectsDirectory();
		const std::wstring name = SafeProjectFolderName(projectName);
		std::error_code ec; std::filesystem::create_directories(parent, ec);
		for (std::uint32_t suffix = 1U; suffix < 10000U; ++suffix) {
			const auto candidate = parent / (suffix == 1U ? name : name + L" (" + std::to_wstring(suffix) + L")");
			if (!std::filesystem::exists(candidate, ec)) return candidate;
		}
		return {};
	}

	bool DownloadProjectSnapshot(const ManoInvite& invite, const std::filesystem::path& installRoot,
		std::filesystem::path& projectRoot, std::string& result, const LauncherProgress& progress) {
		const std::string endpoint = invite.projectEndpoint.empty()
			? "/projects/" + invite.projectId + "/project.manifest" : invite.projectEndpoint;
		const auto cacheManifest = installRoot / "Cache" / "Hub" / invite.projectId / "project.manifest";
		std::string resolvedUrl; std::string detail;
		if (!DownloadHubFile(invite.hubHost, endpoint, cacheManifest, resolvedUrl, &detail)) {
			result = WithReason("Project初期Snapshotを取得できません: " + resolvedUrl, detail); return false;
		}
		ProjectSnapshotManifest manifest{};
		if (!LoadProjectSnapshotManifest(cacheManifest, manifest, result)) return false;
		if (manifest.projectId != invite.projectId || manifest.channel != invite.updateChannel) {
			result = "Project Snapshotと参加情報が一致しません"; return false;
		}
		if (!invite.requiredEngineVersion.empty() && manifest.requiredEngineVersion != invite.requiredEngineVersion) {
			result = "Project SnapshotのEngineバージョンが参加情報と一致しません"; return false;
		}

		projectRoot = FindAvailableProjectDirectory(manifest.projectName);
		if (projectRoot.empty()) { result = "Project作成先を決められません"; return false; }
		auto staging = projectRoot;
		staging += L".joining-" + std::to_wstring(GetCurrentProcessId());
		std::error_code ec;
		if (std::filesystem::exists(staging, ec)) { result = "同じProjectの取得処理が既に存在します"; return false; }
		std::filesystem::create_directories(staging, ec);
		if (ec) { result = "Project作成先を準備できません: " + ec.message(); return false; }

		std::uint32_t processed = 0U;
		for (const auto& file : manifest.files) {
			const auto destination = staging / file.relativePath;
			std::string fileUrl;
			if (!DownloadHubFile(manifest.baseUrl, EncodeUrlPath(file.relativePath), destination, fileUrl)) {
				result = "Projectファイルを取得できません: " + PathToUtf8(file.relativePath); std::filesystem::remove_all(staging, ec); return false;
			}
			std::string hashError;
			if (std::filesystem::file_size(destination, ec) != file.size ||
				LauncherUpdate::CalculateSha256(destination, hashError) != file.sha256) {
				result = "Projectファイルの検証に失敗しました: " + PathToUtf8(file.relativePath); std::filesystem::remove_all(staging, ec); return false;
			}
			++processed;
			if (progress && (processed == manifest.files.size() || processed % 20U == 0U)) {
				progress("Projectを取得中 " + std::to_string(processed) + " / " + std::to_string(manifest.files.size()));
			}
		}

		std::ostringstream collaboration;
		collaboration << "TeamCollaborationInvite|1\r\nProjectId|" << manifest.projectId
			<< "\r\nCollaborationId|" << manifest.collaborationId
			<< "\r\nOwnerId|" << manifest.ownerId
			<< "\r\nCollaborationHost|" << manifest.collaborationHost
			<< "\r\nCollaborationPort|" << manifest.collaborationPort
			<< "\r\nRevision|" << manifest.snapshotRevision
			<< "\r\nLastSyncedRevision|" << manifest.snapshotRevision
			<< "\r\nAutoConnect|1\r\nIsHost|0\r\n";
		if (!WriteText(staging / "ProjectSettings" / "TeamCollaboration.invite", collaboration.str(), result)) {
			std::filesystem::remove_all(staging, ec); return false;
		}
		if (!WriteProjectCollaborationMetadata(staging, manifest.projectId, manifest.projectName,
			manifest.collaborationId, manifest.ownerId, manifest.snapshotRevision, result)) {
			std::filesystem::remove_all(staging, ec); return false;
		}
		ProjectVersionSettings projectVersion{};
		if (!ProjectVersionManager::Load(staging, projectVersion, result)) {
			result = "取得したProjectのVersion情報が不正です: " + result; std::filesystem::remove_all(staging, ec); return false;
		}
		if (projectVersion.requiredEngineVersion.ToString() != manifest.requiredEngineVersion ||
			projectVersion.requiredScriptApiVersion != manifest.requiredScriptApiVersion ||
			projectVersion.projectFormatVersion != manifest.projectFormatVersion ||
			projectVersion.updateChannel != manifest.channel) {
			result = "Project Snapshot内のVersion情報がManifestと一致しません";
			std::filesystem::remove_all(staging, ec); return false;
		}
		std::filesystem::rename(staging, projectRoot, ec);
		if (ec) { result = "Projectを確定できません: " + ec.message(); std::filesystem::remove_all(staging, ec); return false; }
		return true;
	}

}

std::filesystem::path LauncherExperience::DefaultInstallRoot() {
	wchar_t value[32768]{};
	const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", value, _countof(value));
	return count > 0U && count < _countof(value) ? std::filesystem::path(value) / "ManoEngine" : std::filesystem::current_path() / "ManoEngineInstall";
}

std::string LauncherExperience::DefaultHubAddress() {
	char configured[2048]{};
	const DWORD count = GetEnvironmentVariableA("MANOENGINE_HUB", configured, static_cast<DWORD>(std::size(configured)));
	if (count > 0U && count < std::size(configured)) return configured;
	// 配布するManoLauncher.exe単体で発見できる既定Hub。環境変数があればそちらを優先する。
	return "http://ms.tailf0bf0a.ts.net:8080";
}

bool LauncherExperience::LoadInvite(const std::filesystem::path& path, ManoInvite& invite, std::string& error) {
	if (path.extension() != ".mano-invite") { error = "*.mano-inviteを選択してください"; return false; }
	const std::string json = ReadText(path); if (json.empty()) { error = "Inviteを読めません"; return false; }
	ManoInvite loaded{}; loaded.formatVersion = JsonUInt(json, "formatVersion");
	loaded.projectId = JsonString(json, "projectId"); loaded.projectName = JsonString(json, "projectName");
	loaded.hubHost = JsonString(json, "hub"); loaded.requiredEngineVersion = JsonString(json, "requiredEngineVersion");
	loaded.projectEndpoint = JsonString(json, "projectEndpoint"); loaded.engineManifestEndpoint = JsonString(json, "engineManifestEndpoint");
	// Collaboration設定は任意。持たない旧形式Inviteでも読み込みは失敗させない。
	loaded.collaborationHost = JsonString(json, "collaborationHost");
	loaded.collaborationPort = static_cast<std::uint16_t>(JsonUInt(json, "collaborationPort"));
	loaded.collaborationId = JsonString(json, "collaborationId");
	loaded.ownerId = JsonString(json, "ownerId");
	loaded.snapshotRevision = JsonUInt(json, "snapshotRevision");
	const std::string channel = JsonString(json, "updateChannel");
	if (loaded.formatVersion != 1U || !IsSafeProjectId(loaded.projectId) || loaded.projectName.empty() || loaded.hubHost.empty() ||
		!TryParseEngineUpdateChannel(channel, loaded.updateChannel)) { error = "Inviteの必須FieldまたはVersionが不正です"; return false; }
	invite = std::move(loaded); return true;
}

bool LauncherExperience::SaveInvite(const std::filesystem::path& path, const ManoInvite& invite, std::string& error) {
	if (!IsSafeProjectId(invite.projectId) || invite.projectName.empty() || invite.hubHost.empty()) {
		error = "InviteのProject ID、Name、Hubは必須です"; return false;
	}
	std::ostringstream json;
	json << "{\r\n  \"formatVersion\": 1,\r\n  \"projectId\": \"" << EscapeJson(invite.projectId)
		<< "\",\r\n  \"projectName\": \"" << EscapeJson(invite.projectName) << "\",\r\n  \"hub\": \""
		<< EscapeJson(invite.hubHost) << "\",\r\n  \"updateChannel\": \"" << GetEngineUpdateChannelText(invite.updateChannel)
		<< "\",\r\n  \"requiredEngineVersion\": \"" << EscapeJson(invite.requiredEngineVersion)
		<< "\",\r\n  \"projectEndpoint\": \"" << EscapeJson(invite.projectEndpoint)
		<< "\",\r\n  \"engineManifestEndpoint\": \"" << EscapeJson(invite.engineManifestEndpoint)
		<< "\",\r\n  \"collaborationHost\": \"" << EscapeJson(invite.collaborationHost)
		<< "\",\r\n  \"collaborationPort\": " << invite.collaborationPort
		<< ",\r\n  \"collaborationId\": \"" << EscapeJson(invite.collaborationId)
		<< "\",\r\n  \"ownerId\": \"" << EscapeJson(invite.ownerId)
		<< "\",\r\n  \"snapshotRevision\": " << invite.snapshotRevision << "\r\n}\r\n";
	return WriteText(path, json.str(), error);
}

bool LauncherExperience::ResolveManifest(const ManoInvite& invite, const std::filesystem::path& installRoot,
	std::filesystem::path& manifestPath, EngineUpdateManifest& manifest, std::string& error) {
	const auto cache = installRoot / "Cache" / "Hub" / invite.projectId;
	std::error_code ec; std::filesystem::create_directories(cache, ec);
	std::string endpoint = invite.engineManifestEndpoint;
	if (endpoint.empty()) {
		const auto hubInfo = cache / "mano-hub.json"; std::string hubInfoUrl;
		if (DownloadHubFile(invite.hubHost, "/mano-hub.json", hubInfo, hubInfoUrl)) {
			const std::string json = ReadText(hubInfo);
			const std::string availableLauncher = JsonString(json, "launcherVersion");
			if (!availableLauncher.empty() && availableLauncher != kManoLauncherVersion) {
				std::string noticeError;
				WriteText(LauncherUpdate::GetLauncherStateDirectory(installRoot) / "launcher-update.notice",
					"Launcher Update Available|" + std::string(kManoLauncherVersion) + "|" + availableLauncher + "\r\n", noticeError);
			}
			const std::string channel = GetEngineUpdateChannelText(invite.updateChannel);
			const auto channelPos = json.find("\"" + channel + "\"");
			if (channelPos != std::string::npos) endpoint = JsonString(json, "manifestEndpoint", channelPos);
		}
		if (endpoint.empty()) {
			std::string channel = GetEngineUpdateChannelText(invite.updateChannel);
			std::transform(channel.begin(), channel.end(), channel.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			endpoint = "/update/" + channel + "/engine.manifest";
		}
	}
	manifestPath = cache / "engine.manifest"; std::string resolvedUrl; std::string detail;
	if (!DownloadHubFile(invite.hubHost, endpoint, manifestPath, resolvedUrl, &detail)) {
		error = WithReason("Hubへ接続できないかEngine情報を取得できません: " + resolvedUrl, detail); return false;
	}
	if (!LauncherUpdate::LoadManifest(manifestPath, manifest, error)) return false;
	if (manifest.channel != invite.updateChannel) { error = "InviteとHubのUpdate Channelが一致しません"; return false; }
	if (!invite.requiredEngineVersion.empty() && manifest.version.ToString() != invite.requiredEngineVersion) {
		const std::string fallback = "/engines/" + invite.requiredEngineVersion + "/engine.manifest";
		if (!DownloadHubFile(invite.hubHost, fallback, manifestPath, resolvedUrl, &detail) ||
			!LauncherUpdate::LoadManifest(manifestPath, manifest, error) || manifest.version.ToString() != invite.requiredEngineVersion) {
			error = WithReason("Project指定EngineをHubから取得できません: " + invite.requiredEngineVersion, detail); return false;
		}
	}
	return RebaseEngineManifest(manifestPath, invite.hubHost, manifest, error);
}

bool LauncherExperience::LoadProjects(const std::filesystem::path& installRoot,
	std::vector<RegisteredProject>& projects, std::string& error) {
	projects.clear(); const auto path = LauncherUpdate::GetLauncherStateDirectory(installRoot) / "projects.registry";
	std::ifstream file(path, std::ios::binary); if (!file) return true;
	std::string line;
	while (std::getline(file, line)) {
		if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == kBom[0]) line.erase(0U, 3U);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto p = Split(line, '|'); if ((p.size() != 9U && p.size() != 12U) || p[0] != "Project") continue;
		RegisteredProject item{}; item.projectId = p[1]; item.projectName = p[2]; item.hubHost = p[3];
		TryParseEngineUpdateChannel(p[4], item.updateChannel); item.requiredEngineVersion = p[5];
		item.projectEndpoint = p[6]; item.projectRoot = ToWide(p[7]); item.status = p[8];
		if (p.size() == 12U) {
			item.collaborationId = p[9]; item.ownerId = p[10];
			try { item.lastSyncedRevision = std::stoull(p[11]); } catch (...) { item.lastSyncedRevision = 0U; }
		}
		RefreshProjectCollaborationMetadata(item);
		RefreshProjectEngineVersion(item, installRoot);
		projects.push_back(std::move(item));
	}
	return true;
}

bool LauncherExperience::SaveProjects(const std::filesystem::path& installRoot,
	const std::vector<RegisteredProject>& projects, std::string& error) {
	std::ostringstream text;
	for (const auto& p : projects) {
		if (p.projectId.find('|') != std::string::npos || p.projectName.find('|') != std::string::npos) { error = "Project情報に使用不可文字があります"; return false; }
		const std::wstring wideRoot = p.projectRoot.wstring();
		const int bytes = WideCharToMultiByte(CP_UTF8, 0, wideRoot.data(), static_cast<int>(wideRoot.size()), nullptr, 0, nullptr, nullptr);
		std::string root(bytes > 0 ? static_cast<std::size_t>(bytes) : 0U, '\0');
		if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, wideRoot.data(), static_cast<int>(wideRoot.size()), root.data(), bytes, nullptr, nullptr);
		text << "Project|" << p.projectId << '|' << p.projectName << '|' << p.hubHost << '|'
			<< GetEngineUpdateChannelText(p.updateChannel) << '|' << p.requiredEngineVersion << '|'
			<< p.projectEndpoint << '|' << root << '|' << p.status << '|'
			<< p.collaborationId << '|' << p.ownerId << '|' << p.lastSyncedRevision << "\r\n";
	}
	return WriteText(LauncherUpdate::GetLauncherStateDirectory(installRoot) / "projects.registry", text.str(), error);
}

bool LauncherExperience::SetupInvite(const std::filesystem::path& invitePath, const std::filesystem::path& installRoot,
	const std::filesystem::path& projectRoot, std::string& result) {
	ManoInvite invite{}; if (!LoadInvite(invitePath, invite, result)) return false;
	if (projectRoot.empty()) {
		HubProjectCatalogEntry project{};
		project.projectId = invite.projectId; project.projectName = invite.projectName;
		project.requiredEngineVersion = invite.requiredEngineVersion; project.updateChannel = invite.updateChannel;
		project.projectManifestEndpoint = invite.projectEndpoint.empty()
			? "/projects/" + invite.projectId + "/project.manifest" : invite.projectEndpoint;
		project.collaborationHost = invite.collaborationHost; project.collaborationPort = invite.collaborationPort;
		project.collaborationId = invite.collaborationId; project.ownerId = invite.ownerId;
		project.snapshotRevision = invite.snapshotRevision;
		std::filesystem::path installedProjectRoot;
		return JoinProject(project, invite.hubHost, installRoot, installedProjectRoot, result);
	}
	if (!invite.projectEndpoint.empty()) {
		const auto projectInfo = installRoot / "Cache" / "Hub" / invite.projectId / "project.json";
		std::string resolved; std::string detail;
		if (!DownloadHubFile(invite.hubHost, invite.projectEndpoint, projectInfo, resolved, &detail)) {
			result = WithReason("HubからProject情報を取得できません: " + resolved, detail); return false;
		}
		const std::string json = ReadText(projectInfo);
		const std::string required = JsonString(json, "requiredEngineVersion");
		if (!required.empty()) invite.requiredEngineVersion = required;
		const std::string channel = JsonString(json, "updateChannel");
		EngineUpdateChannel remoteChannel{};
		if (!channel.empty() && (!TryParseEngineUpdateChannel(channel, remoteChannel) || remoteChannel != invite.updateChannel)) {
			result = "Project情報とInviteのChannelが一致しません"; return false;
		}
	}
	if (!projectRoot.empty()) {
		ProjectVersionSettings project{}; std::string projectError;
		if (!ProjectVersionManager::Load(projectRoot, project, projectError)) { result = "選択FolderはManoEngine Projectではありません: " + projectError; return false; }
		invite.requiredEngineVersion = project.requiredEngineVersion.ToString();
		if (project.updateChannel != invite.updateChannel) { result = "ProjectとInviteのChannelが一致しません"; return false; }
	}
	std::filesystem::path manifestPath; EngineUpdateManifest manifest{};
	if (!ResolveManifest(invite, installRoot, manifestPath, manifest, result)) return false;
	if (!projectRoot.empty()) {
		ProjectVersionSettings project{}; std::string metadataError;
		if (ProjectVersionManager::Load(projectRoot, project, metadataError) && manifest.scriptApiVersion != 0U &&
			project.requiredScriptApiVersion != manifest.scriptApiVersion) {
			result = "Project Script API " + std::to_string(project.requiredScriptApiVersion) + "とEngine Script API " +
				std::to_string(manifest.scriptApiVersion) + "が一致しません"; return false;
		}
	}
	const auto engineDirectory = LauncherUpdate::GetEnginesDirectory(installRoot) / manifest.version.ToString();
	std::error_code ec;
	if (!std::filesystem::exists(engineDirectory / "CG2.exe", ec)) {
		if (!LauncherUpdate::Apply(manifestPath, installRoot, projectRoot, false, result)) return false;
	} else if (!LauncherUpdate::Verify(manifestPath, installRoot, result)) {
		if (!LauncherUpdate::Apply(manifestPath, installRoot, projectRoot, true, result)) return false;
	}
	// Inviteに共同制作Serverが書かれていれば、Project側へ置いておく。
	// これが無いと、LauncherでHostを設定したのにEditorでも同じHostを手入力することになる。
	// Editor側の LoadSettings がこのファイルを読み、未設定の項目だけを補完する。
	if (!projectRoot.empty() && !invite.collaborationHost.empty()) {
		std::ostringstream collaborationText;
		collaborationText
			<< "TeamCollaborationInvite|1\r\n"
			<< "ProjectId|" << invite.projectId << "\r\n"
			<< "CollaborationId|" << invite.collaborationId << "\r\n"
			<< "OwnerId|" << invite.ownerId << "\r\n"
			<< "CollaborationHost|" << invite.collaborationHost << "\r\n"
			<< "CollaborationPort|"
			<< (invite.collaborationPort == 0U ? 48000U : invite.collaborationPort) << "\r\n"
			<< "Revision|" << invite.snapshotRevision << "\r\n"
			<< "LastSyncedRevision|" << invite.snapshotRevision << "\r\n"
			<< "AutoConnect|1\r\nIsHost|0\r\n";
		std::error_code settingsError;
		std::filesystem::create_directories(projectRoot / "ProjectSettings", settingsError);
		std::string collaborationWriteError;
		WriteText(
			projectRoot / "ProjectSettings" / "TeamCollaboration.invite",
			collaborationText.str(),
			collaborationWriteError);
		WriteProjectCollaborationMetadata(projectRoot, invite.projectId, invite.projectName,
			invite.collaborationId, invite.ownerId, invite.snapshotRevision, collaborationWriteError);
	}

	std::vector<RegisteredProject> projects; if (!LoadProjects(installRoot, projects, result)) return false;
	RegisteredProject registered{invite.projectId, invite.projectName, invite.hubHost, invite.updateChannel,
		invite.requiredEngineVersion.empty() ? manifest.version.ToString() : invite.requiredEngineVersion,
		invite.projectEndpoint, projectRoot, projectRoot.empty() ? "Project Not Installed" : "Ready",
		invite.collaborationId, invite.ownerId, invite.snapshotRevision};
	auto existing = std::find_if(projects.begin(), projects.end(), [&](const auto& p) { return p.projectId == registered.projectId; });
	if (existing == projects.end()) projects.push_back(registered); else *existing = registered;
	if (!SaveProjects(installRoot, projects, result)) return false;
	const auto report = EngineEnvironmentCheck::Run(engineDirectory, EnvironmentCheckMode::EditorUser);
	if (report.HasRequiredFailure()) {
		auto saved = std::find_if(projects.begin(), projects.end(), [&](const auto& p) { return p.projectId == registered.projectId; });
		if (saved != projects.end()) saved->status = "Environment Failed";
		std::string saveError; SaveProjects(installRoot, projects, saveError);
		result = "Engine導入済み。環境確認に失敗:\n" + report.ToText(); return false;
	}
	result = "セットアップ完了: " + invite.projectName + " / Engine " + manifest.version.ToString(); return true;
}

bool LauncherExperience::FetchProjectCatalog(const std::string& hubHost,
	const std::filesystem::path& installRoot, std::vector<HubProjectCatalogEntry>& projects, std::string& error) {
	projects.clear();
	const auto catalogPath = installRoot / "Cache" / "Hub" / "project-catalog.manifest";
	std::string resolvedUrl; std::string detail;
	if (!DownloadHubFile(hubHost, "/projects/catalog.manifest", catalogPath, resolvedUrl, &detail)) {
		error = WithReason("配布HubからProject一覧を取得できません: " + resolvedUrl, detail); return false;
	}
	std::istringstream input(ReadText(catalogPath)); std::string line; bool header = false;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto values = Split(line, '|');
		if (values.size() == 2U && values[0] == "ManoProjectCatalog" && values[1] == "1") { header = true; continue; }
		if ((values.size() != 8U && values.size() != 11U) || values[0] != "Project") continue;
		HubProjectCatalogEntry project{}; project.projectId = values[1]; project.projectName = values[2];
		project.requiredEngineVersion = values[3]; project.projectManifestEndpoint = values[5];
		project.collaborationHost = values[6];
		try { project.collaborationPort = static_cast<std::uint16_t>(std::stoul(values[7])); } catch (...) { project.collaborationPort = 0U; }
		if (values.size() == 11U) {
			project.collaborationId = values[8]; project.ownerId = values[9];
			try { project.snapshotRevision = std::stoull(values[10]); } catch (...) { project.snapshotRevision = 0U; }
		}
		if (!IsSafeProjectId(project.projectId) || project.projectName.empty() || project.requiredEngineVersion.empty() ||
			!TryParseEngineUpdateChannel(values[4], project.updateChannel) || project.projectManifestEndpoint.empty() ||
			project.collaborationHost.empty() || project.collaborationPort == 0U) continue;
		projects.push_back(std::move(project));
	}
	if (!header) { error = "Project一覧の形式が不正です"; return false; }
	if (projects.empty()) { error = "配布Hubに参加可能なProjectがありません"; return false; }
	return true;
}

bool LauncherExperience::FetchEngineCatalog(const std::string& hubHost, const std::filesystem::path& installRoot,
	std::vector<EngineCatalogEntry>& entries, std::string& error) {
	entries.clear();
	if (hubHost.empty()) { error = "Hubサーバーのアドレスが未設定です"; return false; }

	const auto cache = installRoot / "Cache" / "Hub" / "__catalog__";
	std::error_code ec; std::filesystem::create_directories(cache, ec);
	const auto hubInfoPath = cache / "mano-hub.json"; std::string hubInfoUrl;

	std::string downloadDetail;
	if (!DownloadHubFile(hubHost, "/mano-hub.json", hubInfoPath, hubInfoUrl, &downloadDetail)) {
		error = WithReason("Hubへ接続できません: " + hubInfoUrl, downloadDetail); return false;
	}

	const std::string json = ReadText(hubInfoPath);
	static const std::pair<const char*, EngineUpdateChannel> kChannels[] = {
		{"Stable", EngineUpdateChannel::Stable}, {"Beta", EngineUpdateChannel::Beta}, {"Dev", EngineUpdateChannel::Dev},
	};

	for (const auto& [channelName, channelValue] : kChannels) {
		const auto channelPos = json.find(std::string("\"") + channelName + "\"");
		if (channelPos == std::string::npos) continue;
		const std::string endpoint = JsonString(json, "manifestEndpoint", channelPos);
		if (endpoint.empty()) continue;

		EngineCatalogEntry entry{}; entry.channel = channelValue;
		entry.manifestPath = cache / (std::string(channelName) + ".manifest");
		std::string resolvedUrl;
		if (!DownloadHubFile(hubHost, endpoint, entry.manifestPath, resolvedUrl)) continue;

		EngineUpdateManifest manifest{}; std::string manifestError;
		if (!LauncherUpdate::LoadManifest(entry.manifestPath, manifest, manifestError)) continue;
		if (!RebaseEngineManifest(entry.manifestPath, hubHost, manifest, manifestError)) continue;
		entry.version = manifest.version;
		entry.isInstalled = std::filesystem::exists(
			LauncherUpdate::GetInstalledManifestPath(installRoot, entry.version), ec);
		entries.push_back(entry);
	}

	if (entries.empty()) { error = "HubにEngineが公開されていません"; return false; }
	return true;
}

bool LauncherExperience::JoinProject(const HubProjectCatalogEntry& project, const std::string& hubHost,
	const std::filesystem::path& installRoot, std::filesystem::path& installedProjectRoot,
	std::string& result, const LauncherProgress& progress) {
	ManoInvite invite{}; invite.formatVersion = 1U; invite.projectId = project.projectId;
	invite.projectName = project.projectName; invite.hubHost = hubHost;
	invite.updateChannel = project.updateChannel; invite.requiredEngineVersion = project.requiredEngineVersion;
	invite.projectEndpoint = project.projectManifestEndpoint; invite.collaborationHost = project.collaborationHost;
	invite.collaborationPort = project.collaborationPort;
	invite.collaborationId = project.collaborationId; invite.ownerId = project.ownerId;
	invite.snapshotRevision = project.snapshotRevision;

	if (progress) progress("必要なEngineを確認中...");
	std::filesystem::path engineManifestPath; EngineUpdateManifest engineManifest{};
	if (!ResolveManifest(invite, installRoot, engineManifestPath, engineManifest, result)) return false;
	const auto engineDirectory = LauncherUpdate::GetEnginesDirectory(installRoot) / engineManifest.version.ToString();
	std::error_code ec;
	if (!std::filesystem::exists(engineDirectory / "CG2.exe", ec)) {
		if (!LauncherUpdate::Apply(engineManifestPath, installRoot, {}, false, result, progress)) return false;
	}
	else if (!LauncherUpdate::Verify(engineManifestPath, installRoot, result)) {
		if (!LauncherUpdate::Apply(engineManifestPath, installRoot, {}, true, result, progress)) return false;
	}

	if (progress) progress("Project初期Snapshotを取得中...");
	if (!DownloadProjectSnapshot(invite, installRoot, installedProjectRoot, result, progress)) return false;
	ProjectVersionSettings projectVersion{};
	if (!ProjectVersionManager::Load(installedProjectRoot, projectVersion, result)) return false;
	if (projectVersion.requiredEngineVersion.ToString() != engineManifest.version.ToString() ||
		(engineManifest.scriptApiVersion != 0U && projectVersion.requiredScriptApiVersion != engineManifest.scriptApiVersion)) {
		result = "取得したProjectとEngineの互換Versionが一致しません"; return false;
	}

	std::vector<RegisteredProject> registeredProjects;
	if (!LoadProjects(installRoot, registeredProjects, result)) return false;
	RegisteredProject registered{project.projectId, project.projectName, hubHost, project.updateChannel,
		engineManifest.version.ToString(), project.projectManifestEndpoint, installedProjectRoot, "Ready",
		project.collaborationId, project.ownerId, project.snapshotRevision};
	auto existing = std::find_if(registeredProjects.begin(), registeredProjects.end(),
		[&](const RegisteredProject& item) { return item.projectId == project.projectId; });
	if (existing == registeredProjects.end()) registeredProjects.push_back(registered); else *existing = registered;
	if (!SaveProjects(installRoot, registeredProjects, result)) return false;

	const auto environment = EngineEnvironmentCheck::Run(engineDirectory, EnvironmentCheckMode::EditorUser);
	if (environment.HasRequiredFailure()) { result = "Engine環境確認に失敗しました:\n" + environment.ToText(); return false; }
	if (progress) progress("Editorを起動して共同制作へ接続中...");
	if (!LauncherUpdate::OpenProject(installRoot, installedProjectRoot, result, engineManifest.version.ToString())) return false;
	result = "Project参加完了: " + project.projectName + " / " + PathToUtf8(installedProjectRoot);
	return true;
}

std::string LauncherExperience::MakeProjectJoinCode(const std::string& projectId) {
	if (projectId.empty()) return {};
	std::uint64_t hash = 1469598103934665603ULL;
	for (const unsigned char character : projectId) {
		hash ^= static_cast<std::uint64_t>(character);
		hash *= 1099511628211ULL;
	}
	// 見間違えやすい I L O U を除いた32文字。口頭やチャットで伝えても崩れにくい8文字にする。
	static constexpr char kJoinCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
	std::string code;
	for (int index = 0; index < 8; ++index) {
		code.push_back(kJoinCodeAlphabet[(hash >> (index * 5)) & 0x1FULL]);
		if (index == 3) code.push_back('-');
	}
	return code;
}

bool LauncherExperience::JoinProjectByCode(const std::string& joinCode, const std::string& hubHost,
	const std::filesystem::path& installRoot, std::filesystem::path& installedProjectRoot,
	std::string& result, const LauncherProgress& progress) {
	const std::string target = NormalizeJoinCode(joinCode);
	if (target.empty()) { result = "参加コードを入力してください"; return false; }
	if (hubHost.empty()) { result = "配布サーバーが分かりません"; return false; }
	std::vector<HubProjectCatalogEntry> projects;
	if (!FetchProjectCatalog(hubHost, installRoot, projects, result)) return false;
	for (const auto& project : projects) {
		if (NormalizeJoinCode(MakeProjectJoinCode(project.projectId)) != target) continue;
		return JoinProject(project, hubHost, installRoot, installedProjectRoot, result, progress);
	}
	result = "この参加コードのプロジェクトが配布サーバーにありません。\r\n"
		"コードの入力と、配布元でプロジェクトが公開済みかを確認してください。";
	return false;
}

bool LauncherExperience::CreateProject(const std::filesystem::path& projectRoot,
	const std::string& projectName, const std::string& engineVersion,
	const std::string& templateName, const std::filesystem::path& installRoot,
	RegisteredProject& registeredProject, std::string& result) {
	if (projectRoot.empty() || projectName.empty()) { result = "Project名と保存先が必要です"; return false; }
	if (projectName.find('|') != std::string::npos) { result = "Project名に | は使用できません"; return false; }
	EngineVersion selectedVersion{};
	if (!EngineVersion::TryParse(engineVersion, selectedVersion)) { result = "選択したEngine Versionが不正です"; return false; }
	EngineUpdateManifest installedManifest{};
	if (!LauncherUpdate::LoadManifest(
		LauncherUpdate::GetInstalledManifestPath(installRoot, selectedVersion), installedManifest, result)) {
		result = "選択したEngineの管理情報を読めません: " + result; return false;
	}
	std::error_code ec;
	if (std::filesystem::exists(projectRoot, ec) && !std::filesystem::is_empty(projectRoot, ec)) {
		result = "保存先Folderが空ではありません"; return false;
	}
	std::filesystem::create_directories(projectRoot / "Assets" / "Scenes", ec);
	std::filesystem::create_directories(projectRoot / "NativeScripts", ec);
	std::filesystem::create_directories(projectRoot / "ProjectSettings", ec);
	std::filesystem::create_directories(projectRoot / "resources" / "scripts", ec);
	if (ec) { result = "Project Folderを作成できません: " + ec.message(); return false; }

	ProjectVersionSettings version = ProjectVersionManager::CreateCurrentDefaults();
	version.requiredEngineVersion = selectedVersion;
	version.engineVersionPolicy = ProjectEngineVersionPolicy::Pinned;
	version.updateChannel = installedManifest.channel;
	if (installedManifest.requiredProjectFormat != 0U) version.projectFormatVersion = installedManifest.requiredProjectFormat;
	if (installedManifest.scriptApiVersion != 0U) version.requiredScriptApiVersion = installedManifest.scriptApiVersion;
	if (!ProjectVersionManager::Save(projectRoot, version, result)) return false;

	const std::string projectId = CreateLauncherUuid();
	const std::string collaborationId = CreateLauncherUuid();
	const std::string ownerId = CurrentWindowsUserName();
	if (!WriteProjectCollaborationMetadata(projectRoot, projectId, projectName,
		collaborationId, ownerId, 0U, result)) return false;
	std::ostringstream team;
	team << "TeamCollaborationSettings|1\r\n"
		<< "UserName|" << ownerId << "\r\n"
		<< "ProjectId|" << projectId << "\r\n"
		<< "Revision|0\r\nLastSyncedRevision|0\r\nAutoConnect|0\r\nIsHost|0\r\n";
	if (!WriteText(projectRoot / "ProjectSettings" / "TeamCollaboration.settings", team.str(), result)) return false;
	if (!WriteProjectRecoveryFiles(projectRoot, result)) return false;
	if (templateName == "標準" && !WriteText(projectRoot / "README.md",
		"# " + projectName + "\r\n\r\nManoEngine 標準Projectです。\r\n", result)) return false;

	registeredProject = {projectId, projectName, DefaultHubAddress(), installedManifest.channel,
		engineVersion, {}, projectRoot, "Ready", collaborationId, ownerId, 0U};
	std::vector<RegisteredProject> projects;
	if (!LoadProjects(installRoot, projects, result)) return false;
	projects.push_back(registeredProject);
	if (!SaveProjects(installRoot, projects, result)) return false;
	result = "Projectを作成しました: " + PathToUtf8(projectRoot);
	return true;
}

bool LauncherExperience::ExportProjectZip(const RegisteredProject& project,
	const std::filesystem::path& outputZip, std::string& result) {
	if (project.projectRoot.empty() || !std::filesystem::exists(project.projectRoot)) {
		result = "Project Folderが見つかりません";
		return false;
	}
	if (outputZip.empty()) { result = "ZIP保存先が指定されていません"; return false; }
	const auto stagingRoot = std::filesystem::temp_directory_path() / "ManoLauncher" / ("project-export-" + CreateLauncherUuid());
	const std::filesystem::path packageName = project.projectRoot.filename().empty() ? "ManoProject" : project.projectRoot.filename();
	std::error_code ec;
	std::filesystem::create_directories(stagingRoot, ec);
	if (ec || !CopyProjectSnapshot(project.projectRoot, stagingRoot / packageName, result)) {
		std::filesystem::remove_all(stagingRoot, ec);
		if (result.empty()) result = "Project ZIP用の一時Folderを作成できません";
		return false;
	}
	const bool ok = CreateZip(stagingRoot, packageName, outputZip, result);
	std::filesystem::remove_all(stagingRoot, ec);
	if (ok) result = "Project ZIPを作成しました: " + PathToUtf8(outputZip);
	return ok;
}

bool LauncherExperience::ImportProjectZip(const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationParent, const std::filesystem::path& installRoot,
	RegisteredProject& importedProject, std::string& result) {
	if (archivePath.empty() || destinationParent.empty()) { result = "ZIPと取込先を指定してください"; return false; }
	const auto staging = std::filesystem::temp_directory_path() / "ManoLauncher" / ("project-import-" + CreateLauncherUuid());
	std::error_code ec;
	std::filesystem::create_directories(staging, ec);
	if (ec || !ExtractZipSafely(archivePath, staging, result)) {
		std::filesystem::remove_all(staging, ec);
		if (result.empty()) result = "Project ZIPを展開できません";
		return false;
	}

	std::vector<std::filesystem::path> projectRoots;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(staging,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (entry.is_regular_file(ec) && entry.path().filename() == "ProjectVersion.cg2" &&
			entry.path().parent_path().filename() == "ProjectSettings") projectRoots.push_back(entry.path().parent_path().parent_path());
	}
	if (projectRoots.size() != 1U) {
		std::filesystem::remove_all(staging, ec);
		result = "ZIPにはProjectが1つだけ必要です";
		return false;
	}
	const auto sourceRoot = projectRoots.front();
	const auto destination = destinationParent / sourceRoot.filename();
	if (std::filesystem::exists(destination, ec)) {
		std::filesystem::remove_all(staging, ec);
		result = "同名のProject Folderが既にあります: " + PathToUtf8(destination);
		return false;
	}
	std::filesystem::create_directories(destinationParent, ec);
		std::error_code renameError;
		std::filesystem::rename(sourceRoot, destination, renameError);
		std::filesystem::remove_all(staging, ec);
		if (renameError) { result = "Projectを取込先へ移動できません: " + renameError.message(); return false; }

	ProjectVersionSettings version{};
	if (!ProjectVersionManager::Load(destination, version, result)) return false;
	if (!WriteProjectRecoveryFiles(destination, result)) return false;
	importedProject = {CreateLauncherUuid(), PathToUtf8(destination.filename()), DefaultHubAddress(),
		version.updateChannel, version.requiredEngineVersion.ToString(), {}, destination, "Ready", {}, {}, 0U};
	RefreshProjectCollaborationMetadata(importedProject);
	if (importedProject.collaborationId.empty()) {
		importedProject.collaborationId = CreateLauncherUuid();
		importedProject.ownerId = CurrentWindowsUserName();
		if (!WriteProjectCollaborationMetadata(destination, importedProject.projectId,
			importedProject.projectName, importedProject.collaborationId, importedProject.ownerId, 0U, result)) return false;
	}
	std::error_code installedError;
	importedProject.status = std::filesystem::exists(LauncherUpdate::GetEnginesDirectory(installRoot) /
		importedProject.requiredEngineVersion / "CG2.exe", installedError) ? "Ready" : "Needs Install";
	std::vector<RegisteredProject> projects;
	if (!LoadProjects(installRoot, projects, result)) return false;
	projects.erase(std::remove_if(projects.begin(), projects.end(), [&](const RegisteredProject& item) {
		return item.projectRoot == destination || item.projectId == importedProject.projectId;
	}), projects.end());
	projects.push_back(importedProject);
	if (!SaveProjects(installRoot, projects, result)) return false;
	result = "Project ZIPを取り込みました: " + PathToUtf8(destination);
	return true;
}

bool LauncherExperience::PrepareGitProject(const std::filesystem::path& projectRoot,
	const std::string& remoteUrl, std::string& result) {
	if (projectRoot.empty() || !std::filesystem::exists(projectRoot / "ProjectSettings")) {
		result = "Project Folderが不正です";
		return false;
	}
	if (remoteUrl.find_first_of("\"\r\n") != std::string::npos) { result = "Git remote URLに使用できない文字があります"; return false; }
	if (!WriteProjectRecoveryFiles(projectRoot, result)) return false;

	wchar_t gitPath[32768]{};
	if (SearchPathW(nullptr, L"git.exe", nullptr, static_cast<DWORD>(std::size(gitPath)), gitPath, nullptr) == 0U) {
		result = "Gitが見つかりません。Git for Windowsを導入してください";
		return false;
	}
	DWORD exitCode = 0U;
	if (!RunProcessAndWait(gitPath, L"init", projectRoot, {}, exitCode, result) || exitCode != 0U) {
		if (result.empty()) result = "git initに失敗しました";
		return false;
	}
	if (!remoteUrl.empty()) {
		DWORD queryExit = 0U;
		if (!RunProcessAndWait(gitPath, L"remote get-url origin", projectRoot, {}, queryExit, result)) return false;
		const std::wstring remoteArgument = L"\"" + ToWide(remoteUrl) + L"\"";
		const std::wstring command = queryExit == 0U ? L"remote set-url origin " + remoteArgument : L"remote add origin " + remoteArgument;
		if (!RunProcessAndWait(gitPath, command, projectRoot, {}, exitCode, result) || exitCode != 0U) {
			if (result.empty()) result = "Git remoteの接続設定に失敗しました";
			return false;
		}
	}
	if (!UpdateTeamFallback(projectRoot, true, result)) return false;
	result = remoteUrl.empty() ? "Git repositoryを作成し、Git共有へ切り替えました。commit/pushは実行していません"
		: "Git repositoryとoriginを設定し、Git共有へ切り替えました。commit/pushは実行していません";
	return true;
}

bool LauncherExperience::SetGitFallback(const std::filesystem::path& projectRoot, bool enabled, std::string& result) {
	if (projectRoot.empty() || !std::filesystem::exists(projectRoot / "ProjectSettings")) {
		result = "Project Folderが不正です";
		return false;
	}
	if (!UpdateTeamFallback(projectRoot, enabled, result)) return false;
	result = enabled ? "共同制作をGit共有へ切り替えました" : "共同制作をリアルタイム接続へ戻しました";
	return true;
}

bool LauncherExperience::EnsureProjectRecoveryFiles(const std::filesystem::path& projectRoot, std::string& result) {
	if (projectRoot.empty() || !std::filesystem::exists(projectRoot / "ProjectSettings")) {
		result = "Project Folderが不正です";
		return false;
	}
	return WriteProjectRecoveryFiles(projectRoot, result);
}

bool LauncherExperience::ExportPortableEngine(const std::filesystem::path& installRoot,
	const std::string& versionText, const std::filesystem::path& outputZip, std::string& result) {
	EngineVersion version{};
	if (!EngineVersion::TryParse(versionText, version)) { result = "Engine Versionが不正です"; return false; }
	const auto engineRoot = LauncherUpdate::GetEnginesDirectory(installRoot) / versionText;
	const auto manifest = LauncherUpdate::GetInstalledManifestPath(installRoot, version);
	if (!std::filesystem::exists(engineRoot) || !std::filesystem::exists(manifest)) { result = "導入済みEngineが見つかりません"; return false; }
	EngineUpdateManifest installedManifest{};
	if (!LauncherUpdate::LoadManifest(manifest, installedManifest, result)) return false;
	const auto staging = std::filesystem::temp_directory_path() / "ManoLauncher" / ("engine-export-" + CreateLauncherUuid());
	const std::filesystem::path packageName = "ManoEngine-" + versionText;
	const auto package = staging / packageName;
	std::error_code ec;
	std::filesystem::create_directories(staging, ec);
	std::filesystem::copy(engineRoot, package, std::filesystem::copy_options::recursive, ec);
	if (ec) { std::filesystem::remove_all(staging, ec); result = "Portable Engine用copyに失敗しました: " + ec.message(); return false; }
	std::filesystem::copy_file(manifest, package / "engine.manifest", std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) { std::filesystem::remove_all(staging, ec); result = "Portable EngineのManifestを保存できません"; return false; }
	std::ostringstream installer;
	installer << "$ErrorActionPreference = 'Stop'\r\n"
		<< "$package = $PSScriptRoot\r\n$version = '" << versionText << "'\r\n"
		<< "$root = Join-Path $env:LOCALAPPDATA 'ManoEngine'\r\n"
		<< "$target = Join-Path $root ('Engines\\' + $version)\r\n"
		<< "if (Test-Path -LiteralPath $target) { throw ('既に導入済みです: ' + $target) }\r\n"
		<< "$manifest = Join-Path $package 'engine.manifest'\r\n"
		<< "foreach ($line in (Get-Content -LiteralPath $manifest)) {\r\n"
		<< "  if ($line -notlike 'File|*') { continue }\r\n"
		<< "  $parts = $line -split '\\|', 4\r\n  $file = Join-Path $package $parts[1]\r\n"
		<< "  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw ('File不足: ' + $parts[1]) }\r\n"
		<< "  if ((Get-Item -LiteralPath $file).Length -ne [Int64]$parts[2]) { throw ('Size不一致: ' + $parts[1]) }\r\n"
		<< "  if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $parts[3].ToLowerInvariant()) { throw ('Hash不一致: ' + $parts[1]) }\r\n}\r\n"
		<< "$pending = $target + '.pending'\r\nif (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Recurse -Force }\r\n"
		<< "New-Item -ItemType Directory -Force -Path $pending | Out-Null\r\n"
		<< "Get-ChildItem -LiteralPath $package -Force | Where-Object { $_.Name -notin @('engine.manifest','Install-ManoEngine.ps1') } | Copy-Item -Destination $pending -Recurse -Force\r\n"
		<< "Move-Item -LiteralPath $pending -Destination $target\r\n"
		<< "$manifestDir = Join-Path $root ('LauncherState\\Manifests\\' + $version)\r\nNew-Item -ItemType Directory -Force -Path $manifestDir | Out-Null\r\n"
		<< "Copy-Item -LiteralPath (Join-Path $package 'engine.manifest') -Destination (Join-Path $manifestDir 'engine.manifest') -Force\r\n"
		<< "$stateDir = Join-Path $root 'LauncherState'\r\n$statePath = Join-Path $stateDir 'launcher.state'\r\n$previous = ''\r\n"
		<< "if (Test-Path -LiteralPath $statePath) { $old = Get-Content -LiteralPath $statePath | Where-Object { $_ -like 'CurrentVersion|*' } | Select-Object -First 1; if ($old) { $previous = ($old -split '\\|', 2)[1] } }\r\n"
		<< "$state = \"ManoLauncherState|1`r`nCurrentVersion|$version`r`nPreviousVersion|$previous`r`nChannel|"
		<< GetEngineUpdateChannelText(installedManifest.channel)
		<< "`r`nManifestLocation|$manifestDir\\engine.manifest`r`n\"\r\n"
		<< "New-Item -ItemType Directory -Force -Path $stateDir | Out-Null\r\n"
		<< "[IO.File]::WriteAllText($statePath, $state, [Text.UTF8Encoding]::new($true))\r\n"
		<< "Write-Host ('Offline導入完了: ' + $version)\r\n";
	if (!WriteText(package / "Install-ManoEngine.ps1", installer.str(), result)) { std::filesystem::remove_all(staging, ec); return false; }
	const bool ok = CreateZip(staging, packageName, outputZip, result);
	std::filesystem::remove_all(staging, ec);
	if (ok) result = "Portable Engine ZIPを作成しました: " + PathToUtf8(outputZip);
	return ok;
}

bool LauncherExperience::LoadOfflineEngineCatalogEntry(const std::filesystem::path& archivePath,
	const std::filesystem::path& installRoot, EngineCatalogEntry& entry, std::string& result) {
	if (!std::filesystem::is_regular_file(archivePath)) {
		result = "配布ZIPが見つかりません";
		return false;
	}

	// 一覧追加時はEngine本体を導入せず、ZIP内のmanifestだけを確認する。
	// 実際の全File Hash検証は「選んだバージョンをインストール」で行う。
	const auto staging = installRoot / "Cache" / "Offline" / ("catalog-" + CreateLauncherUuid());
	std::error_code ec;
	std::filesystem::create_directories(staging.parent_path(), ec);
	if (ec || !ExtractZipSafely(archivePath, staging, result)) {
		std::filesystem::remove_all(staging, ec);
		if (result.empty()) result = "配布ZIPを読み込めません";
		return false;
	}

	std::vector<std::filesystem::path> manifests;
	for (const auto& item : std::filesystem::recursive_directory_iterator(staging,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (item.is_regular_file(ec) && item.path().filename() == "engine.manifest") manifests.push_back(item.path());
	}
	if (manifests.size() != 1U) {
		std::filesystem::remove_all(staging, ec);
		result = "配布ZIPにはengine.manifestが1つだけ必要です";
		return false;
	}

	EngineUpdateManifest manifest{};
	if (!LauncherUpdate::LoadManifest(manifests.front(), manifest, result)) {
		std::filesystem::remove_all(staging, ec);
		return false;
	}

	entry = {};
	entry.channel = manifest.channel;
	entry.version = manifest.version;
	entry.offlineArchivePath = std::filesystem::weakly_canonical(archivePath, ec);
	if (ec) entry.offlineArchivePath = archivePath;
	entry.isInstalled = std::filesystem::exists(
		LauncherUpdate::GetInstalledManifestPath(installRoot, entry.version), ec);
	std::filesystem::remove_all(staging, ec);
	result = "配布ZIPからEngine " + entry.version.ToString() + " をインストール候補へ追加しました";
	return true;
}

bool LauncherExperience::InstallOfflineEngine(const std::filesystem::path& archivePath,
	const std::filesystem::path& installRoot, std::string& result) {
	const auto staging = installRoot / "Cache" / "Offline" / ("archive-" + CreateLauncherUuid());
	std::error_code ec;
	std::filesystem::create_directories(staging.parent_path(), ec);
	if (ec || !ExtractZipSafely(archivePath, staging, result)) {
		std::filesystem::remove_all(staging, ec);
		if (result.empty()) result = "Portable Engine ZIPを展開できません";
		return false;
	}
	std::vector<std::filesystem::path> packages;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(staging,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (entry.is_regular_file(ec) && entry.path().filename() == "engine.manifest") packages.push_back(entry.path().parent_path());
	}
	if (packages.size() != 1U) {
		std::filesystem::remove_all(staging, ec);
		result = "Portable Engine ZIPにはengine.manifestが1つだけ必要です";
		return false;
	}
	const bool ok = LauncherUpdate::InstallOfflinePackage(packages.front(), installRoot, result);
	std::filesystem::remove_all(staging, ec);
	return ok;
}

bool LauncherExperience::GetProjectManifest(const RegisteredProject& project, const std::filesystem::path& installRoot,
	std::filesystem::path& manifestPath, EngineUpdateManifest& manifest, std::string& error) {
	ManoInvite invite{1U, project.projectId, project.projectName, project.hubHost, project.updateChannel,
		project.requiredEngineVersion, project.projectEndpoint, {}};
	return ResolveManifest(invite, installRoot, manifestPath, manifest, error);
}

bool LauncherExperience::PublishEngine(const std::filesystem::path& releaseDirectory,
	const std::filesystem::path& publicationRoot, const EngineUpdateManifest& settings,
	const std::string& hubBaseUrl, std::string& result) {
	PublisherSettings publisher{}; publisher.releaseSource = releaseDirectory; publisher.hubRoot = publicationRoot;
	publisher.publicHubAddress = hubBaseUrl; publisher.defaultChannel = settings.channel;
	PublishPreview preview{};
	if (!PublisherService::CreatePreviewForVersion(publisher, settings.version, preview, result)) return false;
	return PublisherService::Publish(publisher, preview, result);
}

std::vector<std::string> LauncherExperience::ListInstalledEngines(const std::filesystem::path& installRoot) {
	std::vector<std::string> values; std::error_code ec; const auto root = LauncherUpdate::GetEnginesDirectory(installRoot);
	if (!std::filesystem::exists(root, ec)) return values;
	for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
		if (!entry.is_directory(ec)) continue;
		const std::string versionText = entry.path().filename().string();
		EngineVersion version{}; EngineUpdateManifest manifest{}; std::string error;
		if (EngineVersion::TryParse(versionText, version) && LauncherUpdate::LoadManifest(LauncherUpdate::GetInstalledManifestPath(installRoot, version), manifest, error))
			values.push_back(versionText + "  " + GetEngineUpdateChannelText(manifest.channel));
		else values.push_back(versionText);
	}
	// 文字列順では +10 が +9 より前になるため、Versionとして比較して新しい順に表示する。
	std::sort(values.begin(), values.end(), [](const std::string& left, const std::string& right) {
		EngineVersion leftVersion{};
		EngineVersion rightVersion{};
		const bool hasLeft = EngineVersion::TryParse(left.substr(0U, left.find_first_of(" \t")), leftVersion);
		const bool hasRight = EngineVersion::TryParse(right.substr(0U, right.find_first_of(" \t")), rightVersion);
		if (hasLeft && hasRight) return rightVersion < leftVersion;
		if (hasLeft != hasRight) return hasLeft;
		return left > right;
	});
	return values;
}

bool LauncherExperience::RemoveInstalledEngine(const std::filesystem::path& installRoot,
	const std::string& versionText, std::string& result) {
	EngineVersion version{};
	if (!EngineVersion::TryParse(versionText, version) || version.ToString() != versionText) {
		result = "削除するEngineバージョンが不正です";
		return false;
	}

	LauncherState launcher{};
	if (!LauncherUpdate::LoadState(installRoot, launcher, result)) return false;
	if (launcher.currentVersion == versionText) {
		result = "現在使用中のEngine " + versionText + " は削除できません";
		return false;
	}
	if (launcher.previousVersion == versionText) {
		result = "1世代前のEngine " + versionText + " はロールバック用のため削除できません";
		return false;
	}

	std::vector<RegisteredProject> projects;
	if (!LoadProjects(installRoot, projects, result)) return false;
	for (const RegisteredProject& project : projects) {
		if (project.requiredEngineVersion == versionText) {
			result = "Project「" + project.projectName + "」がEngine " + versionText + " を使用しているため削除できません";
			return false;
		}
	}

	const std::filesystem::path enginesRoot = LauncherUpdate::GetEnginesDirectory(installRoot);
	const std::filesystem::path target = enginesRoot / versionText;
	std::error_code error;
	if (!std::filesystem::is_directory(target, error) || error) {
		result = "Engine " + versionText + " は導入されていません";
		return false;
	}

	std::filesystem::remove_all(target, error);
	if (error || std::filesystem::exists(target)) {
		result = "Engine " + versionText + " を削除できません: " + error.message();
		return false;
	}

	// Engine本体と一緒に導入時Manifestも除去し、一覧の管理情報を残さない。
	error.clear();
	std::filesystem::remove(LauncherUpdate::GetInstalledManifestPath(installRoot, version), error);
	result = "未使用のEngine " + versionText + " を削除しました";
	return true;
}
