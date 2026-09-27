#include "EditorTeamCollaborationManager.h"

#pragma warning(push, 0)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <shellapi.h>
#pragma warning(pop)

#include "EditorScene.h"
#include "EditorAssetUtility.h"
#include "EditorSharedState.h"
#include "EditorTeamUuid.h"
#include "Source/Engine/Asset/AssetManager.h"
#include "Source/Engine/Asset/AssetRegistry.h"
#include "Source/Engine/Core/EngineVersion.h"
#include "Source/Engine/Core/ProjectVersionManager.h"
#include "Source/Engine/Collaboration/CollaborationProtocol.h"
#include "Source/Engine/Collaboration/TcpCollaborationTransport.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
	EditorTeamCollaborationManager* g_activeTeamCollaborationManager = nullptr;
	constexpr const char* kTeamSettingsPath = "ProjectSettings/TeamCollaboration.settings";
	constexpr const char* kTeamChangeLogPath = ".team/change-log.jsonl";
	constexpr const char* kTeamLiveSnapshotPath = ".team/live/current.scene";
	constexpr const char* kTeamIncomingSnapshotPath = ".team/live/incoming.scene";
	constexpr const char* kTeamLiveFragmentSnapshotPath = ".team/live/fragment.scene";
	constexpr const char* kTeamAssetUuidRegistryPath = ".team/asset-uuids.txt";
	constexpr const char* kDedicatedServerPidPath = ".team/cg2-team-server.pid";
	constexpr const char* kDedicatedServerStatusPath = ".team/cg2-team-server.status";

	// 共同制作ServerをLauncherからEditorへ移した以前の版は、PIDをLocalAppDataへ保存していた。
	// 移行前に開始したServerもEditorから停止できないと、CG2TeamServer.exeがロックされたままになる。
	std::filesystem::path LegacyDedicatedServerPidPath() {
		wchar_t localAppData[32768]{};
		const DWORD length = GetEnvironmentVariableW(
			L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
		if (length == 0u || length >= static_cast<DWORD>(std::size(localAppData))) return {};
		return std::filesystem::path(localAppData) /
			L"CG2Engine" / L"LauncherState" / L"collaboration-server.pid";
	}

	std::wstring ToWideText(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (count <= 0) return {};
		std::wstring result(static_cast<std::size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	std::filesystem::path FindDedicatedServerExecutable() {
		wchar_t modulePath[32768]{};
		if (GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0u) return {};
		return std::filesystem::path(modulePath).parent_path() / "CG2TeamServer.exe";
	}

	std::uint32_t ReadProcessIdFile(const std::filesystem::path& path) {
		if (path.empty()) return 0u;
		std::ifstream input(path, std::ios::binary);
		std::string value;
		std::getline(input, value);
		if (value.size() >= 3u && static_cast<unsigned char>(value[0]) == 0xEFu) value.erase(0u, 3u);
		try { return static_cast<std::uint32_t>(std::stoul(value)); }
		catch (const std::exception&) { return 0u; }
	}

	std::uint32_t ReadDedicatedServerProcessId() {
		return ReadProcessIdFile(kDedicatedServerPidPath);
	}

	std::uint32_t ReadLegacyDedicatedServerProcessId() {
		return ReadProcessIdFile(LegacyDedicatedServerPidPath());
	}

	struct DedicatedServerStatusIdentity {
		std::uint16_t port = 0u;
		std::string projectId;
		bool hasIdentity = false;
	};

	DedicatedServerStatusIdentity ReadDedicatedServerStatusIdentity() {
		std::ifstream input(kDedicatedServerStatusPath, std::ios::binary);
		std::string line;
		std::getline(input, line);
		if (line.size() >= 3u && static_cast<unsigned char>(line[0]) == 0xEFu) line.erase(0u, 3u);
		if (!line.empty() && line.back() == '\r') line.pop_back();

		std::vector<std::string> values;
		std::size_t begin = 0u;
		while (begin <= line.size()) {
			const std::size_t end = line.find('|', begin);
			values.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
			if (end == std::string::npos) break;
			begin = end + 1u;
		}

		DedicatedServerStatusIdentity status{};
		if (values.size() < 5u || values[0] != "Running") return status;
		try {
			const int parsedPort = std::stoi(values[1]);
			if (parsedPort < 1 || parsedPort > 65535) return status;
			status.port = static_cast<std::uint16_t>(parsedPort);
		}
		catch (const std::exception&) {
			return status;
		}
		status.projectId = values[4];
		status.hasIdentity = CG2Collaboration::IsValidProjectId(status.projectId);
		return status;
	}

	bool IsDedicatedServerProcess(std::uint32_t processId) {
		if (processId == 0u) return false;
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (process == nullptr) return false;
		wchar_t processPath[32768]{};
		DWORD processPathLength = static_cast<DWORD>(std::size(processPath));
		const bool matches = QueryFullProcessImageNameW(process, 0u, processPath, &processPathLength) != FALSE &&
			_wcsicmp(std::filesystem::path(processPath).filename().c_str(), L"CG2TeamServer.exe") == 0;
		CloseHandle(process);
		return matches;
	}

	bool CanConnectToServer(const std::string& host, std::uint16_t port, std::string& error) {
		if (host.empty()) { error = "接続先Hostを入力してください"; return false; }
		WSADATA winsock{};
		if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) { error = "ネットワークを初期化できません"; return false; }
		addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
		addrinfo* addresses = nullptr;
		const std::string portText = std::to_string(port);
		if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &addresses) != 0 || addresses == nullptr) {
			error = "Host名を解決できません: " + host; WSACleanup(); return false;
		}
		bool connected = false;
		for (addrinfo* address = addresses; address != nullptr && !connected; address = address->ai_next) {
			SOCKET probe = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if (probe == INVALID_SOCKET) continue;
			u_long nonBlocking = 1u; ioctlsocket(probe, FIONBIO, &nonBlocking);
			const int connectResult = connect(probe, address->ai_addr, static_cast<int>(address->ai_addrlen));
			connected = connectResult == 0;
			if (!connected && WSAGetLastError() == WSAEWOULDBLOCK) {
				fd_set writable; FD_ZERO(&writable); FD_SET(probe, &writable);
				timeval timeout{}; timeout.tv_sec = 2;
				if (select(0, nullptr, &writable, nullptr, &timeout) > 0) {
					int socketError = SOCKET_ERROR; int socketErrorSize = sizeof(socketError);
					connected = getsockopt(probe, SOL_SOCKET, SO_ERROR,
						reinterpret_cast<char*>(&socketError), &socketErrorSize) == 0 && socketError == 0;
				}
			}
			closesocket(probe);
		}
		freeaddrinfo(addresses); WSACleanup();
		if (!connected) error = "接続できません: " + host + ":" + portText;
		return connected;
	}

	bool RunScheduledTaskCommand(const std::wstring& arguments, DWORD& exitCode) {
		std::wstring commandLine = L"schtasks.exe " + arguments;
		STARTUPINFOW startupInfo{}; startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESHOWWINDOW; startupInfo.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION processInfo{};
		if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
				nullptr, nullptr, &startupInfo, &processInfo) == FALSE) return false;
		const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 15000u);
		const bool completed = waitResult == WAIT_OBJECT_0 &&
			GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return completed;
	}

	std::wstring DedicatedServerTaskName(const std::string& projectId) {
		return L"CG2Engine Team Server " + ToWideText(projectId);
	}
	constexpr float kSnapshotIntervalSeconds = 0.75f;
	constexpr float kAssetScanIntervalSeconds = 2.0f;
	constexpr float kLockHeartbeatIntervalSeconds = 10.0f;
	constexpr float kLockTimeoutSeconds = 30.0f;
	// Cursorや操作対象を見ながら共同編集できるよう、Presenceは差分履歴より高頻度で送る。
	constexpr float kPresenceIntervalSeconds = 0.20f;
	constexpr float kPresenceTimeoutSeconds = 5.0f;
	constexpr float kActivityTimeoutSeconds = 2.0f;
	constexpr std::size_t kMaximumRecentChangeCount = 5000u;
	constexpr std::size_t kMaximumRemoteMemberCount = 2u;
	// FBXや無圧縮WAVは数十MBになるため、Projectで実際に利用するAssetを転送できる容量を確保する。
	// Asset本体はBase64化されるので、受信Bufferは元データ上限より大きくする必要がある。
	constexpr std::size_t kMaximumNetworkBufferBytes = 192u * 1024u * 1024u;
	constexpr std::uint64_t kMaximumSynchronizedAssetBytes = 128u * 1024u * 1024u;
	// これを超えるSnapshot/Asset本体は assetBegin/assetChunk/assetEnd に分割し、
	// 送受信バイト数を実測できるようにする（1メッセージ丸ごと送信だと進捗が見えない）。
	constexpr std::size_t kChunkedTransferThresholdBytes = 512u * 1024u;
	constexpr std::size_t kChunkTransportPayloadLength = 256u * 1024u;

	std::string EscapeJsonText(const std::string& text) {
		std::string escapedText;
		escapedText.reserve(text.size() + 32u);

		for (const char character : text) {
			switch (character) {
			case '\\':
				escapedText += "\\\\";
				break;
			case '"':
				escapedText += "\\\"";
				break;
			case '\n':
				escapedText += "\\n";
				break;
			case '\r':
				escapedText += "\\r";
				break;
			case '\t':
				escapedText += "\\t";
				break;
			default:
				escapedText.push_back(character);
				break;
			}
		}

		return escapedText;
	}

	std::string ReadJsonString(const std::string& jsonText, const char* key) {
		const std::string keyToken = std::string("\"") + key + "\":\"";
		const std::size_t valueStart = jsonText.find(keyToken);

		if (valueStart == std::string::npos) {
			return {};
		}

		std::string value;
		bool isEscaped = false;

		for (std::size_t characterIndex = valueStart + keyToken.size();
			characterIndex < jsonText.size();
			characterIndex++) {
			const char character = jsonText[characterIndex];

			if (isEscaped) {
				switch (character) {
				case 'n': value.push_back('\n'); break;
				case 'r': value.push_back('\r'); break;
				case 't': value.push_back('\t'); break;
				default: value.push_back(character); break;
				}

				isEscaped = false;
				continue;
			}

			if (character == '\\') {
				isEscaped = true;
				continue;
			}

			if (character == '"') {
				break;
			}

			value.push_back(character);
		}

		return value;
	}

	std::uint64_t ReadJsonUnsigned(const std::string& jsonText, const char* key) {
		const std::string keyToken = std::string("\"") + key + "\":";
		const std::size_t valueStart = jsonText.find(keyToken);

		if (valueStart == std::string::npos) {
			return 0u;
		}

		std::uint64_t value = 0u;

		for (std::size_t characterIndex = valueStart + keyToken.size();
			characterIndex < jsonText.size();
			characterIndex++) {
			const char character = jsonText[characterIndex];

			if (character < '0' || character > '9') {
				break;
			}

			value = value * 10u + static_cast<std::uint64_t>(character - '0');
		}

		return value;
	}

	float ReadJsonFloat(const std::string& jsonText, const char* key) {
		const std::string keyToken = std::string("\"") + key + "\":";
		const std::size_t valueStart = jsonText.find(keyToken);
		if (valueStart == std::string::npos) return 0.0f;
		const std::size_t numberStart = valueStart + keyToken.size();
		const std::size_t numberEnd = jsonText.find_first_of(",}", numberStart);
		try {
			return std::stof(jsonText.substr(numberStart, numberEnd - numberStart));
		}
		catch (const std::exception&) {
			return 0.0f;
		}
	}

	bool ReadJsonBool(const std::string& jsonText, const char* key) {
		return ReadJsonUnsigned(jsonText, key) != 0u;
	}

	std::string SetTargetUserId(std::string jsonText, const std::string& targetUserId) {
		if (targetUserId.empty() || jsonText.empty() || jsonText.back() != '}') {
			return jsonText;
		}

		jsonText.pop_back();
		jsonText += ",\"targetUserId\":\"" + EscapeJsonText(targetUserId) + "\"}";
		return jsonText;
	}

	std::uint32_t BuildUserColor(const std::string& userId) {
		std::uint64_t hash = 1469598103934665603ull;
		for (const unsigned char byte : userId) {
			hash ^= static_cast<std::uint64_t>(byte);
			hash *= 1099511628211ull;
		}
		const std::uint8_t red = static_cast<std::uint8_t>(96u + (hash & 0x7Fu));
		const std::uint8_t green = static_cast<std::uint8_t>(96u + ((hash >> 8u) & 0x7Fu));
		const std::uint8_t blue = static_cast<std::uint8_t>(96u + ((hash >> 16u) & 0x7Fu));
		return 0xFF000000u | (static_cast<std::uint32_t>(blue) << 16u) |
			(static_cast<std::uint32_t>(green) << 8u) | red;
	}

	std::string SerializeChangeEvent(const EditorTeamChangeEvent& changeEvent, const char* messageType) {
		std::ostringstream jsonStream;
		jsonStream
			<< "{\"type\":\"" << messageType
			<< "\",\"changeId\":\"" << EscapeJsonText(changeEvent.changeId)
			<< "\",\"userId\":\"" << EscapeJsonText(changeEvent.userId)
			<< "\",\"userName\":\"" << EscapeJsonText(changeEvent.userName)
			<< "\",\"scenePath\":\"" << EscapeJsonText(changeEvent.scenePath)
			<< "\",\"sceneUuid\":\"" << EscapeJsonText(changeEvent.sceneUuid)
			<< "\",\"objectUuid\":\"" << EscapeJsonText(changeEvent.objectUuid)
			<< "\",\"componentUuid\":\"" << EscapeJsonText(changeEvent.componentUuid)
			<< "\",\"operation\":\"" << EscapeJsonText(changeEvent.operation)
			<< "\",\"property\":\"" << EscapeJsonText(changeEvent.property)
			<< "\",\"oldValue\":\"" << EscapeJsonText(changeEvent.oldValue)
			<< "\",\"newValue\":\"" << EscapeJsonText(changeEvent.newValue)
			<< "\",\"snapshotData\":\"" << EscapeJsonText(changeEvent.snapshotData)
			<< "\",\"assetHash\":\"" << EscapeJsonText(changeEvent.assetHash)
			<< "\",\"fileSize\":" << changeEvent.fileSize
			<< ",\"timestampUnixMilliseconds\":" << changeEvent.timestampUnixMilliseconds
			<< ",\"baseRevision\":" << changeEvent.baseRevision
			<< ",\"revision\":" << changeEvent.revision
			<< "}";
		return jsonStream.str();
	}

	EditorTeamChangeEvent DeserializeChangeEvent(const std::string& jsonText) {
		EditorTeamChangeEvent changeEvent{};
		changeEvent.changeId = ReadJsonString(jsonText, "changeId");
		changeEvent.userId = ReadJsonString(jsonText, "userId");
		changeEvent.userName = ReadJsonString(jsonText, "userName");
		changeEvent.scenePath = ReadJsonString(jsonText, "scenePath");
		changeEvent.sceneUuid = ReadJsonString(jsonText, "sceneUuid");
		changeEvent.objectUuid = ReadJsonString(jsonText, "objectUuid");
		changeEvent.componentUuid = ReadJsonString(jsonText, "componentUuid");
		changeEvent.operation = ReadJsonString(jsonText, "operation");
		changeEvent.property = ReadJsonString(jsonText, "property");
		changeEvent.oldValue = ReadJsonString(jsonText, "oldValue");
		changeEvent.newValue = ReadJsonString(jsonText, "newValue");
		changeEvent.snapshotData = ReadJsonString(jsonText, "snapshotData");
		changeEvent.assetHash = ReadJsonString(jsonText, "assetHash");
		changeEvent.fileSize = ReadJsonUnsigned(jsonText, "fileSize");
		changeEvent.timestampUnixMilliseconds = ReadJsonUnsigned(
			jsonText,
			"timestampUnixMilliseconds");
		changeEvent.baseRevision = ReadJsonUnsigned(jsonText, "baseRevision");
		changeEvent.revision = ReadJsonUnsigned(jsonText, "revision");
		return changeEvent;
	}

	std::uint64_t GetCurrentUnixTimestampMilliseconds() {
		const auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch());
		return static_cast<std::uint64_t>(elapsedTime.count());
	}

	std::uint64_t CalculateTextHash(const std::string& text) {
		std::uint64_t hash = 1469598103934665603ull;

		for (const unsigned char byte : text) {
			hash ^= static_cast<std::uint64_t>(byte);
			hash *= 1099511628211ull;
		}

		return hash;
	}

	std::string FormatHash(std::uint64_t hash) {
		std::ostringstream hashStream;
		hashStream << std::hex << std::uppercase << hash;
		return hashStream.str();
	}

	std::filesystem::path BuildConflictDirectoryPath(const std::string& changeId) {
		std::string safeChangeId;
		safeChangeId.reserve(changeId.size());

		for (const unsigned char character : changeId) {
			if (std::isalnum(character) != 0 || character == '-') {
				safeChangeId.push_back(static_cast<char>(character));
			}
			else {
				safeChangeId.push_back('_');
			}
		}

		if (safeChangeId.empty()) {
			safeChangeId = "UnknownConflict";
		}

		return std::filesystem::path(".team/conflicts") / safeChangeId;
	}

	std::string EncodeBase64(const std::string& binaryData) {
		constexpr char encodingTable[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string encodedData;
		encodedData.reserve(((binaryData.size() + 2u) / 3u) * 4u);

		for (std::size_t byteIndex = 0u; byteIndex < binaryData.size(); byteIndex += 3u) {
			const std::uint32_t firstByte = static_cast<unsigned char>(binaryData[byteIndex]);
			const std::uint32_t secondByte = byteIndex + 1u < binaryData.size()
				? static_cast<unsigned char>(binaryData[byteIndex + 1u])
				: 0u;
			const std::uint32_t thirdByte = byteIndex + 2u < binaryData.size()
				? static_cast<unsigned char>(binaryData[byteIndex + 2u])
				: 0u;
			const std::uint32_t combinedValue =
				(firstByte << 16u) | (secondByte << 8u) | thirdByte;
			encodedData.push_back(encodingTable[(combinedValue >> 18u) & 0x3Fu]);
			encodedData.push_back(encodingTable[(combinedValue >> 12u) & 0x3Fu]);
			encodedData.push_back(
				byteIndex + 1u < binaryData.size()
					? encodingTable[(combinedValue >> 6u) & 0x3Fu]
					: '=');
			encodedData.push_back(
				byteIndex + 2u < binaryData.size()
					? encodingTable[combinedValue & 0x3Fu]
					: '=');
		}

		return encodedData;
	}

	std::string DecodeBase64(const std::string& encodedData) {
		std::array<int32_t, 256> decodingTable{};
		decodingTable.fill(-1);
		constexpr char encodingTable[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		for (int32_t tableIndex = 0; tableIndex < 64; tableIndex++) {
			decodingTable[static_cast<unsigned char>(encodingTable[tableIndex])] = tableIndex;
		}

		std::string decodedData;
		decodedData.reserve((encodedData.size() / 4u) * 3u);
		std::uint32_t combinedValue = 0u;
		int32_t bitCount = 0;

		for (const unsigned char character : encodedData) {
			if (character == '=') {
				break;
			}

			const int32_t decodedValue = decodingTable[character];

			if (decodedValue < 0) {
				continue;
			}

			combinedValue = (combinedValue << 6u) | static_cast<std::uint32_t>(decodedValue);
			bitCount += 6;

			if (bitCount >= 8) {
				bitCount -= 8;
				decodedData.push_back(static_cast<char>((combinedValue >> bitCount) & 0xFFu));
			}
		}

		return decodedData;
	}

	bool ResolveSafeAssetPath(const std::string& assetPath, std::filesystem::path& resolvedPath) {
		const std::filesystem::path normalizedPath =
			std::filesystem::path(assetPath).lexically_normal();

		if (normalizedPath.empty() || normalizedPath.is_absolute()) {
			return false;
		}

		const std::string normalizedText = normalizedPath.generic_string();
		const bool isInsideAssets = normalizedText == "Assets" ||
			normalizedText.starts_with("Assets/");
		const bool isInsideResources = normalizedText == "resources" ||
			normalizedText.starts_with("resources/");

		if ((!isInsideAssets && !isInsideResources) || normalizedText.find("..") != std::string::npos) {
			return false;
		}

		resolvedPath = normalizedPath;
		return true;
	}

	std::string FormatLocalTimestamp(std::uint64_t timestampMilliseconds) {
		if (timestampMilliseconds == 0u) {
			return "不明";
		}

		const std::time_t timestampSeconds = static_cast<std::time_t>(timestampMilliseconds / 1000u);
		std::tm localTime{};
		if (localtime_s(&localTime, &timestampSeconds) != 0) {
			return "不明";
		}

		std::ostringstream timestampText;
		timestampText << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return timestampText.str();
	}

	bool IsPathInsideSharedSceneFolder(
		const std::string& candidatePath,
		const std::string& sharedSceneFolder) {
		if (sharedSceneFolder.empty()) {
			return false;
		}

		const std::string normalizedCandidate =
			std::filesystem::path(candidatePath).lexically_normal().generic_string();
		std::string normalizedFolder =
			std::filesystem::path(sharedSceneFolder).lexically_normal().generic_string();

		while (!normalizedFolder.empty() && normalizedFolder.back() == '/') {
			normalizedFolder.pop_back();
		}

		return normalizedCandidate == normalizedFolder ||
			normalizedCandidate.starts_with(normalizedFolder + "/");
	}

	bool IsValidSharedGameSceneFolder(const std::string& folderPath) {
		const std::string normalizedFolder =
			std::filesystem::path(folderPath).lexically_normal().generic_string();
		return normalizedFolder.starts_with("Assets/Scenes/") &&
			normalizedFolder.find("/_Archive") == std::string::npos;
	}

	// 抽出アルゴリズム自体はAssetManager(Source/Engine/Asset)側のExtractProjectPathReferencesへ
	// 移した。ここではSetへの追加という呼び出し元の期待に合わせるための薄い委譲のみ行う。
	void CollectProjectPathsFromText(
		const std::string& text,
		std::unordered_set<std::string>& paths) {
		for (const std::string& referencedPath : ExtractProjectPathReferences(text)) {
			paths.insert(referencedPath);
		}
	}

	bool IsScriptAssetPath(const std::string& assetPath) {
		std::string extension = std::filesystem::path(assetPath).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".cpp" || extension == ".h" || extension == ".hpp";
	}

	bool IsCollaborationSceneAssetPath(const std::string& assetPath) {
		std::string extension = std::filesystem::path(assetPath).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".scene";
	}

	bool IsIgnoredCollaborationAssetPath(const std::string& assetPath) {
		constexpr const char* ignoredDirectoryPrefixes[] = {
			"Assets/Scenes/_Archive/",
			"Assets/Shaders/lygia/",
			"Assets/Shaders/FidelityFX/",
			"Assets/Shaders/RTXGIDDGI/",
			"Assets/Shaders/NRD-4.17.3/",
			"Assets/Shaders/RTXGI-SDK/",
		};

		for (const char* ignoredPrefix : ignoredDirectoryPrefixes) {
			if (assetPath.starts_with(ignoredPrefix)) {
				return true;
			}
		}

		if (assetPath.find("/_Archive/") != std::string::npos) {
			return true;
		}

		return assetPath.find("/x64/") != std::string::npos ||
			assetPath.find(".bak") != std::string::npos ||
			std::filesystem::path(assetPath).filename() == "imgui.ini";
	}

	std::vector<std::string> SplitSnapshotLine(const std::string& line) {
		std::vector<std::string> elements;
		std::size_t elementStart = 0u;

		while (elementStart <= line.size()) {
			const std::size_t separatorPosition = line.find('|', elementStart);
			elements.push_back(line.substr(
				elementStart,
				separatorPosition == std::string::npos
					? std::string::npos
					: separatorPosition - elementStart));

			if (separatorPosition == std::string::npos) {
				break;
			}

			elementStart = separatorPosition + 1u;
		}

		return elements;
	}

	struct SnapshotObjectRecord {
		std::string uuid;
		std::string serializedLine;
		std::string name;
		std::string parentId;
		std::string transform;
		std::string isActive;
	};

	struct SnapshotComponentRecord {
		std::string uuid;
		std::string ownerUuid;
		std::string serializedLine;
	};

	struct SceneSnapshotIndex {
		std::unordered_map<std::string, SnapshotObjectRecord> objects;
		std::unordered_map<std::string, SnapshotComponentRecord> components;
	};

	SceneSnapshotIndex BuildSceneSnapshotIndex(const std::string& snapshotText) {
		SceneSnapshotIndex snapshotIndex{};
		std::unordered_map<int32_t, SnapshotObjectRecord> objectsById;
		std::unordered_map<int32_t, std::string> objectUuidById;
		std::string pendingComponentUuid;
		int32_t pendingComponentOwnerId = -1;
		std::istringstream snapshotStream(snapshotText);
		std::string line;

		while (std::getline(snapshotStream, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			std::vector<std::string> elements = SplitSnapshotLine(line);

			if (elements.empty()) {
				continue;
			}

			if (elements[0].size() >= 3u &&
				static_cast<unsigned char>(elements[0][0]) == 0xEFu &&
				static_cast<unsigned char>(elements[0][1]) == 0xBBu &&
				static_cast<unsigned char>(elements[0][2]) == 0xBFu) {
				elements[0].erase(0u, 3u);
			}

			if (elements[0] == "GameObject" && elements.size() >= 14u) {
				const int32_t objectId = std::stoi(elements[1]);
				SnapshotObjectRecord objectRecord{};
				objectRecord.serializedLine = line;
				objectRecord.parentId = elements[2];
				objectRecord.name = elements[3];
				objectRecord.transform =
					elements[4] + "|" + elements[5] + "|" + elements[6] + "|" +
					elements[7] + "|" + elements[8] + "|" + elements[9] + "|" +
					elements[10] + "|" + elements[11] + "|" + elements[12];
				objectRecord.isActive = elements[13];
				objectsById[objectId] = std::move(objectRecord);
			}
			else if (elements[0] == "GameObjectUuid" && elements.size() >= 3u) {
				const int32_t objectId = std::stoi(elements[1]);
				auto objectIterator = objectsById.find(objectId);

				if (objectIterator != objectsById.end()) {
					objectIterator->second.uuid = elements[2];
					objectUuidById[objectId] = elements[2];
					snapshotIndex.objects[elements[2]] = objectIterator->second;
				}
			}
			else if (elements[0] == "ComponentUuid" && elements.size() >= 4u) {
				pendingComponentOwnerId = std::stoi(elements[1]);
				pendingComponentUuid = elements[3];
			}
			else if (elements[0] == "Component" && !pendingComponentUuid.empty()) {
				SnapshotComponentRecord componentRecord{};
				componentRecord.uuid = pendingComponentUuid;
				componentRecord.serializedLine = line;
				const auto ownerUuidIterator = objectUuidById.find(pendingComponentOwnerId);

				if (ownerUuidIterator != objectUuidById.end()) {
					componentRecord.ownerUuid = ownerUuidIterator->second;
				}

				snapshotIndex.components[pendingComponentUuid] = std::move(componentRecord);
				pendingComponentUuid.clear();
				pendingComponentOwnerId = -1;
			}
		}

		return snapshotIndex;
	}

	std::vector<EditorTeamChangeEvent> CollectSceneChanges(
		const std::string& previousSnapshot,
		const std::string& currentSnapshot) {
		const SceneSnapshotIndex previousIndex = BuildSceneSnapshotIndex(previousSnapshot);
		const SceneSnapshotIndex currentIndex = BuildSceneSnapshotIndex(currentSnapshot);
		std::vector<EditorTeamChangeEvent> changeEvents;
		std::unordered_set<std::string> createdObjectUuids;
		std::unordered_set<std::string> deletedObjectUuids;

		for (const auto& currentObjectPair : currentIndex.objects) {
			if (previousIndex.objects.find(currentObjectPair.first) == previousIndex.objects.end()) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "CreateObject";
				changeEvent.objectUuid = currentObjectPair.first;
				changeEvent.property = "GameObject";
				changeEvent.newValue = currentObjectPair.second.name;
				changeEvents.push_back(std::move(changeEvent));
				createdObjectUuids.insert(currentObjectPair.first);
			}
		}

		for (const auto& previousObjectPair : previousIndex.objects) {
			if (currentIndex.objects.find(previousObjectPair.first) == currentIndex.objects.end()) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "DeleteObject";
				changeEvent.objectUuid = previousObjectPair.first;
				changeEvent.property = "GameObject";
				changeEvent.oldValue = previousObjectPair.second.name;
				changeEvents.push_back(std::move(changeEvent));
				deletedObjectUuids.insert(previousObjectPair.first);
			}
		}

		for (const auto& currentObjectPair : currentIndex.objects) {
			const auto previousObjectIterator = previousIndex.objects.find(currentObjectPair.first);

			if (previousObjectIterator == previousIndex.objects.end()) {
				continue;
			}

			const SnapshotObjectRecord& previousObject = previousObjectIterator->second;
			const SnapshotObjectRecord& currentObject = currentObjectPair.second;

			if (previousObject.name != currentObject.name) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "RenameObject";
				changeEvent.objectUuid = currentObjectPair.first;
				changeEvent.property = "Name";
				changeEvent.oldValue = previousObject.name;
				changeEvent.newValue = currentObject.name;
				changeEvents.push_back(std::move(changeEvent));
			}

			if (previousObject.parentId != currentObject.parentId) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "SetParent";
				changeEvent.objectUuid = currentObjectPair.first;
				changeEvent.property = "Parent";
				changeEvent.oldValue = previousObject.parentId;
				changeEvent.newValue = currentObject.parentId;
				changeEvents.push_back(std::move(changeEvent));
			}

			if (previousObject.transform != currentObject.transform) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "SetProperty";
				changeEvent.objectUuid = currentObjectPair.first;
				changeEvent.property = "Transform";
				changeEvent.oldValue = previousObject.transform;
				changeEvent.newValue = currentObject.transform;
				changeEvents.push_back(std::move(changeEvent));
			}

			if (previousObject.isActive != currentObject.isActive) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "SetProperty";
				changeEvent.objectUuid = currentObjectPair.first;
				changeEvent.property = "Active";
				changeEvent.oldValue = previousObject.isActive;
				changeEvent.newValue = currentObject.isActive;
				changeEvents.push_back(std::move(changeEvent));
			}
		}

		for (const auto& currentComponentPair : currentIndex.components) {
			const auto previousComponentIterator = previousIndex.components.find(currentComponentPair.first);

			if (previousComponentIterator == previousIndex.components.end() &&
				createdObjectUuids.find(currentComponentPair.second.ownerUuid) ==
					createdObjectUuids.end()) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "AddComponent";
				changeEvent.objectUuid = currentComponentPair.second.ownerUuid;
				changeEvent.componentUuid = currentComponentPair.first;
				changeEvent.property = "Component";
				changeEvent.newValue = currentComponentPair.second.serializedLine;
				changeEvents.push_back(std::move(changeEvent));
				continue;
			}

			if (previousComponentIterator != previousIndex.components.end() &&
				previousComponentIterator->second.serializedLine !=
					currentComponentPair.second.serializedLine) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "SetProperty";
				changeEvent.objectUuid = currentComponentPair.second.ownerUuid;
				changeEvent.componentUuid = currentComponentPair.first;
				changeEvent.property = "ComponentData";
				changeEvent.oldValue = previousComponentIterator->second.serializedLine;
				changeEvent.newValue = currentComponentPair.second.serializedLine;
				changeEvents.push_back(std::move(changeEvent));
			}
		}

		for (const auto& previousComponentPair : previousIndex.components) {
			if (currentIndex.components.find(previousComponentPair.first) == currentIndex.components.end() &&
				deletedObjectUuids.find(previousComponentPair.second.ownerUuid) ==
					deletedObjectUuids.end()) {
				EditorTeamChangeEvent changeEvent{};
				changeEvent.operation = "RemoveComponent";
				changeEvent.objectUuid = previousComponentPair.second.ownerUuid;
				changeEvent.componentUuid = previousComponentPair.first;
				changeEvent.property = "Component";
				changeEvent.oldValue = previousComponentPair.second.serializedLine;
				changeEvents.push_back(std::move(changeEvent));
			}
		}

		if (changeEvents.empty() && previousSnapshot != currentSnapshot) {
			EditorTeamChangeEvent changeEvent{};
			changeEvent.operation = "SetProperty";
			changeEvent.property = "SceneData";
			changeEvents.push_back(std::move(changeEvent));
		}

		return changeEvents;
	}

	std::string BuildPropertyKey(const EditorTeamChangeEvent& changeEvent) {
		const std::string& sceneIdentity = changeEvent.sceneUuid.empty()
			? changeEvent.scenePath
			: changeEvent.sceneUuid;
		return sceneIdentity + "|" +
			changeEvent.objectUuid + "|" +
			changeEvent.componentUuid + "|" +
			changeEvent.property;
	}

	std::string RemoveUtf8Bom(const std::string& text);
	std::string AddUtf8Bom(const std::string& text);

	struct LineEdit {
		std::size_t begin = 0U;
		std::size_t end = 0U;
		std::vector<std::string> replacement;
	};

	std::vector<std::string> SplitTextLines(const std::string& text) {
		std::istringstream input(RemoveUtf8Bom(text));
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(std::move(line));
		}
		return lines;
	}

	std::vector<LineEdit> BuildLineEdits(
		const std::vector<std::string>& base,
		const std::vector<std::string>& changed) {
		const std::size_t rows = base.size() + 1U;
		const std::size_t columns = changed.size() + 1U;
		if (rows * columns > 4'000'000U) return {{0U, base.size(), changed}};
		std::vector<std::uint32_t> lcs(rows * columns, 0U);
		auto cell = [&](std::size_t row, std::size_t column) -> std::uint32_t& { return lcs[row * columns + column]; };
		for (std::size_t row = base.size(); row-- > 0U;) {
			for (std::size_t column = changed.size(); column-- > 0U;) {
				cell(row, column) = base[row] == changed[column]
					? cell(row + 1U, column + 1U)
					: (std::max)(cell(row + 1U, column), cell(row, column + 1U));
			}
		}
		std::vector<LineEdit> edits;
		std::size_t row = 0U;
		std::size_t column = 0U;
		while (row < base.size() || column < changed.size()) {
			if (row < base.size() && column < changed.size() && base[row] == changed[column]) {
				++row; ++column; continue;
			}
			LineEdit edit{}; edit.begin = row;
			while (row < base.size() || column < changed.size()) {
				if (row < base.size() && column < changed.size() && base[row] == changed[column]) break;
				if (column < changed.size() && (row == base.size() || cell(row, column + 1U) >= cell(row + 1U, column))) {
					edit.replacement.push_back(changed[column++]);
				}
				else if (row < base.size()) ++row;
			}
			edit.end = row;
			edits.push_back(std::move(edit));
		}
		return edits;
	}

	bool LineEditsOverlap(const LineEdit& left, const LineEdit& right) {
		if (left.begin == left.end && right.begin == right.end) return left.begin == right.begin;
		if (left.begin == left.end) return left.begin >= right.begin && left.begin <= right.end;
		if (right.begin == right.end) return right.begin >= left.begin && right.begin <= left.end;
		return (std::max)(left.begin, right.begin) < (std::min)(left.end, right.end);
	}

	bool TryThreeWayMergeLines(
		const std::string& baseText,
		const std::string& localText,
		const std::string& serverText,
		std::string& mergedText) {
		const auto base = SplitTextLines(baseText);
		const auto localEdits = BuildLineEdits(base, SplitTextLines(localText));
		const auto serverEdits = BuildLineEdits(base, SplitTextLines(serverText));
		std::vector<LineEdit> edits = localEdits;
		for (const LineEdit& serverEdit : serverEdits) {
			bool duplicate = false;
			for (const LineEdit& localEdit : localEdits) {
				if (!LineEditsOverlap(localEdit, serverEdit)) continue;
				if (localEdit.begin == serverEdit.begin && localEdit.end == serverEdit.end &&
					localEdit.replacement == serverEdit.replacement) { duplicate = true; break; }
				return false;
			}
			if (!duplicate) edits.push_back(serverEdit);
		}
		std::sort(edits.begin(), edits.end(), [](const LineEdit& left, const LineEdit& right) {
			return left.begin != right.begin ? left.begin > right.begin : left.end > right.end;
		});
		auto mergedLines = base;
		for (const LineEdit& edit : edits) {
			mergedLines.erase(mergedLines.begin() + static_cast<std::ptrdiff_t>(edit.begin),
				mergedLines.begin() + static_cast<std::ptrdiff_t>(edit.end));
			mergedLines.insert(mergedLines.begin() + static_cast<std::ptrdiff_t>(edit.begin),
				edit.replacement.begin(), edit.replacement.end());
		}
		std::ostringstream output;
		for (std::size_t index = 0U; index < mergedLines.size(); ++index) {
			if (index != 0U) output << "\r\n";
			output << mergedLines[index];
		}
		mergedText = AddUtf8Bom(output.str());
		return true;
	}

	std::string SerializeLockMessage(
		const char* messageType,
		const std::string& objectUuid,
		const std::string& userId,
		const std::string& userName,
		EditorTeamLockMode mode = EditorTeamLockMode::Hard,
		const std::string& targetType = {},
		const std::string& targetId = {}) {
		return
			"{\"type\":\"" + std::string(messageType) +
			"\",\"objectUuid\":\"" + EscapeJsonText(objectUuid) +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"userName\":\"" + EscapeJsonText(userName) +
			"\",\"lockMode\":\"" + (mode == EditorTeamLockMode::Hard ? "Hard" : "Soft") +
			"\",\"targetType\":\"" + EscapeJsonText(targetType) +
			"\",\"targetId\":\"" + EscapeJsonText(targetId) + "\"}";
	}

	std::string SerializeCompatibilityMessage(const ProjectVersionSettings& projectSettings) {
		return "{\"type\":\"compatibility\",\"engineVersion\":\"" +
			EscapeJsonText(GetCG2EngineDisplayVersion()) + "\",\"projectFormat\":" +
			std::to_string(projectSettings.projectFormatVersion) + ",\"scriptApi\":" +
			std::to_string(GetCG2ScriptApiVersion()) + ",\"channel\":\"" +
			GetEngineUpdateChannelText(projectSettings.updateChannel) + "\"}";
	}

	std::string SerializeCompatibilityAcceptedMessage() {
		return "{\"type\":\"compatibilityAccepted\"}";
	}

	std::string SerializeCompatibilityRejectedMessage(const std::string& reason) {
		return "{\"type\":\"compatibilityRejected\",\"reason\":\"" + EscapeJsonText(reason) + "\"}";
	}

	// Handshake: Protocol / Engine / Project / ProjectId をまとめて送る。
	// 遠隔では異なるEngine Buildが繋がり得るため、壊れた同期を始める前にここで弾く。
	std::string SerializeHandshakeMessage(
		const ProjectVersionSettings& projectSettings,
		const std::string& projectId,
		const std::string& userId,
		const std::string& userName) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHandshake +
			"\",\"protocol\":" + std::to_string(CG2Collaboration::kCollaborationProtocolVersion) +
			",\"engineVersion\":\"" + EscapeJsonText(GetCG2EngineDisplayVersion()) +
			"\",\"projectFormat\":" + std::to_string(projectSettings.projectFormatVersion) +
			",\"scriptApi\":" + std::to_string(GetCG2ScriptApiVersion()) +
			",\"channel\":\"" + GetEngineUpdateChannelText(projectSettings.updateChannel) +
			"\",\"projectId\":\"" + EscapeJsonText(projectId) +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"userName\":\"" + EscapeJsonText(userName) + "\"}";
	}

	std::string SerializeHandshakeAcceptedMessage(std::uint64_t serverRevision) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHandshakeAccepted +
			"\",\"protocol\":" + std::to_string(CG2Collaboration::kCollaborationProtocolVersion) +
			",\"revision\":" + std::to_string(serverRevision) + "}";
	}

	std::string SerializeHandshakeRejectedMessage(const std::string& reason) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHandshakeRejected +
			"\",\"protocol\":" + std::to_string(CG2Collaboration::kCollaborationProtocolVersion) +
			",\"reason\":\"" + EscapeJsonText(reason) + "\"}";
	}

	std::string SerializeHistoryRequestMessage(
		const std::string& userId,
		std::uint64_t afterRevision) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHistoryRequest +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"afterRevision\":" + std::to_string(afterRevision) + "}";
	}

	std::string SerializeHeartbeatMessage(const std::string& userId, std::uint64_t sentUnixMilliseconds) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHeartbeat +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"sentAt\":" + std::to_string(sentUnixMilliseconds) + "}";
	}

	std::string SerializeHeartbeatAckMessage(const std::string& userId, std::uint64_t sentUnixMilliseconds) {
		return std::string("{\"type\":\"") + CG2Collaboration::MessageType::kHeartbeatAck +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"sentAt\":" + std::to_string(sentUnixMilliseconds) + "}";
	}

	std::string BuildComponentLockKey(
		const std::string& objectUuid,
		const std::string& componentUuid) {
		return componentUuid.empty()
			? objectUuid
			: objectUuid + "#component:" + componentUuid;
	}

	std::string SerializeSharedSceneFolderMessage(const std::string& sharedSceneFolder) {
		return
			"{\"type\":\"sharedSceneFolder\",\"path\":\"" +
			EscapeJsonText(sharedSceneFolder) + "\"}";
	}

	// snapshotData を除いた EditorTeamChangeEvent のメタデータと、
	// 転送本体（Base64化した snapshotData）の長さ・実バイト数・分割数を送る。
	// snapshotData自体はAssetの場合すでにBase64、Sceneの場合は生テキストなので、
	// ここで改めてBase64化してから分割することで、改行やクォートを含む生データでも
	// 1行1メッセージの通信フォーマットを壊さずに送れる。
	std::string SerializeAssetTransferBegin(
		const EditorTeamChangeEvent& changeEvent,
		const char* commitType,
		std::uint32_t chunkCount,
		std::uint64_t transportPayloadLength,
		std::uint64_t rawByteCount) {
		EditorTeamChangeEvent metadataEvent = changeEvent;
		metadataEvent.snapshotData.clear();
		std::string serialized = SerializeChangeEvent(metadataEvent, "assetBegin");
		serialized.pop_back();
		serialized += ",\"commitType\":\"" + EscapeJsonText(commitType) + "\"";
		serialized += ",\"payloadBytes\":" + std::to_string(transportPayloadLength);
		serialized += ",\"rawBytes\":" + std::to_string(rawByteCount);
		serialized += ",\"chunkCount\":" + std::to_string(chunkCount);
		serialized += "}";
		return serialized;
	}

	// dataはBase64文字のみなのでJSONエスケープ不要。EscapeJsonTextを通さない方が
	// 大きなAssetでも余計なコピー・走査を増やさない。
	std::string SerializeAssetTransferChunk(
		const std::string& transferId,
		std::uint32_t chunkIndex,
		const std::string& chunkTransportData) {
		std::string serialized;
		serialized.reserve(chunkTransportData.size() + 96u);
		serialized += "{\"type\":\"assetChunk\",\"transferId\":\"";
		serialized += EscapeJsonText(transferId);
		serialized += "\",\"chunkIndex\":";
		serialized += std::to_string(chunkIndex);
		serialized += ",\"data\":\"";
		serialized += chunkTransportData;
		serialized += "\"}";
		return serialized;
	}

	std::string SerializeAssetTransferEnd(const std::string& transferId) {
		return "{\"type\":\"assetEnd\",\"transferId\":\"" + EscapeJsonText(transferId) + "\"}";
	}

	// 参加直後のCatch-up用。本体はまだ送らず、Hashだけ提示して相手に持っているか確認させる。
	// 相手が同じHashを持っていれば何も送らずに済み、72MBのFBXのような大きなAssetを
	// 変更していないのに再送する事故を防げる。
	std::string SerializeAssetOfferMessage(
		const std::string& scenePath,
		const std::string& assetHash,
		std::uint64_t fileSize) {
		return
			"{\"type\":\"assetOffer\",\"scenePath\":\"" + EscapeJsonText(scenePath) +
			"\",\"assetHash\":\"" + EscapeJsonText(assetHash) +
			"\",\"fileSize\":" + std::to_string(fileSize) + "}";
	}

	std::string SerializeAssetRequestMessage(
		const std::string& scenePath,
		const std::string& userId) {
		return
			"{\"type\":\"assetRequest\",\"scenePath\":\"" +
			EscapeJsonText(scenePath) + "\",\"userId\":\"" +
			EscapeJsonText(userId) + "\"}";
	}

	bool ReadBinaryTextFile(const std::filesystem::path& filePath, std::string& text) {
		std::ifstream file(filePath, std::ios::binary);

		if (!file.is_open()) {
			return false;
		}

		text.assign(
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
		return file.good() || file.eof();
	}

	bool WriteUtf8BomTextFile(const std::filesystem::path& filePath, const std::string& text) {
		const std::filesystem::path parentPath = filePath.parent_path();

		if (!parentPath.empty()) {
			std::error_code directoryError;
			std::filesystem::create_directories(parentPath, directoryError);
		}

		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

		if (!file.is_open()) {
			return false;
		}

		constexpr unsigned char utf8Bom[] = {0xEFu, 0xBBu, 0xBFu};
		file.write(
			reinterpret_cast<const char*>(utf8Bom),
			static_cast<std::streamsize>(sizeof(utf8Bom)));
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		return file.good();
	}

	std::string RemoveUtf8Bom(const std::string& text) {
		if (text.size() >= 3u &&
			static_cast<unsigned char>(text[0]) == 0xEFu &&
			static_cast<unsigned char>(text[1]) == 0xBBu &&
			static_cast<unsigned char>(text[2]) == 0xBFu) {
			return text.substr(3u);
		}

		return text;
	}

	std::string AddUtf8Bom(const std::string& text) {
		constexpr char utf8Bom[] = {
			static_cast<char>(0xEFu),
			static_cast<char>(0xBBu),
			static_cast<char>(0xBFu),
		};
		return std::string(utf8Bom, sizeof(utf8Bom)) + RemoveUtf8Bom(text);
	}

	bool SendSocketText(
		SOCKET socketHandle,
		const std::string& message,
		std::atomic<std::uint64_t>* sentBytes = nullptr,
		std::atomic<std::uint64_t>* totalBytes = nullptr) {
		const std::string framedMessage = message + "\n";
		std::size_t sentByteCount = 0u;

		if (sentBytes != nullptr) {
			sentBytes->store(0u, std::memory_order_release);
		}

		if (totalBytes != nullptr) {
			totalBytes->store(
				static_cast<std::uint64_t>(framedMessage.size()),
				std::memory_order_release);
		}

		while (sentByteCount < framedMessage.size()) {
			const int remainingByteCount = static_cast<int>((std::min)(
				framedMessage.size() - sentByteCount,
				static_cast<std::size_t>(INT_MAX)));
			const int result = send(
				socketHandle,
				framedMessage.data() + sentByteCount,
				remainingByteCount,
				0);

			if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
				fd_set writableSockets;
				FD_ZERO(&writableSockets);
				FD_SET(socketHandle, &writableSockets);
				timeval waitTime{};
				waitTime.tv_sec = 1;

				if (select(0, nullptr, &writableSockets, nullptr, &waitTime) > 0) {
					continue;
				}

				return false;
			}

			if (result <= 0) {
				return false;
			}

			sentByteCount += static_cast<std::size_t>(result);

			if (sentBytes != nullptr) {
				sentBytes->store(
					static_cast<std::uint64_t>(sentByteCount),
					std::memory_order_release);
			}
		}

		return true;
	}

	const char* GetStatusText(EditorTeamConnectionStatus status) {
		switch (status) {
		case EditorTeamConnectionStatus::Connecting: return "接続中";
		case EditorTeamConnectionStatus::Online: return "オンライン";
		case EditorTeamConnectionStatus::Synchronizing: return "同期中";
		case EditorTeamConnectionStatus::Conflict: return "競合あり";
		case EditorTeamConnectionStatus::Incompatible: return "非互換";
		case EditorTeamConnectionStatus::Disconnected: return "切断";
		case EditorTeamConnectionStatus::Reconnecting: return "再接続中";
		default: return "オフライン";
		}
	}
}

