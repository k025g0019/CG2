#include "PublisherService.h"

#include "LauncherExperience.h"
#include "Source/Engine/Core/EngineVersion.h"
#include "Source/Engine/Core/ProjectVersionManager.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>

namespace {
	constexpr unsigned char kBom[] = {0xEFU, 0xBBU, 0xBFU};

	std::string ToUtf8(const std::wstring& value) {
		if (value.empty()) return {};
		const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string result(count > 0 ? static_cast<std::size_t>(count) : 0U, '\0');
		if (count > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
		return result;
	}

	std::wstring ToWide(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (count <= 0) return {};
		std::wstring result(static_cast<std::size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	std::string ReadText(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		std::ostringstream stream; stream << file.rdbuf();
		std::string text = stream.str();
		if (text.size() >= 3U && static_cast<unsigned char>(text[0]) == kBom[0] &&
			static_cast<unsigned char>(text[1]) == kBom[1] && static_cast<unsigned char>(text[2]) == kBom[2]) text.erase(0U, 3U);
		return text;
	}

	bool WriteText(const std::filesystem::path& path, const std::string& text, std::string& error) {
		std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) { error = "フォルダーを作成できません: " + ec.message(); return false; }
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) { error = "ファイルを書き込めません: " + path.generic_string(); return false; }
		file.write(reinterpret_cast<const char*>(kBom), sizeof(kBom)); file << text;
		if (!file.good()) { error = "ファイルの書き込みに失敗しました: " + path.generic_string(); return false; }
		return true;
	}

	std::vector<std::string> Split(const std::string& line, char separator) {
		std::vector<std::string> values; std::size_t begin = 0U;
		for (;;) {
			const auto end = line.find(separator, begin); values.push_back(line.substr(begin, end - begin));
			if (end == std::string::npos) return values; begin = end + 1U;
		}
	}

	std::string Lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string ChannelDisplayName(EngineUpdateChannel channel) {
		if (channel == EngineUpdateChannel::Beta) return "ベータ版";
		if (channel == EngineUpdateChannel::Dev) return "開発版";
		return "安定版";
	}

	std::string NormalizeHub(std::string hub) {
		while (!hub.empty() && hub.back() == '/') hub.pop_back();
		return hub;
	}

	std::filesystem::path SettingsPath(const std::filesystem::path& installRoot) {
		return LauncherUpdate::GetLauncherStateDirectory(installRoot) / "publisher.settings";
	}

	std::filesystem::path HistoryPath(const std::filesystem::path& installRoot) {
		return LauncherUpdate::GetLauncherStateDirectory(installRoot) / "publish-history.log";
	}

	std::string NowText() {
		const auto now = std::chrono::system_clock::now();
		const std::time_t value = std::chrono::system_clock::to_time_t(now);
		std::tm local{}; localtime_s(&local, &value);
		std::ostringstream text; text << std::put_time(&local, "%Y-%m-%d %H:%M:%S"); return text.str();
	}

	bool AppendHistory(const std::filesystem::path& installRoot, const PublishPreview& preview,
		const std::string& resultText, std::string& error) {
		std::vector<PublishHistoryEntry> history; PublisherService::LoadHistory(installRoot, history, error);
		history.push_back({preview.version.ToString(), preview.channel, NowText(), preview.fileCount, preview.totalSize, resultText});
		if (history.size() > 100U) history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - 100U));
		std::ostringstream output;
		for (const auto& item : history) output << "Publish|" << item.version << '|' << GetEngineUpdateChannelText(item.channel)
			<< '|' << item.publishedAt << '|' << item.fileCount << '|' << item.totalSize << '|' << item.result << "\r\n";
		return WriteText(HistoryPath(installRoot), output.str(), error);
	}

	bool IsAbsoluteHttpUrl(const std::string& value) {
		return value.starts_with("http://") || value.starts_with("https://");
	}

	bool ValidateRequiredReleaseFiles(const std::filesystem::path& release, std::string& error) {
		const std::array<const wchar_t*, 17U> required = {L"CG2.exe", L"CG2TeamServer.exe",
			L"dxcompiler.dll", L"dxil.dll",
			L"libfbxsdk.dll", L"onnxruntime.dll", L"PhysX_64.dll", L"PhysXCommon_64.dll",
			L"PhysXFoundation_64.dll", L"PhysXCooking_64.dll", L"PhysXGpu_64.dll",
			L"NvBlast.dll", L"NvBlastExtAuthoring.dll", L"NvBlastGlobals.dll",
			L"behaviortree_cpp.dll", L"minitrace.dll", L"tinyxml2.dll"};
		for (const wchar_t* name : required) {
			std::error_code ec;
			if (!std::filesystem::is_regular_file(release / name, ec)) {
				error = "リリース成果物が不足しています: " + ToUtf8(name); return false;
			}
		}
		// ScriptApiはユーザーC++ Scriptのbuild batが/Iへ渡すHeaderなので、欠けると配布先でScriptをBuildできない。
		const std::array<const wchar_t*, 15U> requiredResources = {
			L"Assets/Shaders", L"Assets/Shaders/Object3d.VS.hlsl",
			L"Assets/Shaders/PostProcess/Sharpen.PS.hlsl", L"Assets/Shaders/lygia",
			L"Assets/Shaders/FidelityFX", L"resources/editorDefault",
			L"resources/editorDefault/uvChecker.png",
			L"ScriptApi/EditorNativeScript.h", L"ScriptApi/EditorScriptApi.h",
			L"Tools/Whisper/whisper-cli.exe", L"Tools/Whisper/whisper.dll",
			L"Tools/Whisper/ggml.dll", L"Tools/Whisper/ggml-base.dll",
			L"Tools/Whisper/ggml-cpu.dll", L"Tools/Whisper/ggml-base.bin"};
		for (const wchar_t* relativePath : requiredResources) {
			std::error_code ec;
			if (!std::filesystem::exists(release / relativePath, ec)) {
				error = "リリース用Engine Resourceが不足しています: " + ToUtf8(relativePath);
				return false;
			}
		}
		return true;
	}

	bool TestHubWrite(const std::filesystem::path& root, std::string& error) {
		std::error_code ec; std::filesystem::create_directories(root, ec);
		if (ec) { error = "配布フォルダーを作成できません: " + ec.message(); return false; }
		const auto marker = root / ".cg2-publish-write-test";
		{
			std::ofstream file(marker, std::ios::binary | std::ios::trunc);
			if (!file) { error = "配布フォルダーへ書き込めません: " + root.generic_string(); return false; }
			file << "ok";
		}
		std::filesystem::remove(marker, ec); return true;
	}

	std::map<std::string, EngineManifestFile> LoadCurrentChannel(const PublisherSettings& settings,
		EngineVersion& currentVersion) {
		std::map<std::string, EngineManifestFile> files;
		const auto manifestPath = settings.hubRoot / "update" / Lower(GetEngineUpdateChannelText(settings.defaultChannel)) / "engine.manifest";
		EngineUpdateManifest manifest{}; std::string error;
		if (!LauncherUpdate::LoadManifest(manifestPath, manifest, error)) return files;
		currentVersion = manifest.version;
		for (const auto& file : manifest.files) files.emplace(file.relativePath.generic_string(), file);
		return files;
	}

	bool ReplaceFileKeepingBackup(const std::filesystem::path& source, const std::filesystem::path& destination,
		std::filesystem::path& backup, std::string& error) {
		std::error_code ec; std::filesystem::create_directories(destination.parent_path(), ec);
		backup = destination; backup += ".previous"; std::filesystem::remove(backup, ec); ec.clear();
		if (std::filesystem::exists(destination, ec)) {
			std::filesystem::rename(destination, backup, ec);
			if (ec) { error = "現在公開中のファイルを退避できません: " + destination.generic_string(); return false; }
		}
		std::filesystem::rename(source, destination, ec);
		if (ec) {
			std::error_code restore; if (std::filesystem::exists(backup, restore)) std::filesystem::rename(backup, destination, restore);
			error = "公開ファイルの切り替えに失敗しました: " + destination.generic_string(); return false;
		}
		return true;
	}

	std::filesystem::path DistributionProcessFile(const std::filesystem::path& installRoot) {
		return LauncherUpdate::GetLauncherStateDirectory(installRoot) / "distribution-server.pid";
	}

	DWORD ReadPid(const std::filesystem::path& path) {
		try { return static_cast<DWORD>(std::stoul(ReadText(path))); } catch (...) { return 0U; }
	}

	bool IsProcessRunning(DWORD processId) {
		if (processId == 0U) return false;
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (!process) return false;
		DWORD exitCode = 0U; const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
		CloseHandle(process); return running;
	}

	bool IsPortReachable(const std::string& host, std::uint16_t port, std::uint32_t& latencyMilliseconds) {
		WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
		addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
		addrinfo* addresses = nullptr; const std::string portText = std::to_string(port);
		const auto started = std::chrono::steady_clock::now(); bool reachable = false;
		if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &addresses) == 0) {
			for (addrinfo* address = addresses; address != nullptr && !reachable; address = address->ai_next) {
				SOCKET socketHandle = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
				if (socketHandle == INVALID_SOCKET) continue;
				u_long nonBlocking = 1U; ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
				connect(socketHandle, address->ai_addr, static_cast<int>(address->ai_addrlen));
				fd_set writes; FD_ZERO(&writes); FD_SET(socketHandle, &writes); timeval timeout{0, 500000};
				if (select(0, nullptr, &writes, nullptr, &timeout) > 0) {
					int socketError = 0; int size = sizeof(socketError);
					reachable = getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &size) == 0 && socketError == 0;
				}
				closesocket(socketHandle);
			}
			freeaddrinfo(addresses);
		}
		latencyMilliseconds = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count());
		WSACleanup(); return reachable;
	}

	// Projectフォルダーが持つ正式Metadata。Launcherが参加時に書き、Editorもこれを識別子に使う。
	struct ProjectCollaborationMetadata {
		std::string projectId;
		std::string projectName;
		std::string collaborationId;
		std::string ownerId;
	};

	// Hostが実際に待ち受けるPortはProject側のTeam設定にある。配布者設定の値をそのまま配ると、
	// Hostが45678で待っているのに参加者が48000へ繋ぐ、といったすれ違いが起きる。
	std::uint16_t ReadProjectCollaborationPort(const std::filesystem::path& projectRoot, std::uint16_t fallback) {
		std::istringstream input(ReadText(projectRoot / "ProjectSettings" / "TeamCollaboration.settings"));
		std::string line;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto separator = line.find('|');
			if (separator == std::string::npos || line.substr(0U, separator) != "Port") continue;
			try {
				const int value = std::stoi(line.substr(separator + 1U));
				if (value >= 1 && value <= 65535) return static_cast<std::uint16_t>(value);
			}
			catch (...) {}
			break;
		}
		return fallback;
	}

	// Sceneが1つも無いProjectを配ると、参加者はGameObjectを持たないProjectを受け取る。
	// 受け取った差分のUUIDがどこにも一致せず、同期が始まらないまま詰まる。
	bool HasAnySceneFile(const std::filesystem::path& projectRoot) {
		std::error_code ec;
		const auto scenesRoot = projectRoot / "Assets" / "Scenes";
		if (!std::filesystem::is_directory(scenesRoot, ec)) return false;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(scenesRoot, ec)) {
			if (ec) { ec.clear(); continue; }
			if (entry.is_regular_file(ec) && entry.path().extension() == ".scene") return true;
			ec.clear();
		}
		return false;
	}

	void ReadProjectCollaborationMetadata(const std::filesystem::path& projectRoot, ProjectCollaborationMetadata& metadata) {
		std::istringstream input(ReadText(projectRoot / "ProjectSettings" / "ProjectCollaboration.cg2"));
		std::string line;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '') line.pop_back();
			const auto separator = line.find('|');
			if (separator == std::string::npos) continue;
			const std::string key = line.substr(0U, separator);
			const std::string value = line.substr(separator + 1U);
			if (key == "ProjectId") metadata.projectId = value;
			else if (key == "ProjectName") metadata.projectName = value;
			else if (key == "CollaborationId") metadata.collaborationId = value;
			else if (key == "OwnerId") metadata.ownerId = value;
		}
	}

	// 公開が終わった時点で、古いSnapshotはどこからも参照されない。
	// project.manifestのBaseUrlは最新の1世代しか指さず、読み出し側もそこしか見ないため、
	// 残しても到達不能なゴミが積み上がるだけになる。
	// ただし公開の瞬間に旧世代からDownloadしている参加者がいるので、猶予として直前の1世代は残す。
	void RemoveOutdatedProjectSnapshots(const std::filesystem::path& snapshotsRoot,
		const std::string& publishedSnapshotId, std::size_t keptGenerationCount) {
		std::error_code ec;
		if (!std::filesystem::is_directory(snapshotsRoot, ec)) return;

		std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> generations;
		for (const auto& entry : std::filesystem::directory_iterator(snapshotsRoot, ec)) {
			if (ec) { ec.clear(); continue; }
			if (!entry.is_directory(ec)) { ec.clear(); continue; }
			const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
			if (ec) { ec.clear(); continue; }
			generations.emplace_back(writeTime, entry.path());
		}

		std::sort(generations.begin(), generations.end(),
			[](const auto& left, const auto& right) { return left.first > right.first; });

		std::size_t kept = 0U;
		for (const auto& generation : generations) {
			// 今公開したものだけは、時刻の前後に関わらず必ず残す。
			if (ToUtf8(generation.second.filename().wstring()) == publishedSnapshotId) { ++kept; continue; }
			if (kept < keptGenerationCount) { ++kept; continue; }
			std::filesystem::remove_all(generation.second, ec);
			if (ec) ec.clear();
		}
	}

	std::uint16_t HubPort(std::string address, std::uint16_t fallback) {
		const auto scheme = address.find("://"); if (scheme != std::string::npos) address.erase(0U, scheme + 3U);
		const auto slash = address.find('/'); if (slash != std::string::npos) address.erase(slash);
		const auto colon = address.rfind(':');
		if (colon == std::string::npos || address.find(':') != colon) return fallback;
		try {
			const int value = std::stoi(address.substr(colon + 1U));
			return value >= 1 && value <= 65535 ? static_cast<std::uint16_t>(value) : fallback;
		} catch (...) { return fallback; }
	}

	std::string HostOnly(std::string address) {
		const auto scheme = address.find("://"); if (scheme != std::string::npos) address.erase(0U, scheme + 3U);
		const auto slash = address.find('/'); if (slash != std::string::npos) address.erase(slash);
		const auto colon = address.rfind(':'); if (colon != std::string::npos && address.find(':') == colon) address.erase(colon);
		return address.empty() ? "127.0.0.1" : address;
	}

	int LanAddressPriority(const sockaddr_in& address) {
		const std::uint32_t value = ntohl(address.sin_addr.s_addr);
		if (value == 0U || (value & 0xFF000000U) == 0x7F000000U ||
			(value & 0xFFFF0000U) == 0xA9FE0000U) {
			return 0;
		}
		// 通常の家庭・学校LANを最優先する。100.64.0.0/10はTailscale等でも使われるため後回しにする。
		if ((value & 0xFF000000U) == 0x0A000000U ||
			(value & 0xFFF00000U) == 0xAC100000U ||
			(value & 0xFFFF0000U) == 0xC0A80000U) {
			return 3;
		}
		if ((value & 0xFFC00000U) == 0x64400000U) return 1;
		return 2;
	}

	std::string DetectLanIPv4Address() {
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return {};

		std::string bestAddress;
		int bestPriority = 0;
		std::array<char, 256U> hostName{};
		if (gethostname(hostName.data(), static_cast<int>(hostName.size())) == 0) {
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* addresses = nullptr;
			if (getaddrinfo(hostName.data(), nullptr, &hints, &addresses) == 0) {
				for (addrinfo* current = addresses; current != nullptr; current = current->ai_next) {
					if (current->ai_addrlen < sizeof(sockaddr_in)) continue;
					const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
					const int priority = LanAddressPriority(*ipv4);
					if (priority <= bestPriority) continue;

					std::array<char, INET_ADDRSTRLEN> text{};
					if (inet_ntop(AF_INET, &ipv4->sin_addr, text.data(), text.size()) != nullptr) {
						bestAddress = text.data();
						bestPriority = priority;
					}
				}
				freeaddrinfo(addresses);
			}
		}

		WSACleanup();
		return bestAddress;
	}

	std::string LocalHubAddress(std::uint16_t port) {
		const std::string address = DetectLanIPv4Address();
		return "http://" + (address.empty() ? std::string("127.0.0.1") : address) + ':' + std::to_string(port);
	}

	bool IsLoopbackHubAddress(const std::string& address) {
		const std::string host = Lower(HostOnly(address));
		return host == "127.0.0.1" || host == "localhost";
	}

	bool UsesMachineLocalAddress(const std::string& address) {
		if (IsLoopbackHubAddress(address)) return true;
		in_addr ipv4{};
		return inet_pton(AF_INET, HostOnly(address).c_str(), &ipv4) == 1;
	}

	bool CanBindDistributionPort(std::uint16_t port) {
		SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (probe == INVALID_SOCKET) return false;
		BOOL exclusive = TRUE;
		setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		address.sin_port = htons(port);
		const bool available = bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR;
		closesocket(probe);
		return available;
	}

	std::uint16_t FindAvailableDistributionPort(std::uint16_t preferredPort) {
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0U;

		std::uint16_t selectedPort = 0U;
		for (std::uint32_t offset = 0U; offset <= 100U; ++offset) {
			const std::uint32_t candidate = static_cast<std::uint32_t>(preferredPort) + offset;
			if (candidate > 65535U) break;
			if (CanBindDistributionPort(static_cast<std::uint16_t>(candidate))) {
				selectedPort = static_cast<std::uint16_t>(candidate);
				break;
			}
		}

		WSACleanup();
		return selectedPort;
	}

	bool StartDetached(const std::filesystem::path& executable, const std::wstring& arguments,
		const std::filesystem::path& workingDirectory, DWORD& processId, std::string& error) {
		std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
		STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
		std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(L'\0');
		if (!CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process)) {
			error = "サーバーを起動できません（Windowsエラー " + std::to_string(GetLastError()) + "）"; return false;
		}
		processId = process.dwProcessId; CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
	}

	// 終了を待つ子プロセス用。公開UIのワーカースレッドから呼ぶため、画面は固めない。
	bool RunProcessAndWait(const std::filesystem::path& executable, const std::wstring& arguments,
		const std::filesystem::path& workingDirectory, DWORD& exitCode, std::string& error) {
		std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
		std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(L'\0');
		STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process)) {
			error = "ビルド処理を開始できません（Windowsエラー " + std::to_string(GetLastError()) + "）";
			return false;
		}

		WaitForSingleObject(process.hProcess, INFINITE);
		const bool gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
		CloseHandle(process.hThread); CloseHandle(process.hProcess);
		if (!gotExitCode) { error = "ビルド処理の終了コードを取得できません"; return false; }
		return true;
	}

	bool ReadJsonUnsigned(const std::string& text, const char* key, std::uint32_t& value) {
		const std::string name = std::string("\"") + key + "\"";
		const std::size_t keyPosition = text.find(name);
		if (keyPosition == std::string::npos) return false;
		const std::size_t colon = text.find(':', keyPosition + name.size());
		if (colon == std::string::npos) return false;
		std::size_t begin = colon + 1U;
		while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
		std::size_t end = begin;
		while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) ++end;
		if (begin == end) return false;
		try {
			const unsigned long long parsed = std::stoull(text.substr(begin, end - begin));
			if (parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
			value = static_cast<std::uint32_t>(parsed);
			return true;
		} catch (...) { return false; }
	}

	bool ReplaceJsonUnsigned(std::string& text, const char* key, std::uint32_t value) {
		const std::string name = std::string("\"") + key + "\"";
		const std::size_t keyPosition = text.find(name);
		if (keyPosition == std::string::npos) return false;
		const std::size_t colon = text.find(':', keyPosition + name.size());
		if (colon == std::string::npos) return false;
		std::size_t begin = colon + 1U;
		while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
		std::size_t end = begin;
		while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) ++end;
		if (begin == end) return false;
		text.replace(begin, end - begin, std::to_string(value));
		return true;
	}

	bool LoadSourceVersion(const std::filesystem::path& path, EngineVersion& version, std::string& text, std::string& error) {
		text = ReadText(path);
		if (text.empty() || !ReadJsonUnsigned(text, "major", version.major) || !ReadJsonUnsigned(text, "minor", version.minor) ||
			!ReadJsonUnsigned(text, "patch", version.patch) || !ReadJsonUnsigned(text, "build", version.build)) {
			error = "engine-version.json の major / minor / patch / build を読み取れません: " + path.generic_string();
			return false;
		}
		return true;
	}

	std::filesystem::path FindMsBuild() {
		const std::array<std::filesystem::path, 4U> candidates = {
			L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
			L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
			L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
			L"C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe"};
		for (const auto& candidate : candidates) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
		}
		return {};
	}

	std::string ContentType(const std::filesystem::path& path) {
		const std::string extension = Lower(path.extension().string());
		if (extension == ".json" || extension == ".manifest" || extension == ".cg2-invite") return "application/json; charset=utf-8";
		if (extension == ".txt" || extension == ".log") return "text/plain; charset=utf-8";
		return "application/octet-stream";
	}

	std::uint64_t LoadProjectRevision(const std::filesystem::path& projectRoot) {
		std::uint64_t revision = 0U;
		std::ifstream settings(projectRoot / "ProjectSettings" / "TeamCollaboration.settings", std::ios::binary);
		std::string line;
		while (std::getline(settings, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto values = Split(line, '|');
			if (values.size() == 2U && (values[0] == "Revision" || values[0] == "LastSyncedRevision")) {
				try { revision = (std::max)(revision, std::stoull(values[1])); } catch (...) {}
			}
		}
		return revision;
	}

	bool DecodeUrlPath(const std::string& encoded, std::string& decoded) {
		auto hex = [](char value) -> int {
			if (value >= '0' && value <= '9') return value - '0';
			if (value >= 'A' && value <= 'F') return value - 'A' + 10;
			if (value >= 'a' && value <= 'f') return value - 'a' + 10;
			return -1;
		};
		decoded.clear();
		decoded.reserve(encoded.size());
		for (std::size_t index = 0U; index < encoded.size(); ++index) {
			if (encoded[index] != '%') { decoded.push_back(encoded[index]); continue; }
			if (index + 2U >= encoded.size()) return false;
			const int high = hex(encoded[index + 1U]);
			const int low = hex(encoded[index + 2U]);
			if (high < 0 || low < 0) return false;
			decoded.push_back(static_cast<char>((high << 4) | low));
			index += 2U;
		}
		return true;
	}

	bool SendAll(SOCKET socketHandle, const char* data, std::size_t size) {
		while (size > 0U) {
			const int sent = send(socketHandle, data, static_cast<int>((std::min)(size, static_cast<std::size_t>(1U << 20U))), 0);
			if (sent <= 0) return false; data += sent; size -= static_cast<std::size_t>(sent);
		}
		return true;
	}
}

