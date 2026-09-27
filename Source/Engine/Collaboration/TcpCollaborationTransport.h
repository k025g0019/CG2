#pragma once

#include "ICollaborationTransport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

namespace CG2Collaboration {

// TCP(改行区切りText)による通信経路。LANでもTailscale越しでも同じ実装を使う。
// 接続先の指定はホスト名・IPv4・IPv6のいずれでも良く、getaddrinfo で解決する。
// そのため Tailscale の MagicDNS hostname (例 ms.tailxxxx.ts.net) をそのまま渡せる。
class TcpCollaborationTransport final : public ICollaborationTransport {
public:
	TcpCollaborationTransport();
	~TcpCollaborationTransport() override;
	TcpCollaborationTransport(const TcpCollaborationTransport&) = delete;
	TcpCollaborationTransport& operator=(const TcpCollaborationTransport&) = delete;

	bool Listen(std::uint16_t port, const TransportConfig& config, std::string& error) override;
	bool Connect(
		const TransportEndpoint& endpoint,
		const TransportConfig& config,
		std::string& error) override;
	bool Send(const TransportMessage& message) override;
	void Poll(std::vector<std::string>& outMessages) override;
	void Disconnect() override;
	TransportState GetState() const override;
	std::int32_t GetPeerCount() const override;
	std::string GetLastError() const override;
	std::string GetDescription() const override;
	std::uint64_t GetSentTransferBytes() const override;
	std::uint64_t GetTotalTransferBytes() const override;
	void ResetTransferProgress() override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace CG2Collaboration

#pragma warning(pop)