namespace {
	// 送信Queueの各要素。チャンク転送の一部であれば、そのチャンクが表す実バイト数を
	// ネットワーク送信スレッド側で outgoingTransferSentBytes へ積み上げて進捗を出す。
	struct OutgoingWireMessage {
		std::string text;
		bool isTransferChunk = false;
		std::uint64_t transferRawByteShare = 0u;
	};
}

struct EditorTeamCollaborationManager::NetworkState {
	// 通信経路の実体。LAN / Tailscale の違いはこの中だけに閉じる。
	// 将来WebSocket等へ差し替える場合も、ここへ別実装を入れるだけで済む。
	std::unique_ptr<CG2Collaboration::ICollaborationTransport> transport;
	std::mutex queueMutex;
	std::mutex errorMutex;
	std::mutex transferLabelMutex;
	std::vector<std::string> incomingMessages;
	std::string errorMessage;
	std::string outgoingTransferLabel;
	std::atomic<EditorTeamConnectionStatus> status{EditorTeamConnectionStatus::Offline};
	std::atomic_bool stopsRequested{false};
	std::atomic_bool isServer{false};
	std::atomic_int memberCount{0};
	std::atomic<std::uint64_t> transferSentBytes{0u};
	std::atomic<std::uint64_t> transferTotalBytes{0u};
	std::atomic<std::uint64_t> outgoingTransferSentBytes{0u};
	std::atomic<std::uint64_t> outgoingTransferTotalBytes{0u};
};

EditorTeamCollaborationManager::EditorTeamCollaborationManager()
	: networkState_(std::make_unique<NetworkState>()) {
}

EditorTeamCollaborationManager::~EditorTeamCollaborationManager() {
	Finalize();
}

void EditorTeamCollaborationManager::Initialize(
	EditorScene* editorScene,
	std::vector<std::string>* consoleMessages) {
	if (isInitialized_) {
		return;
	}

	editorScene_ = editorScene;
	consoleMessages_ = consoleMessages;
	LoadSettings();
	LoadChangeLog();
	MemberRecord& localMember = memberRecords_[userId_];
	localMember.userName = userNameBuffer_.data();
	localMember.color = BuildUserColor(userId_);
	localMember.isOnline = true;
	isInitialized_ = editorScene_ != nullptr;
	g_activeTeamCollaborationManager = this;

	if (editorScene_ != nullptr) {
		editorScene_->EnsurePersistentUuids();
		CaptureSceneChanges(false);
		ScanAssetChanges(true);
	}

	if (startsServerAutomatically_ && isHost_) {
		StartServer();
	}
	else if (!isHost_ && autoConnect_) {
		ConnectToHost();
	}
}

void EditorTeamCollaborationManager::Finalize() {
	if (!isInitialized_ && networkState_->transport == nullptr) {
		return;
	}

	StopNetworkThread();
	SaveSettings();

	if (g_activeTeamCollaborationManager == this) {
		g_activeTeamCollaborationManager = nullptr;
	}

	isInitialized_ = false;
}

void EditorTeamCollaborationManager::Update(float deltaTime, bool isPlaying) {
	if (!isInitialized_) {
		return;
	}

	const bool hasStoppedPlaying = isPlaying_ && !isPlaying;
	isPlaying_ = isPlaying;
	// 履歴へ残さないPingは作成者だけが期限切れを削除同期する。
	// 全Clientが同時にDeleteを送ると同じPingへ不要な競合が発生するため、所有者に限定する。
	const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();
	std::vector<TeamItem> expiredPings;
	for (const auto& itemPair : teamItems_) {
		const TeamItem& item = itemPair.second;
		if (item.kind == "Ping" && !item.keepsPingHistory &&
			item.creatorUserId == userId_ && item.expiresAtUnixMilliseconds != 0u &&
			item.expiresAtUnixMilliseconds <= now) {
			expiredPings.push_back(item);
		}
	}
	for (const TeamItem& expiredPing : expiredPings) {
		QueueTeamItemDelete(expiredPing);
	}
	// RuntimeManagerがPlay開始前のSceneを復元した後に、Play中に他ユーザーが行った編集を重ねる。
	// 先に適用してしまうとRuntimeManagerの復元で消え、削除差分として再送されてしまう。
	if (hasStoppedPlaying) {
		ApplyDeferredPlayModeChanges();
	}
	activityIdleSeconds_ += (std::max)(deltaTime, 0.0f);
	if (activityIdleSeconds_ >= kActivityTimeoutSeconds) {
		activityAction_ = "閲覧中";
	}

	// Transportの状態(接続 / 再接続 / エラー)を先に取り込んでから中身を処理する。
	SynchronizeTransportStatus();
	ProcessIncomingMessages();
	UpdateHeartbeat(deltaTime);
	const int32_t memberCount = networkState_->memberCount.load(std::memory_order_acquire);
	const bool socketOnline = networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online;
	if (!socketOnline && networkState_->status.load(std::memory_order_acquire) !=
		EditorTeamConnectionStatus::Synchronizing) {
		compatibilityHelloSent_ = false;
		// 新しいClientが来た。相手のHandshakeを受けるまでPayloadは適用しない。
		compatibilityAccepted_ = false;
		handshakeAccepted_ = false;
	}
	const bool transportLinkUp = networkState_->transport != nullptr &&
		(networkState_->transport->GetState() == CG2Collaboration::TransportState::Connected ||
		 networkState_->transport->GetState() == CG2Collaboration::TransportState::Listening);

	// 通信状態とProject保存を分離する。Hostが停止中でもScene/Asset/Script差分を
	// Offline ChangeLogへ積み、再接続時の3-way比較に使えるようにする。
	if (!isPlaying && !isApplyingRemoteChange_) {
		snapshotElapsedSeconds_ += (std::max)(deltaTime, 0.0f);
		if (snapshotElapsedSeconds_ >= kSnapshotIntervalSeconds) {
			snapshotElapsedSeconds_ = 0.0f;
			CaptureSceneChanges(false);
		}
		assetScanElapsedSeconds_ += (std::max)(deltaTime, 0.0f);
		if (assetScanElapsedSeconds_ >= kAssetScanIntervalSeconds) {
			assetScanElapsedSeconds_ = 0.0f;
			ScanAssetChanges(false);
		}
	}

	if (!isHost_ && transportLinkUp && !compatibilityHelloSent_) {
		SendHandshake();
	}
	// Socket数の減少だけはTransportから分かる。参加時の認証とCatch-upは人数差分ではなく、
	// Handshakeに含まれるUser ID単位で処理する。人数だけを見て共通認証状態を戻すと、
	// 3台目の参加時に接続済みClientまで未認証扱いになるためである。
	const bool hasLostMember = isHost_ && memberCount < previousMemberCount_;
	previousMemberCount_ = memberCount;

	if (!isHost_ && !compatibilityAccepted_) {
		return;
	}
	UpdateMemberPresence(deltaTime);
	if (!followUserId_.empty()) {
		const auto followedMember = memberRecords_.find(followUserId_);
		if (followedMember != memberRecords_.end() && followedMember->second.isOnline) {
			EditorSharedState::g_cameraTransform.translate = {
				followedMember->second.cameraPosition[0],
				followedMember->second.cameraPosition[1],
				followedMember->second.cameraPosition[2]};
			EditorSharedState::g_cameraTransform.rotate = {
				followedMember->second.cameraRotation[0],
				followedMember->second.cameraRotation[1],
				followedMember->second.cameraRotation[2]};
		}
	}
	UpdateSelectionLock(deltaTime);
	UpdateLockTimeouts(deltaTime);
	const bool isConnected = handshakeAccepted_ && transportLinkUp;

	if (isConnected && !wasConnected_) {
		networkState_->status.store(EditorTeamConnectionStatus::Synchronizing, std::memory_order_release);
		lockHeartbeatElapsedSeconds_ = kLockHeartbeatIntervalSeconds;

		if (isHost_) {
			for (EditorTeamChangeEvent& changeEvent : unsyncedChanges_) {
				currentRevision_++;
				changeEvent.revision = currentRevision_;
				const auto recent = std::find_if(recentChanges_.begin(), recentChanges_.end(),
					[&changeEvent](const EditorTeamChangeEvent& value) {
						return value.changeId == changeEvent.changeId;
					});
				if (recent != recentChanges_.end()) *recent = changeEvent;
				if (IsTeamItemChange(changeEvent)) ApplyTeamItemChange(changeEvent);
				lastPropertyRevision_[BuildPropertyKey(changeEvent)] = currentRevision_;
				AppendChangeLog(changeEvent);
				QueueChangeEventMessage(changeEvent, "commit");
			}

			unsyncedChanges_.clear();
			SaveSettings();
		}
		else if (!historySyncInProgress_) {
			FlushUnsyncedChanges();
		}

		if (!historySyncInProgress_) {
			networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
			AddConsoleMessage("Team: 接続しました。未同期変更を送信します");
		}
	}

	wasConnected_ = isConnected;
	if (isHost_ && !pendingJoinCatchUpUserIds_.empty()) {
		const std::vector<std::string> catchUpUserIds = std::move(pendingJoinCatchUpUserIds_);
		pendingJoinCatchUpUserIds_.clear();
		for (const std::string& catchUpUserId : catchUpUserIds) {
			QueueOutgoingMessage(SetTargetUserId(
				SerializeSharedSceneFolderMessage(sharedSceneFolderBuffer_.data()), catchUpUserId));
			QueueCurrentSceneSnapshotForUser(catchUpUserId);

			// 途中参加者だけへAsset Hashを提示する。既存参加者へ再送して編集中Assetを
			// 上書きすることはない。
			for (const auto& assetPair : assetRecords_) {
				QueueOutgoingMessage(SetTargetUserId(SerializeAssetOfferMessage(
					assetPair.first,
					assetPair.second.hash,
					assetPair.second.fileSize), catchUpUserId));
			}

			for (const auto& lockPair : lockedByUserId_) {
				const std::string& objectUuid = lockPair.first;
				const std::string lockUserName = lockedByUserName_.contains(objectUuid)
					? lockedByUserName_.at(objectUuid) : std::string{};
				const EditorTeamLockMode lockMode = lockModes_.contains(objectUuid)
					? lockModes_.at(objectUuid) : EditorTeamLockMode::Hard;
				const std::string targetType = lockTargetTypes_.contains(objectUuid)
					? lockTargetTypes_.at(objectUuid) : std::string{};
				QueueOutgoingMessage(SetTargetUserId(SerializeLockMessage(
					"lockState", objectUuid, lockPair.second, lockUserName,
					lockMode, targetType, {}), catchUpUserId));
			}
		}
	}
	else if (hasLostMember) {
		for (auto lockIterator = lockedByUserId_.begin(); lockIterator != lockedByUserId_.end();) {
			if (lockIterator->second == userId_) {
				++lockIterator;
				continue;
			}

			const std::string objectUuid = lockIterator->first;
			lockedByUserName_.erase(objectUuid);
			remoteLockIdleSeconds_.erase(objectUuid);
			lockModes_.erase(objectUuid);
			lockTargetTypes_.erase(objectUuid);
			lockIterator = lockedByUserId_.erase(lockIterator);
			BroadcastLockState(objectUuid);
		}
	}
}

bool EditorTeamCollaborationManager::StartServer() {
	if (!isInitialized_) {
		return false;
	}

	isHost_ = true;
	lastError_.clear();
	SaveSettings();
	StartNetworkThread(true);
	return true;
}

void EditorTeamCollaborationManager::StopServer() {
	StopNetworkThread();
}

bool EditorTeamCollaborationManager::ConnectToHost() {
	if (!isInitialized_) {
		return false;
	}

	isHost_ = false;
	SaveSettings();
	StartNetworkThread(false);
	return true;
}

void EditorTeamCollaborationManager::Disconnect() {
	StopNetworkThread();
}

bool EditorTeamCollaborationManager::TestServerConnection() {
	std::string error;
	// Host自身の確認では、参加者向けに保存されたHost名ではなく、このPCで待ち受けている
	// Serverへ直接接続する。旧InviteのHost値が残っていても誤った失敗表示を出さない。
	const std::string testHost = isHost_ ? "127.0.0.1" : std::string(hostAddressBuffer_.data());
	const bool connected = CanConnectToServer(testHost, port_, error);
	lastError_ = connected ? std::string{} : error;
	AddConsoleMessage(connected
		? "Team: 接続テスト成功 " + testHost + ":" + std::to_string(port_)
		: "Team: " + error);
	return connected;
}

bool EditorTeamCollaborationManager::IsDedicatedServerRunning() const {
	if (IsDedicatedServerProcess(dedicatedServerProcessId_)) return true;
	if (IsDedicatedServerProcess(ReadDedicatedServerProcessId())) return true;
	return IsDedicatedServerProcess(ReadLegacyDedicatedServerProcessId());
}

bool EditorTeamCollaborationManager::StartDedicatedServer() {
	if (!CG2Collaboration::IsValidProjectId(projectIdBuffer_.data())) {
		lastError_ = "専用Serverを開始するには有効なProject IDが必要です";
		return false;
	}
	if (IsDedicatedServerRunning()) {
		const DedicatedServerStatusIdentity runningServer = ReadDedicatedServerStatusIdentity();
		if (runningServer.hasIdentity && runningServer.projectId == projectIdBuffer_.data() &&
			runningServer.port == port_) {
			lastError_.clear();
			return true;
		}

		// PIDだけ一致していても、旧Project用または旧形式のServerなら再利用しない。
		// 違うProjectのHandshakeを受けると参加者側にはProject ID不一致としか見えないため、
		// 現在のProject設定で起動し直す。
		const std::string oldProjectId = runningServer.hasIdentity
			? runningServer.projectId : std::string("不明(旧形式)");
		if (!StopDedicatedServer()) return false;
		AddConsoleMessage("Team: 旧Project用の専用Serverを停止しました (Project " + oldProjectId + ")");
	}
	const std::filesystem::path executable = FindDedicatedServerExecutable();
	if (!std::filesystem::is_regular_file(executable)) {
		lastError_ = "CG2TeamServer.exeがありません: " + executable.generic_string();
		return false;
	}
	std::error_code directoryError;
	std::filesystem::create_directories(".team/server", directoryError);
	if (directoryError) {
		lastError_ = "専用Serverの保存フォルダを作成できません: " + directoryError.message();
		return false;
	}
	std::wstring commandLine = L"\"" + executable.wstring() + L"\" --project-id \"" +
		ToWideText(projectIdBuffer_.data()) + L"\" --port " + std::to_wstring(port_) +
		L" --max-clients " + std::to_wstring(maximumClientCount_) +
		L" --data \"" + std::filesystem::absolute(".team/server").wstring() +
		L"\" --status-file \"" + std::filesystem::absolute(kDedicatedServerStatusPath).wstring() +
		L"\" --pid-file \"" + std::filesystem::absolute(kDedicatedServerPidPath).wstring() + L"\"";
	STARTUPINFOW startupInfo{}; startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};
	if (CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, executable.parent_path().c_str(),
			&startupInfo, &processInfo) == FALSE) {
		lastError_ = "CG2TeamServerを開始できません (Windows Error " + std::to_string(GetLastError()) + ")";
		return false;
	}
	// Listen失敗（特に古いServerが同じPortを占有）を開始成功として扱わない。
	// ServerはBindに失敗すると直ちに終了するため、短時間だけ終了を確認する。
	const DWORD earlyExit = WaitForSingleObject(processInfo.hProcess, 500u);
	if (earlyExit == WAIT_OBJECT_0) {
		DWORD exitCode = 0u;
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		lastError_ = "専用CG2TeamServerを開始できません。Port " + std::to_string(port_) +
			" を旧Serverが使用していないか確認してください (終了コード " +
			std::to_string(exitCode) + ")";
		return false;
	}
	dedicatedServerProcessId_ = processInfo.dwProcessId;
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	WriteUtf8BomTextFile(kDedicatedServerPidPath, std::to_string(dedicatedServerProcessId_) + "\r\n");
	lastError_.clear();
	AddConsoleMessage("Team: 専用CG2TeamServerを開始しました (Port " + std::to_string(port_) + ")");
	return true;
}