PublisherSettings PublisherService::CreateDefaults(const std::filesystem::path&) {
	wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, _countof(executable));
	PublisherSettings settings{}; settings.releaseSource = std::filesystem::path(executable).parent_path();
	settings.hubRoot = L"C:\\CG2Hub"; settings.collaborationServer = settings.releaseSource / "CG2TeamServer.exe";
	// 招待先の別PCから到達できるよう、Tailscaleではなく通常のLANアドレスを既定値にする。
	settings.publicHubAddress = LocalHubAddress(settings.distributionPort);
	wchar_t userName[256]{}; DWORD userNameLength = static_cast<DWORD>(std::size(userName));
	settings.ownerId = GetUserNameW(userName, &userNameLength)
		? ToUtf8(std::wstring(userName, userNameLength > 0U ? userNameLength - 1U : 0U))
		: "owner";
	settings.collaborationId = settings.collaborationProjectId + "-team";
	return settings;
}

bool PublisherService::LoadSettings(const std::filesystem::path& installRoot, PublisherSettings& settings, std::string& error) {
	settings = CreateDefaults(installRoot); std::ifstream file(SettingsPath(installRoot), std::ios::binary);
	if (!file) return true;
	std::string line; while (std::getline(file, line)) {
		if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == kBom[0]) line.erase(0U, 3U);
		if (!line.empty() && line.back() == '\r') line.pop_back(); const auto values = Split(line, '|'); if (values.size() < 2U) continue;
		if (values[0] == "ReleaseSource") settings.releaseSource = ToWide(values[1]);
		else if (values[0] == "HubRoot") settings.hubRoot = ToWide(values[1]);
		else if (values[0] == "PublicHubAddress") settings.publicHubAddress = values[1];
		else if (values[0] == "DefaultChannel") TryParseEngineUpdateChannel(values[1], settings.defaultChannel);
		else if (values[0] == "CollaborationServer") settings.collaborationServer = ToWide(values[1]);
		else if (values[0] == "CollaborationProjectId") settings.collaborationProjectId = values[1];
		else if (values[0] == "CollaborationProjectName") settings.collaborationProjectName = values[1];
		else if (values[0] == "CollaborationId") settings.collaborationId = values[1];
		else if (values[0] == "OwnerId") settings.ownerId = values[1];
		else if (values[0] == "HubMode") settings.hubMode = values[1] == "Remote" ? PublisherHubMode::Remote : PublisherHubMode::Local;
		else if (values[0] == "DistributionPort") { try { settings.distributionPort = static_cast<std::uint16_t>(std::stoul(values[1])); } catch (...) { error = "保存済みの配布ポートが不正です"; return false; } }
		else if (values[0] == "CollaborationPort") { try { settings.collaborationPort = static_cast<std::uint16_t>(std::stoul(values[1])); } catch (...) {} }
		else if (values[0] == "CollaborationMode") settings.collaborationMode = values[1] == "Lan" ? CollaborationConnectionMode::Lan : CollaborationConnectionMode::Tailscale;
		else if (values[0] == "CollaborationHost") settings.collaborationHost = values[1];
		else if (values[0] == "CollaborationOwnerUserId") settings.collaborationOwnerUserId = values[1];
		else if (values[0] == "CollaborationAutoStart") settings.collaborationAutoStart = values[1] == "1";
	}
	// 設定一式を別PCへ移しても、旧PCのIPv4アドレスを配布し続けないよう現在のPCへ追従する。
	// DNS名を明示した場合は利用者の指定を維持する。
	if (settings.hubMode == PublisherHubMode::Local && UsesMachineLocalAddress(settings.publicHubAddress)) {
		settings.publicHubAddress = LocalHubAddress(settings.distributionPort);
	}
	return true;
}

