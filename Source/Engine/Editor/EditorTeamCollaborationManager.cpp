#include "EditorTeamCollaborationManager.h"

#pragma warning(push, 0)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma warning(pop)

#include "EditorScene.h"
#include "EditorSharedState.h"
#include "EditorTeamUuid.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)
#endif

#pragma comment(lib, "ws2_32.lib")

namespace {
	EditorTeamCollaborationManager* g_activeTeamCollaborationManager = nullptr;
	constexpr const char* kTeamSettingsPath = "ProjectSettings/TeamCollaboration.settings";
	constexpr const char* kTeamChangeLogPath = ".team/change-log.jsonl";
	constexpr const char* kTeamLiveSnapshotPath = ".team/live/current.scene";
	constexpr const char* kTeamIncomingSnapshotPath = ".team/live/incoming.scene";
	constexpr const char* kTeamAssetUuidRegistryPath = ".team/asset-uuids.txt";
	constexpr float kSnapshotIntervalSeconds = 0.75f;
	constexpr float kAssetScanIntervalSeconds = 2.0f;
	constexpr float kLockHeartbeatIntervalSeconds = 10.0f;
	constexpr float kLockTimeoutSeconds = 30.0f;
	constexpr float kPresenceIntervalSeconds = 5.0f;
	constexpr float kPresenceTimeoutSeconds = 15.0f;
	constexpr std::size_t kMaximumRemoteMemberCount = 2u;
	constexpr std::size_t kMaximumNetworkBufferBytes = 64u * 1024u * 1024u;
	constexpr std::uint64_t kMaximumSynchronizedAssetBytes = 32u * 1024u * 1024u;

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
		const bool isInsideScripts = normalizedText == "resources/scripts" ||
			normalizedText.starts_with("resources/scripts/");

		if ((!isInsideAssets && !isInsideScripts) || normalizedText.find("..") != std::string::npos) {
			return false;
		}