bool EditorTeamCollaborationManager::StopDedicatedServer() {
	const std::uint32_t processIds[] = {
		dedicatedServerProcessId_,
		ReadDedicatedServerProcessId(),
		ReadLegacyDedicatedServerProcessId(),
	};
	std::unordered_set<std::uint32_t> stoppedProcessIds;
	for (const std::uint32_t processId : processIds) {
		if (!IsDedicatedServerProcess(processId) || !stoppedProcessIds.insert(processId).second) continue;
		HANDLE process = OpenProcess(
			PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (process == nullptr || TerminateProcess(process, 0u) == FALSE) {
			if (process != nullptr) CloseHandle(process);
			lastError_ = "専用CG2TeamServerを停止できません (PID " + std::to_string(processId) + ")";
			return false;
		}
		WaitForSingleObject(process, 3000u);
		CloseHandle(process);
	}
	dedicatedServerProcessId_ = 0u;
	std::error_code removeError;
	std::filesystem::remove(kDedicatedServerPidPath, removeError);
	removeError.clear();
	const std::filesystem::path legacyPidPath = LegacyDedicatedServerPidPath();
	if (!legacyPidPath.empty()) std::filesystem::remove(legacyPidPath, removeError);
	lastError_.clear();
	AddConsoleMessage("Team: 専用CG2TeamServerを停止しました");
	return true;
}

bool EditorTeamCollaborationManager::RestartDedicatedServer() {
	return StopDedicatedServer() && StartDedicatedServer();
}

bool EditorTeamCollaborationManager::IsDedicatedServerAutoStartAtLogonEnabled() const {
	if (!CG2Collaboration::IsValidProjectId(projectIdBuffer_.data())) return false;
	DWORD exitCode = 1u;
	return RunScheduledTaskCommand(
		L"/Query /TN \"" + DedicatedServerTaskName(projectIdBuffer_.data()) + L"\"",
		exitCode) && exitCode == 0u;
}

bool EditorTeamCollaborationManager::SetDedicatedServerAutoStartAtLogon(bool enabled) {
	if (!CG2Collaboration::IsValidProjectId(projectIdBuffer_.data())) {
		lastError_ = "ログオン時自動起動には有効なProject IDが必要です";
		return false;
	}
	DWORD exitCode = 1u;
	const std::wstring taskName = DedicatedServerTaskName(projectIdBuffer_.data());
	if (!enabled) {
		RunScheduledTaskCommand(L"/Delete /TN \"" + taskName + L"\" /F", exitCode);
		dedicatedServerAutoStartAtLogon_ = false;
		lastError_.clear();
		return true;
	}
	const std::filesystem::path executable = FindDedicatedServerExecutable();
	if (!std::filesystem::is_regular_file(executable)) {
		lastError_ = "CG2TeamServer.exeがありません: " + executable.generic_string();
		return false;
	}
	const std::filesystem::path dataPath = std::filesystem::absolute(".team/server");
	const std::filesystem::path statusPath = std::filesystem::absolute(kDedicatedServerStatusPath);
	std::wstring serverCommand = L"\"" + executable.wstring() + L"\" --project-id \"" +
		ToWideText(projectIdBuffer_.data()) + L"\" --port " + std::to_wstring(port_) +
		L" --max-clients " + std::to_wstring(maximumClientCount_) +
		L" --data \"" + dataPath.wstring() + L"\" --status-file \"" + statusPath.wstring() + L"\"";
	serverCommand += L" --pid-file \"" + std::filesystem::absolute(kDedicatedServerPidPath).wstring() + L"\"";
	std::wstring escapedCommand;
	for (const wchar_t character : serverCommand) {
		if (character == L'"') escapedCommand += L"\\\"";
		else escapedCommand += character;
	}
	const std::wstring arguments = L"/Create /F /SC ONLOGON /TN \"" + taskName +
		L"\" /TR \"" + escapedCommand + L"\"";
	if (!RunScheduledTaskCommand(arguments, exitCode) || exitCode != 0u) {
		lastError_ = "専用Serverのログオン時自動起動を登録できません (終了コード " +
			std::to_string(exitCode) + ")";
		return false;
	}
	dedicatedServerAutoStartAtLogon_ = true;
	lastError_.clear();
	return true;
}

EditorTeamConnectionStatus EditorTeamCollaborationManager::GetStatus() const {
	return networkState_->status.load(std::memory_order_acquire);
}

bool EditorTeamCollaborationManager::IsGameObjectLockedByAnotherUser(
	int32_t gameObjectId) const {
	if (editorScene_ == nullptr || gameObjectId < 0) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return false;
	}
	if (IsTargetHardLockedByAnotherUser("Scene", EditorSharedState::g_currentScenePath)) return true;

	const auto lockIterator = lockedByUserId_.find(gameObject->uuid);
	const auto modeIterator = lockModes_.find(gameObject->uuid);
	return lockIterator != lockedByUserId_.end() && lockIterator->second != userId_ &&
		(modeIterator == lockModes_.end() || modeIterator->second == EditorTeamLockMode::Hard);
}

bool EditorTeamCollaborationManager::IsComponentLockedByAnotherUser(
	int32_t gameObjectId,
	const std::string& componentUuid) const {
	if (editorScene_ == nullptr || gameObjectId < 0 || componentUuid.empty()) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	// Hierarchy操作用のGameObjectロックは全Componentに優先する。
	const auto objectLockIterator = lockedByUserId_.find(gameObject->uuid);
	const auto objectModeIterator = lockModes_.find(gameObject->uuid);
	if (objectLockIterator != lockedByUserId_.end() && objectLockIterator->second != userId_ &&
		(objectModeIterator == lockModes_.end() || objectModeIterator->second == EditorTeamLockMode::Hard)) {
		return true;
	}

	const std::string componentLockKey = BuildComponentLockKey(gameObject->uuid, componentUuid);
	const auto componentLockIterator = lockedByUserId_.find(componentLockKey);
	const auto componentModeIterator = lockModes_.find(componentLockKey);
	if (componentLockIterator != lockedByUserId_.end() &&
		componentLockIterator->second != userId_ &&
		(componentModeIterator == lockModes_.end() || componentModeIterator->second == EditorTeamLockMode::Hard)) {
		return true;
	}
	const std::string propertyPrefix = "Property:" + componentLockKey + "#property:";
	for (const auto& lockPair : lockedByUserId_) {
		if (!lockPair.first.starts_with(propertyPrefix) || lockPair.second == userId_) continue;
		const auto mode = lockModes_.find(lockPair.first);
		if (mode == lockModes_.end() || mode->second == EditorTeamLockMode::Hard) return true;
	}
	return false;
}

std::string EditorTeamCollaborationManager::BuildTargetLockKey(
	const std::string& targetType,
	const std::string& targetId) const {
	if (targetType == "GameObject" || targetType == "Component") return targetId;
	return targetType + ":" + targetId;
}

bool EditorTeamCollaborationManager::IsTargetHardLockedByAnotherUser(
	const std::string& targetType,
	const std::string& targetId) const {
	const std::string lockKey = BuildTargetLockKey(targetType, targetId);
	const auto owner = lockedByUserId_.find(lockKey);
	if (owner == lockedByUserId_.end() || owner->second == userId_) return false;
	const auto mode = lockModes_.find(lockKey);
	return mode == lockModes_.end() || mode->second == EditorTeamLockMode::Hard;
}

void EditorTeamCollaborationManager::RequestEditingLock(
	int32_t gameObjectId,
	const std::string& componentUuid) {
	requestedEditingGameObjectId_ = gameObjectId;
	requestedEditingComponentUuid_ = componentUuid;
}

void EditorTeamCollaborationManager::RequestTargetLock(
	const std::string& targetType,
	const std::string& targetId,
	EditorTeamLockMode mode) {
	if (targetId.empty()) return;
	const std::string lockKey = BuildTargetLockKey(targetType, targetId);
	const bool isOnline = GetStatus() == EditorTeamConnectionStatus::Online ||
		GetStatus() == EditorTeamConnectionStatus::Synchronizing;
	if (!isOnline) return;
	if (isHost_) {
		const auto existing = lockedByUserId_.find(lockKey);
		if (existing == lockedByUserId_.end() || existing->second == userId_) {
			lockedByUserId_[lockKey] = userId_;
			lockedByUserName_[lockKey] = userNameBuffer_.data();
			lockModes_[lockKey] = mode;
			lockTargetTypes_[lockKey] = targetType;
		}
		BroadcastLockState(lockKey);
	}
	else {
		QueueOutgoingMessage(SerializeLockMessage(
			"lockRequest", lockKey, userId_, userNameBuffer_.data(), mode, targetType, targetId));
	}
}

void EditorTeamCollaborationManager::ReleaseTargetLock(
	const std::string& targetType,
	const std::string& targetId) {
	if (targetId.empty()) return;
	const std::string lockKey = BuildTargetLockKey(targetType, targetId);
	const auto owner = lockedByUserId_.find(lockKey);
	if (owner == lockedByUserId_.end() || owner->second != userId_) return;
	if (isHost_) {
		lockedByUserId_.erase(lockKey);
		lockedByUserName_.erase(lockKey);
		lockModes_.erase(lockKey);
		lockTargetTypes_.erase(lockKey);
		BroadcastLockState(lockKey);
	}
	else {
		QueueOutgoingMessage(SerializeLockMessage(
			"unlock", lockKey, userId_, userNameBuffer_.data(),
			EditorTeamLockMode::Hard, targetType, targetId));
	}
}

void EditorTeamCollaborationManager::ReportActivity(
	const std::string& panel,
	const std::string& action,
	const std::string& componentUuid,
	const std::string& propertyName) {
	activityPanel_ = panel;
	activityAction_ = action;
	if (!componentUuid.empty()) activityComponentUuid_ = componentUuid;
	if (!propertyName.empty()) activityPropertyName_ = propertyName;
	activityIdleSeconds_ = 0.0f;
}

void EditorTeamCollaborationManager::SetBuildActivity(bool isBuilding) {
	isBuilding_ = isBuilding;
	ReportActivity("Build", isBuilding ? "Build中" : "Build完了", {}, {});
	// 同期Build処理へ入る直前でも相手へ届くよう、次回送信を即時化する。
	presenceElapsedSeconds_ = kPresenceIntervalSeconds;
	UpdateMemberPresence(0.0f);
}

EditorTeamItemTargetSummary EditorTeamCollaborationManager::GetTargetItemSummary(
	const std::string& targetType,
	const std::string& targetId) const {
	EditorTeamItemTargetSummary summary{};
	const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();

	for (const auto& itemPair : teamItems_) {
		const TeamItem& item = itemPair.second;
		if (!item.parentId.empty()) {
			continue;
		}
		if (item.targetType != targetType || item.targetId != targetId) {
			continue;
		}
		if (item.kind == "Ping" && !item.keepsPingHistory &&
			item.expiresAtUnixMilliseconds != 0u && item.expiresAtUnixMilliseconds <= now) {
			continue;
		}

		if (item.kind == "Note") summary.noteCount++;
		else if (item.kind == "Ping") summary.pingCount++;
		else if (item.kind == "Chat") summary.chatCount++;
		else if (item.kind == "Review") summary.reviewCount++;

		if ((item.kind == "Note" && !item.isResolved) ||
			(item.kind == "Review" && item.reviewStatus != 2)) {
			summary.unresolvedCount++;
		}
	}

	return summary;
}

std::vector<EditorTeamSceneMarker> EditorTeamCollaborationManager::GetSceneMarkers() const {
	std::vector<EditorTeamSceneMarker> markers;
	if (editorScene_ == nullptr) {
		return markers;
	}

	const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();
	for (const auto& itemPair : teamItems_) {
		const TeamItem& item = itemPair.second;
		if (!item.parentId.empty()) {
			continue;
		}
		if (item.kind != "Note" && item.kind != "Ping") {
			continue;
		}
		if (item.kind == "Note" && item.isResolved) {
			continue;
		}
		if (item.kind == "Ping" && item.expiresAtUnixMilliseconds != 0u &&
			item.expiresAtUnixMilliseconds <= now) {
			continue;
		}

		EditorTeamSceneMarker marker{};
		marker.targetType = item.targetType;
		marker.targetId = item.targetId;
		marker.creatorName = item.creatorUserName;
		marker.color = BuildUserColor(item.creatorUserId);
		if (item.kind == "Ping") {
			marker.pingAnimationSeconds = static_cast<float>(
				now > item.timestampUnixMilliseconds
					? now - item.timestampUnixMilliseconds : 0u) / 1000.0f;
		}

		bool hasPosition = false;
		if (item.hasWorldPosition && item.targetType == "ScenePosition") {
			marker.worldPosition = item.worldPosition;
			hasPosition = true;
		}
		else if (item.targetType == "GameObject") {
			for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
				if (gameObject.uuid != item.targetId) {
					continue;
				}
				Vector3 worldScale{};
				Vector3 worldRotation{};
				Vector3 worldPosition{};
				if (editorScene_->GetWorldTransform(
						gameObject.id, worldScale, worldRotation, worldPosition)) {
					marker.worldPosition = {worldPosition.x, worldPosition.y, worldPosition.z};
					hasPosition = true;
				}
				break;
			}
		}

		if (!hasPosition) {
			continue;
		}

		auto existingMarker = std::find_if(
			markers.begin(), markers.end(),
			[&marker](const EditorTeamSceneMarker& value) {
				return value.targetType == marker.targetType && value.targetId == marker.targetId;
			});
		if (existingMarker == markers.end()) {
			marker.summary = GetTargetItemSummary(marker.targetType, marker.targetId);
			markers.push_back(std::move(marker));
		}
		else if (marker.pingAnimationSeconds >= 0.0f) {
			existingMarker->pingAnimationSeconds = marker.pingAnimationSeconds;
			existingMarker->creatorName = marker.creatorName;
			existingMarker->color = marker.color;
		}
	}

	return markers;
}

void EditorTeamCollaborationManager::OpenTargetItems(
	const std::string& targetType,
	const std::string& targetId,
	bool startsCreation,
	const std::string& initialKind) {
	focusedTeamItemTargetType_ = targetType;
	focusedTeamItemTargetId_ = targetId;
	hasPendingTeamItemWorldPosition_ = false;
	if (targetType == "Script" || targetType == "ScriptLine") {
		strncpy_s(
			scriptTargetPathBuffer_.data(),
			scriptTargetPathBuffer_.size(),
			targetId.c_str(),
			_TRUNCATE);
	}
	EditorSharedState::g_isTeamCollaborationWindowVisible = true;

	if (!startsCreation) {
		return;
	}

	constexpr const char* kindValues[] = {"Note", "Ping", "Chat", "Review"};
	for (int32_t kindIndex = 0; kindIndex < static_cast<int32_t>(std::size(kindValues)); ++kindIndex) {
		if (initialKind == kindValues[kindIndex]) {
			collaborationKindIndex_ = kindIndex;
			break;
		}
	}
}

void EditorTeamCollaborationManager::OpenScenePositionItems(
	const std::array<float, 3>& worldPosition,
	bool startsCreation,
	const std::string& initialKind) {
	pendingTeamItemWorldPosition_ = worldPosition;
	hasPendingTeamItemWorldPosition_ = true;
	focusedTeamItemTargetType_ = "ScenePosition";
	focusedTeamItemTargetId_ = EditorSharedState::g_currentScenePath + "#position:" + CreateEditorTeamUuid();
	EditorSharedState::g_isTeamCollaborationWindowVisible = true;
	if (startsCreation) {
		OpenTargetItems("ScenePosition", focusedTeamItemTargetId_, true, initialKind);
		hasPendingTeamItemWorldPosition_ = true;
		pendingTeamItemWorldPosition_ = worldPosition;
	}
}

std::string EditorTeamCollaborationManager::GetGameObjectEditorLabel(int32_t gameObjectId) const {
	if (editorScene_ == nullptr || gameObjectId < 0) {
		return {};
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return {};
	}

	const auto editingIterator = lockedByUserName_.find(gameObject->uuid);

	if (editingIterator != lockedByUserName_.end()) {
		return editingIterator->second + " 編集中";
	}

	const std::string componentPrefix = gameObject->uuid + "#component:";
	for (const auto& lockPair : lockedByUserName_) {
		if (lockPair.first.starts_with(componentPrefix)) {
			return lockPair.second + " Component編集中";
		}
	}

	const auto changedIterator = lastChangedByObjectUuid_.find(gameObject->uuid);
	return changedIterator != lastChangedByObjectUuid_.end()
		? changedIterator->second
		: std::string{};
}

void EditorTeamCollaborationManager::LoadSettings() {
	strncpy_s(userNameBuffer_.data(), userNameBuffer_.size(), "User", _TRUNCATE);
	strncpy_s(hostAddressBuffer_.data(), hostAddressBuffer_.size(), "100.64.0.1", _TRUNCATE);
	sharedSceneFolderBuffer_[0] = '\0';
	userId_ = CreateEditorTeamUuid();
	std::ifstream file(kTeamSettingsPath, std::ios::binary);
	std::string line;
	bool hasExplicitHostAddress = false;
	bool hasExplicitPort = false;
	bool hasExplicitAutoConnect = false;
	bool hasExplicitIsHost = false;
	bool hasExplicitRevision = false;
	bool hasExplicitLastSyncedRevision = false;

	while (std::getline(file, line)) {
		while (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.size() >= 3u &&
			static_cast<unsigned char>(line[0]) == 0xEFu &&
			static_cast<unsigned char>(line[1]) == 0xBBu &&
			static_cast<unsigned char>(line[2]) == 0xBFu) {
			line.erase(0u, 3u);
		}

		const std::size_t separatorPosition = line.find('|');

		if (separatorPosition == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0u, separatorPosition);
		const std::string value = line.substr(separatorPosition + 1u);

		if (key == "UserId" && IsEditorTeamUuidValid(value)) {
			userId_ = value;
		}
		else if (key == "UserName") {
			strncpy_s(userNameBuffer_.data(), userNameBuffer_.size(), value.c_str(), _TRUNCATE);
		}
		else if (key == "HostAddress") {
			hasExplicitHostAddress = !value.empty();
			strncpy_s(hostAddressBuffer_.data(), hostAddressBuffer_.size(), value.c_str(), _TRUNCATE);
		}
		else if (key == "SharedSceneFolder" && !value.empty()) {
			strncpy_s(
				sharedSceneFolderBuffer_.data(),
				sharedSceneFolderBuffer_.size(),
				value.c_str(),
				_TRUNCATE);
		}
		else if (key == "Port") {
			hasExplicitPort = true;
			try {
				port_ = static_cast<uint16_t>((std::clamp)(std::stoi(value), 1, 65535));
			}
			catch (const std::exception&) {
				port_ = 45678u;
			}
		}
		else if (key == "IsHost") {
			hasExplicitIsHost = true;
			isHost_ = value == "1";
		}
		else if (key == "AutoStartServer") {
			startsServerAutomatically_ = value == "1";
		}
		else if (key == "ProjectId") {
			// 前回の控え。InviteとProject Metadataが無いときだけ使う。
			strncpy_s(projectIdBuffer_.data(), projectIdBuffer_.size(), value.c_str(), _TRUNCATE);
		}
		else if (key == "AutoConnect") {
			hasExplicitAutoConnect = true;
			autoConnect_ = value == "1";
		}
		else if (key == "MaxClients") {
			try {
				maximumClientCount_ = (std::clamp)(std::stoi(value), 1, 32);
			}
			catch (const std::exception&) {
				maximumClientCount_ = 4;
			}
		}
		else if (key == "Revision") {
			hasExplicitRevision = true;
			try {
				currentRevision_ = static_cast<std::uint64_t>(std::stoull(value));
			}
			catch (const std::exception&) {
				currentRevision_ = 0u;
			}
		}
		else if (key == "LastSyncedRevision") {
			hasExplicitLastSyncedRevision = true;
			try { lastSyncedRevision_ = static_cast<std::uint64_t>(std::stoull(value)); }
			catch (const std::exception&) { lastSyncedRevision_ = 0u; }
		}
	}

	// Launcherが .cg2-invite から書き出したCollaboration設定を取り込む。
	// 利用者が画面で変更できる項目(接続先Host・ポート等)は、既にある設定を優先する。
	// Project IDは読み取り専用で直せないため、新しいInviteの値を必ず採用する。
	std::ifstream inviteFile("ProjectSettings/TeamCollaboration.invite", std::ios::binary);
	std::string inviteLine;

	while (std::getline(inviteFile, inviteLine)) {
		while (!inviteLine.empty() && inviteLine.back() == '') {
			inviteLine.pop_back();
		}

		if (inviteLine.size() >= 3u &&
			static_cast<unsigned char>(inviteLine[0]) == 0xEFu &&
			static_cast<unsigned char>(inviteLine[1]) == 0xBBu &&
			static_cast<unsigned char>(inviteLine[2]) == 0xBFu) {
			inviteLine.erase(0u, 3u);
		}

		const std::size_t inviteSeparator = inviteLine.find('|');

		if (inviteSeparator == std::string::npos) {
			continue;
		}

		const std::string inviteKey = inviteLine.substr(0u, inviteSeparator);
		const std::string inviteValue = inviteLine.substr(inviteSeparator + 1u);

		if (inviteKey == "ProjectId" && !inviteValue.empty()) {
			strncpy_s(projectIdBuffer_.data(), projectIdBuffer_.size(), inviteValue.c_str(), _TRUNCATE);
		}
		else if (inviteKey == "CollaborationHost" && !hasExplicitHostAddress) {
			strncpy_s(hostAddressBuffer_.data(), hostAddressBuffer_.size(), inviteValue.c_str(), _TRUNCATE);
		}
		else if (inviteKey == "CollaborationPort" && !hasExplicitPort) {
			try {
				port_ = static_cast<uint16_t>((std::clamp)(std::stoi(inviteValue), 1, 65535));
			}
			catch (const std::exception&) {
				// Inviteの値が壊れていてもTeam設定側の値を残す。
			}
		}
		else if (inviteKey == "AutoConnect" && !hasExplicitAutoConnect) {
			autoConnect_ = inviteValue == "1";
		}
		else if (inviteKey == "IsHost" && !hasExplicitIsHost) {
			isHost_ = inviteValue == "1";
		}
		else if (inviteKey == "Revision" && !hasExplicitRevision) {
			try { currentRevision_ = static_cast<std::uint64_t>(std::stoull(inviteValue)); }
			catch (const std::exception&) { currentRevision_ = 0u; }
		}
		else if (inviteKey == "LastSyncedRevision" && !hasExplicitLastSyncedRevision) {
			try { lastSyncedRevision_ = static_cast<std::uint64_t>(std::stoull(inviteValue)); }
			catch (const std::exception&) { lastSyncedRevision_ = 0u; }
		}
	}
	// Project IDの正はProjectフォルダーのMetadata。Launcherが参加時に書き、画面からは直せない。
	// TeamCollaboration.settings側の値は前回の控えでしかなく、これを優先すると古いIDが残り続け、
	// 新しい招待で入り直しても「Project IDが一致しません」から抜け出せなくなる。
	std::ifstream metadataFile("ProjectSettings/ProjectCollaboration.cg2", std::ios::binary);
	std::string metadataLine;

	while (std::getline(metadataFile, metadataLine)) {
		while (!metadataLine.empty() && metadataLine.back() == '\r') {
			metadataLine.pop_back();
		}

		if (metadataLine.size() >= 3u &&
			static_cast<unsigned char>(metadataLine[0]) == 0xEFu &&
			static_cast<unsigned char>(metadataLine[1]) == 0xBBu &&
			static_cast<unsigned char>(metadataLine[2]) == 0xBFu) {
			metadataLine.erase(0u, 3u);
		}

		const std::size_t metadataSeparator = metadataLine.find('|');

		if (metadataSeparator == std::string::npos ||
			metadataLine.substr(0u, metadataSeparator) != "ProjectId") {
			continue;
		}

		const std::string metadataProjectId = metadataLine.substr(metadataSeparator + 1u);

		if (!metadataProjectId.empty()) {
			strncpy_s(projectIdBuffer_.data(), projectIdBuffer_.size(), metadataProjectId.c_str(), _TRUNCATE);
		}
	}

	if (!hasExplicitLastSyncedRevision && lastSyncedRevision_ == 0u) lastSyncedRevision_ = currentRevision_;
	dedicatedServerAutoStartAtLogon_ = IsDedicatedServerAutoStartAtLogonEnabled();
}

void EditorTeamCollaborationManager::SaveSettings() const {
	std::ostringstream settingsText;
	settingsText
		<< "TeamCollaborationSettings|1\r\n"
		<< "UserId|" << userId_ << "\r\n"
		<< "UserName|" << userNameBuffer_.data() << "\r\n"
		<< "HostAddress|" << hostAddressBuffer_.data() << "\r\n"
		<< "SharedSceneFolder|" << sharedSceneFolderBuffer_.data() << "\r\n"
		<< "Port|" << port_ << "\r\n"
		<< "IsHost|" << (isHost_ ? 1 : 0) << "\r\n"
		<< "AutoStartServer|" << (startsServerAutomatically_ ? 1 : 0) << "\r\n"
		<< "ProjectId|" << projectIdBuffer_.data() << "\r\n"
		<< "AutoConnect|" << (autoConnect_ ? 1 : 0) << "\r\n"
		<< "MaxClients|" << maximumClientCount_ << "\r\n"
		<< "Revision|" << currentRevision_ << "\r\n"
		<< "LastSyncedRevision|" << lastSyncedRevision_ << "\r\n";
	WriteUtf8BomTextFile(kTeamSettingsPath, settingsText.str());

	// LauncherがProject一覧へ表示する正式Metadataも同じRevisionへ追従させる。
	// 不明なFieldは保持し、LastSyncedRevisionだけを書き換える。
	const std::filesystem::path metadataPath =
		"ProjectSettings/ProjectCollaboration.cg2";
	std::string metadataText;
	if (ReadBinaryTextFile(metadataPath, metadataText)) {
		std::istringstream input(RemoveUtf8Bom(metadataText));
		std::ostringstream output;
		std::string line;
		bool replaced = false;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.starts_with("LastSyncedRevision|")) {
				line = "LastSyncedRevision|" + std::to_string(lastSyncedRevision_);
				replaced = true;
			}
			output << line << "\r\n";
		}
		if (!replaced) output << "LastSyncedRevision|" << lastSyncedRevision_ << "\r\n";
		WriteUtf8BomTextFile(metadataPath, output.str());
	}
}

void EditorTeamCollaborationManager::LoadChangeLog() {
	std::ifstream file(kTeamChangeLogPath, std::ios::binary);
	std::string line;
	std::unordered_map<std::string, EditorTeamChangeEvent> pendingChanges;

	while (std::getline(file, line)) {
		if (line.empty()) {
			continue;
		}

		EditorTeamChangeEvent changeEvent = DeserializeChangeEvent(line);

		if (changeEvent.changeId.empty()) {
			continue;
		}

		if (!changeEvent.objectUuid.empty()) {
			lastChangedByObjectUuid_[changeEvent.objectUuid] = changeEvent.userName;
		}

		const auto recentIterator = std::find_if(
			recentChanges_.begin(), recentChanges_.end(),
			[&changeEvent](const EditorTeamChangeEvent& recent) {
				return recent.changeId == changeEvent.changeId;
			});
		if (recentIterator != recentChanges_.end()) *recentIterator = changeEvent;
		else recentChanges_.push_back(changeEvent);

		if (recentChanges_.size() > kMaximumRecentChangeCount) {
			recentChanges_.erase(recentChanges_.begin());
		}

		if (IsTeamItemChange(changeEvent)) {
			ApplyTeamItemChange(changeEvent);
		}

		if (changeEvent.revision == 0u) {
			pendingChanges[changeEvent.changeId] = std::move(changeEvent);
		}
		else {
			pendingChanges.erase(changeEvent.changeId);
			currentRevision_ = (std::max)(currentRevision_, changeEvent.revision);
			const std::string propertyKey = BuildPropertyKey(changeEvent);
			lastPropertyRevision_[propertyKey] = (std::max)(
				lastPropertyRevision_[propertyKey],
				changeEvent.revision);
		}
	}

	unsyncedChanges_.reserve(pendingChanges.size());

	for (auto& pendingChangePair : pendingChanges) {
		unsyncedChanges_.push_back(std::move(pendingChangePair.second));
	}
}

void EditorTeamCollaborationManager::AppendChangeLog(
	const EditorTeamChangeEvent& changeEvent) const {
	std::error_code directoryError;
	std::filesystem::create_directories(".team", directoryError);
	std::ofstream file(kTeamChangeLogPath, std::ios::binary | std::ios::app);

	if (!file.is_open()) {
		return;
	}

	file << SerializeChangeEvent(changeEvent, "log") << "\r\n";
}

void EditorTeamCollaborationManager::CaptureSceneChanges(bool forcesBroadcast) {
	if (editorScene_ == nullptr) {
		return;
	}

	const std::string sharedSceneFolder = sharedSceneFolderBuffer_.data();

	if (!IsPathInsideSharedSceneFolder(EditorSharedState::g_currentScenePath, sharedSceneFolder) ||
		EditorSharedState::g_currentScenePath.find("/_Archive/") != std::string::npos) {
		return;
	}

	editorScene_->EnsurePersistentUuids();
	const std::filesystem::path snapshotPath(kTeamLiveSnapshotPath);
	std::error_code directoryError;
	std::filesystem::create_directories(snapshotPath.parent_path(), directoryError);

	if (!editorScene_->SaveScene(snapshotPath.generic_string())) {
		lastError_ = "共同制作スナップショットを保存できません";
		return;
	}

	std::string snapshotText;

	if (!ReadBinaryTextFile(snapshotPath, snapshotText)) {
		lastError_ = "共同制作スナップショットを読み込めません";
		return;
	}

	const std::uint64_t snapshotHash = CalculateTextHash(snapshotText);

	if (!forcesBroadcast && lastSnapshotHash_ == 0u) {
		lastSnapshotHash_ = snapshotHash;
		lastSnapshotText_ = std::move(snapshotText);
		return;
	}

	if (!forcesBroadcast && snapshotHash == lastSnapshotHash_) {
		return;
	}

	std::vector<EditorTeamChangeEvent> changeEvents;

	if (forcesBroadcast) {
		EditorTeamChangeEvent fullSceneChange{};
		fullSceneChange.operation = "SetProperty";
		fullSceneChange.property = "SceneData";
		fullSceneChange.oldValue = std::to_string(lastSnapshotHash_);
		fullSceneChange.newValue = std::to_string(snapshotHash);
		changeEvents.push_back(std::move(fullSceneChange));
	}
	else {
		changeEvents = CollectSceneChanges(lastSnapshotText_, snapshotText);
	}

	lastSnapshotHash_ = snapshotHash;
	lastSnapshotText_ = snapshotText;

	for (EditorTeamChangeEvent& changeEvent : changeEvents) {
		changeEvent.changeId = CreateEditorTeamUuid();
		changeEvent.userId = userId_;
		changeEvent.userName = userNameBuffer_.data();
		changeEvent.scenePath = EditorSharedState::g_currentScenePath;
		changeEvent.sceneUuid = editorScene_->GetUuid();

		// Transform/Active/ComponentDataのような値だけの変更は、GameObject/Component
		// 構造そのものを変えないため、Scene全体ではなく対象GameObject 1つだけの断片で送る。
		// CreateObject/DeleteObject/SetParent/AddComponent/RemoveComponentのような構造操作は
		// 親子復元・ID対応にScene全体の情報が要るため、これまで通りSnapshot全体を使う。
		const bool isValueOnlyChange =
			changeEvent.operation == "SetProperty" &&
			(changeEvent.property == "Transform" ||
				changeEvent.property == "Active" ||
				changeEvent.property == "ComponentData") &&
			!changeEvent.objectUuid.empty();
		std::string fragmentText;

		if (isValueOnlyChange &&
			editorScene_->SaveGameObjectFragment(changeEvent.objectUuid, kTeamLiveFragmentSnapshotPath) &&
			ReadBinaryTextFile(kTeamLiveFragmentSnapshotPath, fragmentText)) {
			changeEvent.snapshotData = fragmentText;
		}
		else {
			changeEvent.snapshotData = snapshotText;
		}

		changeEvent.baseRevision = currentRevision_;
		QueueLocalChange(std::move(changeEvent));
	}
}