bool PublisherService::SaveSettings(const std::filesystem::path& installRoot, const PublisherSettings& settings, std::string& error) {
	if (settings.releaseSource.empty() || settings.hubRoot.empty() || settings.publicHubAddress.empty()) {
		error = "リリース元、配布フォルダー、公開アドレスは必須です"; return false;
	}
	if (!IsAbsoluteHttpUrl(settings.publicHubAddress)) { error = "公開アドレスは http:// または https:// から指定してください"; return false; }
	if (settings.distributionPort == 0U) { error = "配布ポートは1〜65535で指定してください"; return false; }
	const bool validProjectId = !settings.collaborationProjectId.empty() && settings.collaborationProjectId.size() <= 64U &&
		std::all_of(settings.collaborationProjectId.begin(), settings.collaborationProjectId.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
	if (!validProjectId || settings.collaborationProjectName.empty()) {
		error = "プロジェクトIDとプロジェクト名は必須です"; return false;
	}
	if (settings.publicHubAddress.find('|') != std::string::npos || settings.collaborationProjectName.find('|') != std::string::npos) {
		error = "配布設定に使用できない文字 | が含まれています"; return false;
	}
	std::ostringstream text;
	text << "PublisherSettings|1\r\nReleaseSource|" << ToUtf8(settings.releaseSource.wstring())
		<< "\r\nHubRoot|" << ToUtf8(settings.hubRoot.wstring()) << "\r\nPublicHubAddress|" << settings.publicHubAddress
		<< "\r\nDefaultChannel|" << GetEngineUpdateChannelText(settings.defaultChannel)
		<< "\r\nCollaborationMode|" << (settings.collaborationMode == CollaborationConnectionMode::Lan ? "Lan" : "Tailscale")
		<< "\r\nCollaborationHost|" << settings.collaborationHost
		<< "\r\nCollaborationOwnerUserId|" << settings.collaborationOwnerUserId
		<< "\r\nCollaborationAutoStart|" << (settings.collaborationAutoStart ? 1 : 0)
		<< "\r\nCollaborationServer|" << ToUtf8(settings.collaborationServer.wstring())
		<< "\r\nCollaborationProjectId|" << settings.collaborationProjectId
		<< "\r\nCollaborationProjectName|" << settings.collaborationProjectName
		<< "\r\nCollaborationId|" << settings.collaborationId
		<< "\r\nOwnerId|" << settings.ownerId
		<< "\r\nHubMode|" << (settings.hubMode == PublisherHubMode::Local ? "Local" : "Remote")
		<< "\r\nDistributionPort|" << settings.distributionPort << "\r\nCollaborationPort|" << settings.collaborationPort << "\r\n";
	return WriteText(SettingsPath(installRoot), text.str(), error);
}

bool PublisherService::IsPublishFile(const std::filesystem::path& relativePath) {
	const std::string name = relativePath.filename().string(); const std::string extension = Lower(relativePath.extension().string());
	if (name == "CG2Launcher.exe" || name == "CG2Launcher.pdb" || name == "CG2.exe" || name == "imgui.ini") return false;
	if (extension == ".pdb" || extension == ".ilk" || extension == ".exp" || extension == ".lib" || extension == ".log") return false;
	// Assets配下はProject固有の中身(サンプルFBX・Scene・Input設定等)を配布しないためExcludeするが、
	// Assets/Shadersだけは例外。RendererがEditorPlatformManager.cppから実行時に読み込むEngine本体の
	// 必須Shader Source(hlsl)がここに置かれているため、除くとRenderingが壊れる。
	bool insideAssets = false;
	for (const auto& part : relativePath) {
		if (insideAssets) {
			if (part == "Shaders") { insideAssets = false; continue; }
			return false;
		}
		if (part == "CG2Launcher" || part == "BuildLogs" || part == "logs" || part == "obj" || part == ".team" ||
			part == "ProjectSettings" || part == "Library") return false;
		if (part == "Assets") insideAssets = true;
	}
	return true;
}

bool PublisherService::IsProjectSnapshotFile(const std::filesystem::path& relativePath) {
	if (relativePath.empty() || relativePath.is_absolute()) return false;
	const std::string normalized = ToUtf8(relativePath.lexically_normal().generic_wstring());
	if (normalized.empty() || normalized == "." || normalized.starts_with("../") || normalized.find('|') != std::string::npos) return false;
	const bool isProjectContent = normalized.starts_with("Assets/") || normalized.starts_with("resources/") ||
		normalized.starts_with("NativeScripts/") || normalized.starts_with("ProjectSettings/");
	if (!isProjectContent) return false;
	// ScriptはSourceだけでなく、Editorが直ちに読み込めるRelease DLLもSnapshotへ含める。
	// 中間生成物(.obj/.lib等)は配布せず、実行に必要なDLLだけを例外扱いする。
	const std::string extension = Lower(relativePath.extension().string());
	const bool isRuntimeScriptDll = normalized.starts_with("resources/scripts/") &&
		normalized.find("/x64/Release/") != std::string::npos && extension == ".dll";
	for (const auto& part : relativePath) {
		if (part == ".git" || part == ".team" || part == "Library" || part == "Debug" ||
			part == "BuildLogs" || part == "obj") return false;
		if (!isRuntimeScriptDll && (part == "x64" || part == "Release")) return false;
	}
	const std::string name = relativePath.filename().string();
	if (name == "TeamCollaboration.settings" || name == "TeamCollaboration.invite") return false;
	return isRuntimeScriptDll || (extension != ".obj" && extension != ".lib" && extension != ".exp" &&
		extension != ".pdb" && extension != ".ilk");
}

bool PublisherService::PublishProjectSnapshot(const std::filesystem::path& projectRoot,
	const PublisherSettings& settings, std::string& result, const PublisherProgress& progress) {
	ProjectVersionSettings projectVersion{};
	if (!ProjectVersionManager::Load(projectRoot, projectVersion, result)) {
		result = "選択したフォルダーはCG2Engine Projectではありません: " + result; return false;
	}
	// Projectの識別子はProject側のMetadataが正。配布者画面の文字入力を使うと、
	// Hubの公開先とProject自身のProject IDがずれて「Project IDが一致しません」になる。
	ProjectCollaborationMetadata metadata{};
	ReadProjectCollaborationMetadata(projectRoot, metadata);
	const std::string projectId = metadata.projectId.empty() ? settings.collaborationProjectId : metadata.projectId;
	const std::string projectName = metadata.projectName.empty() ? settings.collaborationProjectName : metadata.projectName;
	const std::string collaborationId = metadata.collaborationId.empty() ? settings.collaborationId : metadata.collaborationId;
	const std::string ownerId = metadata.ownerId.empty() ? settings.ownerId : metadata.ownerId;
	const std::uint16_t collaborationPort = ReadProjectCollaborationPort(projectRoot, settings.collaborationPort);

	if (!HasAnySceneFile(projectRoot)) {
		result = "Assets/Scenes にSceneがありません。Sceneを保存してから公開してください。\r\n"
			"Sceneの無いProjectを配ると、参加者は中身の無いProjectを受け取り同期できません。";
		return false;
	}
	const bool validProjectId = !projectId.empty() && projectId.size() <= 64U &&
		std::all_of(projectId.begin(), projectId.end(),
			[](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
	if (!validProjectId || projectName.empty() || projectName.find('|') != std::string::npos) {
		result = "Project IDまたはProject名が不正です"; return false;
	}
	if (!IsAbsoluteHttpUrl(settings.publicHubAddress)) { result = "公開アドレスが不正です"; return false; }

	const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const std::string snapshotId = projectVersion.requiredEngineVersion.ToString() + "-" + std::to_string(now);
	const std::uint64_t snapshotRevision = LoadProjectRevision(projectRoot);
	const auto pendingRoot = settings.hubRoot / ".publish" / "projects" /
		(projectId + "." + std::to_string(now) + ".pending");
	const auto snapshotRoot = settings.hubRoot / "projects" / projectId / "snapshots" / snapshotId;
	std::error_code ec;
	if (std::filesystem::exists(pendingRoot, ec)) { result = "同じProjectの公開処理が既に存在します"; return false; }
	std::filesystem::create_directories(pendingRoot, ec);
	if (ec) { result = "Project公開準備フォルダーを作成できません: " + ec.message(); return false; }

	std::vector<EngineManifestFile> files;
	std::uint32_t processed = 0U;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (ec) { ec.clear(); continue; }
		if (!entry.is_regular_file(ec)) continue;
		const auto relative = std::filesystem::relative(entry.path(), projectRoot, ec);
		if (ec || !IsProjectSnapshotFile(relative)) { ec.clear(); continue; }
		const auto destination = pendingRoot / relative;
		std::filesystem::create_directories(destination.parent_path(), ec);
		std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) { result = "Projectファイルを収集できません: " + ToUtf8(relative.generic_wstring()); std::filesystem::remove_all(pendingRoot, ec); return false; }
		std::string hashError;
		EngineManifestFile file{}; file.relativePath = relative; file.size = std::filesystem::file_size(destination, ec);
		file.sha256 = LauncherUpdate::CalculateSha256(destination, hashError);
		if (file.sha256.empty()) { result = "Projectファイルのハッシュ計算に失敗しました: " + ToUtf8(relative.generic_wstring()); std::filesystem::remove_all(pendingRoot, ec); return false; }
		files.push_back(std::move(file)); ++processed;
		if (progress && (processed == 1U || processed % 20U == 0U)) progress("Project Snapshotを作成中 " + std::to_string(processed) + " ファイル...");
	}
	if (files.empty()) { result = "公開できるProjectファイルがありません"; std::filesystem::remove_all(pendingRoot, ec); return false; }
	const auto requiredMetadata = std::filesystem::path("ProjectSettings") / "ProjectVersion.cg2";
	if (std::none_of(files.begin(), files.end(), [&](const EngineManifestFile& file) { return file.relativePath == requiredMetadata; })) {
		result = "Project Version情報がSnapshotに含まれていません"; std::filesystem::remove_all(pendingRoot, ec); return false;
	}

	const std::string publicHub = NormalizeHub(settings.publicHubAddress);
	const std::string baseUrl = publicHub + "/projects/" + projectId + "/snapshots/" + snapshotId;
	const std::string collaborationHost = HostOnly(publicHub);
	std::ostringstream manifest;
	manifest << "CG2ProjectManifest|1\r\nProjectId|" << projectId
		<< "\r\nProjectName|" << projectName
		<< "\r\nRequiredEngineVersion|" << projectVersion.requiredEngineVersion.ToString()
		<< "\r\nRequiredScriptApiVersion|" << projectVersion.requiredScriptApiVersion
		<< "\r\nProjectFormatVersion|" << projectVersion.projectFormatVersion
		<< "\r\nSnapshotRevision|" << snapshotRevision
		<< "\r\nCollaborationId|" << collaborationId
		<< "\r\nOwnerId|" << ownerId
		<< "\r\nChannel|" << GetEngineUpdateChannelText(projectVersion.updateChannel)
		<< "\r\nBaseUrl|" << baseUrl << "\r\nCollaborationHost|" << collaborationHost
		<< "\r\nCollaborationPort|" << collaborationPort << "\r\n";
	for (const auto& file : files) manifest << "File|" << ToUtf8(file.relativePath.generic_wstring()) << '|' << file.size << '|' << file.sha256 << "\r\n";
	if (!WriteText(pendingRoot / "project.manifest", manifest.str(), result)) { std::filesystem::remove_all(pendingRoot, ec); return false; }

	std::filesystem::create_directories(snapshotRoot.parent_path(), ec);
	if (ec || std::filesystem::exists(snapshotRoot, ec)) { result = "Project Snapshotの公開先を準備できません"; std::filesystem::remove_all(pendingRoot, ec); return false; }
	std::filesystem::rename(pendingRoot, snapshotRoot, ec);
	if (ec) { result = "Project Snapshotを公開先へ切り替えられません: " + ec.message(); return false; }

	const auto endpoint = settings.hubRoot / "projects" / projectId / "project.manifest";
	const auto endpointPending = settings.hubRoot / ".publish" / "projects" / (projectId + ".manifest.pending");
	std::filesystem::copy_file(snapshotRoot / "project.manifest", endpointPending, std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) { result = "Project Manifestを準備できません"; return false; }
	std::filesystem::path endpointBackup;
	if (!ReplaceFileKeepingBackup(endpointPending, endpoint, endpointBackup, result)) return false;
	auto restoreEndpoint = [&]() {
		std::error_code restoreError;
		std::filesystem::remove(endpoint, restoreError);
		if (std::filesystem::exists(endpointBackup, restoreError)) std::filesystem::rename(endpointBackup, endpoint, restoreError);
	};

	const auto catalogPath = settings.hubRoot / "projects" / "catalog.manifest";
	std::vector<std::string> catalogLines;
	std::istringstream existingCatalog(ReadText(catalogPath)); std::string line;
	while (std::getline(existingCatalog, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto values = Split(line, '|');
		if (!line.empty() && !(values.size() >= 2U && values[0] == "Project" && values[1] == projectId)) catalogLines.push_back(line);
	}
	if (catalogLines.empty() || catalogLines.front() != "CG2ProjectCatalog|1") catalogLines.insert(catalogLines.begin(), "CG2ProjectCatalog|1");
	catalogLines.push_back("Project|" + projectId + "|" + projectName + "|" +
		projectVersion.requiredEngineVersion.ToString() + "|" + GetEngineUpdateChannelText(projectVersion.updateChannel) +
		"|/projects/" + projectId + "/project.manifest|" + collaborationHost + "|" + std::to_string(collaborationPort) +
		"|" + collaborationId + "|" + ownerId + "|" + std::to_string(snapshotRevision));
	std::ostringstream catalog; for (const auto& catalogLine : catalogLines) catalog << catalogLine << "\r\n";
	const auto catalogPending = settings.hubRoot / ".publish" / "projects" / "catalog.pending";
	if (!WriteText(catalogPending, catalog.str(), result)) { restoreEndpoint(); return false; }
	std::filesystem::path catalogBackup;
	if (!ReplaceFileKeepingBackup(catalogPending, catalogPath, catalogBackup, result)) { restoreEndpoint(); return false; }
	std::filesystem::remove(endpointBackup, ec); std::filesystem::remove(catalogBackup, ec);
	RemoveOutdatedProjectSnapshots(settings.hubRoot / "projects" / projectId / "snapshots", snapshotId, 2U);
	result = "Project Snapshotを公開しました: " + projectName + " / " + std::to_string(files.size()) + " ファイル";
	return true;
}

bool PublisherService::CreatePreview(const PublisherSettings& settings, PublishPreview& preview, std::string& error,
	const PublisherProgress& progress) {
	return CreatePreviewForVersion(settings, GetCG2EngineVersion(), preview, error, progress);
}

bool PublisherService::BuildNextVersionPreview(const PublisherSettings& settings, PublishPreview& preview, std::string& error,
	const PublisherProgress& progress) {
	// 標準のリリース先は <repo>\x64\Release。別のフォルダーを選んだ場合も、同じ構成だけを許可する。
	const std::filesystem::path repositoryRoot = settings.releaseSource.parent_path().parent_path();
	const std::filesystem::path versionFile = repositoryRoot / "Engine" / "Version" / "engine-version.json";
	const std::filesystem::path engineProject = repositoryRoot / "CG2.vcxproj";
	if (!std::filesystem::is_regular_file(versionFile) || !std::filesystem::is_regular_file(engineProject)) {
		error = "リリース元は <CG2Engine>\\x64\\Release を指定してください。engine-version.json または CG2.vcxproj が見つかりません。";
		return false;
	}
	if (!ValidateRequiredReleaseFiles(settings.releaseSource, error)) return false;

	EngineVersion sourceVersion{}; std::string originalJson;
	if (!LoadSourceVersion(versionFile, sourceVersion, originalJson, error)) return false;

	EngineVersion publishedVersion{};
	LoadCurrentChannel(settings, publishedVersion);
	if (sourceVersion.major != publishedVersion.major || sourceVersion.minor != publishedVersion.minor || sourceVersion.patch != publishedVersion.patch) {
		if (sourceVersion < publishedVersion) {
			error = "バージョンの逆行を拒否しました: 公開中 " + publishedVersion.ToString() + " / ソース " + sourceVersion.ToString();
			return false;
		}
	}
	if (sourceVersion.build == (std::numeric_limits<std::uint32_t>::max)()) {
		error = "build 番号が上限です。major / minor / patch を更新してください。";
		return false;
	}

	EngineVersion nextVersion = sourceVersion;
	nextVersion.build += 1U;
	if (nextVersion.major == publishedVersion.major && nextVersion.minor == publishedVersion.minor && nextVersion.patch == publishedVersion.patch &&
		nextVersion.build <= publishedVersion.build) {
		if (publishedVersion.build == (std::numeric_limits<std::uint32_t>::max)()) {
			error = "公開済み build 番号が上限です。major / minor / patch を更新してください。";
			return false;
		}
		nextVersion.build = publishedVersion.build + 1U;
	}

	std::string nextJson = originalJson;
	if (!ReplaceJsonUnsigned(nextJson, "build", nextVersion.build)) {
		error = "engine-version.json の build 番号を更新できません";
		return false;
	}
	if (!WriteText(versionFile, nextJson, error)) return false;

	const auto restoreVersionFile = [&]() {
		std::string restoreError;
		if (!WriteText(versionFile, originalJson, restoreError)) {
			error += "\nさらに engine-version.json を元へ戻せません: " + restoreError;
			return;
		}
		const auto syncScript = repositoryRoot / "Tools" / "Versioning" / "Sync-Version.ps1";
		DWORD syncExitCode = 0U;
		std::string syncError;
		RunProcessAndWait(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
			L"-NoProfile -File \"" + syncScript.wstring() + L"\"", repositoryRoot, syncExitCode, syncError);
	};

	const std::filesystem::path msBuild = FindMsBuild();
	if (msBuild.empty()) {
		error = "Visual Studio の MSBuild.exe が見つかりません。Visual Studio 2022 または Build Tools をインストールしてください。";
		restoreVersionFile();
		return false;
	}

	if (progress) progress("Engine " + nextVersion.ToString() + " をビルド中...");
	DWORD buildExitCode = 0U;
	if (!RunProcessAndWait(msBuild, L"\"" + engineProject.wstring() + L"\" /p:Configuration=Release /p:Platform=x64",
		repositoryRoot, buildExitCode, error) || buildExitCode != 0U) {
		if (error.empty()) error = "Engine のビルドに失敗しました（終了コード " + std::to_string(buildExitCode) + "）";
		else error += "\nEngine のビルドに失敗しました（終了コード " + std::to_string(buildExitCode) + "）";
		restoreVersionFile();
		return false;
	}

	if (progress) progress("公開内容を確認中...");
	if (!CreatePreviewForVersion(settings, nextVersion, preview, error, progress)) {
		error = "Engine " + nextVersion.ToString() + " のビルドは完了しましたが、公開内容を作成できません。\n" + error;
		return false;
	}
	return true;
}

bool PublisherService::CreatePreviewForVersion(const PublisherSettings& settings, const EngineVersion& version,
	PublishPreview& preview, std::string& error, const PublisherProgress& progress) {
	preview = {}; preview.version = version; preview.channel = settings.defaultChannel;
	preview.publicHub = NormalizeHub(settings.publicHubAddress);
	if (!IsAbsoluteHttpUrl(preview.publicHub)) { error = "公開アドレスが不正です"; return false; }
	if (!ValidateRequiredReleaseFiles(settings.releaseSource, error) || !TestHubWrite(settings.hubRoot, error)) return false;
	EngineVersion publishedVersion{}; auto current = LoadCurrentChannel(settings, publishedVersion);
	if (publishedVersion > preview.version) {
		error = "バージョンの逆行を拒否しました: 公開中 " + publishedVersion.ToString() + " / 今回 " + preview.version.ToString(); return false;
	}
	if (!current.empty() && publishedVersion == preview.version) {
		error = "同じバージョンの上書きを拒否しました。engine-version.json のビルド番号を上げてください: " + preview.version.ToString(); return false;
	}
	std::error_code ec; std::uint32_t index = 0U;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(settings.releaseSource,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (!entry.is_regular_file(ec)) continue;
		const auto relative = std::filesystem::relative(entry.path(), settings.releaseSource, ec);
		if (ec || !IsPublishFile(relative)) { ec.clear(); continue; }
		preview.includedFiles.push_back(relative); preview.totalSize += entry.file_size(ec); ++preview.fileCount; ++index;
		if (progress && (index == 1U || index % 20U == 0U)) progress("ハッシュ計算中 " + std::to_string(index) + " ファイル...");
		std::string hashError; const std::string hash = LauncherUpdate::CalculateSha256(entry.path(), hashError);
		if (hash.empty()) { error = "ハッシュ計算に失敗しました: " + relative.generic_string() + " / " + hashError; return false; }
		const auto existing = current.find(relative.generic_string());
		if (existing == current.end()) ++preview.addedCount;
		else {
			if (existing->second.size != entry.file_size(ec) || existing->second.sha256 != hash) ++preview.changedCount;
			current.erase(existing);
		}
	}
	for (const auto& [path, file] : current) { preview.removedFiles.push_back(file.relativePath); ++preview.removedCount; }
	if (preview.fileCount == 0U) { error = "配布対象ファイルがありません"; return false; }
	return true;
}

std::string PublisherService::PreviewText(const PublishPreview& preview) {
	std::ostringstream text; text << "バージョン: " << preview.version.ToString() << "\n公開先: " << ChannelDisplayName(preview.channel)
		<< "\nファイル数: " << preview.fileCount << "\n合計サイズ: " << preview.totalSize << " バイト\n追加 / 変更 / 削除: "
		<< preview.addedCount << " / " << preview.changedCount << " / " << preview.removedCount << "\n公開アドレス: " << preview.publicHub;
	return text.str();
}

bool PublisherService::Publish(const PublisherSettings& settings, const PublishPreview& preview, std::string& result,
	const PublisherProgress& progress, const std::filesystem::path& installRoot) {
	bool publishSucceeded = false;
	const auto historyRoot = installRoot.empty() ? LauncherExperience::DefaultInstallRoot() : installRoot;
	auto historyGuard = std::shared_ptr<void>(reinterpret_cast<void*>(1), [&](void*) {
		std::string historyError; AppendHistory(historyRoot, preview,
			publishSucceeded ? "成功" : "失敗: " + result, historyError);
	});
	const std::string version = preview.version.ToString(); const auto pendingRoot = settings.hubRoot / ".publish" / (version + ".pending");
	const auto package = settings.hubRoot / "engines" / version; std::error_code ec;
	if (progress) progress("成果物を収集中..."); std::filesystem::remove_all(pendingRoot, ec); ec.clear(); std::filesystem::create_directories(pendingRoot, ec);
	if (ec) { result = "公開準備フォルダーを作成できません: " + ec.message(); return false; }
	std::uint32_t copied = 0U;
	for (const auto& relative : preview.includedFiles) {
		const auto destination = pendingRoot / relative; std::filesystem::create_directories(destination.parent_path(), ec);
		std::filesystem::copy_file(settings.releaseSource / relative, destination, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) { result = "成果物収集に失敗: " + relative.generic_string(); return false; }
		++copied; if (progress && (copied == preview.fileCount || copied % 20U == 0U)) progress("成果物を収集中 " + std::to_string(copied) + " / " + std::to_string(preview.fileCount));
	}
	if (progress) progress("更新情報を生成中..."); EngineUpdateManifest settingsManifest{};
	settingsManifest.version = preview.version; settingsManifest.channel = preview.channel;
	settingsManifest.requiredProjectFormat = GetCG2ProjectFormatVersion(); settingsManifest.scriptApiVersion = GetCG2ScriptApiVersion();
	settingsManifest.baseUrl = preview.publicHub + "/engines/" + version; settingsManifest.removedFiles = preview.removedFiles;
	if (!LauncherUpdate::CreateManifest(pendingRoot, pendingRoot / "engine.manifest", settingsManifest, result)) return false;
	EngineUpdateManifest generated{}; if (!LauncherUpdate::LoadManifest(pendingRoot / "engine.manifest", generated, result)) return false;
	if (progress) progress("検証中...");
	for (std::size_t i = 0U; i < generated.files.size(); ++i) {
		std::string hashError; const auto& file = generated.files[i]; const auto path = pendingRoot / file.relativePath;
		if (std::filesystem::file_size(path, ec) != file.size || LauncherUpdate::CalculateSha256(path, hashError) != file.sha256) {
			result = "公開準備データの検証に失敗しました: " + file.relativePath.generic_string(); return false;
		}
		if (progress && (i + 1U == generated.files.size() || (i + 1U) % 20U == 0U)) progress("検証中 " + std::to_string(i + 1U) + " / " + std::to_string(generated.files.size()));
	}
	if (progress) progress("公開中..."); const auto previousPackage = settings.hubRoot / ".publish" / (version + ".previous");
	std::filesystem::remove_all(previousPackage, ec); ec.clear();
	if (std::filesystem::exists(package, ec)) { std::filesystem::rename(package, previousPackage, ec); if (ec) { result = "既存バージョンを退避できません"; return false; } }
	std::filesystem::create_directories(package.parent_path(), ec); std::filesystem::rename(pendingRoot, package, ec);
	if (ec) {
		std::error_code restore; if (std::filesystem::exists(previousPackage, restore)) std::filesystem::rename(previousPackage, package, restore);
		result = "バージョン切り替えに失敗しました。旧公開バージョンは維持されました"; return false;
	}
	const std::string channelName = Lower(GetEngineUpdateChannelText(preview.channel)); const auto metadataPending = settings.hubRoot / ".publish" / "metadata.pending";
	std::filesystem::remove_all(metadataPending, ec); std::filesystem::create_directories(metadataPending, ec);
	std::filesystem::copy_file(package / "engine.manifest", metadataPending / "engine.manifest", std::filesystem::copy_options::overwrite_existing, ec);
	std::ostringstream hub; hub << "{\r\n  \"formatVersion\": 1,\r\n  \"launcherVersion\": \"" << kCG2LauncherVersion << "\",\r\n  \"channels\": {\r\n";
	const char* channels[] = {"Stable", "Beta", "Dev"};
	for (std::size_t i = 0U; i < 3U; ++i) hub << "    \"" << channels[i] << "\": { \"manifestEndpoint\": \"/update/" << Lower(channels[i])
		<< "/engine.manifest\" }" << (i < 2U ? "," : "") << "\r\n";
	hub << "  }\r\n}\r\n"; if (!WriteText(metadataPending / "cg2-hub.json", hub.str(), result)) return false;
	std::filesystem::path channelBackup; const auto channelDestination = settings.hubRoot / "update" / channelName / "engine.manifest";
	if (!ReplaceFileKeepingBackup(metadataPending / "engine.manifest", channelDestination, channelBackup, result)) return false;
	std::filesystem::path hubBackup;
	if (!ReplaceFileKeepingBackup(metadataPending / "cg2-hub.json", settings.hubRoot / "cg2-hub.json", hubBackup, result)) {
		std::error_code restore; std::filesystem::remove(channelDestination, restore);
		if (std::filesystem::exists(channelBackup, restore)) std::filesystem::rename(channelBackup, channelDestination, restore);
		result += "。公開先は旧バージョンへ復元しました"; return false;
	}
	std::filesystem::remove(channelBackup, ec); std::filesystem::remove(hubBackup, ec); std::filesystem::remove_all(previousPackage, ec); std::filesystem::remove_all(metadataPending, ec);
	result = "公開完了: " + version + " " + ChannelDisplayName(preview.channel);
	publishSucceeded = true; return true;
}

bool PublisherService::LoadHistory(const std::filesystem::path& installRoot, std::vector<PublishHistoryEntry>& history, std::string& error) {
	history.clear(); std::ifstream file(HistoryPath(installRoot), std::ios::binary); if (!file) return true;
	std::string line; while (std::getline(file, line)) {
		if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == kBom[0]) line.erase(0U, 3U);
		if (!line.empty() && line.back() == '\r') line.pop_back(); const auto values = Split(line, '|');
		if (values.size() != 7U || values[0] != "Publish") continue; PublishHistoryEntry entry{}; entry.version = values[1];
		TryParseEngineUpdateChannel(values[2], entry.channel); entry.publishedAt = values[3]; entry.fileCount = static_cast<std::uint32_t>(std::stoul(values[4]));
		entry.totalSize = std::stoull(values[5]); entry.result = values[6]; history.push_back(std::move(entry));
	}
	return true;
}

bool PublisherServerService::Start(PublisherSettings& settings, const std::filesystem::path& installRoot, std::string& result) {
	if (settings.hubMode != PublisherHubMode::Local) { result = "外部サーバーはランチャーから開始・停止できません"; return false; }
	const auto current = GetStatus(settings, installRoot); if (current.running) { result = "既に稼働中です"; return true; }
	const std::uint16_t availablePort = FindAvailableDistributionPort(settings.distributionPort);
	if (availablePort == 0U) {
		result = "配布サーバーに使用できるポートが見つかりません（開始ポートから100個を確認しました）";
		return false;
	}
	settings.distributionPort = availablePort;
	settings.publicHubAddress = LocalHubAddress(availablePort);
	std::string saveError;
	if (!PublisherService::SaveSettings(installRoot, settings, saveError)) {
		result = "自動選択した配布ポートを保存できません: " + saveError;
		return false;
	}
	wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, _countof(module));
	const std::filesystem::path executable = module;
	const std::filesystem::path working = settings.hubRoot;
	const std::wstring arguments = L"serve-hub --output \"" + settings.hubRoot.wstring() + L"\" --port " + std::to_wstring(settings.distributionPort);
	DWORD processId = 0U; if (!StartDetached(executable, arguments, working, processId, result)) return false;
	std::string error; if (!WriteText(DistributionProcessFile(installRoot), std::to_string(processId) + "\r\n", error)) { result = error; return false; }
	result = "配布サーバーを開始しました";
	result += "\r\nLAN内の接続先: " + NormalizeHub(settings.publicHubAddress) + "/cg2-hub.json";
	result += "\r\n接続できない場合は、Windows ファイアウォールでTCP " +
		std::to_string(settings.distributionPort) + " の受信を許可してください";
	return true;
}

