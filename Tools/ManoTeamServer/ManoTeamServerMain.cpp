// ManoTeamServer
//
// 共同制作の中継Server。誰かのEditorがHostとして起動していなくても、
// 各自のEditorがここへ接続するだけで共同制作できるようにするために独立させた。
//
//                ManoTeamServer
//                      │
//         ┌────────────┼────────────┐
//         │            │            │
//      Editor A     Editor B     Editor C
//      自宅          大学          遠隔地
//
// 通信経路は ICollaborationTransport に委譲しているため、このServer自体は
// LANかTailscaleかを一切知らない。将来WebSocket/Cloudへ移す場合もTransport差し替えで済む。
//
// Serverが担当するのは「接続受付・Project識別・Presence・Lock・Revision・
// Conflict情報・Scene/Asset/Script中継・Change Log」であり、
// Sceneの意味解釈(差分の中身・Merge・Conflict解決)はEditor側のまま変更しない。

#include "Source/Engine/Collaboration/CollaborationProtocol.h"
#include "Source/Engine/Collaboration/TcpCollaborationTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <process.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// 既に配布済みのEditorとその設定ファイルをそのまま接続できる互換ポート。
constexpr std::uint16_t kDefaultPort = 45678u;
constexpr std::int32_t kDefaultMaximumClientCount = 4;

// ---------------------------------------------------------------------------
// 最小限のJSON読み取り。Editor側と同じ「1行1メッセージのフラットなJSON」しか扱わない。
// 完全なJSONパーサは持たず、想定外の入力は「読めない＝空」として安全側に倒す。
// ---------------------------------------------------------------------------
std::string ReadJsonString(const std::string& jsonText, const char* key) {
	// Editorは空白無しのJSONを送るが、他の実装が空白入りで送ってきても読めるようにしておく。
	// 読めずに「空」と解釈すると、検証をすり抜けたように見えてしまうため。
	const std::string pattern = std::string("\"") + key + "\"";
	const std::size_t keyPosition = jsonText.find(pattern);

	if (keyPosition == std::string::npos) {
		return {};
	}

	std::size_t cursor = keyPosition + pattern.size();

	while (cursor < jsonText.size() && (jsonText[cursor] == ' ' || jsonText[cursor] == '\t')) {
		cursor++;
	}

	if (cursor >= jsonText.size() || jsonText[cursor] != ':') {
		return {};
	}

	cursor++;

	while (cursor < jsonText.size() && (jsonText[cursor] == ' ' || jsonText[cursor] == '\t')) {
		cursor++;
	}

	if (cursor >= jsonText.size() || jsonText[cursor] != '"') {
		return {};
	}

	std::size_t valueStart = cursor + 1u;
	std::string value;

	while (valueStart < jsonText.size() && jsonText[valueStart] != '"') {
		if (jsonText[valueStart] == '\\' && valueStart + 1u < jsonText.size()) {
			const char escaped = jsonText[valueStart + 1u];
			valueStart += 2u;

			switch (escaped) {
			case 'n': value += '\n'; break;
			case 'r': value += '\r'; break;
			case 't': value += '\t'; break;
			default: value += escaped; break;
			}

			continue;
		}

		value += jsonText[valueStart];
		valueStart++;
	}

	return value;
}

std::uint64_t ReadJsonUnsigned(const std::string& jsonText, const char* key) {
	const std::string pattern = std::string("\"") + key + "\"";
	const std::size_t keyPosition = jsonText.find(pattern);

	if (keyPosition == std::string::npos) {
		return 0u;
	}

	std::size_t valueStart = keyPosition + pattern.size();

	while (valueStart < jsonText.size() && (jsonText[valueStart] == ' ' || jsonText[valueStart] == '\t')) {
		valueStart++;
	}

	if (valueStart >= jsonText.size() || jsonText[valueStart] != ':') {
		return 0u;
	}

	valueStart++;

	while (valueStart < jsonText.size() &&
		(jsonText[valueStart] == ' ' || jsonText[valueStart] == '\t' || jsonText[valueStart] == '"')) {
		valueStart++;
	}

	std::uint64_t value = 0u;

	while (valueStart < jsonText.size() && jsonText[valueStart] >= '0' && jsonText[valueStart] <= '9') {
		value = value * 10u + static_cast<std::uint64_t>(jsonText[valueStart] - '0');
		valueStart++;
	}

	return value;
}