void EditorTeamCollaborationManager::ScanAssetChanges(bool recordsBaselineOnly) {
	std::unordered_map<std::string, std::string> registeredUuids;
	std::ifstream registryFile(kTeamAssetUuidRegistryPath, std::ios::binary);
	std::string registryLine;

	while (std::getline(registryFile, registryLine)) {
		while (!registryLine.empty() && registryLine.back() == '\r') {
			registryLine.pop_back();
		}

		if (registryLine.size() >= 3u &&
			static_cast<unsigned char>(registryLine[0]) == 0xEFu &&
			static_cast<unsigned char>(registryLine[1]) == 0xBBu &&
			static_cast<unsigned char>(registryLine[2]) == 0xBFu) {
			registryLine.erase(0u, 3u);
		}

		const std::size_t separatorPosition = registryLine.find('|');

		if (separatorPosition != std::string::npos) {
			registeredUuids[registryLine.substr(0u, separatorPosition)] =
				registryLine.substr(separatorPosition + 1u);
		}
	}

	std::unordered_set<std::string> referencedAssetPaths;
	const std::filesystem::path sharedSceneFolder(sharedSceneFolderBuffer_.data());
	std::error_code iteratorError;

	// 選択したゲームフォルダ内のSceneと、Scene本文に記録されたProject Asset参照だけを集める。
	// Assets/resources全体を走査しないことで、他ゲームや未使用素材、作業用Backupを送信しない。
	if (!sharedSceneFolder.empty() &&
		std::filesystem::exists(sharedSceneFolder, iteratorError) &&
		IsValidSharedGameSceneFolder(sharedSceneFolder.generic_string())) {
		for (const std::filesystem::directory_entry& entry :
			std::filesystem::recursive_directory_iterator(
				sharedSceneFolder,
				std::filesystem::directory_options::skip_permission_denied,
				iteratorError)) {
			if (iteratorError) {
				iteratorError.clear();
				continue;
			}

			const std::string scenePath = entry.path().lexically_normal().generic_string();

			if (entry.is_directory(iteratorError) &&
				scenePath.find("/_Archive") != std::string::npos) {
				continue;
			}

			if (!entry.is_regular_file(iteratorError) ||
				!IsCollaborationSceneAssetPath(scenePath) ||
				IsIgnoredCollaborationAssetPath(scenePath)) {
				continue;
			}

			referencedAssetPaths.insert(scenePath);
			std::string sceneText;

			if (ReadBinaryTextFile(entry.path(), sceneText)) {
				CollectProjectPathsFromText(sceneText, referencedAssetPaths);
			}
		}
	}

	// 保存前の編集中Sceneに追加された参照も、次のScene保存を待たず同期対象へ含める。
	CollectProjectPathsFromText(lastSnapshotText_, referencedAssetPaths);
	// 現在Sceneは差分Snapshot側で同期する。同じ内容をAssetとして二重送信しない。
	const std::string normalizedCurrentScenePath =
		std::filesystem::path(EditorSharedState::g_currentScenePath).lexically_normal().generic_string();
	referencedAssetPaths.erase(normalizedCurrentScenePath);
	std::unordered_set<std::string> scriptModuleFolders;

	for (const std::string& referencedPath : referencedAssetPaths) {
		const std::size_t buildDirectoryPosition = referencedPath.find("/x64/");

		if (referencedPath.starts_with("resources/scripts/") &&
			buildDirectoryPosition != std::string::npos) {
			scriptModuleFolders.insert(referencedPath.substr(0u, buildDirectoryPosition));
		}
	}

	// SceneのScript Componentは実行DLLを参照するため、対応Moduleの編集ソースだけを追加する。
	// x64配下のDLL/PDB/LIB等は各PCで生成する中間物なので送信しない。
	for (const std::string& moduleFolder : scriptModuleFolders) {
		for (const std::filesystem::directory_entry& entry :
			std::filesystem::directory_iterator(moduleFolder, iteratorError)) {
			if (iteratorError) {
				iteratorError.clear();
				continue;
			}

			const std::string sourcePath = entry.path().lexically_normal().generic_string();

			if (entry.is_regular_file(iteratorError) &&
				(IsScriptAssetPath(sourcePath) ||
				 entry.path().extension() == ".bat")) {
				referencedAssetPaths.insert(sourcePath);
			}
		}
	}

	std::unordered_map<std::string, AssetRecord> currentAssetRecords;
	missingReferencedAssetPaths_.clear();

	for (const std::string& assetPath : referencedAssetPaths) {
		const std::filesystem::path filePath(assetPath);

		if (IsIgnoredCollaborationAssetPath(assetPath)) {
			continue;
		}
		if (!std::filesystem::is_regular_file(filePath, iteratorError)) {
			missingReferencedAssetPaths_.push_back(assetPath);
			continue;
		}

			AssetRecord assetRecord{};
			assetRecord.fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(filePath, iteratorError));
			assetRecord.lastWriteTimestamp = static_cast<std::int64_t>(
				std::filesystem::last_write_time(filePath, iteratorError).time_since_epoch().count());
			const auto previousRecordIterator = assetRecords_.find(assetPath);

			if (previousRecordIterator != assetRecords_.end() &&
				previousRecordIterator->second.fileSize == assetRecord.fileSize &&
				previousRecordIterator->second.lastWriteTimestamp == assetRecord.lastWriteTimestamp) {
				assetRecord = previousRecordIterator->second;
			}
			else {
				const auto registeredUuidIterator = registeredUuids.find(assetPath);
				assetRecord.uuid = previousRecordIterator != assetRecords_.end()
					? previousRecordIterator->second.uuid
					: registeredUuidIterator != registeredUuids.end()
						? registeredUuidIterator->second
						: CreateEditorTeamUuid();
				if (recordsBaselineOnly) {
					assetRecord.hash = "BASE:" + std::to_string(assetRecord.fileSize) + ":" +
						std::to_string(assetRecord.lastWriteTimestamp);
				}
				else {
					assetRecord.hash = AssetManager::Get().GetHash(assetPath);
					// 共同制作が検知した実変更をAsset Registryへも反映しておく。
					// Registry側から見れば、共同制作の変更検知がRegistryを最新に保つ入力元の1つになる。
					AssetRegistry::Get().NotifyAssetChanged(assetPath);
				}

				if (IsScriptAssetPath(assetPath) &&
					assetRecord.fileSize <= kMaximumSynchronizedAssetBytes) {
					ReadBinaryTextFile(filePath, assetRecord.textContent);
				}
			}

			currentAssetRecords[assetPath] = std::move(assetRecord);
	}

	if (!recordsBaselineOnly) {
		for (const auto& currentAssetPair : currentAssetRecords) {
			const auto previousRecordIterator = assetRecords_.find(currentAssetPair.first);

			if (previousRecordIterator == assetRecords_.end()) {
				QueueAssetChange(currentAssetPair.first, nullptr, &currentAssetPair.second);
			}
			else if (previousRecordIterator->second.hash != currentAssetPair.second.hash) {
				QueueAssetChange(
					currentAssetPair.first,
					&previousRecordIterator->second,
					&currentAssetPair.second);
			}
		}

		for (const auto& previousAssetPair : assetRecords_) {
			// Sceneから参照を外しただけでは相手のAssetを削除しない。
			// 参照が残っているのに実ファイルが消えた場合だけ明示的な削除として送る。
			if (referencedAssetPaths.contains(previousAssetPair.first) &&
				currentAssetRecords.find(previousAssetPair.first) == currentAssetRecords.end()) {
				QueueAssetChange(previousAssetPair.first, &previousAssetPair.second, nullptr);
			}
		}
	}

	assetRecords_ = std::move(currentAssetRecords);
	std::ostringstream registryText;

	for (const auto& assetPair : assetRecords_) {
		registryText << assetPair.first << "|" << assetPair.second.uuid << "\r\n";
	}

	WriteUtf8BomTextFile(kTeamAssetUuidRegistryPath, registryText.str());
}

void EditorTeamCollaborationManager::QueueAssetChange(
	const std::string& assetPath,
	const AssetRecord* previousRecord,
	const AssetRecord* currentRecord) {
	EditorTeamChangeEvent changeEvent{};
	changeEvent.changeId = CreateEditorTeamUuid();
	changeEvent.userId = userId_;
	changeEvent.userName = userNameBuffer_.data();
	changeEvent.scenePath = assetPath;
	changeEvent.objectUuid = currentRecord != nullptr
		? currentRecord->uuid
		: previousRecord != nullptr
			? previousRecord->uuid
			: std::string{};
	changeEvent.operation = previousRecord == nullptr
		? "CreateAsset"
		: currentRecord == nullptr
			? "DeleteAsset"
			: "UpdateAsset";
	changeEvent.property = "Asset";
	changeEvent.oldValue = previousRecord != nullptr ? previousRecord->hash : std::string{};
	changeEvent.newValue = currentRecord != nullptr ? currentRecord->hash : std::string{};
	changeEvent.assetHash = changeEvent.newValue;
	changeEvent.fileSize = currentRecord != nullptr ? currentRecord->fileSize : 0u;
	changeEvent.baseRevision = currentRevision_;

	if (IsScriptAssetPath(assetPath)) {
		changeEvent.oldValue = previousRecord != nullptr ? previousRecord->textContent : std::string{};
		changeEvent.newValue = currentRecord != nullptr ? currentRecord->textContent : std::string{};
	}

	if (currentRecord != nullptr &&
		currentRecord->fileSize <= kMaximumSynchronizedAssetBytes) {
		std::string assetContent;

		if (ReadBinaryTextFile(assetPath, assetContent)) {
			changeEvent.snapshotData = EncodeBase64(assetContent);
		}
	}

	if (currentRecord != nullptr && changeEvent.snapshotData.empty()) {
		lastError_ = "Assetが転送上限を超えています: " + assetPath;
		return;
	}

	QueueLocalChange(std::move(changeEvent));
}

void EditorTeamCollaborationManager::QueueLocalChange(EditorTeamChangeEvent changeEvent) {
	if (changeEvent.timestampUnixMilliseconds == 0u) {
		changeEvent.timestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	}

	if (!changeEvent.objectUuid.empty()) {
		lastChangedByObjectUuid_[changeEvent.objectUuid] = changeEvent.userName;
	}

	recentChanges_.push_back(changeEvent);

	if (recentChanges_.size() > kMaximumRecentChangeCount) {
		recentChanges_.erase(recentChanges_.begin());
	}

	if (isHost_ && networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online) {
		currentRevision_++;
		changeEvent.revision = currentRevision_;
		if (!recentChanges_.empty() && recentChanges_.back().changeId == changeEvent.changeId) {
			recentChanges_.back() = changeEvent;
		}
		// Host自身のTeamItem操作はServerからcommitを受け直さないため、ここで確定Revisionを反映する。
		// これを行わないと次回編集のbaseRevisionが古いままとなり、同時編集の競合を見逃す。
		if (IsTeamItemChange(changeEvent) && changeEvent.operation != "TeamItemDelete") {
			const TeamItem committedItem = DeserializeTeamItem(changeEvent);
			const auto teamItem = teamItems_.find(committedItem.id);
			if (teamItem != teamItems_.end()) {
				teamItem->second.revision = currentRevision_;
			}
		}
		lastPropertyRevision_[BuildPropertyKey(changeEvent)] = currentRevision_;
		AppendChangeLog(changeEvent);
		QueueChangeEventMessage(changeEvent, "commit");
		SaveSettings();
		return;
	}

	unsyncedChanges_.push_back(changeEvent);
	AppendChangeLog(changeEvent);

	if (networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online) {
		QueueChangeEventMessage(changeEvent, "change");
	}
}

void EditorTeamCollaborationManager::ProcessIncomingMessages() {
	std::vector<std::string> incomingMessages;

	if (networkState_->transport != nullptr) {
		networkState_->transport->Poll(incomingMessages);
	}

	{
		std::lock_guard<std::mutex> queueLock(networkState_->queueMutex);

		if (!networkState_->incomingMessages.empty()) {
			incomingMessages.insert(
				incomingMessages.end(),
				std::make_move_iterator(networkState_->incomingMessages.begin()),
				std::make_move_iterator(networkState_->incomingMessages.end()));
			networkState_->incomingMessages.clear();
		}
	}

	for (const std::string& message : incomingMessages) {
		const std::string messageType = ReadJsonString(message, "type");
		const std::string targetUserId = ReadJsonString(message, "targetUserId");
		if (!targetUserId.empty() && targetUserId != userId_) {
			continue;
		}
		if (messageType == CG2Collaboration::MessageType::kHistoryBegin) {
			historySyncInProgress_ = true;
			serverRevision_ = ReadJsonUnsigned(message, "serverRevision");
			networkState_->status.store(EditorTeamConnectionStatus::Synchronizing, std::memory_order_release);
			continue;
		}
		if (messageType == CG2Collaboration::MessageType::kHistoryEnd) {
			CompleteHistorySynchronization(ReadJsonUnsigned(message, "serverRevision"));
			continue;
		}

		// Protocol / Project ID を含む新しいHandshake。旧Buildのcompatibilityとは別種別のため、
		// 互換性の無い相手でも「拒否理由」だけは相手へ届く。
		if (messageType == CG2Collaboration::MessageType::kHandshake) {
			ProcessHandshakeMessage(message);
			continue;
		}
		if (messageType == CG2Collaboration::MessageType::kHandshakeAccepted) {
			ProcessHandshakeResult(message, true);
			continue;
		}
		if (messageType == CG2Collaboration::MessageType::kHandshakeRejected) {
			ProcessHandshakeResult(message, false);
			continue;
		}

		// Editor Hostは複数Clientを同時に受ける。Handshakeを通過していない送信者の
		// Payloadだけを捨て、3台目の参加で既存参加者まで未認証へ戻さない。
		if (isHost_) {
			std::string senderUserId = ReadJsonString(message, "userId");
			// 分割転送のChunk/Endは転送IDだけを持つ。Beginで検証済みの送信者を
			// 組み立て状態から引き継ぎ、正規の大容量Scene/Assetを落とさない。
			if (senderUserId.empty() &&
				(messageType == "assetChunk" || messageType == "assetEnd")) {
				const std::string transferId = ReadJsonString(message, "transferId");
				const auto transferIterator = pendingIncomingTransfers_.find(transferId);
				if (transferIterator != pendingIncomingTransfers_.end()) {
					senderUserId = transferIterator->second.metadataEvent.userId;
				}
			}
			if (senderUserId.empty() || !acceptedPeerUserIds_.contains(senderUserId)) {
				continue;
			}
		}
		if (messageType == CG2Collaboration::MessageType::kHeartbeat) {
			ProcessHeartbeatMessage(message, false);
			continue;
		}
		if (messageType == CG2Collaboration::MessageType::kHeartbeatAck) {
			ProcessHeartbeatMessage(message, true);
			continue;
		}
		// Serverが「Clientが消えた」と判断した通知。そのUserのLockを解放する。
		if (messageType == CG2Collaboration::MessageType::kPeerLeft) {
			ReleaseLocksOwnedBy(ReadJsonString(message, "userId"));
			continue;
		}
		if (messageType == "compatibility") {
			ProcessCompatibilityMessage(message, false);
			continue;
		}
		if (messageType == "compatibilityAccepted") {
			ProcessCompatibilityMessage(message, true);
			continue;
		}
		if (messageType == "compatibilityRejected") {
			compatibilityAccepted_ = false;
			lastError_ = "共同制作を拒否されました: " + ReadJsonString(message, "reason");
			networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
			continue;
		}
		// Version交換が完了するまで、Scene・Asset・LockのPayloadは一切適用しない。
		if (!isHost_ && !compatibilityAccepted_) {
			continue;
		}

		if (messageType == "lockRequest" || messageType == "unlock" || messageType == "lockState") {
			ProcessLockMessage(message, messageType);
			continue;
		}

		if (messageType == "presence") {
			ProcessPresenceMessage(message);

			// Transportのサーバーは、あるClientから受け取ったMessageを他のClientへ転送しない。
			// Scene差分はHostがcommitとして配り直すので転送は不要だが、PresenceだけはHostが
			// 配り直さないと、参加者同士が互いを一生認識できない(人数もCursorも自分だけになる)。
			// 素通しで全Messageを中継すると、Clientのchangeが他のClientへ直接届いたあとに
			// Hostのcommitでもう一度届き、同じ変更を二重に適用してしまう。中継はPresenceに限る。
			// 送り主へ返っても ProcessPresenceMessage が自分のuserIdを弾くため害は無い。
			if (isHost_) {
				QueueOutgoingMessage(message);
			}

			continue;
		}

		if (messageType == "sharedSceneFolder" && !isHost_) {
			const std::string sharedSceneFolder = ReadJsonString(message, "path");

			if (IsValidSharedGameSceneFolder(sharedSceneFolder)) {
				strncpy_s(
					sharedSceneFolderBuffer_.data(),
					sharedSceneFolderBuffer_.size(),
					sharedSceneFolder.c_str(),
					_TRUNCATE);
				assetRecords_.clear();
				lastSnapshotText_.clear();
				lastSnapshotHash_ = 0u;
				SaveSettings();
				AddConsoleMessage("Team: 共有Sceneフォルダ " + sharedSceneFolder);
			}

			continue;
		}

		// 参加直後のCatch-up。Hostからの assetOffer に対し、既に同じHashを持っていれば
		// 本体を要求せずRecordだけ更新し、持っていなければ assetRequest で本体を要求する。
		if (messageType == "assetOffer" && !isHost_) {
			const std::string scenePath = ReadJsonString(message, "scenePath");
			const std::string offeredHash = ReadJsonString(message, "assetHash");
			std::filesystem::path localAssetPath;
			bool hasMatchingLocalFile = false;
			std::error_code fileError;

			if (ResolveSafeAssetPath(scenePath, localAssetPath) &&
				std::filesystem::exists(localAssetPath, fileError) &&
				AssetManager::Get().GetHash(localAssetPath.generic_string()) == offeredHash) {
				hasMatchingLocalFile = true;
			}

			if (hasMatchingLocalFile) {
				AssetRecord assetRecord{};
				assetRecord.hash = offeredHash;
				assetRecord.fileSize = ReadJsonUnsigned(message, "fileSize");
				assetRecord.lastWriteTimestamp = static_cast<std::int64_t>(
					std::filesystem::last_write_time(localAssetPath, fileError).time_since_epoch().count());
				assetRecords_[scenePath] = std::move(assetRecord);
			}
			else {
				QueueOutgoingMessage(SerializeAssetRequestMessage(scenePath, userId_));
			}

			continue;
		}

		if (messageType == "assetRequest" && isHost_) {
			const std::string scenePath = ReadJsonString(message, "scenePath");
			const std::string requestingUserId = ReadJsonString(message, "userId");
			const auto recordIterator = assetRecords_.find(scenePath);
			std::filesystem::path localAssetPath;
			std::error_code fileError;

			if (recordIterator == assetRecords_.end() ||
				!ResolveSafeAssetPath(scenePath, localAssetPath) ||
				!std::filesystem::exists(localAssetPath, fileError)) {
				continue;
			}

			const AssetRecord& record = recordIterator->second;

			if (record.fileSize > kMaximumSynchronizedAssetBytes) {
				lastError_ = "Assetが転送上限を超えています: " + scenePath;
				continue;
			}

			std::string assetContent;

			if (!ReadBinaryTextFile(localAssetPath, assetContent)) {
				continue;
			}

			EditorTeamChangeEvent changeEvent{};
			changeEvent.changeId = CreateEditorTeamUuid();
			changeEvent.userId = userId_;
			changeEvent.userName = userNameBuffer_.data();
			changeEvent.scenePath = scenePath;
			changeEvent.objectUuid = record.uuid;
			changeEvent.operation = "UpdateAsset";
			changeEvent.property = "Asset";
			changeEvent.newValue = record.hash;
			changeEvent.assetHash = record.hash;
			changeEvent.fileSize = record.fileSize;
			changeEvent.baseRevision = currentRevision_;
			changeEvent.snapshotData = EncodeBase64(assetContent);
			currentRevision_++;
			changeEvent.revision = currentRevision_;
			lastPropertyRevision_[BuildPropertyKey(changeEvent)] = currentRevision_;
			AppendChangeLog(changeEvent);
			QueueChangeEventMessage(changeEvent, "commit", requestingUserId);
			SaveSettings();
			continue;
		}

		// 大きなAsset/Sceneはassetファイル本体をassetBegin/assetChunk/assetEndの3種へ分割して届く。
		// 受信側で本体を組み立て終えるまでは既存のchange/commit経路には流さない。
		if (messageType == "assetBegin") {
			IncomingAssetTransfer transfer{};
			transfer.metadataEvent = DeserializeChangeEvent(message);
			transfer.commitType = ReadJsonString(message, "commitType");
			transfer.expectedTransportPayloadBytes = ReadJsonUnsigned(message, "payloadBytes");
			transfer.expectedRawBytes = ReadJsonUnsigned(message, "rawBytes");
			transfer.expectedChunkCount = static_cast<std::uint32_t>(
				ReadJsonUnsigned(message, "chunkCount"));
			transfer.accumulatedTransportPayload.reserve(
				static_cast<std::size_t>(transfer.expectedTransportPayloadBytes));
			incomingTransferLabel_ = transfer.metadataEvent.scenePath.empty()
				? std::string("Scene同期")
				: transfer.metadataEvent.scenePath;
			incomingTransferReceivedBytes_ = 0u;
			incomingTransferTotalBytes_ = transfer.expectedRawBytes;
			pendingIncomingTransfers_[transfer.metadataEvent.changeId] = std::move(transfer);
			continue;
		}

		if (messageType == "assetChunk") {
			const std::string transferId = ReadJsonString(message, "transferId");
			const auto transferIterator = pendingIncomingTransfers_.find(transferId);

			if (transferIterator == pendingIncomingTransfers_.end()) {
				continue;
			}

			IncomingAssetTransfer& transfer = transferIterator->second;
			transfer.accumulatedTransportPayload += ReadJsonString(message, "data");
			transfer.receivedChunkCount++;

			if (transfer.expectedTransportPayloadBytes > 0u) {
				const double receivedRatio =
					static_cast<double>(transfer.accumulatedTransportPayload.size()) /
					static_cast<double>(transfer.expectedTransportPayloadBytes);
				incomingTransferReceivedBytes_ = static_cast<std::uint64_t>(
					(std::clamp)(receivedRatio, 0.0, 1.0) *
					static_cast<double>(transfer.expectedRawBytes));
			}

			continue;
		}

		if (messageType == "assetEnd") {
			const std::string transferId = ReadJsonString(message, "transferId");
			const auto transferIterator = pendingIncomingTransfers_.find(transferId);

			if (transferIterator == pendingIncomingTransfers_.end()) {
				continue;
			}

			EditorTeamChangeEvent reassembledChangeEvent = transferIterator->second.metadataEvent;
			reassembledChangeEvent.snapshotData =
				DecodeBase64(transferIterator->second.accumulatedTransportPayload);
			const std::string commitType = transferIterator->second.commitType;
			pendingIncomingTransfers_.erase(transferIterator);
			incomingTransferReceivedBytes_ = incomingTransferTotalBytes_;

			if (commitType == "change" && isHost_) {
				ProcessRemoteChange(std::move(reassembledChangeEvent), false);
			}
			else if (commitType == "commit") {
				ProcessRemoteChange(std::move(reassembledChangeEvent), true);
			}

			continue;
		}

		EditorTeamChangeEvent changeEvent = DeserializeChangeEvent(message);

		if (messageType == "change" && isHost_) {
			ProcessRemoteChange(std::move(changeEvent), false);
		}
		else if (messageType == "commit") {
			ProcessRemoteChange(std::move(changeEvent), true);
		}
	}

	{
		std::lock_guard<std::mutex> errorLock(networkState_->errorMutex);

		if (!networkState_->errorMessage.empty()) {
			lastError_ = networkState_->errorMessage;
		}
	}
}

void EditorTeamCollaborationManager::SendCompatibilityHello() {
	ProjectVersionSettings projectSettings{};
	std::string error;
	if (!ProjectVersionManager::Load(std::filesystem::current_path(), projectSettings, error)) {
		lastError_ = "共同制作Version確認失敗: " + error;
		networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
		return;
	}
	QueueOutgoingMessage(SerializeCompatibilityMessage(projectSettings));
	compatibilityHelloSent_ = true;
}

void EditorTeamCollaborationManager::ProcessCompatibilityMessage(
	const std::string& message,
	bool isAcceptance) {
	if (isAcceptance) {
		compatibilityAccepted_ = true;
		return;
	}

	ProjectVersionSettings localProject{};
	std::string error;
	if (!ProjectVersionManager::Load(std::filesystem::current_path(), localProject, error)) {
		QueueOutgoingMessage(SerializeCompatibilityRejectedMessage(error));
		lastError_ = error;
		networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
		return;
	}

	const std::string peerEngine = ReadJsonString(message, "engineVersion");
	const std::string peerChannel = ReadJsonString(message, "channel");
	const std::uint64_t peerProjectFormat = ReadJsonUnsigned(message, "projectFormat");
	const std::uint64_t peerScriptApi = ReadJsonUnsigned(message, "scriptApi");
	CheckCG2EnginePeerCompatibility(
		peerEngine,
		static_cast<std::uint32_t>(peerProjectFormat),
		static_cast<std::uint32_t>(peerScriptApi),
		peerChannel,
		localProject.projectFormatVersion,
		localProject.updateChannel,
		error);

	if (!error.empty()) {
		QueueOutgoingMessage(SerializeCompatibilityRejectedMessage(error));
		compatibilityAccepted_ = false;
		lastError_ = "共同制作開始拒否: " + error;
		networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
		return;
	}

	compatibilityAccepted_ = true;
	QueueOutgoingMessage(SerializeCompatibilityAcceptedMessage());
}


void EditorTeamCollaborationManager::SendHandshake() {
	ProjectVersionSettings projectSettings{};
	std::string error;

	if (!ProjectVersionManager::Load(std::filesystem::current_path(), projectSettings, error)) {
		lastError_ = "Handshake失敗: " + error;
		networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
		return;
	}

	QueueOutgoingMessage(SerializeHandshakeMessage(
		projectSettings,
		projectIdBuffer_.data(),
		userId_,
		userNameBuffer_.data()));
	compatibilityHelloSent_ = true;
}

void EditorTeamCollaborationManager::ProcessHandshakeMessage(const std::string& message) {
	// Server側(またはHost側Editor)が相手のHandshakeを検証する。
	// 互換性が無いまま同期を続けるとScene/Assetが壊れるため、必ずここで拒否する。
	const std::uint64_t peerProtocol = ReadJsonUnsigned(message, "protocol");
	const std::string peerUserId = ReadJsonString(message, "userId");
	const std::string peerUserName = ReadJsonString(message, "userName");
	if (peerUserId.empty()) {
		lastError_ = "接続拒否: User IDがありません";
		AddConsoleMessage("Team: " + lastError_);
		return;
	}

	if (peerProtocol != CG2Collaboration::kCollaborationProtocolVersion) {
		const std::string reason =
			"共同制作Protocolが一致しません。Server: " +
			std::to_string(CG2Collaboration::kCollaborationProtocolVersion) +
			" / Client: " + std::to_string(peerProtocol);
		QueueOutgoingMessage(SetTargetUserId(
			SerializeHandshakeRejectedMessage(reason), peerUserId));
		lastError_ = "接続拒否: " + reason;
		AddConsoleMessage("Team: " + lastError_);
		return;
	}

	const std::string peerProjectId = ReadJsonString(message, "projectId");
	const std::string localProjectId = projectIdBuffer_.data();

	// Project IDが違う相手を受け入れると、別Projectの差分でScene/Assetを上書きしてしまう。
	if (!localProjectId.empty() && peerProjectId != localProjectId) {
		const std::string reason =
			"Project IDが一致しません。Server: " + localProjectId + " / Client: " + peerProjectId;
		QueueOutgoingMessage(SetTargetUserId(
			SerializeHandshakeRejectedMessage(reason), peerUserId));
		lastError_ = "接続拒否: " + reason;
		AddConsoleMessage("Team: " + lastError_);
		return;
	}

	ProjectVersionSettings localProject{};
	std::string error;

	if (!ProjectVersionManager::Load(std::filesystem::current_path(), localProject, error)) {
		QueueOutgoingMessage(SetTargetUserId(
			SerializeHandshakeRejectedMessage(error), peerUserId));
		lastError_ = error;
		return;
	}

	CheckCG2EnginePeerCompatibility(
		ReadJsonString(message, "engineVersion"),
		static_cast<std::uint32_t>(ReadJsonUnsigned(message, "projectFormat")),
		static_cast<std::uint32_t>(ReadJsonUnsigned(message, "scriptApi")),
		ReadJsonString(message, "channel"),
		localProject.projectFormatVersion,
		localProject.updateChannel,
		error);

	if (!error.empty()) {
		QueueOutgoingMessage(SetTargetUserId(
			SerializeHandshakeRejectedMessage(error), peerUserId));
		lastError_ = "接続拒否: " + error;
		AddConsoleMessage("Team: " + lastError_);
		return;
	}

	if (!peerUserId.empty()) {
		MemberRecord& peer = memberRecords_[peerUserId];
		peer.userName = peerUserName.empty() ? peerUserId : peerUserName;
		peer.color = BuildUserColor(peerUserId);
		peer.idleSeconds = 0.0f;
		peer.isOnline = true;
	}

	handshakeAccepted_ = true;
	compatibilityAccepted_ = true;
	lastError_.clear();
	acceptedPeerUserIds_.insert(peerUserId);
	if (std::find(pendingJoinCatchUpUserIds_.begin(), pendingJoinCatchUpUserIds_.end(), peerUserId) ==
		pendingJoinCatchUpUserIds_.end()) {
		pendingJoinCatchUpUserIds_.push_back(peerUserId);
	}
	QueueOutgoingMessage(SetTargetUserId(
		SerializeHandshakeAcceptedMessage(currentRevision_), peerUserId));
	AddConsoleMessage("Team: " + (peerUserName.empty() ? peerUserId : peerUserName) + " が参加しました");
}

void EditorTeamCollaborationManager::ProcessHandshakeResult(
	const std::string& message,
	bool isAccepted) {
	if (isAccepted) {
		handshakeAccepted_ = true;
		compatibilityAccepted_ = true;
		lastError_.clear();
		// Serverが持つRevisionを受け取る。再接続時はここからの差分だけを取り直し、
		// Scene全体を無条件に上書きしない。
		const std::uint64_t serverRevision = ReadJsonUnsigned(message, "revision");
		serverRevision_ = serverRevision;

		if (serverRevision > lastSyncedRevision_) {
			AddConsoleMessage(
				"Team: Server Revision " + std::to_string(serverRevision) +
				" を確認しました(Local " + std::to_string(lastSyncedRevision_) + ")");
			RequestMissingHistory(serverRevision);
		}
		else {
			historySyncInProgress_ = false;
		}

		return;
	}

	handshakeAccepted_ = false;
	compatibilityAccepted_ = false;
	const std::string reason = ReadJsonString(message, "reason");
	lastError_ = "接続拒否:\n" + reason;
	AddConsoleMessage("Team: " + lastError_);
	networkState_->status.store(EditorTeamConnectionStatus::Incompatible, std::memory_order_release);
	// 互換性が無い相手と通信を続けない。
	StopNetworkThread();
}

void EditorTeamCollaborationManager::RequestMissingHistory(std::uint64_t serverRevision) {
	serverRevision_ = serverRevision;
	historySyncInProgress_ = true;
	networkState_->status.store(EditorTeamConnectionStatus::Synchronizing, std::memory_order_release);
	QueueOutgoingMessage(SerializeHistoryRequestMessage(userId_, lastSyncedRevision_));
}

void EditorTeamCollaborationManager::FlushUnsyncedChanges() {
	if (isHost_) return;
	for (const EditorTeamChangeEvent& changeEvent : unsyncedChanges_) {
		QueueChangeEventMessage(changeEvent, "change");
	}
}

void EditorTeamCollaborationManager::CaptureBaseState() {
	std::error_code directoryError;
	std::filesystem::create_directories(".team/base", directoryError);
	if (directoryError) return;
	if (!lastSnapshotText_.empty()) {
		WriteUtf8BomTextFile(".team/base/current.scene", RemoveUtf8Bom(lastSnapshotText_));
	}
	WriteUtf8BomTextFile(".team/base/revision.txt", std::to_string(lastSyncedRevision_) + "\r\n");
}

void EditorTeamCollaborationManager::CompleteHistorySynchronization(std::uint64_t serverRevision) {
	serverRevision_ = serverRevision;
	historySyncInProgress_ = false;
	if (!teamItemConflicts_.empty() || !conflicts_.empty()) {
		networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
		AddConsoleMessage("Team: 履歴取得完了。競合を解決してください");
		return;
	}
	currentRevision_ = (std::max)(currentRevision_, serverRevision_);
	lastSyncedRevision_ = serverRevision_;
	CaptureBaseState();
	SaveSettings();
	FlushUnsyncedChanges();
	networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
	AddConsoleMessage("Team: Revision " + std::to_string(lastSyncedRevision_) + " まで同期しました");
}

void EditorTeamCollaborationManager::UpdateHeartbeat(float deltaTime) {
	if (networkState_->transport == nullptr) {
		return;
	}

	const CG2Collaboration::TransportState transportState = networkState_->transport->GetState();
	const bool isLinkUp =
		transportState == CG2Collaboration::TransportState::Connected ||
		transportState == CG2Collaboration::TransportState::Listening;

	if (!isLinkUp) {
		heartbeatElapsedSeconds_ = 0.0f;
		heartbeatSilenceSeconds_ = 0.0f;
		return;
	}

	// Clientから定期的にHeartbeatを送る。受け取った側は即ackを返す。
	if (!isHost_) {
		heartbeatElapsedSeconds_ += deltaTime;

		if (heartbeatElapsedSeconds_ >= CG2Collaboration::kHeartbeatIntervalSeconds) {
			heartbeatElapsedSeconds_ = 0.0f;
			heartbeatSentUnixMilliseconds_ = GetCurrentUnixTimestampMilliseconds();
			QueueOutgoingMessage(SerializeHeartbeatMessage(userId_, heartbeatSentUnixMilliseconds_));
		}
	}

	// 相手からの応答が途切れた時間を数える。
	// PCスリープ・Wi-Fi切替・Tailscale再接続では、TCPが切れないまま無言になることがある。
	if (networkState_->transport->GetPeerCount() > 0) {
		heartbeatSilenceSeconds_ += deltaTime;

		if (heartbeatSilenceSeconds_ >
			CG2Collaboration::kHeartbeatTimeoutSeconds +
				CG2Collaboration::kLockReleaseGracePeriodSeconds) {
			heartbeatSilenceSeconds_ = 0.0f;

			// Host側は、消えたClientのLockを解放して他の人が編集できるようにする。
			if (isHost_) {
				std::vector<std::string> staleUserIds;

				for (const auto& [memberUserId, memberRecord] : memberRecords_) {
					if (memberUserId != userId_) {
						staleUserIds.push_back(memberUserId);
					}
				}

				for (const std::string& staleUserId : staleUserIds) {
					ReleaseLocksOwnedBy(staleUserId);
				}

				AddConsoleMessage("Team: 応答の無いClientのLockを解放しました");
			}
			else {
				AddConsoleMessage("Team: Serverから応答がありません。再接続を待ちます");
				networkState_->status.store(
					EditorTeamConnectionStatus::Reconnecting, std::memory_order_release);
			}
		}
	}
	else {
		heartbeatSilenceSeconds_ = 0.0f;
	}
}

void EditorTeamCollaborationManager::ProcessHeartbeatMessage(
	const std::string& message,
	bool isAcknowledgement) {
	// 何か受け取れた時点で相手は生きているので、無応答カウントを戻す。
	heartbeatSilenceSeconds_ = 0.0f;

	if (isAcknowledgement) {
		const std::uint64_t sentAt = ReadJsonUnsigned(message, "sentAt");

		if (sentAt != 0u) {
			const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();
			latencyMilliseconds_ = now >= sentAt ? static_cast<float>(now - sentAt) : 0.0f;
		}

		return;
	}

	const std::string peerUserId = ReadJsonString(message, "userId");

	if (!peerUserId.empty()) {
		const auto memberIterator = memberRecords_.find(peerUserId);

		if (memberIterator != memberRecords_.end()) {
			memberIterator->second.idleSeconds = 0.0f;
			memberIterator->second.isOnline = true;
		}
	}

	// 送信時刻をそのまま返し、相手側でLatencyを算出させる。
	QueueOutgoingMessage(SetTargetUserId(
		SerializeHeartbeatAckMessage(peerUserId, ReadJsonUnsigned(message, "sentAt")),
		peerUserId));
}