bool PublisherServerService::Stop(const std::filesystem::path& installRoot, std::string& result) {
	const auto processFile = DistributionProcessFile(installRoot); const DWORD processId = ReadPid(processFile);
	if (!IsProcessRunning(processId)) { std::error_code ec; std::filesystem::remove(processFile, ec); result = "既に停止しています"; return true; }
	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
	if (!process || !TerminateProcess(process, 0U)) { if (process) CloseHandle(process); result = "サーバーを停止できません"; return false; }
	WaitForSingleObject(process, 3000U); CloseHandle(process); std::error_code ec; std::filesystem::remove(processFile, ec);
	result = "配布サーバーを停止しました"; return true;
}

bool PublisherServerService::Restart(PublisherSettings& settings, const std::filesystem::path& installRoot, std::string& result) {
	std::string stopResult; if (!Stop(installRoot, stopResult)) { result = stopResult; return false; }
	return Start(settings, installRoot, result);
}

ManagedServerStatus PublisherServerService::GetStatus(const PublisherSettings& settings, const std::filesystem::path& installRoot) {
	ManagedServerStatus status{}; status.port = settings.distributionPort;
	status.running = settings.hubMode == PublisherHubMode::Local && IsProcessRunning(ReadPid(DistributionProcessFile(installRoot)));
	const std::string host = settings.hubMode == PublisherHubMode::Local ? "127.0.0.1" : HostOnly(settings.publicHubAddress);
	status.reachable = IsPortReachable(host, status.port, status.latencyMilliseconds);
	status.detail = (status.running ? "稼働中" : status.reachable ? "接続可能" : "停止中") + std::string(" / ポート ") + std::to_string(status.port)
		+ " / " + std::to_string(status.latencyMilliseconds) + " ms";
	return status;
}