std::string EscapeJsonText(const std::string& text) {
	std::string escaped;
	escaped.reserve(text.size() + 16u);

	for (const char character : text) {
		switch (character) {
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default: escaped += character; break;
		}
	}

	return escaped;
}

// 共同制作ProtocolのJSONは平坦な1行形式に限定されているため、ServerがRevisionを
// 採番するFieldだけを安全に置換する。完全なJSON再Serializeで大きなPayloadを複製しない。
std::string SetJsonUnsigned(std::string jsonText, const char* key, std::uint64_t value) {
	const std::string keyMarker = std::string("\"") + key + "\"";
	const std::size_t keyPosition = jsonText.find(keyMarker);
	if (keyPosition == std::string::npos) {
		if (!jsonText.empty() && jsonText.back() == '}') {
			jsonText.insert(jsonText.size() - 1U, ",\"" + std::string(key) + "\":" + std::to_string(value));
		}
		return jsonText;
	}
	const std::size_t colon = jsonText.find(':', keyPosition + keyMarker.size());
	if (colon == std::string::npos) return jsonText;
	std::size_t valueStart = colon + 1U;
	while (valueStart < jsonText.size() &&
		(jsonText[valueStart] == ' ' || jsonText[valueStart] == '\t' || jsonText[valueStart] == '"')) ++valueStart;
	std::size_t valueEnd = valueStart;
	while (valueEnd < jsonText.size() && jsonText[valueEnd] >= '0' && jsonText[valueEnd] <= '9') ++valueEnd;
	jsonText.replace(valueStart, valueEnd - valueStart, std::to_string(value));
	return jsonText;
}

std::string SetJsonString(std::string jsonText, const char* key, const std::string& value) {
	const std::string keyMarker = std::string("\"") + key + "\"";
	const std::size_t keyPosition = jsonText.find(keyMarker);
	if (keyPosition == std::string::npos) {
		if (!jsonText.empty() && jsonText.back() == '}') {
			jsonText.insert(jsonText.size() - 1U, ",\"" + std::string(key) + "\":\"" + EscapeJsonText(value) + "\"");
		}
		return jsonText;
	}
	const std::size_t colon = jsonText.find(':', keyPosition + keyMarker.size());
	if (colon == std::string::npos) return jsonText;
	const std::size_t quote = jsonText.find('"', colon + 1U);
	if (quote == std::string::npos) return jsonText;
	const std::size_t valueStart = quote + 1U;
	std::size_t valueEnd = valueStart;
	bool escaped = false;
	for (; valueEnd < jsonText.size(); ++valueEnd) {
		if (!escaped && jsonText[valueEnd] == '"') break;
		escaped = !escaped && jsonText[valueEnd] == '\\';
		if (jsonText[valueEnd] != '\\') escaped = false;
	}
	jsonText.replace(valueStart, valueEnd - valueStart, EscapeJsonText(value));
	return jsonText;
}

std::uint64_t GetCurrentUnixTimestampMilliseconds() {
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

std::string GetTimeText() {
	const std::time_t now = std::time(nullptr);
	std::tm localTime{};
	localtime_s(&localTime, &now);
	char buffer[32] = {};
	std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime);
	return buffer;
}

void LogLine(const std::string& message) {
	std::cout << "[" << GetTimeText() << "] " << message << std::endl;
}

// ---------------------------------------------------------------------------
// Remote Clientの送信内容を無条件に信用しない。
// Project外への書き込みや、巨大Payloadによる資源枯渇を、中継前にここで弾く。
// ---------------------------------------------------------------------------

