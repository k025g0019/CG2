#pragma once

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

namespace CG2Collaboration {

// Editor と CG2TeamServer が共有する通信規約。
// 遠隔共同制作では異なるEngine Buildが接続し得るため、接続時に必ず突き合わせる。
//
// Version履歴:
//   1 : Editor内Host同士のLAN接続(暗黙。handshakeにprotocol項目が無い旧Build)
//   2 : CG2TeamServer導入。projectId / protocolVersion / heartbeat を追加
//   3 : Snapshot Revision、履歴再送、Offline 3-way同期を追加
constexpr std::uint32_t kCollaborationProtocolVersion = 3u;

// Handshakeの応答を待つ上限。これを超えたら相手が旧Buildか別プロトコルとみなす。
constexpr float kHandshakeTimeoutSeconds = 10.0f;

// Clientが送るHeartbeatの間隔と、Server側が「消えた」と判断するまでの猶予。
// 遠隔ではPCスリープ・Wi-Fi切替・Tailscale再接続で無言のまま消えることがあるため、
// 応答が無いClientのLockを永久に残さないようにする。
constexpr float kHeartbeatIntervalSeconds = 5.0f;
constexpr float kHeartbeatTimeoutSeconds = 20.0f;
// Lockは安全側に倒し、Timeout後すぐには解放せず短い猶予を置く。
// (一時的なネットワーク瞬断で他人が同じObjectを触り始めるのを避ける)
constexpr float kLockReleaseGracePeriodSeconds = 5.0f;

// 1Fileあたりの転送上限。既存のEditor側制限と一致させる。
constexpr std::uint64_t kMaximumSynchronizedFileBytes = 128ull * 1024ull * 1024ull;
// 1Messageの上限。これを超えるものは分割転送されているはずなので、壊れた送信元とみなす。
constexpr std::size_t kMaximumMessageBytes = 8u * 1024u * 1024u;

// Message種別。値はJSONの "type" と一致する。
namespace MessageType {
	constexpr const char* kHandshake = "handshake";           // Client -> Server
	constexpr const char* kHandshakeAccepted = "handshakeOk"; // Server -> Client
	constexpr const char* kHandshakeRejected = "handshakeNg"; // Server -> Client(理由付き)
	constexpr const char* kHeartbeat = "heartbeat";           // Client -> Server
	constexpr const char* kHeartbeatAck = "heartbeatAck";     // Server -> Client(Latency計測用)
	constexpr const char* kPeerLeft = "peerLeft";             // Server -> Client(Lock解放通知)
	constexpr const char* kHistoryRequest = "historyRequest"; // Client -> Server(afterRevision以降を要求)
	constexpr const char* kHistoryBegin = "historyBegin";     // Server -> Client(履歴送信開始)
	constexpr const char* kHistoryEnd = "historyEnd";         // Server -> Client(履歴送信完了)
}

// ProjectId として使える文字かどうか。Path要素へ流用されても危険が無い範囲へ限定する。
inline bool IsValidProjectIdCharacter(char character) {
	return (character >= 'a' && character <= 'z') ||
		(character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') ||
		character == '-' || character == '_' || character == '.';
}

inline bool IsValidProjectId(const std::string& projectId) {
	if (projectId.empty() || projectId.size() > 64u) {
		return false;
	}

	for (const char character : projectId) {
		if (!IsValidProjectIdCharacter(character)) {
			return false;
		}
	}

	return true;
}

}  // namespace CG2Collaboration

#pragma warning(pop)