std::string PublisherServerService::ResolveReachableHubAddress(const PublisherSettings& settings, const std::string& preferred) {
	std::vector<std::string> candidates;
	const auto add = [&candidates](std::string address) {
		address = NormalizeHub(std::move(address));
		if (address.empty()) return;
		if (std::find(candidates.begin(), candidates.end(), address) == candidates.end()) candidates.push_back(std::move(address));
	};
	add(preferred);
	add(settings.publicHubAddress);
	// Tailscale名は招待相手も同じtailnetなら使える。LANアドレスより先に試す。
	if (!settings.collaborationHost.empty()) {
		add("http://" + settings.collaborationHost + ':' + std::to_string(settings.distributionPort));
	}
	add(LocalHubAddress(settings.distributionPort));
	// 127.0.0.1は配った相手には使えないので最後の保険に留める。
	add("http://127.0.0.1:" + std::to_string(settings.distributionPort));
	for (const auto& candidate : candidates) {
		std::uint32_t latency = 0U;
		if (IsPortReachable(HostOnly(candidate), HubPort(candidate, settings.distributionPort), latency)) return candidate;
	}
	return candidates.empty() ? std::string{} : candidates.front();
}

std::string PublisherServerService::ResolveCollaborationHost(const PublisherSettings& settings) {
	std::vector<std::string> candidates;
	const auto add = [&candidates](std::string host) {
		if (host.empty()) return;
		if (std::find(candidates.begin(), candidates.end(), host) == candidates.end()) candidates.push_back(std::move(host));
	};
	if (settings.collaborationMode == CollaborationConnectionMode::Tailscale) add(settings.collaborationHost);
	add(HostOnly(NormalizeHub(settings.publicHubAddress)));
	add(settings.collaborationHost);
	add(HostOnly(LocalHubAddress(settings.collaborationPort)));
	for (const auto& host : candidates) {
		std::uint32_t latency = 0U;
		if (IsPortReachable(host, settings.collaborationPort, latency)) return host;
	}
	// 共同制作Serverが止まっていると全滅するため、その場合は設定どおりの先頭候補を返す。
	return candidates.empty() ? std::string{} : candidates.front();
}