void EditorTeamCollaborationManager::ReleaseLocksOwnedBy(const std::string& ownerUserId) {
	if (ownerUserId.empty()) {
		return;
	}

	std::vector<std::string> releasedKeys;

	for (const auto& [lockKey, lockOwnerId] : lockedByUserId_) {
		if (lockOwnerId == ownerUserId) {
			releasedKeys.push_back(lockKey);
		}
	}

	for (const std::string& lockKey : releasedKeys) {
		lockedByUserId_.erase(lockKey);
		lockedByUserName_.erase(lockKey);
		remoteLockIdleSeconds_.erase(lockKey);
		lockModes_.erase(lockKey);
		lockTargetTypes_.erase(lockKey);
	}

	const auto memberIterator = memberRecords_.find(ownerUserId);

	if (memberIterator != memberRecords_.end()) {
		memberIterator->second.isOnline = false;
	}
	acceptedPeerUserIds_.erase(ownerUserId);
	pendingJoinCatchUpUserIds_.erase(
		std::remove(pendingJoinCatchUpUserIds_.begin(), pendingJoinCatchUpUserIds_.end(), ownerUserId),
		pendingJoinCatchUpUserIds_.end());
}

bool EditorTeamCollaborationManager::IsTeamItemChange(
	const EditorTeamChangeEvent& changeEvent) const {
	return changeEvent.operation == "TeamItemUpsert" ||
		changeEvent.operation == "TeamItemDelete";
}

std::string EditorTeamCollaborationManager::SerializeTeamItem(const TeamItem& item) const {
	std::ostringstream json;
	json << "{\"id\":\"" << EscapeJsonText(item.id)
		<< "\",\"kind\":\"" << EscapeJsonText(item.kind)
		<< "\",\"targetType\":\"" << EscapeJsonText(item.targetType)
		<< "\",\"targetId\":\"" << EscapeJsonText(item.targetId)
		<< "\",\"scenePath\":\"" << EscapeJsonText(item.scenePath)
		<< "\",\"componentUuid\":\"" << EscapeJsonText(item.componentUuid)
		<< "\",\"propertyName\":\"" << EscapeJsonText(item.propertyName)
		<< "\",\"text\":\"" << EscapeJsonText(item.text)
		<< "\",\"parentId\":\"" << EscapeJsonText(item.parentId)
		<< "\",\"assigneeUserId\":\"" << EscapeJsonText(item.assigneeUserId)
		<< "\",\"mentionedUserId\":\"" << EscapeJsonText(item.mentionedUserId)
		<< "\",\"creatorUserId\":\"" << EscapeJsonText(item.creatorUserId)
		<< "\",\"creatorUserName\":\"" << EscapeJsonText(item.creatorUserName)
		<< "\",\"timestamp\":" << item.timestampUnixMilliseconds
		<< ",\"updatedTimestamp\":" << item.updatedTimestampUnixMilliseconds
		<< ",\"updatedByUserId\":\"" << EscapeJsonText(item.updatedByUserId)
		<< "\",\"updatedByUserName\":\"" << EscapeJsonText(item.updatedByUserName)
		<< "\",\"hasWorldPosition\":" << (item.hasWorldPosition ? 1 : 0)
		<< ",\"worldX\":" << item.worldPosition[0]
		<< ",\"worldY\":" << item.worldPosition[1]
		<< ",\"worldZ\":" << item.worldPosition[2]
		<< ",\"scriptLine\":" << item.scriptLine
		<< ",\"codeContext\":\"" << EscapeJsonText(item.codeContext)
		<< "\",\"functionName\":\"" << EscapeJsonText(item.functionName)
		<< "\",\"reviewStatus\":" << item.reviewStatus
		<< ",\"targetRevision\":" << item.targetRevision
		<< ",\"targetChangeId\":\"" << EscapeJsonText(item.targetChangeId)
		<< "\",\"expiresAt\":" << item.expiresAtUnixMilliseconds
		<< ",\"keepsPingHistory\":" << (item.keepsPingHistory ? 1 : 0)
		<< ",\"resolved\":" << (item.isResolved ? 1 : 0) << "}";
	return json.str();
}

EditorTeamCollaborationManager::TeamItem EditorTeamCollaborationManager::DeserializeTeamItem(
	const EditorTeamChangeEvent& changeEvent) const {
	TeamItem item{};
	const std::string& json = changeEvent.newValue;
	item.id = ReadJsonString(json, "id");
	item.kind = ReadJsonString(json, "kind");
	item.targetType = ReadJsonString(json, "targetType");
	item.targetId = ReadJsonString(json, "targetId");
	item.scenePath = ReadJsonString(json, "scenePath");
	item.componentUuid = ReadJsonString(json, "componentUuid");
	item.propertyName = ReadJsonString(json, "propertyName");
	item.text = ReadJsonString(json, "text");
	item.parentId = ReadJsonString(json, "parentId");
	item.assigneeUserId = ReadJsonString(json, "assigneeUserId");
	item.mentionedUserId = ReadJsonString(json, "mentionedUserId");
	item.creatorUserId = ReadJsonString(json, "creatorUserId");
	item.creatorUserName = ReadJsonString(json, "creatorUserName");
	item.timestampUnixMilliseconds = ReadJsonUnsigned(json, "timestamp");
	item.updatedTimestampUnixMilliseconds = ReadJsonUnsigned(json, "updatedTimestamp");
	item.updatedByUserId = ReadJsonString(json, "updatedByUserId");
	item.updatedByUserName = ReadJsonString(json, "updatedByUserName");
	item.hasWorldPosition = ReadJsonBool(json, "hasWorldPosition");
	item.worldPosition = {
		ReadJsonFloat(json, "worldX"),
		ReadJsonFloat(json, "worldY"),
		ReadJsonFloat(json, "worldZ")};
	item.scriptLine = static_cast<int32_t>(ReadJsonUnsigned(json, "scriptLine"));
	item.codeContext = ReadJsonString(json, "codeContext");
	item.functionName = ReadJsonString(json, "functionName");
	item.reviewStatus = static_cast<int32_t>(ReadJsonUnsigned(json, "reviewStatus"));
	item.targetRevision = ReadJsonUnsigned(json, "targetRevision");
	item.targetChangeId = ReadJsonString(json, "targetChangeId");
	item.expiresAtUnixMilliseconds = ReadJsonUnsigned(json, "expiresAt");
	item.keepsPingHistory = json.find("\"keepsPingHistory\"") == std::string::npos ||
		ReadJsonBool(json, "keepsPingHistory");
	item.isResolved = ReadJsonBool(json, "resolved");
	item.revision = changeEvent.revision;
	if (item.id.empty()) item.id = changeEvent.changeId;
	if (item.kind.empty()) item.kind = changeEvent.property;
	if (item.kind == "ChangeComment") item.kind = "Review";
	if (item.scenePath.empty()) item.scenePath = changeEvent.scenePath;
	if (item.targetId.empty()) item.targetId = changeEvent.objectUuid;
	if (item.componentUuid.empty()) item.componentUuid = changeEvent.componentUuid;
	if (item.creatorUserId.empty()) item.creatorUserId = changeEvent.userId;
	if (item.creatorUserName.empty()) item.creatorUserName = changeEvent.userName;
	if (item.timestampUnixMilliseconds == 0u) item.timestampUnixMilliseconds = changeEvent.timestampUnixMilliseconds;
	if (item.updatedTimestampUnixMilliseconds == 0u) item.updatedTimestampUnixMilliseconds = item.timestampUnixMilliseconds;
	return item;
}

void EditorTeamCollaborationManager::ApplyTeamItemChange(
	const EditorTeamChangeEvent& changeEvent) {
	const TeamItem item = DeserializeTeamItem(changeEvent);
	if (changeEvent.operation == "TeamItemDelete") {
		std::vector<std::string> removedItemIds{item.id};
		bool foundReply = true;
		while (foundReply) {
			foundReply = false;
			for (const auto& existingPair : teamItems_) {
				if (std::find(removedItemIds.begin(), removedItemIds.end(), existingPair.second.parentId) !=
					removedItemIds.end() &&
					std::find(removedItemIds.begin(), removedItemIds.end(), existingPair.first) == removedItemIds.end()) {
					removedItemIds.push_back(existingPair.first);
					foundReply = true;
				}
			}
		}
		for (const std::string& removedId : removedItemIds) {
			teamItems_.erase(removedId);
			notificationItemIds_.erase(
				std::remove(notificationItemIds_.begin(), notificationItemIds_.end(), removedId),
				notificationItemIds_.end());
		}
		return;
	}
	if (item.id.empty()) return;
	teamItems_[item.id] = item;
	const bool isPersonal = item.mentionedUserId == userId_ || item.assigneeUserId == userId_;
	if (item.creatorUserId != userId_ && (isPersonal || item.kind == "Ping")) {
		if (std::find(notificationItemIds_.begin(), notificationItemIds_.end(), item.id) ==
			notificationItemIds_.end()) {
			notificationItemIds_.push_back(item.id);
		}
	}
}

void EditorTeamCollaborationManager::QueueTeamItem(TeamItem item) {
	if (item.id.empty()) item.id = CreateEditorTeamUuid();
	if (item.creatorUserId.empty()) item.creatorUserId = userId_;
	if (item.creatorUserName.empty()) item.creatorUserName = userNameBuffer_.data();
	if (item.timestampUnixMilliseconds == 0u) item.timestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	item.updatedTimestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	item.updatedByUserId = userId_;
	item.updatedByUserName = userNameBuffer_.data();
	EditorTeamChangeEvent change{};
	change.changeId = CreateEditorTeamUuid();
	change.userId = userId_;
	change.userName = userNameBuffer_.data();
	change.scenePath = item.scenePath;
	change.sceneUuid = editorScene_ != nullptr ? editorScene_->GetUuid() : std::string{};
	change.objectUuid = item.targetId;
	change.componentUuid = item.componentUuid;
	change.operation = "TeamItemUpsert";
	change.property = item.kind;
	change.newValue = SerializeTeamItem(item);
	change.baseRevision = item.revision;
	ApplyTeamItemChange(change);
	QueueLocalChange(std::move(change));
}

void EditorTeamCollaborationManager::QueueTeamItemDelete(const TeamItem& item) {
	EditorTeamChangeEvent change{};
	change.changeId = CreateEditorTeamUuid();
	change.userId = userId_;
	change.userName = userNameBuffer_.data();
	change.scenePath = item.scenePath;
	change.sceneUuid = editorScene_ != nullptr ? editorScene_->GetUuid() : std::string{};
	change.objectUuid = item.targetId;
	change.componentUuid = item.componentUuid;
	change.operation = "TeamItemDelete";
	change.property = item.kind;
	change.newValue = SerializeTeamItem(item);
	change.baseRevision = item.revision;
	ApplyTeamItemChange(change);
	QueueLocalChange(std::move(change));
}

