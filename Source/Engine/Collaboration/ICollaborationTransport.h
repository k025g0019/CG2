#pragma once

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

namespace ManoCollaboration {

// 共同制作の通信経路。ManoEngine本体から見ると「MessageをTextで送受信する箱」でしかない。
// LAN / Tailscale / 将来のWebSocket(WSS) / Cloud Backend は、この実装差し替えだけで対応する。
// Scene同期・Lock・Conflict等の上位処理へ `if (tailscale)` のような分岐を持ち込まないこと。
//
// 現在の構成:
//   ICollaborationTransport
//   └─ TcpCollaborationTransport
//       ├─ LAN        (192.168.x.x など)
//       └─ Tailscale  (MagicDNS hostname など。経路が違うだけで実装は同じ)
//
// Tailscale固有のAPI・Node Key・Tailnet管理はここにも上位にも入れない。
// ManoEngineから見れば「到達できるHost名とPort」以上の意味を持たない。

enum class TransportState : std::int32_t {
	Disconnected = 0,
	Connecting,
	Connected,
	Listening,     // Server として待ち受け中
	Reconnecting,  // 一度確立した接続が切れ、再接続を試みている
	Error,
};

struct TransportEndpoint {
	// ホスト名でもIP文字列でもよい。実装側が名前解決する。
	// 通常設定ではTailscaleの 100.x.x.x を直接保存せず、MagicDNS hostname を優先する。
	std::string host;
	std::uint16_t port = 0u;
};

struct TransportConfig {
	// Serverが同時に受け入れるClient数。小規模チーム想定。
	std::int32_t maximumClientCount = 4;
	// Clientが接続失敗・切断後に再試行する間隔。
	std::int32_t reconnectIntervalMilliseconds = 1000;
	// 受信Bufferがこれを超えたら、壊れた送信元とみなして切断する。
	std::size_t maximumReceiveBufferBytes = 192u * 1024u * 1024u;
};

// 送信1件分。チャンク転送の進捗計測のため、実バイト数を併せて持つ。
struct TransportMessage {
	std::string text;
	bool isTransferChunk = false;
	std::uint64_t transferRawByteShare = 0u;
};

class ICollaborationTransport {
public:
	virtual ~ICollaborationTransport() = default;

	// Serverとして待ち受ける。失敗時は false を返し、error へ利用者向けの理由を入れる。
	virtual bool Listen(std::uint16_t port, const TransportConfig& config, std::string& error) = 0;

	// Clientとして接続する。endpoint.host はホスト名・IPv4・IPv6のいずれでもよい。
	// 接続確立は非同期で、確立状態は GetState() で確認する。
	virtual bool Connect(
		const TransportEndpoint& endpoint,
		const TransportConfig& config,
		std::string& error) = 0;

	// 送信Queueへ積む。Serverの場合は接続中の全Clientへ配信する。
	virtual bool Send(const TransportMessage& message) = 0;

	// 受信済みMessageを取り出す(改行区切りで1件ずつ組み立て済み)。呼び出し側のBufferへ移す。
	virtual void Poll(std::vector<std::string>& outMessages) = 0;

	virtual void Disconnect() = 0;

	virtual TransportState GetState() const = 0;

	// Serverなら接続中Client数、Clientなら接続済みで1、未接続で0。
	virtual std::int32_t GetPeerCount() const = 0;

	// 直近の利用者向けエラー文。Socketのエラー番号だけを出さないこと。
	virtual std::string GetLastError() const = 0;

	// 接続先の表示名(解決後のHost:Port)。Diagnostics表示に使う。
	virtual std::string GetDescription() const = 0;

	// 進捗表示用。送信済み/総バイト数。
	virtual std::uint64_t GetSentTransferBytes() const = 0;
	virtual std::uint64_t GetTotalTransferBytes() const = 0;
	virtual void ResetTransferProgress() = 0;
};

}  // namespace ManoCollaboration

#pragma warning(pop)