		resolvedPath = normalizedPath;
		return true;
	}

	bool IsScriptAssetPath(const std::string& assetPath) {
		std::string extension = std::filesystem::path(assetPath).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".cpp" || extension == ".h" || extension == ".hpp";
	}

	bool IsIgnoredCollaborationAssetPath(const std::string& assetPath) {
		constexpr const char* ignoredDirectoryPrefixes[] = {
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

	std::string SerializeLockMessage(
		const char* messageType,
		const std::string& objectUuid,
		const std::string& userId,
		const std::string& userName) {
		return
			"{\"type\":\"" + std::string(messageType) +
			"\",\"objectUuid\":\"" + EscapeJsonText(objectUuid) +
			"\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"userName\":\"" + EscapeJsonText(userName) + "\"}";
	}

	std::string SerializePresenceMessage(
		const std::string& userId,
		const std::string& userName) {
		return
			"{\"type\":\"presence\",\"userId\":\"" + EscapeJsonText(userId) +
			"\",\"userName\":\"" + EscapeJsonText(userName) + "\"}";
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

	std::string CalculateFileHash(const std::filesystem::path& filePath) {
		std::ifstream file(filePath, std::ios::binary);

		if (!file.is_open()) {
			return {};
		}

		std::uint64_t hash = 1469598103934665603ull;
		std::array<char, 65536> buffer{};

		while (file.good()) {
			file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const std::streamsize readByteCount = file.gcount();

			for (std::streamsize byteIndex = 0; byteIndex < readByteCount; byteIndex++) {
				hash ^= static_cast<std::uint64_t>(
					static_cast<unsigned char>(buffer[static_cast<std::size_t>(byteIndex)]));
				hash *= 1099511628211ull;
			}
		}

		return FormatHash(hash);
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

	bool SendSocketText(SOCKET socketHandle, const std::string& message) {
		const std::string framedMessage = message + "\n";
		std::size_t sentByteCount = 0u;

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
		}

		return true;
	}

	const char* GetStatusText(EditorTeamConnectionStatus status) {
		switch (status) {
		case EditorTeamConnectionStatus::Connecting: return "Connecting";
		case EditorTeamConnectionStatus::Online: return "Online";
		case EditorTeamConnectionStatus::Synchronizing: return "Synchronizing";
		case EditorTeamConnectionStatus::Conflict: return "Conflict";
		case EditorTeamConnectionStatus::Disconnected: return "Disconnected";
		default: return "Offline";
		}
	}
}

struct EditorTeamCollaborationManager::NetworkState {
	std::thread workerThread;
	std::mutex queueMutex;
	std::mutex errorMutex;
	std::vector<std::string> incomingMessages;
	std::vector<std::string> outgoingMessages;
	std::string errorMessage;
	std::atomic<EditorTeamConnectionStatus> status{EditorTeamConnectionStatus::Offline};
	std::atomic_bool stopsRequested{false};
	std::atomic_bool isServer{false};
	std::atomic_int memberCount{0};
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
	memberRecords_[userId_] = {userNameBuffer_.data(), 0.0f, true};
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
	else if (!isHost_) {
		ConnectToHost();
	}
}

void EditorTeamCollaborationManager::Finalize() {
	if (!isInitialized_ && !networkState_->workerThread.joinable()) {
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

	ProcessIncomingMessages();
	UpdateMemberPresence(deltaTime);
	UpdateSelectionLock(deltaTime);
	UpdateLockTimeouts(deltaTime);
	const bool isConnected = networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online;

	if (isConnected && !wasConnected_) {
		networkState_->status.store(EditorTeamConnectionStatus::Synchronizing, std::memory_order_release);
		lockHeartbeatElapsedSeconds_ = kLockHeartbeatIntervalSeconds;

		if (isHost_) {
			for (EditorTeamChangeEvent& changeEvent : unsyncedChanges_) {
				currentRevision_++;
				changeEvent.revision = currentRevision_;
				lastPropertyRevision_[BuildPropertyKey(changeEvent)] = currentRevision_;
				AppendChangeLog(changeEvent);
				QueueOutgoingMessage(SerializeChangeEvent(changeEvent, "commit"));
			}

			unsyncedChanges_.clear();
			SaveSettings();
		}
		else {
			for (const EditorTeamChangeEvent& changeEvent : unsyncedChanges_) {
				QueueOutgoingMessage(SerializeChangeEvent(changeEvent, "change"));
			}
		}

		networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
		AddConsoleMessage("Team: 接続しました。未同期変更を送信します");
	}

	wasConnected_ = isConnected;
	const int32_t memberCount = networkState_->memberCount.load(std::memory_order_acquire);

	if (isHost_ && memberCount > previousMemberCount_ && memberCount > 0) {
		CaptureSceneChanges(true);

		for (const auto& lockPair : lockedByUserId_) {
			BroadcastLockState(lockPair.first);
		}
	}
	else if (isHost_ && memberCount < previousMemberCount_) {
		for (auto lockIterator = lockedByUserId_.begin(); lockIterator != lockedByUserId_.end();) {
			if (lockIterator->second == userId_) {
				++lockIterator;
				continue;
			}

			const std::string objectUuid = lockIterator->first;
			lockedByUserName_.erase(objectUuid);
			remoteLockIdleSeconds_.erase(objectUuid);
			lockIterator = lockedByUserId_.erase(lockIterator);
			BroadcastLockState(objectUuid);
		}
	}

	previousMemberCount_ = memberCount;

	if (isPlaying || isApplyingRemoteChange_) {
		return;
	}

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

bool EditorTeamCollaborationManager::StartServer() {
	if (!isInitialized_) {
		return false;
	}

	isHost_ = true;
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

	const auto lockIterator = lockedByUserId_.find(gameObject->uuid);
	return lockIterator != lockedByUserId_.end() && lockIterator->second != userId_;
}

void EditorTeamCollaborationManager::LoadSettings() {
	strncpy_s(userNameBuffer_.data(), userNameBuffer_.size(), "User", _TRUNCATE);
	strncpy_s(hostAddressBuffer_.data(), hostAddressBuffer_.size(), "100.64.0.1", _TRUNCATE);
	userId_ = CreateEditorTeamUuid();
	std::ifstream file(kTeamSettingsPath, std::ios::binary);
	std::string line;

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
			strncpy_s(hostAddressBuffer_.data(), hostAddressBuffer_.size(), value.c_str(), _TRUNCATE);
		}
		else if (key == "Port") {
			try {
				port_ = static_cast<uint16_t>((std::clamp)(std::stoi(value), 1, 65535));
			}
			catch (const std::exception&) {
				port_ = 45678u;
			}
		}
		else if (key == "IsHost") {
			isHost_ = value == "1";
		}
		else if (key == "AutoStartServer") {
			startsServerAutomatically_ = value == "1";
		}
		else if (key == "Revision") {
			try {
				currentRevision_ = static_cast<std::uint64_t>(std::stoull(value));
			}
			catch (const std::exception&) {
				currentRevision_ = 0u;
			}
		}
	}
}

void EditorTeamCollaborationManager::SaveSettings() const {
	std::ostringstream settingsText;
	settingsText
		<< "TeamCollaborationSettings|1\r\n"
		<< "UserId|" << userId_ << "\r\n"
		<< "UserName|" << userNameBuffer_.data() << "\r\n"
		<< "HostAddress|" << hostAddressBuffer_.data() << "\r\n"
		<< "Port|" << port_ << "\r\n"
		<< "IsHost|" << (isHost_ ? 1 : 0) << "\r\n"
		<< "AutoStartServer|" << (startsServerAutomatically_ ? 1 : 0) << "\r\n"
		<< "Revision|" << currentRevision_ << "\r\n";
	WriteUtf8BomTextFile(kTeamSettingsPath, settingsText.str());
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
		changeEvent.snapshotData = snapshotText;
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

	std::unordered_map<std::string, AssetRecord> currentAssetRecords;
	const std::filesystem::path scanRoots[] = {"Assets", "resources/scripts"};

	for (const std::filesystem::path& scanRoot : scanRoots) {
		std::error_code iteratorError;

		if (!std::filesystem::exists(scanRoot, iteratorError)) {
			continue;
		}

		std::filesystem::recursive_directory_iterator fileIterator(
			scanRoot,
			std::filesystem::directory_options::skip_permission_denied,
			iteratorError);
		const std::filesystem::recursive_directory_iterator endIterator;

		for (; fileIterator != endIterator; fileIterator.increment(iteratorError)) {
			if (iteratorError) {
				iteratorError.clear();
				continue;
			}

			const std::string candidatePath =
				fileIterator->path().lexically_normal().generic_string();

			if (fileIterator->is_directory(iteratorError) &&
				IsIgnoredCollaborationAssetPath(candidatePath + "/")) {
				fileIterator.disable_recursion_pending();
				continue;
			}

			if (!fileIterator->is_regular_file(iteratorError) ||
				IsIgnoredCollaborationAssetPath(candidatePath)) {
				continue;
			}

			const std::string assetPath = candidatePath;
			AssetRecord assetRecord{};
			assetRecord.fileSize = static_cast<std::uint64_t>(fileIterator->file_size(iteratorError));
			assetRecord.lastWriteTimestamp = static_cast<std::int64_t>(
				fileIterator->last_write_time(iteratorError).time_since_epoch().count());
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
				assetRecord.hash = recordsBaselineOnly
					? "BASE:" + std::to_string(assetRecord.fileSize) + ":" +
						std::to_string(assetRecord.lastWriteTimestamp)
					: CalculateFileHash(fileIterator->path());

				if (IsScriptAssetPath(assetPath) &&
					assetRecord.fileSize <= kMaximumSynchronizedAssetBytes) {
					ReadBinaryTextFile(fileIterator->path(), assetRecord.textContent);
				}
			}

			currentAssetRecords[assetPath] = std::move(assetRecord);
		}
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
			if (currentAssetRecords.find(previousAssetPair.first) == currentAssetRecords.end()) {
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

	if (isHost_ && networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online) {
		currentRevision_++;
		changeEvent.revision = currentRevision_;
		lastPropertyRevision_[BuildPropertyKey(changeEvent)] = currentRevision_;
		AppendChangeLog(changeEvent);
		QueueOutgoingMessage(SerializeChangeEvent(changeEvent, "commit"));
		SaveSettings();
		return;
	}

	unsyncedChanges_.push_back(changeEvent);
	AppendChangeLog(changeEvent);

	if (networkState_->status.load(std::memory_order_acquire) ==
		EditorTeamConnectionStatus::Online) {
		QueueOutgoingMessage(SerializeChangeEvent(changeEvent, "change"));
	}
}

void EditorTeamCollaborationManager::ProcessIncomingMessages() {
	std::vector<std::string> incomingMessages;

	{
		std::lock_guard<std::mutex> queueLock(networkState_->queueMutex);
		incomingMessages.swap(networkState_->incomingMessages);
	}

	for (const std::string& message : incomingMessages) {
		const std::string messageType = ReadJsonString(message, "type");

		if (messageType == "lockRequest" || messageType == "unlock" || messageType == "lockState") {
			ProcessLockMessage(message, messageType);
			continue;
		}

		if (messageType == "presence") {
			ProcessPresenceMessage(message);
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

void EditorTeamCollaborationManager::ProcessRemoteChange(
	EditorTeamChangeEvent changeEvent,
	bool isCommitted) {
	if (changeEvent.timestampUnixMilliseconds == 0u) {
		changeEvent.timestampUnixMilliseconds = GetCurrentUnixTimestampMilliseconds();
	}

	const std::string propertyKey = BuildPropertyKey(changeEvent);
	const auto revisionIterator = lastPropertyRevision_.find(propertyKey);
	bool hasConflict = !isCommitted &&
		revisionIterator != lastPropertyRevision_.end() &&
		revisionIterator->second > changeEvent.baseRevision;
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
		PrepareConflictCopies(changeEvent);
		conflicts_.push_back(std::move(changeEvent));
		networkState_->status.store(EditorTeamConnectionStatus::Conflict, std::memory_order_release);
		AddConsoleMessage("Team: 同一Sceneのオフライン変更が競合しました");
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
		AppendChangeLog(changeEvent);
		QueueOutgoingMessage(SerializeChangeEvent(changeEvent, "commit"));
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
	lastPropertyRevision_[propertyKey] = changeEvent.revision;

	unsyncedChanges_.erase(
		std::remove_if(
			unsyncedChanges_.begin(),
			unsyncedChanges_.end(),
			[&changeEvent](const EditorTeamChangeEvent& pendingChange) {
				return pendingChange.changeId == changeEvent.changeId;
			}),
		unsyncedChanges_.end());
	AppendChangeLog(changeEvent);
	SaveSettings();
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
	const std::filesystem::path remoteScenePath = conflictDirectory / "Remote.scene";
	const std::filesystem::path resolutionScenePath = conflictDirectory / "Resolution.scene";
	editorScene_->SaveScene(localScenePath.generic_string());
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
	QueueOutgoingMessage(SerializePresenceMessage(userId_, userNameBuffer_.data()));
}

void EditorTeamCollaborationManager::ProcessPresenceMessage(const std::string& message) {
	const std::string presenceUserId = ReadJsonString(message, "userId");
	const std::string presenceUserName = ReadJsonString(message, "userName");

	if (presenceUserId.empty() || presenceUserId == userId_) {
		return;
	}

	memberRecords_[presenceUserId] = {presenceUserName, 0.0f, true};

	if (isHost_) {
		QueueOutgoingMessage(SerializePresenceMessage(presenceUserId, presenceUserName));
	}
}

void EditorTeamCollaborationManager::UpdateSelectionLock(float deltaTime) {
	std::string currentObjectUuid;
	const int32_t selectedGameObjectId = EditorSharedState::g_selectedEditorGameObjectId;

	if (editorScene_ != nullptr && selectedGameObjectId >= 0) {
		const EditorGameObject* selectedGameObject = editorScene_->FindGameObject(selectedGameObjectId);

		if (selectedGameObject != nullptr) {
			currentObjectUuid = selectedGameObject->uuid;
		}
	}

	const bool isOnline = GetStatus() == EditorTeamConnectionStatus::Online ||
		GetStatus() == EditorTeamConnectionStatus::Synchronizing;
	lockHeartbeatElapsedSeconds_ += (std::max)(deltaTime, 0.0f);

	if (currentObjectUuid == selectedObjectUuid_) {
		if (!isHost_ && isOnline && !selectedObjectUuid_.empty() &&
			lockHeartbeatElapsedSeconds_ >= kLockHeartbeatIntervalSeconds) {
			QueueOutgoingMessage(SerializeLockMessage(
				"lockRequest",
				selectedObjectUuid_,
				userId_,
				userNameBuffer_.data()));
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

	if (objectUuid.empty()) {
		return;
	}

	if (messageType == "lockState") {
		if (lockUserId.empty()) {
			lockedByUserId_.erase(objectUuid);
			lockedByUserName_.erase(objectUuid);
		}
		else {
			lockedByUserId_[objectUuid] = lockUserId;
			lockedByUserName_[objectUuid] = lockUserName;
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
	QueueOutgoingMessage(SerializeLockMessage(
		"lockState",
		objectUuid,
		lockUserId,
		lockUserName));
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
		CalculateFileHash(assetPath) == changeEvent.assetHash) {
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
	AddConsoleMessage("Team Asset: " + changeEvent.operation + " " + changeEvent.scenePath);
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

	if (localText == changeEvent.oldValue) {
		mergedText = changeEvent.newValue;
	}
	else if (changeEvent.newValue == changeEvent.oldValue ||
		localText == changeEvent.newValue) {
		mergedText = localText;
	}
	else {
		mergedText = AddUtf8Bom(
			"<<<<<<< Local " + std::string(userNameBuffer_.data()) + "\r\n" +
			RemoveUtf8Bom(localText) +
			"\r\n=======\r\n" +
			RemoveUtf8Bom(changeEvent.newValue) +
			"\r\n>>>>>>> Remote " + changeEvent.userName + "\r\n");
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

void EditorTeamCollaborationManager::QueueOutgoingMessage(const std::string& message) {
	std::lock_guard<std::mutex> queueLock(networkState_->queueMutex);
	networkState_->outgoingMessages.push_back(message);
}

void EditorTeamCollaborationManager::StartNetworkThread(bool startsAsServer) {
	StopNetworkThread();
	networkState_->stopsRequested.store(false, std::memory_order_release);
	networkState_->isServer.store(startsAsServer, std::memory_order_release);
	networkState_->status.store(EditorTeamConnectionStatus::Connecting, std::memory_order_release);
	const std::string hostAddress = hostAddressBuffer_.data();
	const uint16_t serverPort = port_;
	NetworkState* networkState = networkState_.get();

	networkState_->workerThread = std::thread([networkState, hostAddress, serverPort]() {
		WSADATA socketData{};

		if (WSAStartup(MAKEWORD(2, 2), &socketData) != 0) {
			networkState->status.store(EditorTeamConnectionStatus::Disconnected, std::memory_order_release);
			return;
		}

		if (networkState->isServer.load(std::memory_order_acquire)) {
			SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in serverAddress{};
			serverAddress.sin_family = AF_INET;
			serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
			serverAddress.sin_port = htons(serverPort);
			const BOOL reuseAddress = TRUE;
			setsockopt(
				listenSocket,
				SOL_SOCKET,
				SO_REUSEADDR,
				reinterpret_cast<const char*>(&reuseAddress),
				sizeof(reuseAddress));

			if (listenSocket == INVALID_SOCKET ||
				bind(listenSocket, reinterpret_cast<const sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR ||
				listen(listenSocket, 3) == SOCKET_ERROR) {
				std::lock_guard<std::mutex> errorLock(networkState->errorMutex);
				networkState->errorMessage = "TCP Serverを開始できません。Portを確認してください";
				networkState->status.store(EditorTeamConnectionStatus::Disconnected, std::memory_order_release);

				if (listenSocket != INVALID_SOCKET) {
					closesocket(listenSocket);
				}

				WSACleanup();
				return;
			}

			u_long nonBlockingMode = 1u;
			ioctlsocket(listenSocket, FIONBIO, &nonBlockingMode);
			std::vector<SOCKET> clientSockets;
			std::unordered_map<SOCKET, std::string> receiveBuffers;
			networkState->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);

			while (!networkState->stopsRequested.load(std::memory_order_acquire)) {
				SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

				if (clientSocket != INVALID_SOCKET) {
					if (clientSockets.size() < kMaximumRemoteMemberCount) {
						clientSockets.push_back(clientSocket);
						receiveBuffers[clientSocket] = {};
						networkState->memberCount.store(
							static_cast<int32_t>(clientSockets.size()),
							std::memory_order_release);
					}
					else {
						closesocket(clientSocket);
					}
				}

				std::vector<std::string> outgoingMessages;

				{
					std::lock_guard<std::mutex> queueLock(networkState->queueMutex);
					outgoingMessages.swap(networkState->outgoingMessages);
				}

				for (auto clientIterator = clientSockets.begin(); clientIterator != clientSockets.end();) {
					SOCKET activeSocket = *clientIterator;
					bool keepsClient = true;

					for (const std::string& outgoingMessage : outgoingMessages) {
						if (!SendSocketText(activeSocket, outgoingMessage)) {
							keepsClient = false;
							break;
						}
					}

					std::array<char, 65536> receiveBuffer{};
					const int receivedByteCount = recv(
						activeSocket,
						receiveBuffer.data(),
						static_cast<int>(receiveBuffer.size()),
						0);

					if (receivedByteCount > 0) {
						std::string& pendingText = receiveBuffers[activeSocket];
						pendingText.append(receiveBuffer.data(), static_cast<std::size_t>(receivedByteCount));
						std::size_t lineEnd = pendingText.find('\n');

						while (lineEnd != std::string::npos) {
							std::string message = pendingText.substr(0u, lineEnd);
							pendingText.erase(0u, lineEnd + 1u);
							std::lock_guard<std::mutex> queueLock(networkState->queueMutex);
							networkState->incomingMessages.push_back(std::move(message));
							lineEnd = pendingText.find('\n');
						}

						if (pendingText.size() > kMaximumNetworkBufferBytes) {
							keepsClient = false;
						}
					}
					else if (receivedByteCount == 0) {
						keepsClient = false;
					}
					else if (WSAGetLastError() != WSAEWOULDBLOCK) {
						keepsClient = false;
					}

					if (!keepsClient) {
						closesocket(activeSocket);
						receiveBuffers.erase(activeSocket);
						clientIterator = clientSockets.erase(clientIterator);
						networkState->memberCount.store(
							static_cast<int32_t>(clientSockets.size()),
							std::memory_order_release);
					}
					else {
						++clientIterator;
					}
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(16));
			}

			for (const SOCKET clientSocketToClose : clientSockets) {
				closesocket(clientSocketToClose);
			}

			closesocket(listenSocket);
		}
		else {
			while (!networkState->stopsRequested.load(std::memory_order_acquire)) {
				SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				u_long nonBlockingMode = 1u;

				if (serverSocket != INVALID_SOCKET) {
					ioctlsocket(serverSocket, FIONBIO, &nonBlockingMode);
				}

				sockaddr_in serverAddress{};
				serverAddress.sin_family = AF_INET;
				serverAddress.sin_port = htons(serverPort);
				const bool hasValidAddress = inet_pton(
					AF_INET,
					hostAddress.c_str(),
					&serverAddress.sin_addr) == 1;

				bool isConnected = false;

				if (hasValidAddress && serverSocket != INVALID_SOCKET) {
					const int connectResult = connect(
						serverSocket,
						reinterpret_cast<const sockaddr*>(&serverAddress),
						sizeof(serverAddress));

					if (connectResult == 0) {
						isConnected = true;
					}
					else if (WSAGetLastError() == WSAEWOULDBLOCK) {
						fd_set writableSockets;
						FD_ZERO(&writableSockets);
						FD_SET(serverSocket, &writableSockets);
						timeval connectTimeout{};
						connectTimeout.tv_sec = 1;
						const int selectResult = select(
							0,
							nullptr,
							&writableSockets,
							nullptr,
							&connectTimeout);
						int socketError = SOCKET_ERROR;
						int socketErrorSize = sizeof(socketError);

						if (selectResult > 0 &&
							getsockopt(
								serverSocket,
								SOL_SOCKET,
								SO_ERROR,
								reinterpret_cast<char*>(&socketError),
								&socketErrorSize) == 0 &&
							socketError == 0) {
							isConnected = true;
						}
					}
				}

				if (!isConnected) {
					if (serverSocket != INVALID_SOCKET) {
						closesocket(serverSocket);
					}

					networkState->status.store(EditorTeamConnectionStatus::Offline, std::memory_order_release);

					for (int32_t retryWaitIndex = 0;
						retryWaitIndex < 10 && !networkState->stopsRequested.load(std::memory_order_acquire);
						retryWaitIndex++) {
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					}

					continue;
				}

				networkState->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
				std::string pendingText;
				bool keepsConnection = true;

				while (keepsConnection && !networkState->stopsRequested.load(std::memory_order_acquire)) {
					std::vector<std::string> outgoingMessages;

					{
						std::lock_guard<std::mutex> queueLock(networkState->queueMutex);
						outgoingMessages.swap(networkState->outgoingMessages);
					}

					for (const std::string& outgoingMessage : outgoingMessages) {
						if (!SendSocketText(serverSocket, outgoingMessage)) {
							keepsConnection = false;
							break;
						}
					}

					std::array<char, 65536> receiveBuffer{};
					const int receivedByteCount = recv(
						serverSocket,
						receiveBuffer.data(),
						static_cast<int>(receiveBuffer.size()),
						0);

					if (receivedByteCount > 0) {
						pendingText.append(receiveBuffer.data(), static_cast<std::size_t>(receivedByteCount));
						std::size_t lineEnd = pendingText.find('\n');

						while (lineEnd != std::string::npos) {
							std::string message = pendingText.substr(0u, lineEnd);
							pendingText.erase(0u, lineEnd + 1u);
							std::lock_guard<std::mutex> queueLock(networkState->queueMutex);
							networkState->incomingMessages.push_back(std::move(message));
							lineEnd = pendingText.find('\n');
						}

						keepsConnection = pendingText.size() <= kMaximumNetworkBufferBytes;
					}
					else if (receivedByteCount == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
						keepsConnection = false;
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(16));
				}

				closesocket(serverSocket);
				networkState->status.store(EditorTeamConnectionStatus::Disconnected, std::memory_order_release);
			}
		}

		networkState->memberCount.store(0, std::memory_order_release);
		networkState->status.store(EditorTeamConnectionStatus::Offline, std::memory_order_release);
		WSACleanup();
	});
}

void EditorTeamCollaborationManager::StopNetworkThread() {
	networkState_->stopsRequested.store(true, std::memory_order_release);

	if (networkState_->workerThread.joinable()) {
		networkState_->workerThread.join();
	}

	networkState_->memberCount.store(0, std::memory_order_release);
	networkState_->status.store(EditorTeamConnectionStatus::Offline, std::memory_order_release);
	wasConnected_ = false;
}

void EditorTeamCollaborationManager::AddConsoleMessage(const std::string& message) const {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}

void EditorTeamCollaborationManager::Draw(bool* isWindowVisible) {
#ifdef USE_IMGUI
	if (isWindowVisible == nullptr || !*isWindowVisible) {
		return;
	}

	if (!ImGui::Begin("TEAM - 共同制作", isWindowVisible, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Status: %s", GetStatusText(GetStatus()));
	ImGui::Text("Revision: %llu", static_cast<unsigned long long>(currentRevision_));
	int32_t connectedMemberCount = 0;

	for (const auto& memberPair : memberRecords_) {
		if (memberPair.second.isOnline) {
			connectedMemberCount++;
		}
	}

	ImGui::Text("Members: %d", connectedMemberCount);
	ImGui::Text("Unsynced Changes: %llu", static_cast<unsigned long long>(unsyncedChanges_.size()));
	ImGui::Text("Conflicts: %llu", static_cast<unsigned long long>(conflicts_.size()));
	ImGui::Text("Editing Locks: %llu", static_cast<unsigned long long>(lockedByUserId_.size()));
	ImGui::SeparatorText("Members");

	for (const auto& memberPair : memberRecords_) {
		const MemberRecord& memberRecord = memberPair.second;
		const char* memberStatus = memberPair.first == userId_
			? GetStatusText(GetStatus())
			: memberRecord.isOnline
				? "Online"
				: "Offline";
		ImGui::BulletText("%s - %s", memberRecord.userName.c_str(), memberStatus);
	}

	ImGui::Separator();
	ImGui::InputText("User Name", userNameBuffer_.data(), userNameBuffer_.size());
	ImGui::InputText("Host", hostAddressBuffer_.data(), hostAddressBuffer_.size());
	int32_t portValue = static_cast<int32_t>(port_);

	if (ImGui::InputInt("Port", &portValue)) {
		port_ = static_cast<uint16_t>((std::clamp)(portValue, 1, 65535));
	}

	ImGui::Checkbox("このPCをHostにする", &isHost_);
	ImGui::Checkbox("Start Team Server Automatically", &startsServerAutomatically_);

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
	else if (ImGui::Button(isHost_ ? "Server Stop" : "切断")) {
		Disconnect();
	}

	ImGui::SameLine();

	if (ImGui::Button("設定保存")) {
		SaveSettings();
	}

	if (!lastError_.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", lastError_.c_str());
	}

	if (!conflicts_.empty()) {
		ImGui::SeparatorText("Conflicts");
		const EditorTeamChangeEvent conflict = conflicts_.front();
		ImGui::Text("%s / %s", conflict.userName.c_str(), conflict.scenePath.c_str());
		ImGui::Text("%s : %s", conflict.operation.c_str(), conflict.property.c_str());
		const std::string conflictDirectory =
			BuildConflictDirectoryPath(conflict.changeId).generic_string();
		ImGui::TextWrapped("Conflict Copy: %s", conflictDirectory.c_str());

		if (ImGui::Button("自分側を採用")) {
			conflicts_.erase(conflicts_.begin());
			networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);

			if (conflict.property == "Asset") {
				const auto assetIterator = assetRecords_.find(conflict.scenePath);

				if (assetIterator != assetRecords_.end()) {
					QueueAssetChange(conflict.scenePath, nullptr, &assetIterator->second);
				}
			}
			else {
				CaptureSceneChanges(true);
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("相手側を採用")) {
			conflicts_.erase(conflicts_.begin());
			networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
			EditorTeamChangeEvent resolvedChange = conflict;
			resolvedChange.changeId = CreateEditorTeamUuid();
			resolvedChange.userId = userId_;
			resolvedChange.userName = userNameBuffer_.data();
			resolvedChange.baseRevision = currentRevision_;

			if (resolvedChange.property == "Asset") {
				ApplyAssetChange(resolvedChange);
			}
			else {
				ApplySceneSnapshot(resolvedChange);
			}

			QueueLocalChange(std::move(resolvedChange));
		}

		if (IsScriptAssetPath(conflict.scenePath)) {
			ImGui::SameLine();

			if (ImGui::Button("両方をMerge")) {
				networkState_->status.store(EditorTeamConnectionStatus::Online, std::memory_order_release);
				MergeScriptConflict(conflict);
				conflicts_.erase(conflicts_.begin());
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