void EditorTeamCollaborationManager::JumpToTarget(
	const std::string& targetType,
	const std::string& targetId) {
	if (targetId.empty()) return;
	if (targetType == "Scene") {
		if (editorScene_ != nullptr && std::filesystem::is_regular_file(targetId) &&
			editorScene_->LoadScene(targetId)) {
			EditorSharedState::g_currentScenePath = targetId;
			EditorSharedState::g_selectedAssetPath = targetId;
			EditorSharedState::g_editorSceneSynchronizer.Update(
				EditorSharedState::g_editorTextureFilePaths,
				EditorSharedState::g_selectedPlacedSceneObjectIndex);
		}
		return;
	}
	if (targetType == "Asset" || targetType == "Prefab" ||
		targetType == "Script" || targetType == "ScriptLine") {
		EditorSharedState::g_selectedAssetPath = targetId;
		if ((targetType == "Script" || targetType == "ScriptLine") &&
			std::filesystem::is_regular_file(targetId)) {
			const std::wstring scriptPath = std::filesystem::absolute(targetId).wstring();
			ShellExecuteW(nullptr, L"open", scriptPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		return;
	}
	if (targetType == "History" || targetType == "ChangeEvent") {
		selectedHistoryTargetType_ = targetType;
		selectedHistoryTargetId_ = targetId;
		focusedTeamItemTargetType_.clear();
		focusedTeamItemTargetId_.clear();
		return;
	}
	if (targetType == "ScenePosition") {
		const auto item = std::find_if(
			teamItems_.begin(), teamItems_.end(),
			[&targetId](const auto& pair) {
				return pair.second.targetType == "ScenePosition" &&
					pair.second.targetId == targetId && pair.second.hasWorldPosition;
			});
		if (item != teamItems_.end()) {
			const float pitch = EditorSharedState::g_cameraTransform.rotate.x;
			const float yaw = EditorSharedState::g_cameraTransform.rotate.y;
			const Vector3 forward{
				std::sin(yaw) * std::cos(pitch),
				-std::sin(pitch),
				std::cos(yaw) * std::cos(pitch)};
			EditorSharedState::g_cameraTransform.translate = {
				item->second.worldPosition[0] - forward.x * 5.0f,
				item->second.worldPosition[1] - forward.y * 5.0f,
				item->second.worldPosition[2] - forward.z * 5.0f};
		}
		return;
	}
	if (editorScene_ == nullptr) return;
	std::string objectUuid = targetId;
	const std::size_t componentSeparator = objectUuid.find("#component:");
	if (componentSeparator != std::string::npos) {
		const std::size_t componentStart = componentSeparator + std::string("#component:").size();
		const std::size_t propertySeparator = objectUuid.find("#property:", componentStart);
		activityComponentUuid_ = objectUuid.substr(
			componentStart,
			propertySeparator == std::string::npos ? std::string::npos : propertySeparator - componentStart);
		if (propertySeparator != std::string::npos) {
			activityPropertyName_ = objectUuid.substr(propertySeparator + std::string("#property:").size());
		}
		objectUuid.resize(componentSeparator);
	}
	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (gameObject.uuid == objectUuid) {
			EditorSharedState::SetSingleSelectedGameObject(gameObject.id);
			return;
		}
	}
}


void EditorTeamCollaborationManager::ProcessRemoteChange(
	EditorTeamChangeEvent changeEvent,
	bool isCommitted) {
	// 遠隔からの変更を実際に受け取った時刻。Diagnosticsの Last Sync に出す。
	lastSyncUnixMilliseconds_ = GetCurrentUnixTimestampMilliseconds();
	if (changeEvent.timestampUnixMilliseconds == 0u) {
		changeEvent.timestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	}

	if (!changeEvent.objectUuid.empty()) {
		lastChangedByObjectUuid_[changeEvent.objectUuid] = changeEvent.userName;
	}

	// 付箋・Chat・Ping・ReviewはScene本体へ適用せず、同じRevision/ChangeLog経路で
	// 永続化する。専用Serverも通常のchangeとして扱えるためProtocol互換を保てる。
	if (IsTeamItemChange(changeEvent)) {
		const TeamItem incomingItem = DeserializeTeamItem(changeEvent);
		const auto currentItem = teamItems_.find(incomingItem.id);
		if (!isCommitted && isHost_ && currentItem != teamItems_.end() &&
			currentItem->second.revision > changeEvent.baseRevision &&
			currentItem->second.updatedByUserId != changeEvent.userId) {
			const bool alreadyQueued = std::any_of(
				teamItemConflicts_.begin(), teamItemConflicts_.end(),
				[&incomingItem](const TeamItemConflict& conflict) {
					return conflict.remoteItem.id == incomingItem.id;
				});
			if (!alreadyQueued) {
				teamItemConflicts_.push_back(TeamItemConflict{
					currentItem->second,
					incomingItem,
					changeEvent.userName});
			}
			networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
			lastError_ = "同じTeamItemが同時に編集されました";
			return;
		}
		const std::string propertyKey = BuildPropertyKey(changeEvent);
		if (!isCommitted && isHost_) {
			currentRevision_++;
			changeEvent.revision = currentRevision_;
			ApplyTeamItemChange(changeEvent);
			lastPropertyRevision_[propertyKey] = currentRevision_;
			recentChanges_.push_back(changeEvent);
			if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
			AppendChangeLog(changeEvent);
			QueueChangeEventMessage(changeEvent, "commit");
			SaveSettings();
			return;
		}

		const bool isOwnChange = changeEvent.userId == userId_;
		if (!isOwnChange) {
			ApplyTeamItemChange(changeEvent);
			recentChanges_.push_back(changeEvent);
			if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
		}
		else {
			const auto recent = std::find_if(recentChanges_.begin(), recentChanges_.end(),
				[&changeEvent](const EditorTeamChangeEvent& value) {
					return value.changeId == changeEvent.changeId;
				});
			if (recent != recentChanges_.end()) *recent = changeEvent;
			const TeamItem committedItem = DeserializeTeamItem(changeEvent);
			const auto item = teamItems_.find(committedItem.id);
			if (item != teamItems_.end()) item->second.revision = changeEvent.revision;
		}
		currentRevision_ = (std::max)(currentRevision_, changeEvent.revision);
		lastSyncedRevision_ = (std::max)(lastSyncedRevision_, changeEvent.revision);
		lastPropertyRevision_[propertyKey] = changeEvent.revision;
		unsyncedChanges_.erase(
			std::remove_if(unsyncedChanges_.begin(), unsyncedChanges_.end(),
				[&changeEvent](const EditorTeamChangeEvent& pending) {
					return pending.changeId == changeEvent.changeId;
				}),
			unsyncedChanges_.end());
		AppendChangeLog(changeEvent);
		SaveSettings();
		return;
	}

	const std::string propertyKey = BuildPropertyKey(changeEvent);
	const auto localPendingIterator = std::find_if(
		unsyncedChanges_.begin(), unsyncedChanges_.end(),
		[&](const EditorTeamChangeEvent& localChange) {
			return BuildPropertyKey(localChange) == propertyKey &&
				localChange.baseRevision < changeEvent.revision &&
				localChange.newValue != changeEvent.newValue;
		});
	const auto revisionIterator = lastPropertyRevision_.find(propertyKey);
	bool hasConflict = !isCommitted &&
		revisionIterator != lastPropertyRevision_.end() &&
		revisionIterator->second > changeEvent.baseRevision;
	if (isCommitted && changeEvent.userId != userId_ && localPendingIterator != unsyncedChanges_.end()) {
		hasConflict = true;
	}
	const std::string& sceneIdentity = changeEvent.sceneUuid.empty()
		? changeEvent.scenePath
		: changeEvent.sceneUuid;
	const std::string objectKeyPrefix =
		sceneIdentity + "|" + changeEvent.objectUuid + "|";
	const std::string componentKeyPrefix =
		objectKeyPrefix + changeEvent.componentUuid + "|";
	const std::string objectStructureKey = objectKeyPrefix + "|GameObject";
	const std::string componentStructureKey = componentKeyPrefix + "Component";

	for (const auto& propertyRevisionPair : lastPropertyRevision_) {
		if (hasConflict || isCommitted ||
			propertyRevisionPair.second <= changeEvent.baseRevision) {
			continue;
		}

		const bool conflictsWithObjectDeletion =
			(changeEvent.operation == "DeleteObject" &&
				propertyRevisionPair.first.starts_with(objectKeyPrefix)) ||
			(!changeEvent.objectUuid.empty() &&
				propertyRevisionPair.first == objectStructureKey);
		const bool conflictsWithComponentRemoval =
			(changeEvent.operation == "RemoveComponent" &&
				propertyRevisionPair.first.starts_with(componentKeyPrefix)) ||
			(!changeEvent.componentUuid.empty() &&
				propertyRevisionPair.first == componentStructureKey);
		hasConflict = conflictsWithObjectDeletion || conflictsWithComponentRemoval;
	}

	if (hasConflict) {
		// ScriptはBase/Local/Serverの3者を行単位で比較する。双方が別の行を
		// 変更しただけなら、人に競合解決を要求せず自動的に統合して次のChangeとして送る。
		if (isCommitted && changeEvent.property == "Asset" &&
			IsScriptAssetPath(changeEvent.scenePath) &&
			localPendingIterator != unsyncedChanges_.end()) {
			const EditorTeamChangeEvent localChange = *localPendingIterator;
			std::string mergedText;
			if (TryThreeWayMergeLines(
				localChange.oldValue, localChange.newValue, changeEvent.newValue, mergedText)) {
				if (!ApplyAssetChange(changeEvent)) return;
				currentRevision_ = (std::max)(currentRevision_, changeEvent.revision);
				lastSyncedRevision_ = (std::max)(lastSyncedRevision_, changeEvent.revision);
				lastPropertyRevision_[propertyKey] = changeEvent.revision;
				AppendChangeLog(changeEvent);
				unsyncedChanges_.erase(
					std::remove_if(unsyncedChanges_.begin(), unsyncedChanges_.end(),
						[&localChange](const EditorTeamChangeEvent& pending) {
							return pending.changeId == localChange.changeId;
						}),
					unsyncedChanges_.end());
				EditorTeamChangeEvent mergedChange = localChange;
				mergedChange.changeId = CreateEditorTeamUuid();
				mergedChange.oldValue = changeEvent.newValue;
				mergedChange.newValue = mergedText;
				mergedChange.snapshotData = EncodeBase64(mergedText);
				mergedChange.fileSize = static_cast<std::uint64_t>(mergedText.size());
				mergedChange.assetHash = FormatHash(CalculateTextHash(mergedText));
				mergedChange.baseRevision = changeEvent.revision;
				if (ApplyAssetChange(mergedChange)) {
					QueueLocalChange(std::move(mergedChange));
					SaveSettings();
					AddConsoleMessage("Team: Scriptの非競合行を3-way Mergeしました");
				}
				return;
			}
		}
		if (localPendingIterator != unsyncedChanges_.end()) {
			conflictLocalChanges_[changeEvent.changeId] = *localPendingIterator;
		}
		PrepareConflictCopies(changeEvent);
		conflicts_.push_back(std::move(changeEvent));
		networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
		AddConsoleMessage("Team: 同一Sceneのオフライン変更が競合しました");
		return;
	}

	// Playは端末ごとのRuntime実行であり、そこで生じた変更はStop時に破棄する。
	// 一方、他ユーザーの編集は共同制作の正式な変更なので、HostでのRevision確定と
	// 他の参加者への配信は止めず、このPlay中端末へのScene適用だけを終了時まで保留する。
	if (isPlaying_) {
		if (!isCommitted && isHost_) {
			currentRevision_++;
			changeEvent.revision = currentRevision_;
			lastPropertyRevision_[propertyKey] = currentRevision_;
			recentChanges_.push_back(changeEvent);
			if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
			AppendChangeLog(changeEvent);
			QueueChangeEventMessage(changeEvent, "commit");
			SaveSettings();

			const bool wasQueueEmpty = deferredPlayModeChanges_.empty();
			deferredPlayModeChanges_.push_back({std::move(changeEvent)});
			if (wasQueueEmpty) {
				AddConsoleMessage("Team: Play中に届いた編集をPlay終了後まで保留します");
			}
			return;
		}

		const bool isOwnChangeDuringPlay = changeEvent.userId == userId_;
		currentRevision_ = (std::max)(currentRevision_, changeEvent.revision);
		lastSyncedRevision_ = (std::max)(lastSyncedRevision_, changeEvent.revision);
		lastPropertyRevision_[propertyKey] = changeEvent.revision;
		unsyncedChanges_.erase(
			std::remove_if(
				unsyncedChanges_.begin(),
				unsyncedChanges_.end(),
				[&changeEvent](const EditorTeamChangeEvent& pendingChange) {
					return pendingChange.changeId == changeEvent.changeId;
				}),
			unsyncedChanges_.end());
		if (!isOwnChangeDuringPlay) {
			recentChanges_.push_back(changeEvent);
			if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
		}
		else {
			const auto recent = std::find_if(recentChanges_.begin(), recentChanges_.end(),
				[&changeEvent](const EditorTeamChangeEvent& value) {
					return value.changeId == changeEvent.changeId;
				});
			if (recent != recentChanges_.end()) *recent = changeEvent;
		}
		AppendChangeLog(changeEvent);
		SaveSettings();

		// 自分がPlay前に送った変更のCommitは、Play開始前Backupに既に含まれている。
		// 他ユーザーの変更だけを終了後の適用対象にする。
		if (!isOwnChangeDuringPlay) {
			const bool wasQueueEmpty = deferredPlayModeChanges_.empty();
			deferredPlayModeChanges_.push_back({std::move(changeEvent)});
			if (wasQueueEmpty) {
				AddConsoleMessage("Team: Play中に届いた編集をPlay終了後まで保留します");
			}
		}
		return;
	}

	if (!isCommitted && isHost_) {
		const bool isApplied = changeEvent.property == "Asset"
			? ApplyAssetChange(changeEvent)
			: ApplySceneSnapshot(changeEvent);

		if (!isApplied) {
			return;
		}

		currentRevision_++;
		changeEvent.revision = currentRevision_;
		lastPropertyRevision_[propertyKey] = currentRevision_;
		recentChanges_.push_back(changeEvent);
		if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
		AppendChangeLog(changeEvent);
		QueueChangeEventMessage(changeEvent, "commit");
		SaveSettings();
		return;
	}

	const bool isOwnChange = changeEvent.userId == userId_;

	if (!isOwnChange) {
		const bool isApplied = changeEvent.property == "Asset"
			? ApplyAssetChange(changeEvent)
			: ApplySceneSnapshot(changeEvent);

		if (!isApplied) {
			return;
		}
	}

	currentRevision_ = (std::max)(currentRevision_, changeEvent.revision);
	lastSyncedRevision_ = (std::max)(lastSyncedRevision_, changeEvent.revision);
	lastPropertyRevision_[propertyKey] = changeEvent.revision;

	unsyncedChanges_.erase(
		std::remove_if(
			unsyncedChanges_.begin(),
			unsyncedChanges_.end(),
			[&changeEvent](const EditorTeamChangeEvent& pendingChange) {
				return pendingChange.changeId == changeEvent.changeId;
			}),
		unsyncedChanges_.end());
	if (!isOwnChange) {
		recentChanges_.push_back(changeEvent);
		if (recentChanges_.size() > kMaximumRecentChangeCount) recentChanges_.erase(recentChanges_.begin());
	}
	else {
		const auto recent = std::find_if(recentChanges_.begin(), recentChanges_.end(),
			[&changeEvent](const EditorTeamChangeEvent& value) {
				return value.changeId == changeEvent.changeId;
			});
		if (recent != recentChanges_.end()) *recent = changeEvent;
	}
	AppendChangeLog(changeEvent);
	SaveSettings();
}

void EditorTeamCollaborationManager::ApplyDeferredPlayModeChanges() {
	if (deferredPlayModeChanges_.empty()) return;

	std::size_t appliedCount = 0u;
	while (appliedCount < deferredPlayModeChanges_.size()) {
		const EditorTeamChangeEvent& changeEvent =
			deferredPlayModeChanges_[appliedCount].changeEvent;
		const bool isApplied = changeEvent.property == "Asset"
			? ApplyAssetChange(changeEvent)
			: ApplySceneSnapshot(changeEvent);
		if (!isApplied) break;
		appliedCount++;
	}

	if (appliedCount > 0u) {
		deferredPlayModeChanges_.erase(
			deferredPlayModeChanges_.begin(),
			deferredPlayModeChanges_.begin() + static_cast<std::ptrdiff_t>(appliedCount));
	}

	if (deferredPlayModeChanges_.empty()) {
		AddConsoleMessage("Team: Play中に保留した編集を反映しました");
	}
	else {
		lastError_ = "Play中に保留した共同制作変更を反映できませんでした";
		AddConsoleMessage("Team: " + lastError_);
	}
}

void EditorTeamCollaborationManager::PrepareConflictCopies(
	const EditorTeamChangeEvent& changeEvent) {
	const std::filesystem::path conflictDirectory =
		BuildConflictDirectoryPath(changeEvent.changeId);
	std::error_code fileError;
	std::filesystem::create_directories(conflictDirectory, fileError);

	if (fileError) {
		lastError_ = "競合データ用Directoryを作成できません";
		return;
	}

	if (changeEvent.property == "Asset") {
		const auto localChange = conflictLocalChanges_.find(changeEvent.changeId);
		if (localChange != conflictLocalChanges_.end() &&
			IsScriptAssetPath(changeEvent.scenePath)) {
			WriteUtf8BomTextFile(
				conflictDirectory / "Base.asset",
				RemoveUtf8Bom(localChange->second.oldValue));
		}
		std::filesystem::path localAssetPath;

		if (ResolveSafeAssetPath(changeEvent.scenePath, localAssetPath) &&
			std::filesystem::exists(localAssetPath, fileError)) {
			std::filesystem::copy_file(
				localAssetPath,
				conflictDirectory / "Local.asset",
				std::filesystem::copy_options::overwrite_existing,
				fileError);
		}

		if (!changeEvent.snapshotData.empty()) {
			const std::string remoteAssetData = DecodeBase64(changeEvent.snapshotData);
			std::ofstream remoteAssetFile(
				conflictDirectory / "Remote.asset",
				std::ios::binary | std::ios::trunc);

			if (remoteAssetFile.is_open()) {
				remoteAssetFile.write(
					remoteAssetData.data(),
					static_cast<std::streamsize>(remoteAssetData.size()));
			}
		}

		return;
	}

	if (editorScene_ == nullptr || changeEvent.snapshotData.empty()) {
		return;
	}

	const std::filesystem::path localScenePath = conflictDirectory / "Local.scene";
	const std::filesystem::path baseScenePath = conflictDirectory / "Base.scene";
	const std::filesystem::path remoteScenePath = conflictDirectory / "Remote.scene";
	const std::filesystem::path resolutionScenePath = conflictDirectory / "Resolution.scene";
	editorScene_->SaveScene(localScenePath.generic_string());
	if (std::filesystem::exists(".team/base/current.scene", fileError)) {
		std::filesystem::copy_file(".team/base/current.scene", baseScenePath,
			std::filesystem::copy_options::overwrite_existing, fileError);
	}
	std::ofstream remoteSceneFile(remoteScenePath, std::ios::binary | std::ios::trunc);

	if (!remoteSceneFile.is_open()) {
		lastError_ = "Remote競合Sceneを作成できません";
		return;
	}

	remoteSceneFile.write(
		changeEvent.snapshotData.data(),
		static_cast<std::streamsize>(changeEvent.snapshotData.size()));
	remoteSceneFile.close();
	std::filesystem::copy_file(
		localScenePath,
		resolutionScenePath,
		std::filesystem::copy_options::overwrite_existing,
		fileError);
}

void EditorTeamCollaborationManager::UpdateMemberPresence(float deltaTime) {
	const float elapsedSeconds = (std::max)(deltaTime, 0.0f);
	const bool isOnline = GetStatus() == EditorTeamConnectionStatus::Online ||
		GetStatus() == EditorTeamConnectionStatus::Synchronizing;
	MemberRecord& localMember = memberRecords_[userId_];
	localMember.userName = userNameBuffer_.data();
	localMember.panel = activityPanel_;
	localMember.action = activityAction_;
	localMember.scenePath = EditorSharedState::g_currentScenePath;
	localMember.assetPath = EditorSharedState::g_selectedAssetPath;
	localMember.componentUuid = activityComponentUuid_;
	localMember.propertyName = activityPropertyName_;
	localMember.objectUuid.clear();
	if (editorScene_ != nullptr && EditorSharedState::g_selectedEditorGameObjectId >= 0) {
		const EditorGameObject* selected = editorScene_->FindGameObject(
			EditorSharedState::g_selectedEditorGameObjectId);
		if (selected != nullptr) localMember.objectUuid = selected->uuid;
	}
	localMember.cameraPosition = {
		EditorSharedState::g_cameraTransform.translate.x,
		EditorSharedState::g_cameraTransform.translate.y,
		EditorSharedState::g_cameraTransform.translate.z};
	localMember.cameraRotation = {
		EditorSharedState::g_cameraTransform.rotate.x,
		EditorSharedState::g_cameraTransform.rotate.y,
		EditorSharedState::g_cameraTransform.rotate.z};
	localMember.color = BuildUserColor(userId_);
	localMember.isPlaying = isPlaying_;
	localMember.isBuilding = isBuilding_;
#ifdef USE_IMGUI
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	const ImVec2 mousePosition = ImGui::GetMousePos();
	// Editorが非アクティブだとGetMousePos()は画面外の値を返す。そのままClampすると
	// 全員のCursorが左上へ固まって見えるため、その間は直前の位置を保つ。
	if (ImGui::IsMousePosValid(&mousePosition) &&
		mainViewport != nullptr && mainViewport->Size.x > 0.0f && mainViewport->Size.y > 0.0f) {
		localMember.cursorX = (std::clamp)(
			(mousePosition.x - mainViewport->Pos.x) / mainViewport->Size.x, 0.0f, 1.0f);
		localMember.cursorY = (std::clamp)(
			(mousePosition.y - mainViewport->Pos.y) / mainViewport->Size.y, 0.0f, 1.0f);
	}
#endif
	localMember.idleSeconds = 0.0f;
	localMember.isOnline = isOnline;
	presenceElapsedSeconds_ += elapsedSeconds;

	for (auto& memberPair : memberRecords_) {
		if (memberPair.first == userId_) {
			continue;
		}

		memberPair.second.idleSeconds += elapsedSeconds;

		if (memberPair.second.idleSeconds >= kPresenceTimeoutSeconds) {
			memberPair.second.isOnline = false;
		}
	}

	if (!isOnline || presenceElapsedSeconds_ < kPresenceIntervalSeconds) {
		return;
	}

	presenceElapsedSeconds_ = 0.0f;
	std::ostringstream presence;
	presence << "{\"type\":\"presence\",\"userId\":\"" << EscapeJsonText(userId_)
		<< "\",\"userName\":\"" << EscapeJsonText(localMember.userName)
		<< "\",\"panel\":\"" << EscapeJsonText(localMember.panel)
		<< "\",\"action\":\"" << EscapeJsonText(localMember.action)
		<< "\",\"scenePath\":\"" << EscapeJsonText(localMember.scenePath)
		<< "\",\"assetPath\":\"" << EscapeJsonText(localMember.assetPath)
		<< "\",\"objectUuid\":\"" << EscapeJsonText(localMember.objectUuid)
		<< "\",\"componentUuid\":\"" << EscapeJsonText(localMember.componentUuid)
		<< "\",\"propertyName\":\"" << EscapeJsonText(localMember.propertyName)
		<< "\",\"cursorX\":" << localMember.cursorX
		<< ",\"cursorY\":" << localMember.cursorY
		<< ",\"cameraX\":" << localMember.cameraPosition[0]
		<< ",\"cameraY\":" << localMember.cameraPosition[1]
		<< ",\"cameraZ\":" << localMember.cameraPosition[2]
		<< ",\"cameraPitch\":" << localMember.cameraRotation[0]
		<< ",\"cameraYaw\":" << localMember.cameraRotation[1]
		<< ",\"cameraRoll\":" << localMember.cameraRotation[2]
		<< ",\"color\":" << localMember.color
		<< ",\"isPlaying\":" << (localMember.isPlaying ? 1 : 0)
		<< ",\"isBuilding\":" << (localMember.isBuilding ? 1 : 0) << "}";
	QueueOutgoingMessage(presence.str());
}

void EditorTeamCollaborationManager::ProcessPresenceMessage(const std::string& message) {
	const std::string presenceUserId = ReadJsonString(message, "userId");
	const std::string presenceUserName = ReadJsonString(message, "userName");

	if (presenceUserId.empty() || presenceUserId == userId_) {
		return;
	}

	MemberRecord& member = memberRecords_[presenceUserId];
	member.userName = presenceUserName;
	member.panel = ReadJsonString(message, "panel");
	member.action = ReadJsonString(message, "action");
	member.scenePath = ReadJsonString(message, "scenePath");
	member.assetPath = ReadJsonString(message, "assetPath");
	member.objectUuid = ReadJsonString(message, "objectUuid");
	member.componentUuid = ReadJsonString(message, "componentUuid");
	member.propertyName = ReadJsonString(message, "propertyName");
	member.cursorX = ReadJsonFloat(message, "cursorX");
	member.cursorY = ReadJsonFloat(message, "cursorY");
	member.cameraPosition = {
		ReadJsonFloat(message, "cameraX"),
		ReadJsonFloat(message, "cameraY"),
		ReadJsonFloat(message, "cameraZ")};
	member.cameraRotation = {
		ReadJsonFloat(message, "cameraPitch"),
		ReadJsonFloat(message, "cameraYaw"),
		ReadJsonFloat(message, "cameraRoll")};
	member.color = static_cast<std::uint32_t>(ReadJsonUnsigned(message, "color"));
	if (member.color == 0u) member.color = BuildUserColor(presenceUserId);
	member.isPlaying = ReadJsonBool(message, "isPlaying");
	member.isBuilding = ReadJsonBool(message, "isBuilding");
	member.idleSeconds = 0.0f;
	member.isOnline = true;

	if (isHost_) {
		QueueOutgoingMessage(message);
	}
}

void EditorTeamCollaborationManager::UpdateSelectionLock(float deltaTime) {
	std::string currentObjectUuid;
	const int32_t selectedGameObjectId = requestedEditingGameObjectId_;
	const std::string requestedComponentUuid = requestedEditingComponentUuid_;
	requestedEditingGameObjectId_ = -1;
	requestedEditingComponentUuid_.clear();

	if (editorScene_ != nullptr && selectedGameObjectId >= 0) {
		const EditorGameObject* selectedGameObject = editorScene_->FindGameObject(selectedGameObjectId);

		if (selectedGameObject != nullptr) {
			currentObjectUuid = BuildComponentLockKey(
				selectedGameObject->uuid,
				requestedComponentUuid);
		}
	}

	const bool isOnline = GetStatus() == EditorTeamConnectionStatus::Online ||
		GetStatus() == EditorTeamConnectionStatus::Synchronizing;
	lockHeartbeatElapsedSeconds_ += (std::max)(deltaTime, 0.0f);

	if (currentObjectUuid == selectedObjectUuid_) {
		if (!isHost_ && isOnline && lockHeartbeatElapsedSeconds_ >= kLockHeartbeatIntervalSeconds) {
			bool sentAnyLock = false;
			for (const auto& lockPair : lockedByUserId_) {
				if (lockPair.second != userId_) continue;
				const auto mode = lockModes_.find(lockPair.first);
				const auto type = lockTargetTypes_.find(lockPair.first);
				QueueOutgoingMessage(SerializeLockMessage(
					"lockRequest", lockPair.first, userId_, userNameBuffer_.data(),
					mode != lockModes_.end() ? mode->second : EditorTeamLockMode::Hard,
					type != lockTargetTypes_.end() ? type->second : std::string{}, {}));
				sentAnyLock = true;
			}
			if (!sentAnyLock && !selectedObjectUuid_.empty()) {
				QueueOutgoingMessage(SerializeLockMessage(
					"lockRequest", selectedObjectUuid_, userId_, userNameBuffer_.data()));
			}
			lockHeartbeatElapsedSeconds_ = 0.0f;
		}

		return;
	}

	lockHeartbeatElapsedSeconds_ = 0.0f;

	if (!selectedObjectUuid_.empty()) {
		if (isHost_) {
			const auto lockIterator = lockedByUserId_.find(selectedObjectUuid_);

			if (lockIterator != lockedByUserId_.end() && lockIterator->second == userId_) {
				lockedByUserId_.erase(lockIterator);
				lockedByUserName_.erase(selectedObjectUuid_);
				lockModes_.erase(selectedObjectUuid_);
				lockTargetTypes_.erase(selectedObjectUuid_);
				BroadcastLockState(selectedObjectUuid_);
			}
		}
		else if (isOnline) {
			QueueOutgoingMessage(SerializeLockMessage(
				"unlock",
				selectedObjectUuid_,
				userId_,
				userNameBuffer_.data()));
		}
	}

	selectedObjectUuid_ = currentObjectUuid;

	if (selectedObjectUuid_.empty() || !isOnline) {
		return;
	}

	if (isHost_) {
		const auto lockIterator = lockedByUserId_.find(selectedObjectUuid_);

		if (lockIterator == lockedByUserId_.end() || lockIterator->second == userId_) {
			lockedByUserId_[selectedObjectUuid_] = userId_;
			lockedByUserName_[selectedObjectUuid_] = userNameBuffer_.data();
		}

		BroadcastLockState(selectedObjectUuid_);
	}
	else {
		QueueOutgoingMessage(SerializeLockMessage(
			"lockRequest",
			selectedObjectUuid_,
			userId_,
			userNameBuffer_.data()));
	}
}

void EditorTeamCollaborationManager::UpdateLockTimeouts(float deltaTime) {
	if (!isHost_) {
		return;
	}

	const float elapsedSeconds = (std::max)(deltaTime, 0.0f);

	for (auto idleIterator = remoteLockIdleSeconds_.begin();
		idleIterator != remoteLockIdleSeconds_.end();) {
		idleIterator->second += elapsedSeconds;

		if (idleIterator->second < kLockTimeoutSeconds) {
			++idleIterator;
			continue;
		}

		const std::string objectUuid = idleIterator->first;
		lockedByUserId_.erase(objectUuid);
		lockedByUserName_.erase(objectUuid);
		lockModes_.erase(objectUuid);
		lockTargetTypes_.erase(objectUuid);
		idleIterator = remoteLockIdleSeconds_.erase(idleIterator);
		BroadcastLockState(objectUuid);
	}
}

void EditorTeamCollaborationManager::ProcessLockMessage(
	const std::string& message,
	const std::string& messageType) {
	const std::string objectUuid = ReadJsonString(message, "objectUuid");
	const std::string lockUserId = ReadJsonString(message, "userId");
	const std::string lockUserName = ReadJsonString(message, "userName");
	const std::string lockModeText = ReadJsonString(message, "lockMode");
	const EditorTeamLockMode lockMode = lockModeText == "Soft"
		? EditorTeamLockMode::Soft
		: EditorTeamLockMode::Hard;
	const std::string targetType = ReadJsonString(message, "targetType");

	if (objectUuid.empty()) {
		return;
	}

	if (messageType == "lockState") {
		if (lockUserId.empty()) {
			lockedByUserId_.erase(objectUuid);
			lockedByUserName_.erase(objectUuid);
			lockModes_.erase(objectUuid);
			lockTargetTypes_.erase(objectUuid);
		}
		else {
			lockedByUserId_[objectUuid] = lockUserId;
			lockedByUserName_[objectUuid] = lockUserName;
			lockModes_[objectUuid] = lockMode;
			lockTargetTypes_[objectUuid] = targetType;
		}

		return;
	}

	if (!isHost_) {
		return;
	}

	if (messageType == "lockRequest") {
		const auto lockIterator = lockedByUserId_.find(objectUuid);

		if (lockIterator == lockedByUserId_.end() || lockIterator->second == lockUserId) {
			lockedByUserId_[objectUuid] = lockUserId;
			lockedByUserName_[objectUuid] = lockUserName;
			lockModes_[objectUuid] = lockMode;
			lockTargetTypes_[objectUuid] = targetType;
			remoteLockIdleSeconds_[objectUuid] = 0.0f;
		}

		BroadcastLockState(objectUuid);
	}
	else if (messageType == "unlock") {
		const auto lockIterator = lockedByUserId_.find(objectUuid);

		if (lockIterator != lockedByUserId_.end() && lockIterator->second == lockUserId) {
			lockedByUserId_.erase(lockIterator);
			lockedByUserName_.erase(objectUuid);
			remoteLockIdleSeconds_.erase(objectUuid);
			lockModes_.erase(objectUuid);
			lockTargetTypes_.erase(objectUuid);
		}

		BroadcastLockState(objectUuid);
	}
}

void EditorTeamCollaborationManager::BroadcastLockState(const std::string& objectUuid) {
	const auto lockIterator = lockedByUserId_.find(objectUuid);
	const std::string lockUserId = lockIterator != lockedByUserId_.end()
		? lockIterator->second
		: std::string{};
	const auto userNameIterator = lockedByUserName_.find(objectUuid);
	const std::string lockUserName = userNameIterator != lockedByUserName_.end()
		? userNameIterator->second
		: std::string{};
	const auto modeIterator = lockModes_.find(objectUuid);
	const EditorTeamLockMode lockMode = modeIterator != lockModes_.end()
		? modeIterator->second
		: EditorTeamLockMode::Hard;
	const auto targetTypeIterator = lockTargetTypes_.find(objectUuid);
	const std::string targetType = targetTypeIterator != lockTargetTypes_.end()
		? targetTypeIterator->second
		: std::string{};
	QueueOutgoingMessage(SerializeLockMessage(
		"lockState",
		objectUuid,
		lockUserId,
		lockUserName,
		lockMode,
		targetType,
		{}));
}

bool EditorTeamCollaborationManager::ApplyAssetChange(
	const EditorTeamChangeEvent& changeEvent) {
	std::filesystem::path assetPath;

	if (!ResolveSafeAssetPath(changeEvent.scenePath, assetPath)) {
		lastError_ = "安全でないAsset Pathを拒否しました: " + changeEvent.scenePath;
		return false;
	}

	std::error_code fileError;
	const bool assetExists = std::filesystem::exists(assetPath, fileError);

	if (assetExists && changeEvent.operation != "DeleteAsset" &&
		AssetManager::Get().GetHash(assetPath.generic_string()) == changeEvent.assetHash) {
		return true;
	}

	if (assetExists) {
		const std::filesystem::path backupPath =
			std::filesystem::path(".team/backups/assets") /
			("Revision_" + std::to_string(currentRevision_)) /
			assetPath;
		std::filesystem::create_directories(backupPath.parent_path(), fileError);
		std::filesystem::copy_file(
			assetPath,
			backupPath,
			std::filesystem::copy_options::overwrite_existing,
			fileError);
	}

	fileError.clear();

	if (changeEvent.operation == "DeleteAsset") {
		if (assetExists) {
			const std::filesystem::path trashPath =
				std::filesystem::path(".team/trash") /
				("Revision_" + std::to_string(currentRevision_)) /
				assetPath;
			std::filesystem::create_directories(trashPath.parent_path(), fileError);
			std::filesystem::rename(assetPath, trashPath, fileError);

			if (fileError) {
				fileError.clear();
				std::filesystem::copy_file(
					assetPath,
					trashPath,
					std::filesystem::copy_options::overwrite_existing,
					fileError);

				if (!fileError) {
					std::filesystem::remove(assetPath, fileError);
				}
			}
		}

		assetRecords_.erase(changeEvent.scenePath);
		// 種別ごとのCache無効化はAssetManager(Source/Engine/Asset)側のAdapterへ集約した。
		// ここでは「変更があった」ことだけ伝え、反映できるかどうかの判断はAssetManagerに任せる。
		const AssetNotifyResult notifyResult = AssetManager::Get().NotifyFileChanged(changeEvent.scenePath);

		if (notifyResult.result != AssetReloadResult::Applied) {
			AddConsoleMessage("Team Asset: " + changeEvent.scenePath + " ／" + notifyResult.reason);
		}

		return !fileError;
	}

	const std::string assetContent = DecodeBase64(changeEvent.snapshotData);

	if (assetContent.size() != changeEvent.fileSize ||
		FormatHash(CalculateTextHash(assetContent)) != changeEvent.assetHash) {
		lastError_ = "AssetのSizeまたはHashが一致しません: " + changeEvent.scenePath;
		return false;
	}

	std::filesystem::create_directories(assetPath.parent_path(), fileError);
	std::ofstream assetFile(assetPath, std::ios::binary | std::ios::trunc);

	if (!assetFile.is_open()) {
		lastError_ = "Assetを書き込めません: " + changeEvent.scenePath;
		return false;
	}

	assetFile.write(assetContent.data(), static_cast<std::streamsize>(assetContent.size()));
	assetFile.close();

	if (!assetFile.good()) {
		lastError_ = "Assetの書き込みに失敗しました: " + changeEvent.scenePath;
		return false;
	}

	AssetRecord assetRecord{};
	assetRecord.uuid = changeEvent.objectUuid;
	assetRecord.hash = changeEvent.assetHash;
	assetRecord.fileSize = changeEvent.fileSize;
	assetRecord.lastWriteTimestamp = static_cast<std::int64_t>(
		std::filesystem::last_write_time(assetPath, fileError).time_since_epoch().count());
	assetRecord.textContent = IsScriptAssetPath(changeEvent.scenePath)
		? assetContent
		: std::string{};
	assetRecords_[changeEvent.scenePath] = std::move(assetRecord);

	// 種別ごとのCache無効化・DLL差し替え可否の判断はAssetManager(Source/Engine/Asset)側の
	// Adapterへ集約した。共同制作側は「変更があった」ことだけを伝える。
	const AssetNotifyResult notifyResult = AssetManager::Get().NotifyFileChanged(changeEvent.scenePath);

	if (notifyResult.result == AssetReloadResult::Applied) {
		AddConsoleMessage("Team Asset: " + changeEvent.operation + " " + changeEvent.scenePath);
	}
	else {
		AddConsoleMessage("Team Asset: " + changeEvent.scenePath + " ／" + notifyResult.reason);
	}

	return true;
}

bool EditorTeamCollaborationManager::MergeScriptConflict(
	const EditorTeamChangeEvent& changeEvent) {
	if (!IsScriptAssetPath(changeEvent.scenePath)) {
		return false;
	}

	std::filesystem::path scriptPath;

	if (!ResolveSafeAssetPath(changeEvent.scenePath, scriptPath)) {
		return false;
	}

	std::string localText;
	ReadBinaryTextFile(scriptPath, localText);
	std::string mergedText;
	const auto localChangeIterator = conflictLocalChanges_.find(changeEvent.changeId);
	const std::string baseText = localChangeIterator != conflictLocalChanges_.end()
		? localChangeIterator->second.oldValue
		: changeEvent.oldValue;
	if (!TryThreeWayMergeLines(baseText, localText, changeEvent.newValue, mergedText)) {
		lastError_ = "同じ行が変更されています。Base / Server / Localを確認して手動編集してください";
		return false;
	}

	EditorTeamChangeEvent mergedChange = changeEvent;
	mergedChange.changeId = CreateEditorTeamUuid();
	mergedChange.userId = userId_;
	mergedChange.userName = userNameBuffer_.data();
	mergedChange.operation = "UpdateAsset";
	mergedChange.oldValue = localText;
	mergedChange.newValue = mergedText;
	mergedChange.snapshotData = EncodeBase64(mergedText);
	mergedChange.fileSize = static_cast<std::uint64_t>(mergedText.size());
	mergedChange.assetHash = FormatHash(CalculateTextHash(mergedText));
	mergedChange.baseRevision = currentRevision_;

	if (!ApplyAssetChange(mergedChange)) {
		return false;
	}

	if (localChangeIterator != conflictLocalChanges_.end()) {
		const std::string localChangeId = localChangeIterator->second.changeId;
		unsyncedChanges_.erase(
			std::remove_if(unsyncedChanges_.begin(), unsyncedChanges_.end(),
				[&localChangeId](const EditorTeamChangeEvent& pending) {
					return pending.changeId == localChangeId;
				}),
			unsyncedChanges_.end());
		conflictLocalChanges_.erase(localChangeIterator);
	}
	QueueLocalChange(std::move(mergedChange));
	return true;
}

bool EditorTeamCollaborationManager::ApplySceneSnapshot(
	const EditorTeamChangeEvent& changeEvent) {
	if (editorScene_ == nullptr || changeEvent.snapshotData.empty()) {
		return false;
	}

	isApplyingRemoteChange_ = true;
	const std::filesystem::path backupPath =
		std::filesystem::path(".team/backups") /
		("BeforeMerge_Revision_" + std::to_string(currentRevision_) + ".scene");
	std::error_code directoryError;
	std::filesystem::create_directories(backupPath.parent_path(), directoryError);
	editorScene_->SaveScene(backupPath.generic_string());
	std::error_code backupSizeError;
	const std::uint64_t backupSize = std::filesystem::exists(backupPath, backupSizeError)
		? static_cast<std::uint64_t>(std::filesystem::file_size(backupPath, backupSizeError))
		: 0u;
	std::ostringstream backupMetadata;
	backupMetadata
		<< "{\"targetScene\":\"" << EscapeJsonText(changeEvent.scenePath)
		<< "\",\"editor\":\"" << EscapeJsonText(changeEvent.userName)
		<< "\",\"timestamp\":" << GetCurrentUnixTimestampMilliseconds()
		<< ",\"beforeSize\":" << backupSize << "}";
	WriteUtf8BomTextFile(backupPath.generic_string() + ".meta", backupMetadata.str());

	std::ofstream incomingFile(kTeamIncomingSnapshotPath, std::ios::binary | std::ios::trunc);

	if (!incomingFile.is_open()) {
		isApplyingRemoteChange_ = false;
		return false;
	}

	incomingFile.write(
		changeEvent.snapshotData.data(),
		static_cast<std::streamsize>(changeEvent.snapshotData.size()));
	incomingFile.close();
	EditorScene incomingScene;

	if (!incomingScene.LoadScene(kTeamIncomingSnapshotPath)) {
		isApplyingRemoteChange_ = false;
		lastError_ = "受信Sceneを読み込めません";
		return false;
	}

	const bool requiresFullSceneReplacement = changeEvent.property == "SceneData";
	const bool isApplied = requiresFullSceneReplacement ||
		editorScene_->ApplyCollaborationChange(
			incomingScene,
			changeEvent.operation,
			changeEvent.objectUuid,
			changeEvent.componentUuid,
			changeEvent.property);

	if (!isApplied) {
		isApplyingRemoteChange_ = false;
		lastError_ = "受信したUUID差分をSceneへ適用できません";
		return false;
	}

	if (requiresFullSceneReplacement) {
		*editorScene_ = std::move(incomingScene);
	}

	editorScene_->EnsurePersistentUuids();
	editorScene_->SaveScene(kTeamLiveSnapshotPath);
	ReadBinaryTextFile(kTeamLiveSnapshotPath, lastSnapshotText_);
	lastSnapshotHash_ = CalculateTextHash(lastSnapshotText_);
	isApplyingRemoteChange_ = false;
	AddConsoleMessage("Team: Revision " + std::to_string(changeEvent.revision) + " を反映しました");
	return true;
}

void EditorTeamCollaborationManager::QueueOutgoingMessage(
	const std::string& message,
	bool isTransferChunk,
	std::uint64_t transferRawByteShare) {
	if (networkState_->transport == nullptr) {
		return;
	}

	// Transportは改行区切りでMessageを区切るため、ここで終端を付ける。
	CG2Collaboration::TransportMessage transportMessage{};
	transportMessage.text = message;

	if (transportMessage.text.empty() || transportMessage.text.back() != '\n') {
		transportMessage.text += '\n';
	}

	transportMessage.isTransferChunk = isTransferChunk;
	transportMessage.transferRawByteShare = transferRawByteShare;
	networkState_->transport->Send(transportMessage);
}

void EditorTeamCollaborationManager::QueueChangeEventMessage(
	const EditorTeamChangeEvent& changeEvent,
	const char* messageType) {
	QueueChangeEventMessage(changeEvent, messageType, {});
}

void EditorTeamCollaborationManager::QueueChangeEventMessage(
	const EditorTeamChangeEvent& changeEvent,
	const char* messageType,
	const std::string& targetUserId) {
	if (changeEvent.snapshotData.size() <= kChunkedTransferThresholdBytes) {
		QueueOutgoingMessage(SetTargetUserId(
			SerializeChangeEvent(changeEvent, messageType), targetUserId));
		return;
	}

	// snapshotDataはAssetの場合すでにBase64、Sceneの場合は生テキストなので、
	// 分割送信の前提を揃えるためにここで一度Base64化する。受信側は再構成後に1回だけ
	// DecodeBase64すれば、元のsnapshotDataとまったく同じ内容へ戻る。
	const std::string transportPayload = EncodeBase64(changeEvent.snapshotData);
	const std::uint32_t chunkCount = static_cast<std::uint32_t>(
		(transportPayload.size() + kChunkTransportPayloadLength - 1u) /
		kChunkTransportPayloadLength);
	const std::uint64_t rawByteCount = changeEvent.fileSize != 0u
		? changeEvent.fileSize
		: static_cast<std::uint64_t>(changeEvent.snapshotData.size());

	{
		networkState_->outgoingTransferSentBytes.store(0u, std::memory_order_release);
		networkState_->outgoingTransferTotalBytes.store(rawByteCount, std::memory_order_release);
		std::lock_guard<std::mutex> labelLock(networkState_->transferLabelMutex);
		networkState_->outgoingTransferLabel =
			changeEvent.scenePath.empty() ? std::string("Scene同期") : changeEvent.scenePath;
	}

	QueueOutgoingMessage(SetTargetUserId(
		SerializeAssetTransferBegin(
			changeEvent,
			messageType,
			chunkCount,
			static_cast<std::uint64_t>(transportPayload.size()),
			rawByteCount),
		targetUserId));

	for (std::uint32_t chunkIndex = 0u; chunkIndex < chunkCount; chunkIndex++) {
		const std::size_t chunkStart =
			static_cast<std::size_t>(chunkIndex) * kChunkTransportPayloadLength;
		const std::size_t chunkLength = (std::min)(
			kChunkTransportPayloadLength,
			transportPayload.size() - chunkStart);
		const std::uint64_t chunkRawByteShare = transportPayload.empty()
			? 0u
			: static_cast<std::uint64_t>(
				(static_cast<double>(chunkLength) / static_cast<double>(transportPayload.size())) *
				static_cast<double>(rawByteCount));
		QueueOutgoingMessage(
			SetTargetUserId(SerializeAssetTransferChunk(
				changeEvent.changeId,
				chunkIndex,
				transportPayload.substr(chunkStart, chunkLength)), targetUserId),
			true,
			chunkRawByteShare);
	}

	QueueOutgoingMessage(SetTargetUserId(
		SerializeAssetTransferEnd(changeEvent.changeId), targetUserId));
}

void EditorTeamCollaborationManager::QueueCurrentSceneSnapshotForUser(
	const std::string& targetUserId) {
	if (editorScene_ == nullptr || targetUserId.empty()) {
		return;
	}

	const std::string sharedSceneFolder = sharedSceneFolderBuffer_.data();
	if (!IsPathInsideSharedSceneFolder(EditorSharedState::g_currentScenePath, sharedSceneFolder)) {
		return;
	}

	editorScene_->EnsurePersistentUuids();
	const std::filesystem::path snapshotPath(kTeamLiveSnapshotPath);
	std::error_code directoryError;
	std::filesystem::create_directories(snapshotPath.parent_path(), directoryError);
	if (directoryError || !editorScene_->SaveScene(snapshotPath.generic_string())) {
		lastError_ = "途中参加用のScene Snapshotを保存できません";
		return;
	}

	std::string snapshotText;
	if (!ReadBinaryTextFile(snapshotPath, snapshotText)) {
		lastError_ = "途中参加用のScene Snapshotを読み込めません";
		return;
	}

	EditorTeamChangeEvent snapshotEvent{};
	snapshotEvent.changeId = CreateEditorTeamUuid();
	snapshotEvent.userId = userId_;
	snapshotEvent.userName = userNameBuffer_.data();
	snapshotEvent.scenePath = EditorSharedState::g_currentScenePath;
	snapshotEvent.sceneUuid = editorScene_->GetUuid();
	snapshotEvent.operation = "SetProperty";
	snapshotEvent.property = "SceneData";
	snapshotEvent.newValue = std::to_string(CalculateTextHash(snapshotText));
	snapshotEvent.snapshotData = std::move(snapshotText);
	snapshotEvent.fileSize = static_cast<std::uint64_t>(snapshotEvent.snapshotData.size());
	snapshotEvent.timestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	snapshotEvent.baseRevision = currentRevision_;
	snapshotEvent.revision = currentRevision_;
	QueueChangeEventMessage(snapshotEvent, "commit", targetUserId);
}

void EditorTeamCollaborationManager::StartNetworkThread(bool startsAsServer) {
	StopNetworkThread();
	acceptedPeerUserIds_.clear();
	pendingJoinCatchUpUserIds_.clear();
	compatibilityHelloSent_ = false;
	compatibilityAccepted_ = false;
	handshakeAccepted_ = false;
	heartbeatElapsedSeconds_ = 0.0f;
	networkState_->isServer.store(startsAsServer, std::memory_order_release);

	// 通信経路はTransportへ委譲する。LANでもTailscale越しでも同じ実装を使うため、
	// ここから下のScene同期・Lock・Conflict処理は接続方式を一切意識しない。
	networkState_->transport = std::make_unique<CG2Collaboration::TcpCollaborationTransport>();

	CG2Collaboration::TransportConfig transportConfig{};
	transportConfig.maximumClientCount = maximumClientCount_;
	transportConfig.maximumReceiveBufferBytes = kMaximumNetworkBufferBytes;

	std::string transportError;
	bool hasStarted = false;

	if (startsAsServer) {
		hasStarted = networkState_->transport->Listen(port_, transportConfig, transportError);
	}
	else {
		CG2Collaboration::TransportEndpoint endpoint{};
		// Host名でもIPv4/IPv6でもよい。Tailscale MagicDNS hostname はここへそのまま渡る。
		endpoint.host = hostAddressBuffer_.data();
		endpoint.port = port_;
		hasStarted = networkState_->transport->Connect(endpoint, transportConfig, transportError);
	}

	if (!hasStarted) {
		lastError_ = transportError;
		AddConsoleMessage("Team: " + transportError);
		networkState_->transport.reset();
		networkState_->status.store(EditorTeamConnectionStatus::Disconnected, std::memory_order_release);
		return;
	}

	networkState_->status.store(EditorTeamConnectionStatus::Connecting, std::memory_order_release);
}

// TransportのStateを、既存のUI/上位ロジックが使う接続状態へ写す。
// Socketのエラー番号ではなく、利用者が判断できる状態だけを見せる。
void EditorTeamCollaborationManager::SynchronizeTransportStatus() {
	if (networkState_->transport == nullptr) {
		return;
	}

	const CG2Collaboration::TransportState transportState = networkState_->transport->GetState();
	networkState_->memberCount.store(
		networkState_->transport->GetPeerCount(), std::memory_order_release);

	const std::string transportError = networkState_->transport->GetLastError();

	if (!transportError.empty()) {
		lastError_ = transportError;
	}

	const EditorTeamConnectionStatus previousStatus =
		networkState_->status.load(std::memory_order_acquire);

	// Handshake拒否やConflictは上位が決めた状態なので、Transportの都合で上書きしない。
	if (previousStatus == EditorTeamConnectionStatus::Incompatible ||
		previousStatus == EditorTeamConnectionStatus::Conflict) {
		if (transportState == CG2Collaboration::TransportState::Disconnected) {
			networkState_->status.store(
				EditorTeamConnectionStatus::Disconnected, std::memory_order_release);
		}

		return;
	}

	EditorTeamConnectionStatus nextStatus = previousStatus;

	switch (transportState) {
	case CG2Collaboration::TransportState::Disconnected:
		nextStatus = EditorTeamConnectionStatus::Offline;
		break;
	case CG2Collaboration::TransportState::Connecting:
		nextStatus = EditorTeamConnectionStatus::Connecting;
		break;
	case CG2Collaboration::TransportState::Reconnecting:
		nextStatus = EditorTeamConnectionStatus::Reconnecting;
		break;
	case CG2Collaboration::TransportState::Error:
		nextStatus = EditorTeamConnectionStatus::Disconnected;
		break;
	case CG2Collaboration::TransportState::Listening:
		// Serverは相手が居なくてもOnline扱い(待ち受け成功)。
		nextStatus = EditorTeamConnectionStatus::Online;
		break;
	case CG2Collaboration::TransportState::Connected:
		// Handshakeが通るまではSynchronizing表示のままにし、
		// 互換性未確認の状態をOnlineと誤解させない。
		nextStatus = handshakeAccepted_ && !historySyncInProgress_
			? EditorTeamConnectionStatus::Online
			: EditorTeamConnectionStatus::Synchronizing;
		break;
	default:
		break;
	}

	// 切断されたらHandshakeをやり直す。再接続後は改めて互換性とProjectを確認する。
	const bool lostConnection =
		transportState == CG2Collaboration::TransportState::Reconnecting ||
		transportState == CG2Collaboration::TransportState::Connecting;

	if (lostConnection && handshakeAccepted_) {
		handshakeAccepted_ = false;
		historySyncInProgress_ = false;
		compatibilityHelloSent_ = false;
		compatibilityAccepted_ = false;
		AddConsoleMessage("Team: 接続が切れました。再接続後にRevisionを確認します");
	}

	networkState_->status.store(nextStatus, std::memory_order_release);
}


void EditorTeamCollaborationManager::StopNetworkThread() {
	networkState_->stopsRequested.store(true, std::memory_order_release);

	if (networkState_->transport != nullptr) {
		networkState_->transport->Disconnect();
		networkState_->transport.reset();
	}

	{
		std::lock_guard<std::mutex> queueLock(networkState_->queueMutex);
		networkState_->incomingMessages.clear();
	}

	networkState_->memberCount.store(0, std::memory_order_release);
	networkState_->status.store(EditorTeamConnectionStatus::Offline, std::memory_order_release);
	wasConnected_ = false;
	handshakeAccepted_ = false;
	acceptedPeerUserIds_.clear();
	pendingJoinCatchUpUserIds_.clear();
}

void EditorTeamCollaborationManager::AddConsoleMessage(const std::string& message) const {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}

void EditorTeamCollaborationManager::Draw(bool* isWindowVisible) {
#ifdef USE_IMGUI
	// Remote cursorはTEAM Windowを閉じていてもEditor全体へ表示する。
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	if (mainViewport != nullptr) {
		ImDrawList* foreground = ImGui::GetForegroundDrawList(mainViewport);
		for (const auto& memberPair : memberRecords_) {
			if (memberPair.first == userId_ || !memberPair.second.isOnline) continue;
			const MemberRecord& member = memberPair.second;
			const ImVec2 cursor{
				mainViewport->Pos.x + member.cursorX * mainViewport->Size.x,
				mainViewport->Pos.y + member.cursorY * mainViewport->Size.y};
			const ImU32 color = member.color != 0u ? member.color : BuildUserColor(memberPair.first);
			foreground->AddTriangleFilled(
				cursor, ImVec2(cursor.x + 13.0f, cursor.y + 5.0f),
				ImVec2(cursor.x + 5.0f, cursor.y + 14.0f), color);
			foreground->AddText(ImVec2(cursor.x + 15.0f, cursor.y + 8.0f), color, member.userName.c_str());
		}
	}

	if (isWindowVisible == nullptr || !*isWindowVisible) {
		return;
	}

	if (!ImGui::Begin("TEAM - 共同制作", isWindowVisible, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::Text("状態: %s", GetStatusText(GetStatus()));

	// Connection Diagnostics: Socketのエラー番号ではなく、利用者が判断できる情報だけを出す。
	// Tailscale固有の情報は表示しない(Engineから見れば経路の違いでしかないため)。
	if (ImGui::CollapsingHeader("接続診断", ImGuiTreeNodeFlags_DefaultOpen)) {
		const std::string serverDescription = networkState_->transport != nullptr
			? networkState_->transport->GetDescription()
			: std::string(hostAddressBuffer_.data()) + ":" + std::to_string(port_);
		ImGui::Text("Server   %s", serverDescription.c_str());
		ImGui::Text("状態     %s", GetStatusText(GetStatus()));

		if (latencyMilliseconds_ > 0.0f) {
			ImGui::Text("遅延     %.0f ms", latencyMilliseconds_);
		}
		else {
			ImGui::TextDisabled("遅延     (未計測)");
		}

		ImGui::Text("Protocol %u", CG2Collaboration::kCollaborationProtocolVersion);
		ImGui::Text("Engine   %s", GetCG2EngineDisplayVersion().c_str());
		ImGui::Text("Project  %s", projectIdBuffer_.data());
		ImGui::Text("役割     %s", isHost_ ? "Host(主催)" : "Client(参加)");
		ImGui::Text("接続人数 %d", networkState_->memberCount.load(std::memory_order_acquire));
		ImGui::Text("Revision %llu", static_cast<unsigned long long>(currentRevision_));

		if (lastSyncUnixMilliseconds_ != 0u) {
			const std::time_t lastSyncTime =
				static_cast<std::time_t>(lastSyncUnixMilliseconds_ / 1000u);
			std::tm lastSyncLocalTime{};
			localtime_s(&lastSyncLocalTime, &lastSyncTime);
			char lastSyncText[32] = {};
			std::strftime(lastSyncText, sizeof(lastSyncText), "%H:%M:%S", &lastSyncLocalTime);
			ImGui::Text("最終同期 %s", lastSyncText);
		}
		else {
			ImGui::TextDisabled("最終同期 (なし)");
		}

		if (!lastError_.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", lastError_.c_str());
		}
	}
	ProjectVersionSettings displayedProjectSettings = ProjectVersionManager::CreateCurrentDefaults();
	std::string displayedVersionError;
	ProjectVersionManager::Load(
		std::filesystem::current_path(), displayedProjectSettings, displayedVersionError);
	ImGui::Text("Engine %s", GetCG2EngineDisplayVersion().c_str());
	ImGui::SameLine();
	ImGui::Text("/ Project Format %u", displayedProjectSettings.projectFormatVersion);
	ImGui::SameLine();
	ImGui::Text("/ Script API %u", GetCG2ScriptApiVersion());
	ImGui::SameLine();
	ImGui::Text("/ Channel %s", GetEngineUpdateChannelText(displayedProjectSettings.updateChannel));
	ImGui::Text("Revision: %llu", static_cast<unsigned long long>(currentRevision_));
	int32_t connectedMemberCount = 0;

	for (const auto& memberPair : memberRecords_) {
		if (memberPair.second.isOnline) {
			connectedMemberCount++;
		}
	}

	ImGui::Text("メンバー数: %d", connectedMemberCount);
	ImGui::Text("未同期の変更: %llu", static_cast<unsigned long long>(unsyncedChanges_.size()));
	ImGui::Text(
		"競合: %llu",
		static_cast<unsigned long long>(conflicts_.size() + teamItemConflicts_.size()));
	ImGui::Text("編集Lock: %llu", static_cast<unsigned long long>(lockedByUserId_.size()));
	ImGui::SeparatorText("メンバー");

	for (const auto& memberPair : memberRecords_) {
		const MemberRecord& memberRecord = memberPair.second;
		const char* memberStatus = memberPair.first == userId_
			? GetStatusText(GetStatus())
			: memberRecord.isOnline
				? "オンライン"
				: "オフライン";
		ImGui::PushID(memberPair.first.c_str());
		const ImVec4 memberColor = ImGui::ColorConvertU32ToFloat4(
			memberRecord.color != 0u ? memberRecord.color : BuildUserColor(memberPair.first));
		ImGui::TextColored(memberColor, "●");
		ImGui::SameLine();
		ImGui::Text("%s - %s", memberRecord.userName.c_str(), memberStatus);
		if (!memberRecord.panel.empty() || !memberRecord.action.empty()) {
			ImGui::TextDisabled("  %s / %s%s%s",
				memberRecord.panel.empty() ? "Editor" : memberRecord.panel.c_str(),
				memberRecord.action.empty() ? "閲覧中" : memberRecord.action.c_str(),
				memberRecord.isPlaying ? " / Play中" : "",
				memberRecord.isBuilding ? " / Build中" : "");
		}
		if (!memberRecord.scenePath.empty()) ImGui::TextDisabled("  Scene: %s", memberRecord.scenePath.c_str());
		if (!memberRecord.assetPath.empty() && EditorAssetUtility::HasExtension(memberRecord.assetPath, ".prefab")) {
			ImGui::TextDisabled("  Prefab: %s", memberRecord.assetPath.c_str());
		}
		if (!memberRecord.objectUuid.empty()) ImGui::TextDisabled("  選択: %s", memberRecord.objectUuid.c_str());
		if (!memberRecord.componentUuid.empty()) {
			std::string componentLabel = memberRecord.componentUuid;
			if (editorScene_ != nullptr) {
				for (const EditorGameObject& object : editorScene_->GetGameObjects()) {
					if (object.uuid != memberRecord.objectUuid) continue;
					for (const EditorComponent& component : object.components) {
						if (component.uuid == memberRecord.componentUuid) {
							componentLabel = ToString(component.type);
							break;
						}
					}
					break;
				}
			}
			ImGui::TextDisabled("  Component: %s", componentLabel.c_str());
		}
		if (!memberRecord.propertyName.empty()) ImGui::TextDisabled("  Property: %s", memberRecord.propertyName.c_str());
		if (memberPair.first != userId_ && memberRecord.isOnline) {
			if (ImGui::SmallButton("対象へジャンプ")) {
				if (!memberRecord.objectUuid.empty()) JumpToTarget("GameObject", memberRecord.objectUuid);
				else if (!memberRecord.assetPath.empty()) JumpToTarget("Asset", memberRecord.assetPath);
				else JumpToTarget("Scene", memberRecord.scenePath);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("視点へジャンプ")) {
				EditorSharedState::g_cameraTransform.translate = {
					memberRecord.cameraPosition[0], memberRecord.cameraPosition[1], memberRecord.cameraPosition[2]};
				EditorSharedState::g_cameraTransform.rotate = {
					memberRecord.cameraRotation[0], memberRecord.cameraRotation[1], memberRecord.cameraRotation[2]};
			}
			ImGui::SameLine();
			const bool isFollowing = followUserId_ == memberPair.first;
			if (ImGui::SmallButton(isFollowing ? "Follow停止" : "Follow")) {
				followUserId_ = isFollowing ? std::string{} : memberPair.first;
			}
		}
		ImGui::PopID();
	}

	if (!notificationItemIds_.empty()) {
		ImGui::SeparatorText("通知");
		for (const std::string& itemId : notificationItemIds_) {
			const auto itemIterator = teamItems_.find(itemId);
			if (itemIterator == teamItems_.end()) continue;
			const TeamItem& item = itemIterator->second;
			ImGui::PushID(("notification-" + itemId).c_str());
			ImGui::TextWrapped("%s: %s", item.creatorUserName.c_str(), item.text.c_str());
			if (ImGui::SmallButton("TeamItemを開く")) {
				focusedTeamItemTargetType_ = item.targetType;
				focusedTeamItemTargetId_ = item.targetId;
				selectedTeamItemId_ = item.id;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("ここを見て")) JumpToTarget(item.targetType, item.targetId);
			ImGui::SameLine();
			if (ImGui::SmallButton("既読")) {
				notificationItemIds_.erase(
					std::remove(notificationItemIds_.begin(), notificationItemIds_.end(), itemId),
					notificationItemIds_.end());
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
	}

	if (ImGui::CollapsingHeader("付箋・Ping・チャット・レビュー", ImGuiTreeNodeFlags_DefaultOpen)) {
		constexpr const char* kindLabels[] = {"付箋", "Ping", "チャット", "レビュー"};
		constexpr const char* kindValues[] = {"Note", "Ping", "Chat", "Review"};
		constexpr const char* targetLabels[] = {
			"対象なし", "Scene座標", "GameObject", "Component", "Property",
			"Prefab", "Asset", "Script", "Scriptコード行", "変更履歴", "ChangeEvent"};
		constexpr const char* targetValues[] = {
			"None", "ScenePosition", "GameObject", "Component", "Property",
			"Prefab", "Asset", "Script", "ScriptLine", "History", "ChangeEvent"};

		if (!focusedTeamItemTargetType_.empty()) {
			ImGui::TextColored(
				ImVec4(0.45f, 0.8f, 1.0f, 1.0f),
				"対象で絞り込み: %s / %s",
				focusedTeamItemTargetType_.c_str(),
				focusedTeamItemTargetId_.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("絞り込み解除")) {
				focusedTeamItemTargetType_.clear();
				focusedTeamItemTargetId_.clear();
			}
		}
		ImGui::Combo("種類", &collaborationKindIndex_, kindLabels, static_cast<int32_t>(std::size(kindLabels)));
		ImGui::Combo("対象", &collaborationTargetIndex_, targetLabels, static_cast<int32_t>(std::size(targetLabels)));

		std::string selectedObjectUuid;
		if (editorScene_ != nullptr && EditorSharedState::g_selectedEditorGameObjectId >= 0) {
			const EditorGameObject* selectedObject = editorScene_->FindGameObject(
				EditorSharedState::g_selectedEditorGameObjectId);
			if (selectedObject != nullptr) selectedObjectUuid = selectedObject->uuid;
		}
		std::string targetType = targetValues[collaborationTargetIndex_];
		std::string targetId;
		if (targetType == "None") targetId.clear();
		else if (targetType == "ScenePosition") targetId = EditorSharedState::g_currentScenePath;
		else if (targetType == "GameObject") targetId = selectedObjectUuid;
		else if (targetType == "Component") {
			targetId = selectedObjectUuid;
			if (!activityComponentUuid_.empty()) targetId += "#component:" + activityComponentUuid_;
		}
		else if (targetType == "Property") {
			targetId = selectedObjectUuid;
			if (!activityComponentUuid_.empty()) targetId += "#component:" + activityComponentUuid_;
			if (!activityPropertyName_.empty()) targetId += "#property:" + activityPropertyName_;
		}
		else if (targetType == "History" || targetType == "ChangeEvent") {
			targetId = selectedHistoryTargetId_;
		}
		else targetId = EditorSharedState::g_selectedAssetPath;

		if (!focusedTeamItemTargetType_.empty()) {
			targetType = focusedTeamItemTargetType_;
			targetId = focusedTeamItemTargetId_;
		}
		if (!selectedTeamItemId_.empty() && !selectedHistoryTargetType_.empty()) {
			targetType = selectedHistoryTargetType_;
			targetId = selectedHistoryTargetId_;
		}

		if (targetType == "Script" || targetType == "ScriptLine") {
			if (scriptTargetPathBuffer_[0] == '\0' && !targetId.empty()) {
				strncpy_s(scriptTargetPathBuffer_.data(), scriptTargetPathBuffer_.size(), targetId.c_str(), _TRUNCATE);
			}
			ImGui::InputText("Script Path", scriptTargetPathBuffer_.data(), scriptTargetPathBuffer_.size());
			targetId = scriptTargetPathBuffer_.data();
			if (targetType == "ScriptLine") {
				ImGui::InputInt("コード行", &scriptTargetLine_);
				scriptTargetLine_ = (std::max)(scriptTargetLine_, 1);
				ImGui::InputText("関数名", functionNameBuffer_.data(), functionNameBuffer_.size());
				ImGui::InputText("周辺コード", codeContextBuffer_.data(), codeContextBuffer_.size());
			}
		}
		ImGui::TextWrapped("添付対象: %s", targetId.empty() ? "未選択" : targetId.c_str());

		if (collaborationKindIndex_ == 0) {
			const char* assigneePreview = "担当者なし";
			if (!selectedAssigneeUserId_.empty()) {
				const auto assignee = memberRecords_.find(selectedAssigneeUserId_);
				if (assignee != memberRecords_.end()) assigneePreview = assignee->second.userName.c_str();
			}
			if (ImGui::BeginCombo("担当者", assigneePreview)) {
				if (ImGui::Selectable("担当者なし", selectedAssigneeUserId_.empty())) selectedAssigneeUserId_.clear();
				for (const auto& memberPair : memberRecords_) {
					if (ImGui::Selectable(memberPair.second.userName.c_str(), selectedAssigneeUserId_ == memberPair.first)) {
						selectedAssigneeUserId_ = memberPair.first;
					}
				}
				ImGui::EndCombo();
			}
		}
		static bool keepsPingHistory = true;
		if (collaborationKindIndex_ == 1) ImGui::Checkbox("Pingを履歴に残す", &keepsPingHistory);
		if (!replyToTeamItemId_.empty()) {
			ImGui::TextDisabled("返信先: %s", replyToTeamItemId_.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("返信解除")) replyToTeamItemId_.clear();
		}
		ImGui::InputTextMultiline(
			"本文", collaborationTextBuffer_.data(), collaborationTextBuffer_.size(), ImVec2(-1.0f, 72.0f));
		const bool canSendItem = collaborationTextBuffer_[0] != '\0' &&
			(targetType == "None" || collaborationKindIndex_ == 2 || !targetId.empty());
		if (!canSendItem) ImGui::BeginDisabled();
		const char* submitLabel = !editingTeamItemId_.empty()
			? "変更を保存"
			: collaborationKindIndex_ == 1 ? "Ping送信" : "作成";
		if (ImGui::Button(submitLabel)) {
			TeamItem item{};
			if (!editingTeamItemId_.empty()) {
				const auto editingItem = teamItems_.find(editingTeamItemId_);
				if (editingItem != teamItems_.end()) item = editingItem->second;
			}
			if (item.id.empty()) item.kind = kindValues[collaborationKindIndex_];
			item.targetType = targetType;
			item.targetId = targetId;
			item.scenePath = EditorSharedState::g_currentScenePath;
			item.componentUuid = activityComponentUuid_;
			item.propertyName = activityPropertyName_;
			item.text = collaborationTextBuffer_.data();
			if (item.id.empty()) {
				item.parentId = replyToTeamItemId_;
				if (item.parentId.empty() && teamItems_.find(selectedTeamItemId_) != teamItems_.end()) {
					item.parentId = selectedTeamItemId_;
				}
			}
			item.assigneeUserId = item.kind == "Note" && item.parentId.empty()
				? selectedAssigneeUserId_
				: std::string{};
			item.scriptLine = targetType == "ScriptLine" ? scriptTargetLine_ : 0;
			item.functionName = functionNameBuffer_.data();
			item.codeContext = codeContextBuffer_.data();
			if (targetType == "ScenePosition" && hasPendingTeamItemWorldPosition_) {
				item.hasWorldPosition = true;
				item.worldPosition = pendingTeamItemWorldPosition_;
			}
			if (item.kind == "Ping") {
				item.keepsPingHistory = keepsPingHistory;
				item.expiresAtUnixMilliseconds = GetCurrentUnixTimestampMilliseconds() + 10u * 1000u;
			}
			if (item.kind == "Review" && item.targetRevision == 0u) {
				item.targetRevision = currentRevision_;
			}
			if (item.kind == "Review" && targetType == "ChangeEvent") {
				item.targetChangeId = targetId;
			}
			for (const auto& memberPair : memberRecords_) {
				if (item.text.find("@" + memberPair.second.userName) != std::string::npos) {
					item.mentionedUserId = memberPair.first;
					break;
				}
			}
			QueueTeamItem(std::move(item));
			collaborationTextBuffer_[0] = '\0';
			editingTeamItemId_.clear();
			replyToTeamItemId_.clear();
			selectedTeamItemId_.clear();
			selectedHistoryTargetType_.clear();
			selectedHistoryTargetId_.clear();
			hasPendingTeamItemWorldPosition_ = false;
		}
		if (!canSendItem) ImGui::EndDisabled();
		if (!editingTeamItemId_.empty()) {
			ImGui::SameLine();
			if (ImGui::Button("編集をキャンセル")) {
				editingTeamItemId_.clear();
				collaborationTextBuffer_[0] = '\0';
			}
		}

		ImGui::InputText("検索", collaborationSearchBuffer_.data(), collaborationSearchBuffer_.size());
		constexpr const char* filterLabels[] = {"すべて", "付箋", "Ping", "チャット", "レビュー"};
		ImGui::Combo("種類フィルター", &collaborationKindFilterIndex_, filterLabels,
			static_cast<int32_t>(std::size(filterLabels)));
		static int32_t noteResolutionFilterIndex = 0;
		constexpr const char* resolutionFilterLabels[] = {"解決状態:すべて", "未解決", "解決済み"};
		ImGui::Combo(
			"付箋状態",
			&noteResolutionFilterIndex,
			resolutionFilterLabels,
			static_cast<int32_t>(std::size(resolutionFilterLabels)));
		constexpr const char* reviewFilterLabels[] = {"レビュー:すべて", "未確認", "確認中", "完了"};
		ImGui::Combo(
			"レビュー状態",
			&reviewStatusFilterIndex_,
			reviewFilterLabels,
			static_cast<int32_t>(std::size(reviewFilterLabels)));
		std::vector<const TeamItem*> displayedItems;
		for (const auto& itemPair : teamItems_) {
			const TeamItem& item = itemPair.second;
			const std::string search = collaborationSearchBuffer_.data();
			std::string assigneeName;
			const auto assignee = memberRecords_.find(item.assigneeUserId);
			if (assignee != memberRecords_.end()) assigneeName = assignee->second.userName;
			if (!search.empty() && item.text.find(search) == std::string::npos &&
				item.creatorUserName.find(search) == std::string::npos &&
				item.targetType.find(search) == std::string::npos &&
				item.targetId.find(search) == std::string::npos &&
				item.assigneeUserId.find(search) == std::string::npos &&
				assigneeName.find(search) == std::string::npos) continue;
			const bool matchesFocusedScriptLine =
				focusedTeamItemTargetType_ == "Script" && item.targetType == "ScriptLine" &&
				item.targetId == focusedTeamItemTargetId_;
			if (!focusedTeamItemTargetType_.empty() && !matchesFocusedScriptLine &&
				(item.targetType != focusedTeamItemTargetType_ || item.targetId != focusedTeamItemTargetId_)) continue;
			if (collaborationKindFilterIndex_ > 0 &&
				item.kind != kindValues[collaborationKindFilterIndex_ - 1]) continue;
			if (item.kind == "Note" && noteResolutionFilterIndex == 1 && item.isResolved) continue;
			if (item.kind == "Note" && noteResolutionFilterIndex == 2 && !item.isResolved) continue;
			if (item.kind == "Review" && reviewStatusFilterIndex_ > 0 &&
				item.reviewStatus != reviewStatusFilterIndex_ - 1) continue;
			displayedItems.push_back(&item);
		}
		std::sort(displayedItems.begin(), displayedItems.end(), [](const TeamItem* left, const TeamItem* right) {
			return left->timestampUnixMilliseconds > right->timestampUnixMilliseconds;
		});
		ImGui::BeginChild("TeamCollaborationFeed", ImVec2(0.0f, 280.0f), ImGuiChildFlags_Borders);
		for (const TeamItem* item : displayedItems) {
			ImGui::PushID(item->id.c_str());
			const char* displayKind = item->kind == "Note" ? "付箋" :
				item->kind == "Chat" ? "チャット" :
				item->kind == "Review" ? "レビュー" : "Ping";
			ImGui::Text("[%s] %s  %s", displayKind, item->creatorUserName.c_str(),
				FormatLocalTimestamp(item->updatedTimestampUnixMilliseconds).c_str());
			if (!item->updatedByUserName.empty() && item->updatedByUserName != item->creatorUserName) {
				ImGui::TextDisabled("最終更新: %s", item->updatedByUserName.c_str());
			}
			if (ImGui::SmallButton(("対象: " + item->targetType + " / " + item->targetId).c_str())) {
				JumpToTarget(item->targetType, item->targetId);
			}
			if (item->targetType == "ScriptLine") {
				ImGui::TextDisabled(
					"行: %d%s%s",
					item->scriptLine,
					item->functionName.empty() ? "" : " / 関数: ",
					item->functionName.c_str());
				if (!item->codeContext.empty()) ImGui::TextDisabled("周辺: %s", item->codeContext.c_str());
			}
			ImGui::TextWrapped("%s", item->text.c_str());
			if (!item->parentId.empty()) ImGui::TextDisabled("返信先: %s", item->parentId.c_str());
			int32_t replyCount = 0;
			for (const auto& replyPair : teamItems_) {
				if (replyPair.second.parentId == item->id) replyCount++;
			}
			if (replyCount > 0) ImGui::TextDisabled("返信: %d件", replyCount);
			if (item->kind == "Note" && !item->assigneeUserId.empty()) {
				const auto assignee = memberRecords_.find(item->assigneeUserId);
				ImGui::TextDisabled("担当: %s", assignee != memberRecords_.end()
					? assignee->second.userName.c_str() : item->assigneeUserId.c_str());
			}
			if (item->kind == "Note" && item->isResolved) {
				ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.55f, 1.0f), "解決済み");
			}
			if (item->kind == "Ping") {
				const bool expired = item->expiresAtUnixMilliseconds != 0u &&
					item->expiresAtUnixMilliseconds <= GetCurrentUnixTimestampMilliseconds();
				ImGui::TextDisabled(expired ? "表示終了" : "一時表示中");
			}
			if (item->kind == "Review") {
				constexpr const char* reviewStates[] = {"未確認", "確認中", "完了"};
				const bool hasTargetChangedAfterReview = std::any_of(
					recentChanges_.begin(), recentChanges_.end(),
					[this, item](const EditorTeamChangeEvent& change) {
						if (IsTeamItemChange(change) || change.revision <= item->targetRevision) return false;
						if (item->targetType == "Asset" || item->targetType == "Prefab" ||
							item->targetType == "Script" || item->targetType == "ScriptLine") {
							return change.scenePath == item->targetId;
						}
						return !change.objectUuid.empty() && item->targetId.starts_with(change.objectUuid);
					});
				ImGui::Text("状態: %s / 対象Revision: %llu%s",
					reviewStates[(std::clamp)(item->reviewStatus, 0, 2)],
					static_cast<unsigned long long>(item->targetRevision),
					hasTargetChangedAfterReview ? " / レビュー後に変更あり" : "");
				if (!item->targetChangeId.empty()) {
					const auto reviewedChange = std::find_if(
						recentChanges_.begin(), recentChanges_.end(),
						[item](const EditorTeamChangeEvent& change) {
							return change.changeId == item->targetChangeId;
						});
					if (reviewedChange != recentChanges_.end()) {
						ImGui::TextDisabled(
							"変更者: %s / %s",
							reviewedChange->userName.c_str(),
							FormatLocalTimestamp(reviewedChange->timestampUnixMilliseconds).c_str());
						if (!reviewedChange->oldValue.empty()) {
							ImGui::TextDisabled("変更前: %s", RemoveUtf8Bom(reviewedChange->oldValue).c_str());
						}
						if (!reviewedChange->newValue.empty()) {
							ImGui::TextDisabled("変更後: %s", RemoveUtf8Bom(reviewedChange->newValue).c_str());
						}
					}
				}
			}
			if (ImGui::SmallButton("ジャンプ")) JumpToTarget(item->targetType, item->targetId);
			if (item->kind != "Ping") {
				ImGui::SameLine();
				if (ImGui::SmallButton("返信")) {
					replyToTeamItemId_ = item->id;
					collaborationKindIndex_ = item->kind == "Note" ? 0 : item->kind == "Chat" ? 2 : 3;
					focusedTeamItemTargetType_ = item->targetType;
					focusedTeamItemTargetId_ = item->targetId;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("編集")) {
				editingTeamItemId_ = item->id;
				collaborationKindIndex_ = item->kind == "Note" ? 0 : item->kind == "Ping" ? 1 : item->kind == "Chat" ? 2 : 3;
				strncpy_s(collaborationTextBuffer_.data(), collaborationTextBuffer_.size(), item->text.c_str(), _TRUNCATE);
				selectedAssigneeUserId_ = item->assigneeUserId;
				focusedTeamItemTargetType_ = item->targetType;
				focusedTeamItemTargetId_ = item->targetId;
				hasPendingTeamItemWorldPosition_ = item->hasWorldPosition;
				pendingTeamItemWorldPosition_ = item->worldPosition;
				scriptTargetLine_ = item->scriptLine;
				strncpy_s(scriptTargetPathBuffer_.data(), scriptTargetPathBuffer_.size(), item->targetId.c_str(), _TRUNCATE);
				strncpy_s(functionNameBuffer_.data(), functionNameBuffer_.size(), item->functionName.c_str(), _TRUNCATE);
				strncpy_s(codeContextBuffer_.data(), codeContextBuffer_.size(), item->codeContext.c_str(), _TRUNCATE);
			}
			if (item->kind == "Note") {
				ImGui::SameLine();
				if (ImGui::SmallButton("選択中の担当者を設定")) {
					TeamItem updated = *item;
					updated.assigneeUserId = selectedAssigneeUserId_;
					QueueTeamItem(std::move(updated));
					ImGui::PopID();
					break;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(item->isResolved ? "再オープン" : "解決済みにする")) {
					TeamItem updated = *item;
					updated.isResolved = !updated.isResolved;
					QueueTeamItem(std::move(updated));
					ImGui::PopID();
					break;
				}
			}
			if (item->kind == "Review") {
				ImGui::SameLine();
				if (ImGui::SmallButton(item->reviewStatus == 1 ? "レビュー完了" : "確認を開始")) {
					TeamItem updated = *item;
					updated.reviewStatus = item->reviewStatus == 1 ? 2 : 1;
					QueueTeamItem(std::move(updated));
					ImGui::PopID();
					break;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("削除")) {
				QueueTeamItemDelete(*item);
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	if (ImGui::CollapsingHeader("Lock拡張")) {
		constexpr const char* lockTargetLabels[] = {"GameObject", "Property", "Asset", "Prefab", "Scene"};
		static int32_t lockTargetIndex = 0;
		static int32_t lockModeIndex = 1;
		ImGui::Combo("Lock対象", &lockTargetIndex, lockTargetLabels, static_cast<int32_t>(std::size(lockTargetLabels)));
		constexpr const char* lockModeLabels[] = {"Soft Lock (警告のみ)", "Hard Lock (編集禁止)"};
		ImGui::Combo("Lock方式", &lockModeIndex, lockModeLabels, static_cast<int32_t>(std::size(lockModeLabels)));
		std::string objectUuid;
		if (editorScene_ != nullptr && EditorSharedState::g_selectedEditorGameObjectId >= 0) {
			const EditorGameObject* selected = editorScene_->FindGameObject(EditorSharedState::g_selectedEditorGameObjectId);
			if (selected != nullptr) objectUuid = selected->uuid;
		}
		const std::string lockTargetType = lockTargetLabels[lockTargetIndex];
		std::string lockTargetId;
		if (lockTargetType == "GameObject") lockTargetId = objectUuid;
		else if (lockTargetType == "Property") {
			lockTargetId = objectUuid;
			if (!activityComponentUuid_.empty()) lockTargetId += "#component:" + activityComponentUuid_;
			if (!activityPropertyName_.empty()) lockTargetId += "#property:" + activityPropertyName_;
		}
		else if (lockTargetType == "Scene") lockTargetId = EditorSharedState::g_currentScenePath;
		else lockTargetId = EditorSharedState::g_selectedAssetPath;
		ImGui::TextWrapped("対象: %s", lockTargetId.empty() ? "未選択" : lockTargetId.c_str());
		if (lockTargetId.empty()) ImGui::BeginDisabled();
		if (ImGui::Button("Lock取得")) {
			RequestTargetLock(lockTargetType, lockTargetId,
				lockModeIndex == 0 ? EditorTeamLockMode::Soft : EditorTeamLockMode::Hard);
		}
		ImGui::SameLine();
		if (ImGui::Button("自分のLock解除")) ReleaseTargetLock(lockTargetType, lockTargetId);
		if (lockTargetId.empty()) ImGui::EndDisabled();
		for (const auto& lockPair : lockedByUserId_) {
			const auto name = lockedByUserName_.find(lockPair.first);
			const auto mode = lockModes_.find(lockPair.first);
			const auto type = lockTargetTypes_.find(lockPair.first);
			ImGui::BulletText("[%s/%s] %s - %s",
				type != lockTargetTypes_.end() && !type->second.empty() ? type->second.c_str() : "Object/Component",
				mode != lockModes_.end() && mode->second == EditorTeamLockMode::Soft ? "Soft" : "Hard",
				lockPair.first.c_str(),
				name != lockedByUserName_.end() ? name->second.c_str() : lockPair.second.c_str());
		}
	}

	if (ImGui::CollapsingHeader("共同作業Dashboard", ImGuiTreeNodeFlags_DefaultOpen)) {
		const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();
		std::time_t nowSeconds = static_cast<std::time_t>(now / 1000u);
		std::tm nowLocal{};
		localtime_s(&nowLocal, &nowSeconds);
		int32_t todayChangeCount = 0;
		for (const EditorTeamChangeEvent& change : recentChanges_) {
			std::time_t changeSeconds = static_cast<std::time_t>(change.timestampUnixMilliseconds / 1000u);
			std::tm changeLocal{};
			if (localtime_s(&changeLocal, &changeSeconds) == 0 &&
				changeLocal.tm_year == nowLocal.tm_year && changeLocal.tm_yday == nowLocal.tm_yday) {
				todayChangeCount++;
			}
		}
		int32_t openNoteCount = 0;
		for (const auto& itemPair : teamItems_) {
			if (!itemPair.second.isResolved && itemPair.second.kind != "Chat") openNoteCount++;
		}
		ImGui::Text("今日の変更: %d", todayChangeCount);
		ImGui::SameLine();
		ImGui::Text("未解決: %d", openNoteCount);
		ImGui::SameLine();
		ImGui::Text("未同期: %llu / 競合: %llu",
			static_cast<unsigned long long>(unsyncedChanges_.size()),
			static_cast<unsigned long long>(conflicts_.size() + teamItemConflicts_.size()));
		ImGui::Text("オンライン: %d / Lock: %llu / Revision: %llu",
			connectedMemberCount,
			static_cast<unsigned long long>(lockedByUserId_.size()),
			static_cast<unsigned long long>(currentRevision_));
		ImGui::InputTextWithHint(
			"Checkpoint名", "例: Boss調整前", checkpointNameBuffer_.data(), checkpointNameBuffer_.size());
		ImGui::SameLine();
		if (ImGui::Button("Checkpoint作成") && editorScene_ != nullptr) {
			const std::filesystem::path checkpointPath = std::filesystem::path(".team/backups") /
				("Checkpoint_" + std::to_string(now) + ".scene");
			std::error_code checkpointError;
			std::filesystem::create_directories(checkpointPath.parent_path(), checkpointError);
			if (!checkpointError && editorScene_->SaveScene(checkpointPath.generic_string())) {
				std::ostringstream metadata;
				metadata << "{\"kind\":\"Checkpoint\",\"name\":\"" << EscapeJsonText(checkpointNameBuffer_.data())
					<< "\",\"targetScene\":\"" << EscapeJsonText(EditorSharedState::g_currentScenePath)
					<< "\",\"editor\":\"" << EscapeJsonText(userNameBuffer_.data())
					<< "\",\"timestamp\":" << now << "}";
				WriteUtf8BomTextFile(checkpointPath.generic_string() + ".meta", metadata.str());
				checkpointNameBuffer_[0] = '\0';
			}
		}
		ImGui::SeparatorText("直近の変更");
		for (std::size_t index = 0u; index < (std::min)(recentChanges_.size(), std::size_t{8u}); ++index) {
			const EditorTeamChangeEvent& change = recentChanges_[recentChanges_.size() - 1u - index];
			ImGui::BulletText("%s %s %s", change.userName.c_str(), change.operation.c_str(), change.property.c_str());
		}
		ImGui::SeparatorText("直近のCheckpoint / Snapshot");
		std::vector<std::filesystem::directory_entry> backups;
		std::error_code backupError;
		const std::filesystem::path backupRoot(".team/backups");
		if (std::filesystem::exists(backupRoot, backupError)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(
				backupRoot, std::filesystem::directory_options::skip_permission_denied, backupError)) {
				if (entry.is_regular_file(backupError) && IsCollaborationSceneAssetPath(entry.path().generic_string())) {
					backups.push_back(entry);
				}
			}
		}
		std::sort(backups.begin(), backups.end(), [](const auto& left, const auto& right) {
			std::error_code leftError;
			std::error_code rightError;
			return left.last_write_time(leftError) > right.last_write_time(rightError);
		});
		if (backups.empty()) ImGui::TextDisabled("Snapshotはありません");
		for (std::size_t index = 0u; index < (std::min)(backups.size(), std::size_t{5u}); ++index) {
			std::string metadataText;
			ReadBinaryTextFile(backups[index].path().generic_string() + ".meta", metadataText);
			const std::string kind = ReadJsonString(metadataText, "kind");
			const std::string name = ReadJsonString(metadataText, "name");
			ImGui::BulletText("[%s] %s%s%s",
				kind.empty() ? "Snapshot" : kind.c_str(),
				backups[index].path().filename().generic_string().c_str(),
				name.empty() ? "" : " / ",
				name.c_str());
		}
	}

	if (ImGui::CollapsingHeader("詳細履歴")) {
		constexpr const char* historyTypeLabels[] = {
			"すべて", "GameObject", "Component", "Property", "Scene", "Prefab", "Asset", "Script", "付箋/Chat"};
		constexpr const char* historyTimeLabels[] = {"全期間", "今日", "直近1時間"};
		ImGui::Combo("履歴種別", &historyTypeFilterIndex_, historyTypeLabels,
			static_cast<int32_t>(std::size(historyTypeLabels)));
		ImGui::Combo("時間範囲", &historyTimeFilterIndex_, historyTimeLabels,
			static_cast<int32_t>(std::size(historyTimeLabels)));
		ImGui::InputText("ユーザー検索", historyUserFilterBuffer_.data(), historyUserFilterBuffer_.size());
		ImGui::InputText("対象検索", historyTargetFilterBuffer_.data(), historyTargetFilterBuffer_.size());
		ImGui::BeginChild("TeamDetailedHistory", ImVec2(0.0f, 340.0f), ImGuiChildFlags_Borders);
		const std::uint64_t now = GetCurrentUnixTimestampMilliseconds();
		for (auto iterator = recentChanges_.rbegin(); iterator != recentChanges_.rend(); ++iterator) {
			const EditorTeamChangeEvent& change = *iterator;
			const std::string userFilter = historyUserFilterBuffer_.data();
			const std::string targetFilter = historyTargetFilterBuffer_.data();
			const std::string targetText = change.scenePath + " " + change.objectUuid + " " +
				change.componentUuid + " " + change.property;
			if (!userFilter.empty() && change.userName.find(userFilter) == std::string::npos) continue;
			if (!targetFilter.empty() && targetText.find(targetFilter) == std::string::npos) continue;
			if (historyTimeFilterIndex_ == 2 && now > change.timestampUnixMilliseconds + 60u * 60u * 1000u) continue;
			if (historyTimeFilterIndex_ == 1) {
				std::time_t nowSeconds = static_cast<std::time_t>(now / 1000u);
				std::time_t changeSeconds = static_cast<std::time_t>(change.timestampUnixMilliseconds / 1000u);
				std::tm nowLocal{};
				std::tm changeLocal{};
				localtime_s(&nowLocal, &nowSeconds);
				localtime_s(&changeLocal, &changeSeconds);
				if (nowLocal.tm_year != changeLocal.tm_year || nowLocal.tm_yday != changeLocal.tm_yday) continue;
			}
			std::string category = "Property";
			if (IsTeamItemChange(change)) category = "付箋/Chat";
			else if (change.property == "Asset") {
				if (IsScriptAssetPath(change.scenePath)) category = "Script";
				else if (EditorAssetUtility::HasExtension(change.scenePath, ".prefab")) category = "Prefab";
				else category = "Asset";
			}
			else if (change.property == "GameObject") category = "GameObject";
			else if (change.property == "Component" || !change.componentUuid.empty()) category = "Component";
			else if (change.property == "SceneData") category = "Scene";
			if (historyTypeFilterIndex_ > 0 && category != historyTypeLabels[historyTypeFilterIndex_]) continue;

			ImGui::PushID(change.changeId.c_str());
			if (ImGui::TreeNode("history", "%s  R%llu  [%s] %s / %s",
				FormatLocalTimestamp(change.timestampUnixMilliseconds).c_str(),
				static_cast<unsigned long long>(change.revision), category.c_str(),
				change.userName.c_str(), change.operation.c_str())) {
				ImGui::TextWrapped("対象: %s", targetText.c_str());
				if (!change.oldValue.empty()) {
					ImGui::TextDisabled("変更前");
					ImGui::BeginChild("old", ImVec2(0.0f, IsScriptAssetPath(change.scenePath) ? 100.0f : 48.0f), ImGuiChildFlags_Borders);
					ImGui::TextUnformatted(RemoveUtf8Bom(change.oldValue).c_str());
					ImGui::EndChild();
				}
				if (!change.newValue.empty()) {
					ImGui::TextDisabled(IsScriptAssetPath(change.scenePath) ? "保存後のScript差分" : "変更後");
					ImGui::BeginChild("new", ImVec2(0.0f, IsScriptAssetPath(change.scenePath) ? 100.0f : 48.0f), ImGuiChildFlags_Borders);
					ImGui::TextUnformatted(RemoveUtf8Bom(change.newValue).c_str());
					ImGui::EndChild();
				}
				if (ImGui::SmallButton("対象へジャンプ")) {
					if (!change.objectUuid.empty()) {
						activityComponentUuid_ = change.componentUuid;
						activityPropertyName_ = change.property;
						JumpToTarget(change.componentUuid.empty() ? "GameObject" : "Component", change.objectUuid);
					}
					else JumpToTarget(IsScriptAssetPath(change.scenePath) ? "Script" : "Asset", change.scenePath);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("この変更へコメント")) {
					collaborationKindIndex_ = 3;
					collaborationTargetIndex_ = 10;
					selectedTeamItemId_ = change.changeId;
					selectedHistoryTargetType_ = "ChangeEvent";
					selectedHistoryTargetId_ = change.changeId;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	ImGui::Separator();
	ImGui::InputText("ユーザー名", userNameBuffer_.data(), userNameBuffer_.size());
	ImGui::InputText("接続先Host", hostAddressBuffer_.data(), hostAddressBuffer_.size());
	ImGui::TextDisabled(
		"LAN: 192.168.x.x / 遠隔: Tailscale MagicDNS hostname (例 ms.tailxxxx.ts.net)");
	ImGui::TextDisabled(
		"100.x.x.x の直接指定より、MagicDNS hostname を推奨します(IP変更に強いため)");
	ImGui::InputText(
		"Project ID",
		projectIdBuffer_.data(),
		projectIdBuffer_.size(),
		ImGuiInputTextFlags_ReadOnly);

	{
		// Project IDは接続時にServerと突き合わせる。不正な文字は接続拒否になるため、ここで警告する。
		const std::string projectIdText = projectIdBuffer_.data();

		if (projectIdText.empty()) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
				"Project IDがありません。Hostから受け取ったProject ZIP／招待を読み込んでください");
		}
		else if (!CG2Collaboration::IsValidProjectId(projectIdText)) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
				"Project IDに使えない文字があります(英数字と - _ . のみ、64文字以内)");
		}
	}
	ImGui::TextDisabled("Project IDは同じ共同制作を識別する値です。Projectフォルダ全体の同一性までは保証しません。");
	ImGui::TextDisabled("参加者は同じ配布Project ZIP／招待から開始してください。Host参加時は共有Sceneと参照AssetをHost側へ合わせます。");

	ImGui::Checkbox("自動接続", &autoConnect_);
	ImGui::SameLine();
	ImGui::TextDisabled("(Editor起動時に自動でServerへ接続する)");

	{
		int32_t maximumClientValue = maximumClientCount_;

		if (ImGui::InputInt("最大接続数", &maximumClientValue)) {
			maximumClientCount_ = (std::clamp)(maximumClientValue, 1, 32);
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(Host以外の参加PC数。3台共同制作なら2以上)");
	}
	ImGui::InputText(
		"共有Sceneフォルダ",
		sharedSceneFolderBuffer_.data(),
		sharedSceneFolderBuffer_.size());
	if (!IsValidSharedGameSceneFolder(sharedSceneFolderBuffer_.data())) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
			"Assets/Scenes内のゲーム用フォルダを指定してください");
	}

	if (ImGui::Button("Projectで選択中のフォルダを共有")) {
		std::filesystem::path selectedPath(EditorSharedState::g_selectedAssetPath);

		if (std::filesystem::is_regular_file(selectedPath)) {
			selectedPath = selectedPath.parent_path();
		}

		const std::string selectedFolder = selectedPath.lexically_normal().generic_string();

		if (std::filesystem::is_directory(selectedPath) &&
			IsValidSharedGameSceneFolder(selectedFolder)) {
			strncpy_s(
				sharedSceneFolderBuffer_.data(),
				sharedSceneFolderBuffer_.size(),
				selectedFolder.c_str(),
				_TRUNCATE);
			assetRecords_.clear();
			lastSnapshotText_.clear();
			lastSnapshotHash_ = 0u;
			ScanAssetChanges(true);
			SaveSettings();

			if (isHost_ && GetStatus() == EditorTeamConnectionStatus::Online) {
				QueueOutgoingMessage(SerializeSharedSceneFolderMessage(selectedFolder));
			}

			lastError_.clear();
		}
		else {
			lastError_ = "Assets/Scenes内のゲームフォルダをProjectで選択してください";
		}
	}

	ImGui::TextDisabled("このフォルダ内のSceneと、Sceneが参照するAssetだけを同期します");
	int32_t portValue = static_cast<int32_t>(port_);

	if (ImGui::InputInt("ポート", &portValue)) {
		port_ = static_cast<uint16_t>((std::clamp)(portValue, 1, 65535));
	}
	if (ImGui::Button("接続テスト")) {
		TestServerConnection();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("入力したHostとポートへTCP接続できるか確認します");

	ImGui::Checkbox("このPCをHostにする", &isHost_);
	ImGui::Checkbox("Editor起動時に自動でServerを開始する", &startsServerAutomatically_);

	if (GetStatus() == EditorTeamConnectionStatus::Offline ||
		GetStatus() == EditorTeamConnectionStatus::Disconnected) {
		if (isHost_) {
			if (ImGui::Button("サーバー開始")) {
				StartServer();
			}
		}
		else if (ImGui::Button("Hostへ接続")) {
			ConnectToHost();
		}
	}
	else if (ImGui::Button(isHost_ ? "サーバー停止" : "切断")) {
		Disconnect();
	}

	ImGui::SameLine();

	if (ImGui::Button("設定保存")) {
		SaveSettings();
	}

	ImGui::SeparatorText("専用CG2TeamServer");
	ImGui::TextDisabled("Editorを閉じても共同制作を続ける場合に使用します。同じポートでEditor Hostとは同時起動できません。");
	ImGui::Text("状態: %s", IsDedicatedServerRunning() ? "稼働中" : "停止中");
	if (ImGui::Button("専用Server開始")) {
		StartDedicatedServer();
	}
	ImGui::SameLine();
	if (ImGui::Button("専用Server停止")) {
		StopDedicatedServer();
	}
	ImGui::SameLine();
	if (ImGui::Button("専用Server再起動")) {
		RestartDedicatedServer();
	}
	bool autoStartAtLogon = dedicatedServerAutoStartAtLogon_;
	if (ImGui::Checkbox("Windowsログオン時に専用Serverを自動起動", &autoStartAtLogon)) {
		SetDedicatedServerAutoStartAtLogon(autoStartAtLogon);
	}

	if (!lastError_.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", lastError_.c_str());
	}

	if (ImGui::CollapsingHeader("共有中の依存Asset", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (assetRecords_.empty()) {
			ImGui::TextDisabled("共有フォルダを選択すると、Sceneが参照するAssetを表示します");
		}

		for (const auto& assetPair : assetRecords_) {
			const bool isPending = std::any_of(
				unsyncedChanges_.begin(),
				unsyncedChanges_.end(),
				[&assetPair](const EditorTeamChangeEvent& changeEvent) {
					return changeEvent.property == "Asset" && changeEvent.scenePath == assetPair.first;
				});
			const double fileSizeMegabytes =
				static_cast<double>(assetPair.second.fileSize) / (1024.0 * 1024.0);
			ImGui::BulletText(
				"%s  %.2f MB  [%s]",
				assetPair.first.c_str(),
				fileSizeMegabytes,
				isPending ? "未同期" : "同期済み");
		}

		for (const std::string& missingAssetPath : missingReferencedAssetPaths_) {
			ImGui::BulletText("%s", missingAssetPath.c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "[参照あり / ファイルなし]");
		}
	}

	if (ImGui::CollapsingHeader("転送予定 / 進捗", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool hasPendingAsset = false;

		for (const EditorTeamChangeEvent& changeEvent : unsyncedChanges_) {
			if (changeEvent.property != "Asset") {
				continue;
			}

			hasPendingAsset = true;
			ImGui::BulletText(
				"%s  %.2f MB",
				changeEvent.scenePath.c_str(),
				static_cast<double>(changeEvent.fileSize) / (1024.0 * 1024.0));
		}

		if (!hasPendingAsset) {
			ImGui::TextDisabled("転送待ちAssetはありません");
		}

		// 大きなAssetはassetBegin/assetChunk/assetEndへ分割されるため、
		// ここは「本当に送受信できたバイト数」をFileごとに表示できる。
		const std::uint64_t outgoingTransferTotal =
			networkState_->outgoingTransferTotalBytes.load(std::memory_order_acquire);
		const std::uint64_t outgoingTransferSent =
			networkState_->outgoingTransferSentBytes.load(std::memory_order_acquire);
		std::string outgoingTransferLabel;

		{
			std::lock_guard<std::mutex> labelLock(networkState_->transferLabelMutex);
			outgoingTransferLabel = networkState_->outgoingTransferLabel;
		}

		if (outgoingTransferTotal > 0u) {
			const float progress = static_cast<float>(
				static_cast<double>(outgoingTransferSent) / static_cast<double>(outgoingTransferTotal));
			char progressText[160]{};
			std::snprintf(
				progressText,
				sizeof(progressText),
				"送信: %s  %.2f / %.2f MB",
				outgoingTransferLabel.c_str(),
				static_cast<double>(outgoingTransferSent) / (1024.0 * 1024.0),
				static_cast<double>(outgoingTransferTotal) / (1024.0 * 1024.0));
			ImGui::ProgressBar((std::clamp)(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), progressText);
		}

		if (incomingTransferTotalBytes_ > 0u) {
			const float progress = static_cast<float>(
				static_cast<double>(incomingTransferReceivedBytes_) /
				static_cast<double>(incomingTransferTotalBytes_));
			char progressText[160]{};
			std::snprintf(
				progressText,
				sizeof(progressText),
				"受信: %s  %.2f / %.2f MB",
				incomingTransferLabel_.c_str(),
				static_cast<double>(incomingTransferReceivedBytes_) / (1024.0 * 1024.0),
				static_cast<double>(incomingTransferTotalBytes_) / (1024.0 * 1024.0));
			ImGui::ProgressBar((std::clamp)(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), progressText);
		}
	}

	if (ImGui::CollapsingHeader("同期除外ルール")) {
		ImGui::BulletText("_Archive / Library / Builds / .team");
		ImGui::BulletText("x64配下と PDB / LIB / EXP / ILK 等の中間生成物");
		ImGui::BulletText("他ゲームのSceneと、共有Sceneから参照されていないAsset");
	}

	if (ImGui::CollapsingHeader("Scene状態 / 履歴")) {
		const std::filesystem::path sharedSceneFolder(sharedSceneFolderBuffer_.data());
		std::error_code sceneError;

		if (IsValidSharedGameSceneFolder(sharedSceneFolder.generic_string()) &&
			std::filesystem::exists(sharedSceneFolder, sceneError)) {
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::recursive_directory_iterator(
					sharedSceneFolder,
					std::filesystem::directory_options::skip_permission_denied,
					sceneError)) {
				const std::string scenePath = entry.path().lexically_normal().generic_string();

				if (!entry.is_regular_file(sceneError) ||
					!IsCollaborationSceneAssetPath(scenePath) ||
					scenePath.find("/_Archive/") != std::string::npos) {
					continue;
				}

				const bool isPending = std::any_of(
					unsyncedChanges_.begin(),
					unsyncedChanges_.end(),
					[&scenePath](const EditorTeamChangeEvent& changeEvent) {
						return changeEvent.scenePath == scenePath;
					});
				const bool isCurrentScene =
					std::filesystem::path(EditorSharedState::g_currentScenePath).lexically_normal().generic_string() ==
					scenePath;
				const bool isEditedByAnotherUser = isCurrentScene && std::any_of(
					lockedByUserId_.begin(),
					lockedByUserId_.end(),
					[this](const auto& lockPair) {
						return lockPair.second != userId_;
					});
				std::string lastEditor;

				for (auto changeIterator = recentChanges_.rbegin();
					changeIterator != recentChanges_.rend(); ++changeIterator) {
					if (changeIterator->scenePath == scenePath) {
						lastEditor = changeIterator->userName;
						break;
					}
				}

				ImGui::BulletText(
					"%s  [%s]%s%s",
					scenePath.c_str(),
					isEditedByAnotherUser
						? "相手側が編集中"
						: isPending ? "未送信変更あり" : "同期済み",
					lastEditor.empty() ? "" : "  最終更新: ",
					lastEditor.c_str());
			}
		}

		ImGui::SeparatorText("最近の変更");
		int32_t displayedHistoryCount = 0;

		for (auto changeIterator = recentChanges_.rbegin();
			changeIterator != recentChanges_.rend() && displayedHistoryCount < 30;
			++changeIterator, ++displayedHistoryCount) {
			ImGui::BulletText(
				"%s  R%llu  %s  %s  %s",
				FormatLocalTimestamp(changeIterator->timestampUnixMilliseconds).c_str(),
				static_cast<unsigned long long>(changeIterator->revision),
				changeIterator->userName.c_str(),
				changeIterator->operation.c_str(),
				changeIterator->scenePath.c_str());
		}
	}

	if (ImGui::CollapsingHeader("復旧バックアップ")) {
		const std::filesystem::path backupRoot(".team/backups");
		std::error_code backupError;
		int32_t displayedBackupCount = 0;
		static std::string comparedBackupPath;
		static std::string comparisonResult;

		if (std::filesystem::exists(backupRoot, backupError)) {
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::recursive_directory_iterator(
					backupRoot,
					std::filesystem::directory_options::skip_permission_denied,
					backupError)) {
				if (!entry.is_regular_file(backupError) ||
					!IsCollaborationSceneAssetPath(entry.path().generic_string()) ||
					displayedBackupCount >= 30) {
					continue;
				}

				const std::string backupPath = entry.path().generic_string();
				const double backupSizeMegabytes =
					static_cast<double>(entry.file_size(backupError)) / (1024.0 * 1024.0);
				std::string metadataText;
				ReadBinaryTextFile(backupPath + ".meta", metadataText);
				const std::string targetScene = ReadJsonString(metadataText, "targetScene");
				const std::string backupEditor = ReadJsonString(metadataText, "editor");
				const std::uint64_t backupTimestamp = ReadJsonUnsigned(metadataText, "timestamp");
				ImGui::PushID(backupPath.c_str());
				ImGui::Text("%s  %.2f MB", backupPath.c_str(), backupSizeMegabytes);
				ImGui::TextDisabled(
					"日時: %s / 編集者: %s / 対象: %s",
					FormatLocalTimestamp(backupTimestamp).c_str(),
					backupEditor.empty() ? "不明" : backupEditor.c_str(),
					targetScene.empty() ? "不明" : targetScene.c_str());

				if (ImGui::Button("現在Sceneと比較")) {
					EditorScene backupScene;
					if (editorScene_ != nullptr && backupScene.LoadScene(backupPath)) {
						int32_t addedCount = 0;
						int32_t removedCount = 0;
						int32_t changedCount = 0;
						for (const EditorGameObject& currentObject : editorScene_->GetGameObjects()) {
							const auto backupObjectIterator = std::find_if(
								backupScene.GetGameObjects().begin(),
								backupScene.GetGameObjects().end(),
								[&currentObject](const EditorGameObject& backupObject) {
									return backupObject.uuid == currentObject.uuid;
								});
							if (backupObjectIterator == backupScene.GetGameObjects().end()) {
								addedCount++;
							}
							else if (backupObjectIterator->name != currentObject.name ||
								backupObjectIterator->parentId != currentObject.parentId ||
								backupObjectIterator->components.size() != currentObject.components.size() ||
								std::memcmp(&backupObjectIterator->translate, &currentObject.translate, sizeof(Vector3)) != 0 ||
								std::memcmp(&backupObjectIterator->rotate, &currentObject.rotate, sizeof(Vector3)) != 0 ||
								std::memcmp(&backupObjectIterator->scale, &currentObject.scale, sizeof(Vector3)) != 0) {
								changedCount++;
							}
						}
						for (const EditorGameObject& backupObject : backupScene.GetGameObjects()) {
							const auto currentObjectIterator = std::find_if(
								editorScene_->GetGameObjects().begin(),
								editorScene_->GetGameObjects().end(),
								[&backupObject](const EditorGameObject& currentObject) {
									return currentObject.uuid == backupObject.uuid;
								});
							if (currentObjectIterator == editorScene_->GetGameObjects().end()) {
								removedCount++;
							}
						}
						comparedBackupPath = backupPath;
						comparisonResult =
							"追加 " + std::to_string(addedCount) +
							" / 削除 " + std::to_string(removedCount) +
							" / 主要変更 " + std::to_string(changedCount);
					}
				}
				if (comparedBackupPath == backupPath && !comparisonResult.empty()) {
					ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", comparisonResult.c_str());
				}

				ImGui::SameLine();

				if (ImGui::Button("現在Sceneへ復元") &&
					editorScene_ != nullptr && !EditorSharedState::g_currentScenePath.empty()) {
					const std::string destinationPath = EditorSharedState::g_currentScenePath;
					const std::filesystem::path safetyCopy =
						backupRoot /
						("BeforeUiRestore_Revision_" + std::to_string(currentRevision_) + ".scene");
					editorScene_->SaveScene(safetyCopy.generic_string());

					if (editorScene_->LoadScene(backupPath) &&
						editorScene_->SaveScene(destinationPath)) {
						CaptureSceneChanges(true);
						AddConsoleMessage("Team Backup: 復元しました " + backupPath);
					}
				}

				ImGui::PopID();
				displayedBackupCount++;
			}
		}

		if (displayedBackupCount == 0) {
			ImGui::TextDisabled("復旧バックアップはありません");
		}
	}

	if (!teamItemConflicts_.empty()) {
		ImGui::SeparatorText("TeamItem競合");
		TeamItemConflict& teamItemConflict = teamItemConflicts_.front();
		ImGui::Text(
			"%s / %s",
			teamItemConflict.remoteUserName.c_str(),
			teamItemConflict.localItem.targetId.c_str());
		ImGui::TextDisabled("現在の本文");
		ImGui::TextWrapped("%s", teamItemConflict.localItem.text.c_str());
		ImGui::TextDisabled("相手の本文");
		ImGui::TextWrapped("%s", teamItemConflict.remoteItem.text.c_str());
		if (manualTeamItemConflictId_ != teamItemConflict.localItem.id) {
			manualTeamItemConflictId_ = teamItemConflict.localItem.id;
			strncpy_s(
				teamItemEditTextBuffer_.data(),
				teamItemEditTextBuffer_.size(),
				teamItemConflict.localItem.text.c_str(),
				_TRUNCATE);
		}
		ImGui::InputTextMultiline(
			"手動マージ本文",
			teamItemEditTextBuffer_.data(),
			teamItemEditTextBuffer_.size(),
			ImVec2(-1.0f, 96.0f));
		auto resolveTeamItemConflict = [this](TeamItem resolvedItem) {
			resolvedItem.revision = currentRevision_;
			QueueTeamItem(std::move(resolvedItem));
			teamItemConflicts_.erase(teamItemConflicts_.begin());
			manualTeamItemConflictId_.clear();
			if (teamItemConflicts_.empty() && conflicts_.empty()) {
				networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
				lastError_.clear();
			}
		};
		if (ImGui::Button("現在側を採用")) {
			resolveTeamItemConflict(teamItemConflict.localItem);
		}
		ImGui::SameLine();
		if (ImGui::Button("相手側を採用")) {
			resolveTeamItemConflict(teamItemConflict.remoteItem);
		}
		ImGui::SameLine();
		if (ImGui::Button("手動マージを採用")) {
			TeamItem mergedItem = teamItemConflict.localItem;
			mergedItem.text = teamItemEditTextBuffer_.data();
			resolveTeamItemConflict(std::move(mergedItem));
		}
	}

	if (!conflicts_.empty()) {
		ImGui::SeparatorText("Conflicts");
		const EditorTeamChangeEvent conflict = conflicts_.front();
		ImGui::Text("%s / %s", conflict.userName.c_str(), conflict.scenePath.c_str());
		ImGui::Text("%s : %s", conflict.operation.c_str(), conflict.property.c_str());
		const std::string conflictDirectory =
			BuildConflictDirectoryPath(conflict.changeId).generic_string();
		ImGui::TextWrapped("Conflict Copy: %s", conflictDirectory.c_str());
		const auto localConflictIterator = conflictLocalChanges_.find(conflict.changeId);
		const std::string baseValue = localConflictIterator != conflictLocalChanges_.end()
			? localConflictIterator->second.oldValue : conflict.oldValue;
		const std::string localValue = localConflictIterator != conflictLocalChanges_.end()
			? localConflictIterator->second.newValue : std::string{};
		if (IsScriptAssetPath(conflict.scenePath)) {
			ImGui::SeparatorText("3-way 比較");
			ImGui::TextDisabled("Base");
			ImGui::BeginChild("ConflictBase", ImVec2(0.0f, 90.0f), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(RemoveUtf8Bom(baseValue).c_str());
			ImGui::EndChild();
			ImGui::TextDisabled("Server");
			ImGui::BeginChild("ConflictServer", ImVec2(0.0f, 90.0f), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(RemoveUtf8Bom(conflict.newValue).c_str());
			ImGui::EndChild();
			ImGui::TextDisabled("Local / 手動編集結果");
			if (manualConflictId_ != conflict.changeId) {
				manualConflictId_ = conflict.changeId;
				const std::string initialText = RemoveUtf8Bom(localValue);
				manualConflictTextBuffer_.assign(
					(std::max)(initialText.size() + static_cast<std::size_t>(65536U),
						static_cast<std::size_t>(262144U)), '\0');
				std::copy(initialText.begin(), initialText.end(), manualConflictTextBuffer_.begin());
			}
			ImGui::InputTextMultiline("##ManualConflictEditor",
				manualConflictTextBuffer_.data(), manualConflictTextBuffer_.size(), ImVec2(0.0f, 180.0f));
		}

		if (ImGui::Button("自分側を採用")) {
			const bool hasQueuedLocalChange = localConflictIterator != conflictLocalChanges_.end();
			conflicts_.erase(conflicts_.begin());
			conflictLocalChanges_.erase(conflict.changeId);

			if (!hasQueuedLocalChange && conflict.property == "Asset") {
				const auto assetIterator = assetRecords_.find(conflict.scenePath);

				if (assetIterator != assetRecords_.end()) {
					QueueAssetChange(conflict.scenePath, nullptr, &assetIterator->second);
				}
			}
			else if (!hasQueuedLocalChange) {
				CaptureSceneChanges(true);
			}
			if (conflicts_.empty()) CompleteHistorySynchronization((std::max)(serverRevision_, currentRevision_));
			else networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
		}

		ImGui::SameLine();

		if (ImGui::Button("相手側を採用")) {
			conflicts_.erase(conflicts_.begin());
			if (localConflictIterator != conflictLocalChanges_.end()) {
				const std::string localChangeId = localConflictIterator->second.changeId;
				unsyncedChanges_.erase(
					std::remove_if(unsyncedChanges_.begin(), unsyncedChanges_.end(),
						[&localChangeId](const EditorTeamChangeEvent& pending) {
							return pending.changeId == localChangeId;
						}), unsyncedChanges_.end());
			}
			conflictLocalChanges_.erase(conflict.changeId);
			EditorTeamChangeEvent resolvedChange = conflict;

			if (resolvedChange.property == "Asset") {
				ApplyAssetChange(resolvedChange);
			}
			else {
				ApplySceneSnapshot(resolvedChange);
			}

			currentRevision_ = (std::max)(currentRevision_, resolvedChange.revision);
			lastSyncedRevision_ = (std::max)(lastSyncedRevision_, resolvedChange.revision);
			lastPropertyRevision_[BuildPropertyKey(resolvedChange)] = resolvedChange.revision;
			AppendChangeLog(resolvedChange);
			SaveSettings();
			if (conflicts_.empty()) CompleteHistorySynchronization((std::max)(serverRevision_, currentRevision_));
			else networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
		}

		if (IsScriptAssetPath(conflict.scenePath)) {
			ImGui::SameLine();

			if (ImGui::Button("両方をMerge")) {
				if (MergeScriptConflict(conflict)) {
					conflicts_.erase(conflicts_.begin());
					if (conflicts_.empty()) CompleteHistorySynchronization((std::max)(serverRevision_, currentRevision_));
					else networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
				}
			}

			if (ImGui::Button("手動編集結果を採用")) {
				std::string manualText = AddUtf8Bom(std::string(manualConflictTextBuffer_.data()));
				EditorTeamChangeEvent resolved = conflict;
				resolved.changeId = CreateEditorTeamUuid();
				resolved.userId = userId_;
				resolved.userName = userNameBuffer_.data();
				resolved.operation = "UpdateAsset";
				resolved.oldValue = conflict.newValue;
				resolved.newValue = manualText;
				resolved.snapshotData = EncodeBase64(manualText);
				resolved.fileSize = static_cast<std::uint64_t>(manualText.size());
				resolved.assetHash = FormatHash(CalculateTextHash(manualText));
				resolved.baseRevision = (std::max)(currentRevision_, conflict.revision);
				if (ApplyAssetChange(resolved)) {
					if (localConflictIterator != conflictLocalChanges_.end()) {
						const std::string localChangeId = localConflictIterator->second.changeId;
						unsyncedChanges_.erase(
							std::remove_if(unsyncedChanges_.begin(), unsyncedChanges_.end(),
								[&localChangeId](const EditorTeamChangeEvent& pending) {
									return pending.changeId == localChangeId;
								}), unsyncedChanges_.end());
					}
					conflictLocalChanges_.erase(conflict.changeId);
					QueueLocalChange(std::move(resolved));
					conflicts_.erase(conflicts_.begin());
					manualConflictId_.clear();
					if (conflicts_.empty()) CompleteHistorySynchronization((std::max)(serverRevision_, currentRevision_));
					else networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
				}
			}
		}
	}

	ImGui::End();
#else
	(void)isWindowVisible;
#endif
}

bool IsEditorTeamGameObjectLockedByAnotherUser(int32_t gameObjectId) {
	return g_activeTeamCollaborationManager != nullptr &&
		g_activeTeamCollaborationManager->IsGameObjectLockedByAnotherUser(gameObjectId);
}

bool IsEditorTeamComponentLockedByAnotherUser(
	int32_t gameObjectId,
	const std::string& componentUuid) {
	return g_activeTeamCollaborationManager != nullptr &&
		g_activeTeamCollaborationManager->IsComponentLockedByAnotherUser(
			gameObjectId,
			componentUuid);
}

bool IsEditorTeamTargetHardLockedByAnotherUser(
	const std::string& targetType,
	const std::string& targetId) {
	return g_activeTeamCollaborationManager != nullptr &&
		g_activeTeamCollaborationManager->IsTargetHardLockedByAnotherUser(targetType, targetId);
}

std::string GetEditorTeamGameObjectEditorLabel(int32_t gameObjectId) {
	return g_activeTeamCollaborationManager != nullptr
		? g_activeTeamCollaborationManager->GetGameObjectEditorLabel(gameObjectId)
		: std::string{};
}

void RequestEditorTeamEditingLock(
	int32_t gameObjectId,
	const std::string& componentUuid) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->RequestEditingLock(gameObjectId, componentUuid);
	}
}

void RequestEditorTeamTargetLock(
	const std::string& targetType,
	const std::string& targetId,
	EditorTeamLockMode mode) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->RequestTargetLock(targetType, targetId, mode);
	}
}

void ReleaseEditorTeamTargetLock(
	const std::string& targetType,
	const std::string& targetId) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->ReleaseTargetLock(targetType, targetId);
	}
}

void ReportEditorTeamActivity(
	const std::string& panel,
	const std::string& action,
	const std::string& componentUuid,
	const std::string& propertyName) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->ReportActivity(panel, action, componentUuid, propertyName);
	}
}

void SetEditorTeamBuildActivity(bool isBuilding) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->SetBuildActivity(isBuilding);
	}
}

EditorTeamItemTargetSummary GetEditorTeamTargetItemSummary(
	const std::string& targetType,
	const std::string& targetId) {
	return g_activeTeamCollaborationManager != nullptr
		? g_activeTeamCollaborationManager->GetTargetItemSummary(targetType, targetId)
		: EditorTeamItemTargetSummary{};
}

std::vector<EditorTeamSceneMarker> GetEditorTeamSceneMarkers() {
	return g_activeTeamCollaborationManager != nullptr
		? g_activeTeamCollaborationManager->GetSceneMarkers()
		: std::vector<EditorTeamSceneMarker>{};
}

void OpenEditorTeamTargetItems(
	const std::string& targetType,
	const std::string& targetId,
	bool startsCreation,
	const std::string& initialKind) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->OpenTargetItems(
			targetType,
			targetId,
			startsCreation,
			initialKind);
	}
}

void OpenEditorTeamScenePositionItems(
	const std::array<float, 3>& worldPosition,
	bool startsCreation,
	const std::string& initialKind) {
	if (g_activeTeamCollaborationManager != nullptr) {
		g_activeTeamCollaborationManager->OpenScenePositionItems(
			worldPosition,
			startsCreation,
			initialKind);
	}
}