bool PublisherServerService::RunDistributionServer(const std::filesystem::path& root, std::uint16_t port, std::string& error) {
	std::error_code ec; const auto absoluteRoot = std::filesystem::weakly_canonical(root, ec);
	if (ec || !std::filesystem::is_directory(absoluteRoot, ec)) { error = "配布フォルダーがありません"; return false; }
	WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "Winsock初期化に失敗"; return false; }
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (listener == INVALID_SOCKET) { error = "Socket作成に失敗"; WSACleanup(); return false; }
	BOOL exclusive = TRUE; setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
	sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(port);
	if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(listener, SOMAXCONN) == SOCKET_ERROR) {
		error = "ポート " + std::to_string(port) + " で待ち受けできません"; closesocket(listener); WSACleanup(); return false;
	}
	for (;;) {
		SOCKET client = accept(listener, nullptr, nullptr); if (client == INVALID_SOCKET) continue;
		std::array<char, 8192U> request{}; const int received = recv(client, request.data(), static_cast<int>(request.size() - 1U), 0);
		std::string target; if (received > 0) { std::istringstream firstLine(std::string(request.data(), static_cast<std::size_t>(received))); std::string method; firstLine >> method >> target; if (method != "GET" && method != "HEAD") target.clear(); }
		const auto query = target.find('?'); if (query != std::string::npos) target.erase(query);
		std::string decodedTarget;
		const bool requestTargetValid = DecodeUrlPath(target, decodedTarget);
		if (requestTargetValid) target = std::move(decodedTarget);
		std::replace(target.begin(), target.end(), '\\', '/'); while (!target.empty() && target.front() == '/') target.erase(target.begin());
		if (target.empty() && requestTargetValid) target = "cg2-hub.json";
		std::filesystem::path filePath; bool safe = requestTargetValid && target.find("..") == std::string::npos;
		if (safe) { filePath = std::filesystem::weakly_canonical(absoluteRoot / ToWide(target), ec); safe = !ec && filePath.native().starts_with(absoluteRoot.native()); }
		if (!safe || !std::filesystem::is_regular_file(filePath, ec)) {
			const std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; SendAll(client, response.data(), response.size());
		} else {
			const std::uint64_t size = std::filesystem::file_size(filePath, ec); const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: " + ContentType(filePath)
				+ "\r\nContent-Length: " + std::to_string(size) + "\r\nConnection: close\r\n\r\n"; SendAll(client, header.data(), header.size());
			if (target.empty() || std::string(request.data()).starts_with("GET")) {
				std::ifstream file(filePath, std::ios::binary); std::array<char, 65536U> buffer{};
				while (file) { file.read(buffer.data(), buffer.size()); const auto count = file.gcount(); if (count > 0 && !SendAll(client, buffer.data(), static_cast<std::size_t>(count))) break; }
			}
		}
		shutdown(client, SD_BOTH); closesocket(client);
	}
}