// Project Root基準へ正規化できないPathを拒否する。
// "../" や絶対Pathで Project 外へ出るものは中継しない。
bool IsSafeRelativeAssetPath(const std::string& path) {
	if (path.empty() || path.size() > 1024u) {
		return false;
	}

	// ドライブ文字・UNC・ルート開始はすべて絶対Pathとして拒否する。
	if (path.find(':') != std::string::npos) {
		return false;
	}

	if (path[0] == '/' || path[0] == '\\') {
		return false;
	}

	std::string normalized;
	normalized.reserve(path.size());

	for (const char character : path) {
		normalized += (character == '\\') ? '/' : character;
	}

	// 正規化しながら深さを数え、どの時点でもRootより上へ出ないことを確認する。
	std::int32_t depth = 0;
	std::size_t segmentStart = 0u;

	while (segmentStart <= normalized.size()) {
		const std::size_t segmentEnd = normalized.find('/', segmentStart);
		const std::string segment = normalized.substr(
			segmentStart,
			segmentEnd == std::string::npos ? std::string::npos : segmentEnd - segmentStart);

		if (segment == "..") {
			depth--;

			if (depth < 0) {
				return false;
			}
		}
		else if (!segment.empty() && segment != ".") {
			depth++;
		}

		if (segmentEnd == std::string::npos) {
			break;
		}

		segmentStart = segmentEnd + 1u;
	}

	return true;
}

struct ClientRecord {
	std::string userId;
	std::string userName;
	std::string projectId;
	std::string currentSceneOrAsset;
	float silenceSeconds = 0.0f;
	bool hasCompletedHandshake = false;
};

class TeamServer {
public:
	bool Start(
		std::uint16_t port,
		const std::string& projectId,
		std::int32_t maximumClientCount,
		const std::filesystem::path& dataDirectory,
		const std::filesystem::path& statusFile,
		const std::filesystem::path& processIdFile) {
		projectId_ = projectId;
		dataDirectory_ = dataDirectory;
		statusFile_ = statusFile;
		std::error_code directoryError;
		std::filesystem::create_directories(dataDirectory_, directoryError);
		LoadRevision();

		ManoCollaboration::TransportConfig config{};
		config.maximumClientCount = maximumClientCount;
		std::string error;

		if (!transport_.Listen(port, config, error)) {
			LogLine("起動失敗: " + error);
			return false;
		}

		// Windowsログオン時の自動起動ではEditorがPIDを書けないため、Server自身が記録する。
		// これが無いと、別Project用の古いServerをEditorが検出・停止できず再利用してしまう。
		if (!processIdFile.empty()) {
			std::error_code processIdDirectoryError;
			std::filesystem::create_directories(processIdFile.parent_path(), processIdDirectoryError);
			std::ofstream processIdOutput(processIdFile, std::ios::binary | std::ios::trunc);
			if (processIdOutput) processIdOutput << _getpid() << "\r\n";
		}

		LogLine("ManoTeamServer 起動");
		LogLine("  Project ID : " + projectId_);
		LogLine("  Port       : " + std::to_string(port));
		LogLine("  Max Clients: " + std::to_string(maximumClientCount));
		LogLine("  Protocol   : " +
			std::to_string(ManoCollaboration::kCollaborationProtocolVersion));
		LogLine("  Revision   : " + std::to_string(revision_));
		LogLine("  Data       : " + dataDirectory_.string());
		LogLine("LAN/Tailscaleのどちらからでも、このPortへ接続できれば共同制作できます");
		WriteStatus(port);
		return true;
	}

	void Run() {
		auto previousTime = std::chrono::steady_clock::now();

		while (true) {
			const auto currentTime = std::chrono::steady_clock::now();
			const float deltaSeconds =
				std::chrono::duration<float>(currentTime - previousTime).count();
			previousTime = currentTime;

			std::vector<std::string> messages;
			transport_.Poll(messages);

			for (const std::string& message : messages) {
				HandleMessage(message);
			}

			UpdatePresence(deltaSeconds);
			ReportPeerCountChange();
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	}

private:
	ManoCollaboration::TcpCollaborationTransport transport_;
	std::string projectId_;
	std::filesystem::path dataDirectory_;
	std::filesystem::path statusFile_;
	std::map<std::string, ClientRecord> clients_;
	// Lock所有者。Editorが作ったGameObject/Component/Property/Asset/Prefab/Scene共通Keyを保持する。
	std::map<std::string, std::string> lockOwnerByKey_;
	std::map<std::string, std::string> lockStateMessageByKey_;
	std::map<std::string, std::uint64_t> activeTransferRevisions_;
	std::uint64_t revision_ = 0u;
	std::int32_t previousPeerCount_ = 0;
	std::uint16_t listeningPort_ = 0U;

	void WriteStatus(std::uint16_t port = 0U) {
		if (port != 0U) listeningPort_ = port;
		if (statusFile_.empty()) return;
		std::error_code error; std::filesystem::create_directories(statusFile_.parent_path(), error);
		std::ofstream file(statusFile_, std::ios::binary | std::ios::trunc);
		// Project IDも記録し、Editorが別Project用の古いServerを同一視しないようにする。
		if (file) file << "Running|" << listeningPort_ << '|' << transport_.GetPeerCount() << '|'
			<< revision_ << '|' << projectId_ << "\r\n";
	}

	void SendText(const std::string& text) {
		ManoCollaboration::TransportMessage message{};
		message.text = text;

		if (message.text.empty() || message.text.back() != '\n') {
			message.text += '\n';
		}

		transport_.Send(message);
	}

	void LoadRevision() {
		std::ifstream revisionFile(dataDirectory_ / "revision.txt");

		if (revisionFile) {
			revisionFile >> revision_;
		}
	}

	void SaveRevision() const {
		std::ofstream revisionFile(dataDirectory_ / "revision.txt", std::ios::trunc);

		if (revisionFile) {
			revisionFile << revision_;
		}
	}

	// Change Log は追記のみ。Editor側の .team/change-log.jsonl と同じ考え方で、
	// 「誰が・いつ・何を」を後から追える形で残す。
	void AppendChangeLog(const std::string& message) const {
		std::ofstream logFile(dataDirectory_ / "change-log.jsonl", std::ios::app | std::ios::binary);

		if (logFile) {
			logFile << message << "\n";
		}
	}

	std::string CommitChange(std::string message) {
		revision_++;
		message = SetJsonString(std::move(message), "type", "commit");
		message = SetJsonUnsigned(std::move(message), "revision", revision_);
		SaveRevision();
		AppendChangeLog(message);
		WriteStatus();
		return message;
	}

	void SendHistory(const std::string& userId, std::uint64_t afterRevision) {
		SendText("{\"type\":\"historyBegin\",\"targetUserId\":\"" + EscapeJsonText(userId) +
			"\",\"afterRevision\":" + std::to_string(afterRevision) + ",\"serverRevision\":" + std::to_string(revision_) + "}");
		std::ifstream logFile(dataDirectory_ / "change-log.jsonl", std::ios::binary);
		std::string line;
		bool sendsTransfer = false;
		while (std::getline(logFile, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const std::string type = ReadJsonString(line, "type");
			if (type == "commit") {
				sendsTransfer = false;
				if (ReadJsonUnsigned(line, "revision") <= afterRevision) continue;
				SendText(SetJsonString(std::move(line), "targetUserId", userId));
			}
			else if (type == "assetBegin") {
				sendsTransfer = ReadJsonUnsigned(line, "revision") > afterRevision;
				if (sendsTransfer) SendText(SetJsonString(std::move(line), "targetUserId", userId));
			}
			else if ((type == "assetChunk" || type == "assetEnd") && sendsTransfer) {
				SendText(SetJsonString(std::move(line), "targetUserId", userId));
				if (type == "assetEnd") sendsTransfer = false;
			}
		}
		SendText("{\"type\":\"historyEnd\",\"targetUserId\":\"" + EscapeJsonText(userId) +
			"\",\"serverRevision\":" + std::to_string(revision_) + "}");
	}

	void HandleMessage(const std::string& message) {
		// Message自体が大きすぎる場合は中継しない(分割転送されているはずのため)。
		if (message.size() > ManoCollaboration::kMaximumMessageBytes) {
			LogLine("拒否: Messageが大きすぎます (" + std::to_string(message.size()) + " bytes)");
			return;
		}

		const std::string messageType = ReadJsonString(message, "type");

		if (messageType.empty()) {
			return;
		}

		if (messageType == ManoCollaboration::MessageType::kHandshake) {
			HandleHandshake(message);
			return;
		}

		if (messageType == ManoCollaboration::MessageType::kHeartbeat) {
			HandleHeartbeat(message);
			return;
		}

		// Handshakeを終えていないClientからのPayloadは中継しない。
		const std::string userId = ReadJsonString(message, "userId");
		const auto clientIterator = clients_.find(userId);

		if (!userId.empty() &&
			(clientIterator == clients_.end() || !clientIterator->second.hasCompletedHandshake)) {
			LogLine("拒否: Handshake未完了のClientからのMessage (" + messageType + ")");
			return;
		}

		if (!ValidatePayload(messageType, message)) {
			return;
		}

		if (messageType == ManoCollaboration::MessageType::kHistoryRequest) {
			SendHistory(userId, ReadJsonUnsigned(message, "afterRevision"));
			return;
		}

		if (messageType == "change" || messageType == "commit") {
			const std::string committed = CommitChange(message);
			SendText(committed);
			return;
		}

		if (messageType == "assetBegin") {
			const std::string commitType = ReadJsonString(message, "commitType");
			if (commitType == "change" || commitType == "commit") {
				revision_++;
				std::string committed = SetJsonString(message, "commitType", "commit");
				committed = SetJsonUnsigned(std::move(committed), "revision", revision_);
				const std::string transferId = ReadJsonString(committed, "changeId");
				if (!transferId.empty()) activeTransferRevisions_[transferId] = revision_;
				SaveRevision(); AppendChangeLog(committed); WriteStatus(); SendText(committed); return;
			}
		}

		if (messageType == "assetChunk" || messageType == "assetEnd") {
			const std::string transferId = ReadJsonString(message, "transferId");
			if (activeTransferRevisions_.contains(transferId)) {
				AppendChangeLog(message);
				if (messageType == "assetEnd") activeTransferRevisions_.erase(transferId);
			}
		}

		// 専用Server利用時もEditor Hostと同じLock仲裁を行う。単なる中継では
		// Client側がlockRequestを適用しないため、確定したlockStateとして返す。
		if (messageType == "lockRequest" || messageType == "unlock") {
			HandleLockMessage(messageType, message);
			return;
		}

		TrackState(messageType, message);
		// 中継。Serverは差分の意味を解釈せず、そのまま他のEditorへ配る。
		// (Sceneの意味解釈・Merge・Conflict解決はEditor側の既存実装のまま)
		SendText(message);
	}

	void HandleLockMessage(const std::string& messageType, const std::string& message) {
		const std::string lockKey = ReadJsonString(message, "objectUuid");
		const std::string userId = ReadJsonString(message, "userId");
		if (lockKey.empty() || userId.empty()) return;
		const auto existing = lockOwnerByKey_.find(lockKey);

		if (messageType == "lockRequest") {
			if (existing == lockOwnerByKey_.end() || existing->second == userId) {
				std::string lockState = SetJsonString(message, "type", "lockState");
				lockOwnerByKey_[lockKey] = userId;
				lockStateMessageByKey_[lockKey] = lockState;
				SendText(lockState);
			}
			else {
				const auto state = lockStateMessageByKey_.find(lockKey);
				if (state != lockStateMessageByKey_.end()) SendText(state->second);
			}
			return;
		}

		if (existing == lockOwnerByKey_.end() || existing->second != userId) return;
		std::string unlockedState = SetJsonString(message, "type", "lockState");
		unlockedState = SetJsonString(std::move(unlockedState), "userId", "");
		unlockedState = SetJsonString(std::move(unlockedState), "userName", "");
		lockOwnerByKey_.erase(lockKey);
		lockStateMessageByKey_.erase(lockKey);
		SendText(unlockedState);
	}

	// Remote Clientの送信内容をここで検証する。
	bool ValidatePayload(const std::string& messageType, const std::string& message) {
		// Asset転送はSize/Hash/Pathを検証する。壊れたAssetでProjectを上書きさせない。
		if (messageType == "assetOffer" || messageType == "assetBegin" || messageType == "commit" ||
			messageType == "change") {
			const std::string scenePath = ReadJsonString(message, "scenePath");

			if (!scenePath.empty() && !IsSafeRelativeAssetPath(scenePath)) {
				LogLine("拒否: Project外を指すPath " + scenePath);
				return false;
			}

			const std::uint64_t fileSize = ReadJsonUnsigned(message, "fileSize");

			if (fileSize > ManoCollaboration::kMaximumSynchronizedFileBytes) {
				LogLine(
					"拒否: File Sizeが上限を超えています " + scenePath + " (" +
					std::to_string(fileSize) + " bytes)");
				return false;
			}
		}

		return true;
	}

	// Presence / Lock / Revision をServer側でも把握する。
	// これが無いと、Clientが落ちた時に誰のLockを解放すべきか分からない。
	void TrackState(const std::string& messageType, const std::string& message) {
		const std::string userId = ReadJsonString(message, "userId");

		if (messageType == "lockRequest" || messageType == "lockState") {
			const std::string objectUuid = ReadJsonString(message, "objectUuid");
			const std::string componentUuid = ReadJsonString(message, "componentUuid");

			if (!objectUuid.empty()) {
				const std::string lockKey =
					componentUuid.empty() ? objectUuid : objectUuid + "|" + componentUuid;
				lockOwnerByKey_[lockKey] = userId;
			}
		}
		else if (messageType == "unlock") {
			const std::string objectUuid = ReadJsonString(message, "objectUuid");
			const std::string componentUuid = ReadJsonString(message, "componentUuid");
			const std::string lockKey =
				componentUuid.empty() ? objectUuid : objectUuid + "|" + componentUuid;
			lockOwnerByKey_.erase(lockKey);
		}
		else if (messageType == "commit") {
			const std::uint64_t messageRevision = ReadJsonUnsigned(message, "revision");

			if (messageRevision > revision_) {
				revision_ = messageRevision;
				SaveRevision();
			}

			AppendChangeLog(message);
		}
		else if (messageType == "presence") {
			const auto clientIterator = clients_.find(userId);

			if (clientIterator != clients_.end()) {
				clientIterator->second.currentSceneOrAsset = ReadJsonString(message, "scenePath");
				clientIterator->second.silenceSeconds = 0.0f;
			}
		}
	}

	void HandleHandshake(const std::string& message) {
		const std::uint64_t peerProtocol = ReadJsonUnsigned(message, "protocol");
		const std::string peerProjectId = ReadJsonString(message, "projectId");
		const std::string userId = ReadJsonString(message, "userId");
		const std::string userName = ReadJsonString(message, "userName");

		if (peerProtocol != ManoCollaboration::kCollaborationProtocolVersion) {
			const std::string reason =
				"共同制作Protocolが一致しません。Server: " +
				std::to_string(ManoCollaboration::kCollaborationProtocolVersion) +
				" / Client: " + std::to_string(peerProtocol);
			SendRejection(reason, userId);
			LogLine("接続拒否 (" + userName + "): " + reason);
			return;
		}

		if (peerProjectId != projectId_) {
			const std::string reason =
				"Project IDが一致しません。Server: " + projectId_ + " / Client: " + peerProjectId;
			SendRejection(reason, userId);
			LogLine("接続拒否 (" + userName + "): " + reason);
			return;
		}

		if (userId.empty()) {
			SendRejection("User IDが指定されていません。", {});
			LogLine("接続拒否: User IDがありません");
			return;
		}

		ClientRecord record{};
		record.userId = userId;
		record.userName = userName.empty() ? userId : userName;
		record.projectId = peerProjectId;
		record.hasCompletedHandshake = true;
		clients_[userId] = record;

		SendText(
			std::string("{\"type\":\"") + ManoCollaboration::MessageType::kHandshakeAccepted +
			"\",\"protocol\":" +
			std::to_string(ManoCollaboration::kCollaborationProtocolVersion) +
			",\"targetUserId\":\"" + EscapeJsonText(userId) +
			"\",\"revision\":" + std::to_string(revision_) + "}");
		for (const auto& lockStatePair : lockStateMessageByKey_) {
			SendText(SetJsonString(lockStatePair.second, "targetUserId", userId));
		}
		LogLine("参加: " + record.userName + " (Project " + peerProjectId + ")");
	}

	void SendRejection(const std::string& reason, const std::string& targetUserId) {
		std::string rejection =
			std::string("{\"type\":\"") + ManoCollaboration::MessageType::kHandshakeRejected +
			"\",\"protocol\":" +
			std::to_string(ManoCollaboration::kCollaborationProtocolVersion) +
			",\"reason\":\"" + EscapeJsonText(reason) + "\"}";
		if (!targetUserId.empty()) {
			rejection = SetJsonString(std::move(rejection), "targetUserId", targetUserId);
		}
		SendText(rejection);
	}

	void HandleHeartbeat(const std::string& message) {
		const std::string userId = ReadJsonString(message, "userId");
		const auto clientIterator = clients_.find(userId);

		if (clientIterator != clients_.end()) {
			clientIterator->second.silenceSeconds = 0.0f;
		}

		// 送信時刻をそのまま返し、Client側でLatencyを算出させる。
		SendText(
			std::string("{\"type\":\"") + ManoCollaboration::MessageType::kHeartbeatAck +
			"\",\"userId\":\"" + EscapeJsonText(userId) + "\",\"targetUserId\":\"" + EscapeJsonText(userId) + "\",\"sentAt\":" +
			std::to_string(ReadJsonUnsigned(message, "sentAt")) + "}");
	}

	// 応答の無いClientを切り離し、そのUserが持っていたLockを解放する。
	// PCスリープ・Wi-Fi切替・Tailscale再接続で消えたClientのLockを永久に残さないため。
	void UpdatePresence(float deltaSeconds) {
		std::vector<std::string> timedOutUserIds;

		for (auto& [userId, record] : clients_) {
			record.silenceSeconds += deltaSeconds;

			if (record.silenceSeconds >
				ManoCollaboration::kHeartbeatTimeoutSeconds +
					ManoCollaboration::kLockReleaseGracePeriodSeconds) {
				timedOutUserIds.push_back(userId);
			}
		}

		for (const std::string& userId : timedOutUserIds) {
			const std::string userName = clients_[userId].userName;
			clients_.erase(userId);
			std::vector<std::string> releasedKeys;

			for (const auto& [lockKey, ownerUserId] : lockOwnerByKey_) {
				if (ownerUserId == userId) {
					releasedKeys.push_back(lockKey);
				}
			}

			for (const std::string& lockKey : releasedKeys) {
				lockOwnerByKey_.erase(lockKey);
				lockStateMessageByKey_.erase(lockKey);
			}

			// 残ったClientへ「このUserは居なくなった」と伝え、Lock表示を消させる。
			SendText(
				std::string("{\"type\":\"") + ManoCollaboration::MessageType::kPeerLeft +
				"\",\"userId\":\"" + EscapeJsonText(userId) + "\",\"userName\":\"" +
				EscapeJsonText(userName) + "\"}");
			LogLine(
				"離脱: " + userName + " (Lock " + std::to_string(releasedKeys.size()) + "件を解放)");
		}
	}

	void ReportPeerCountChange() {
		const std::int32_t peerCount = transport_.GetPeerCount();

		if (peerCount == previousPeerCount_) {
			return;
		}

		previousPeerCount_ = peerCount;
		LogLine("接続数: " + std::to_string(peerCount));
		WriteStatus();
	}
};

void PrintUsage() {
	std::cout
		<< "ManoTeamServer - ManoEngine 共同制作中継Server\n"
		<< "\n"
		<< "使い方:\n"
		<< "  ManoTeamServer.exe --project-id <id> [--port <port>] [--max-clients <n>] [--data <dir>] [--status-file <file>] [--pid-file <file>]\n"
		<< "\n"
		<< "  --project-id  必須。接続してくるEditorのProject IDと一致させる。\n"
		<< "  --port        既定 " << kDefaultPort << "\n"
		<< "  --max-clients 既定 " << kDefaultMaximumClientCount << "\n"
		<< "  --data        Revision/Change Logの保存先。既定 ./ManoTeamServerData\n"
		<< "\n"
		<< "Tailscale越しで使う場合も、このServer側の設定は変わりません。\n"
		<< "Tailscaleが動いているPCでこのServerを起動し、各EditorのCollaboration Hostへ\n"
		<< "そのPCのMagicDNS hostname (例 ms.tailxxxx.ts.net) を設定してください。\n"
		<< "ManoTeamServer自体はTailscaleのAPIを一切使いません。\n";
}

}  // namespace

int main(int argumentCount, char** argumentValues) {
	std::uint16_t port = kDefaultPort;
	std::string projectId;
	std::int32_t maximumClientCount = kDefaultMaximumClientCount;
	std::filesystem::path dataDirectory = "ManoTeamServerData";
	std::filesystem::path statusFile;
	std::filesystem::path processIdFile;

	for (int argumentIndex = 1; argumentIndex < argumentCount; argumentIndex++) {
		const std::string argument = argumentValues[argumentIndex];
		const bool hasNext = argumentIndex + 1 < argumentCount;

		if ((argument == "--help") || (argument == "-h")) {
			PrintUsage();
			return 0;
		}

		if (argument == "--project-id" && hasNext) {
			projectId = argumentValues[++argumentIndex];
		}
		else if (argument == "--port" && hasNext) {
			try {
				port = static_cast<std::uint16_t>(
					(std::clamp)(std::stoi(argumentValues[++argumentIndex]), 1, 65535));
			}
			catch (const std::exception&) {
				std::cout << "Portの指定が不正です" << std::endl;
				return 1;
			}
		}
		else if (argument == "--max-clients" && hasNext) {
			try {
				maximumClientCount =
					(std::clamp)(std::stoi(argumentValues[++argumentIndex]), 1, 32);
			}
			catch (const std::exception&) {
				std::cout << "Max Clientsの指定が不正です" << std::endl;
				return 1;
			}
		}
		else if (argument == "--data" && hasNext) {
			dataDirectory = argumentValues[++argumentIndex];
		}
		else if (argument == "--status-file" && hasNext) {
			statusFile = argumentValues[++argumentIndex];
		}
		else if (argument == "--pid-file" && hasNext) {
			processIdFile = argumentValues[++argumentIndex];
		}
	}

	if (!ManoCollaboration::IsValidProjectId(projectId)) {
		std::cout << "--project-id を指定してください (英数字と - _ . のみ、64文字以内)" << std::endl;
		std::cout << std::endl;
		PrintUsage();
		return 1;
	}

	TeamServer server;

	if (!server.Start(port, projectId, maximumClientCount, dataDirectory, statusFile, processIdFile)) {
		return 1;
	}

	server.Run();
	return 0;
}
