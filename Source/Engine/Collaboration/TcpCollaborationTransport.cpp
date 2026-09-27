#include "TcpCollaborationTransport.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")

namespace ManoCollaboration {

namespace {
	// WSAStartup/WSACleanup をTransport生成数に関わらず1回ずつにする。
	// Editor内Host と ManoTeamServer のどちらでも同じ実装を使うため、参照数で管理する。
	std::mutex g_winsockMutex;
	std::int32_t g_winsockReferenceCount = 0;

	bool AcquireWinsock(std::string& error) {
		std::lock_guard<std::mutex> lock(g_winsockMutex);

		if (g_winsockReferenceCount > 0) {
			g_winsockReferenceCount++;
			return true;
		}

		WSADATA socketData{};

		if (WSAStartup(MAKEWORD(2, 2), &socketData) != 0) {
			error = "ネットワーク機能を初期化できません (WSAStartup失敗)";
			return false;
		}

		g_winsockReferenceCount = 1;
		return true;
	}

	void ReleaseWinsock() {
		std::lock_guard<std::mutex> lock(g_winsockMutex);

		if (g_winsockReferenceCount <= 0) {
			return;
		}

		g_winsockReferenceCount--;

		if (g_winsockReferenceCount == 0) {
			WSACleanup();
		}
	}

	void SetNonBlocking(SOCKET targetSocket) {
		u_long nonBlockingMode = 1u;
		ioctlsocket(targetSocket, FIONBIO, &nonBlockingMode);
	}

	// 送信は部分送信があり得るため、送り切るまで繰り返す。
	// 相手が遅い(遠隔でLatencyが大きい)場合にWSAEWOULDBLOCKが返るので、その間は待つ。
	bool SendAllBytes(SOCKET targetSocket, const std::string& payload) {
		std::size_t sentTotal = 0u;

		while (sentTotal < payload.size()) {
			const int sentByteCount = send(
				targetSocket,
				payload.data() + sentTotal,
				static_cast<int>(payload.size() - sentTotal),
				0);

			if (sentByteCount > 0) {
				sentTotal += static_cast<std::size_t>(sentByteCount);
				continue;
			}

			if (sentByteCount == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			return false;
		}

		return true;
	}
}

struct TcpCollaborationTransport::Impl {
	std::thread workerThread;
	std::mutex queueMutex;
	std::mutex errorMutex;
	std::vector<std::string> incomingMessages;
	std::vector<TransportMessage> outgoingMessages;
	std::string lastError;
	std::string description;
	std::atomic<TransportState> state{TransportState::Disconnected};
	std::atomic_bool stopsRequested{false};
	std::atomic_bool isServer{false};
	std::atomic_int peerCount{0};
	std::atomic<std::uint64_t> sentTransferBytes{0u};
	std::atomic<std::uint64_t> totalTransferBytes{0u};
	TransportConfig config{};
	TransportEndpoint endpoint{};
	std::uint16_t listenPort = 0u;
	bool hasWinsock = false;

	void SetError(const std::string& message) {
		std::lock_guard<std::mutex> lock(errorMutex);
		lastError = message;
	}

	void SetDescription(const std::string& value) {
		std::lock_guard<std::mutex> lock(errorMutex);
		description = value;
	}

	void PushIncoming(std::string&& message) {
		std::lock_guard<std::mutex> lock(queueMutex);
		incomingMessages.push_back(std::move(message));
	}

	std::vector<TransportMessage> TakeOutgoing() {
		std::vector<TransportMessage> taken;
		std::lock_guard<std::mutex> lock(queueMutex);
		taken.swap(outgoingMessages);
		return taken;
	}

	// 受信Bufferから改行区切りでMessageを切り出す。
	// 戻り値 false は「Bufferが上限を超えた＝壊れた送信元」で、呼び出し側が切断する。
	bool DrainReceiveBuffer(std::string& pendingText) {
		std::size_t lineEnd = pendingText.find('\n');

		while (lineEnd != std::string::npos) {
			std::string message = pendingText.substr(0u, lineEnd);
			pendingText.erase(0u, lineEnd + 1u);

			if (!message.empty() && message.back() == '\r') {
				message.pop_back();
			}

			if (!message.empty()) {
				PushIncoming(std::move(message));
			}

			lineEnd = pendingText.find('\n');
		}

		return pendingText.size() <= config.maximumReceiveBufferBytes;
	}

	void RunServerLoop();
	void RunClientLoop();
};

void TcpCollaborationTransport::Impl::RunServerLoop() {
	// Host名解決は不要。IPv6 Dual Stackで待ち受け、IPv4(LAN)とIPv6の両方を受け入れる。
	// Tailscaleは100.x.x.x(IPv4)とfd7a:(IPv6)の双方を使うため、両対応にしておく。
	SOCKET listenSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	bool usesDualStack = listenSocket != INVALID_SOCKET;

	if (usesDualStack) {
		DWORD ipv6Only = 0;

		if (setsockopt(
				listenSocket,
				IPPROTO_IPV6,
				IPV6_V6ONLY,
				reinterpret_cast<const char*>(&ipv6Only),
				sizeof(ipv6Only)) == SOCKET_ERROR) {
			// Dual Stackにできない環境ではIPv4専用へ落とす。
			closesocket(listenSocket);
			listenSocket = INVALID_SOCKET;
			usesDualStack = false;
		}
	}

	if (listenSocket == INVALID_SOCKET) {
		listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		usesDualStack = false;
	}

	if (listenSocket == INVALID_SOCKET) {
		SetError("Socketを作成できません");
		state.store(TransportState::Error, std::memory_order_release);
		return;
	}

	const BOOL reuseAddress = TRUE;
	setsockopt(
		listenSocket,
		SOL_SOCKET,
		SO_REUSEADDR,
		reinterpret_cast<const char*>(&reuseAddress),
		sizeof(reuseAddress));

	bool hasBound = false;

	if (usesDualStack) {
		sockaddr_in6 serverAddress{};
		serverAddress.sin6_family = AF_INET6;
		serverAddress.sin6_addr = in6addr_any;
		serverAddress.sin6_port = htons(listenPort);
		hasBound = bind(
			listenSocket,
			reinterpret_cast<const sockaddr*>(&serverAddress),
			sizeof(serverAddress)) != SOCKET_ERROR;
	}
	else {
		sockaddr_in serverAddress{};
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddress.sin_port = htons(listenPort);
		hasBound = bind(
			listenSocket,
			reinterpret_cast<const sockaddr*>(&serverAddress),
			sizeof(serverAddress)) != SOCKET_ERROR;
	}

	if (!hasBound || listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
		SetError(
			"Port " + std::to_string(listenPort) +
			" で待ち受けできません。別のプロセスが使用中か、Portが許可されていません");
		state.store(TransportState::Error, std::memory_order_release);
		closesocket(listenSocket);
		return;
	}

	SetNonBlocking(listenSocket);
	SetDescription("0.0.0.0:" + std::to_string(listenPort));
	state.store(TransportState::Listening, std::memory_order_release);

	std::vector<SOCKET> clientSockets;
	std::unordered_map<SOCKET, std::string> receiveBuffers;

	while (!stopsRequested.load(std::memory_order_acquire)) {
		SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

		if (clientSocket != INVALID_SOCKET) {
			if (static_cast<std::int32_t>(clientSockets.size()) < config.maximumClientCount) {
				SetNonBlocking(clientSocket);
				const BOOL noDelay = TRUE;
				// 遠隔ではNagleによる遅延がEditor操作の体感を悪くするため無効化する。
				setsockopt(
					clientSocket,
					IPPROTO_TCP,
					TCP_NODELAY,
					reinterpret_cast<const char*>(&noDelay),
					sizeof(noDelay));
				clientSockets.push_back(clientSocket);
				receiveBuffers[clientSocket] = {};
				peerCount.store(static_cast<std::int32_t>(clientSockets.size()), std::memory_order_release);
			}
			else {
				closesocket(clientSocket);
			}
		}

		const std::vector<TransportMessage> outgoing = TakeOutgoing();

		// 進捗はClient数に関わらず1回だけ積む(Client数分の重複加算を避ける)。
		for (const TransportMessage& message : outgoing) {
			if (message.isTransferChunk) {
				sentTransferBytes.fetch_add(message.transferRawByteShare, std::memory_order_release);
			}
		}

		for (auto clientIterator = clientSockets.begin(); clientIterator != clientSockets.end();) {
			const SOCKET activeSocket = *clientIterator;
			bool keepsClient = true;

			for (const TransportMessage& message : outgoing) {
				if (!SendAllBytes(activeSocket, message.text)) {
					keepsClient = false;
					break;
				}
			}

			if (keepsClient) {
				std::array<char, 65536> receiveBuffer{};
				const int receivedByteCount = recv(
					activeSocket,
					receiveBuffer.data(),
					static_cast<int>(receiveBuffer.size()),
					0);

				if (receivedByteCount > 0) {
					std::string& pendingText = receiveBuffers[activeSocket];
					pendingText.append(receiveBuffer.data(), static_cast<std::size_t>(receivedByteCount));
					keepsClient = DrainReceiveBuffer(pendingText);
				}
				else if (receivedByteCount == 0) {
					keepsClient = false;
				}
				else if (WSAGetLastError() != WSAEWOULDBLOCK) {
					keepsClient = false;
				}
			}

			if (!keepsClient) {
				closesocket(activeSocket);
				receiveBuffers.erase(activeSocket);
				clientIterator = clientSockets.erase(clientIterator);
				peerCount.store(static_cast<std::int32_t>(clientSockets.size()), std::memory_order_release);
			}
			else {
				++clientIterator;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(8));
	}

	for (const SOCKET clientSocketToClose : clientSockets) {
		closesocket(clientSocketToClose);
	}

	closesocket(listenSocket);
	peerCount.store(0, std::memory_order_release);
}

void TcpCollaborationTransport::Impl::RunClientLoop() {
	bool hasConnectedBefore = false;

	while (!stopsRequested.load(std::memory_order_acquire)) {
		// ホスト名・IPv4・IPv6のいずれでも解決する。
		// Tailscale MagicDNS(例 ms.tailxxxx.ts.net)はここで解決されるため、
		// 上位のScene同期・Lock処理はTailscaleの存在を一切知らない。
		addrinfo resolveHints{};
		resolveHints.ai_family = AF_UNSPEC;
		resolveHints.ai_socktype = SOCK_STREAM;
		resolveHints.ai_protocol = IPPROTO_TCP;
		addrinfo* resolvedAddresses = nullptr;
		const std::string portText = std::to_string(endpoint.port);
		const int resolveResult = getaddrinfo(
			endpoint.host.c_str(),
			portText.c_str(),
			&resolveHints,
			&resolvedAddresses);

		if (resolveResult != 0 || resolvedAddresses == nullptr) {
			SetError(
				"Collaboration Serverのホスト名を解決できません\nHost: " +
				endpoint.host + ":" + portText);
			state.store(
				hasConnectedBefore ? TransportState::Reconnecting : TransportState::Connecting,
				std::memory_order_release);

			for (std::int32_t waitIndex = 0;
				waitIndex < config.reconnectIntervalMilliseconds / 50 &&
				!stopsRequested.load(std::memory_order_acquire);
				waitIndex++) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			continue;
		}

		SOCKET serverSocket = INVALID_SOCKET;
		std::string resolvedDescription;

		// 解決されたAddressを順に試す(IPv6が先に返ってもIPv4へFallbackできる)。
		for (addrinfo* candidate = resolvedAddresses;
			candidate != nullptr && serverSocket == INVALID_SOCKET;
			candidate = candidate->ai_next) {
			SOCKET candidateSocket = socket(
				candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);

			if (candidateSocket == INVALID_SOCKET) {
				continue;
			}

			SetNonBlocking(candidateSocket);
			const int connectResult = connect(
				candidateSocket,
				candidate->ai_addr,
				static_cast<int>(candidate->ai_addrlen));
			bool isConnected = connectResult == 0;

			if (!isConnected && WSAGetLastError() == WSAEWOULDBLOCK) {
				fd_set writableSockets;
				FD_ZERO(&writableSockets);
				FD_SET(candidateSocket, &writableSockets);
				timeval connectTimeout{};
				// 遠隔(Tailscale経由)はLANより確立に時間がかかるため、LANの1秒より長く待つ。
				connectTimeout.tv_sec = 3;
				const int selectResult = select(0, nullptr, &writableSockets, nullptr, &connectTimeout);
				int socketError = SOCKET_ERROR;
				int socketErrorSize = sizeof(socketError);

				isConnected = selectResult > 0 &&
					getsockopt(
						candidateSocket,
						SOL_SOCKET,
						SO_ERROR,
						reinterpret_cast<char*>(&socketError),
						&socketErrorSize) == 0 &&
					socketError == 0;
			}

			if (!isConnected) {
				closesocket(candidateSocket);
				continue;
			}

			const BOOL noDelay = TRUE;
			setsockopt(
				candidateSocket,
				IPPROTO_TCP,
				TCP_NODELAY,
				reinterpret_cast<const char*>(&noDelay),
				sizeof(noDelay));

			std::array<char, NI_MAXHOST> resolvedHost{};
			std::array<char, NI_MAXSERV> resolvedService{};

			if (getnameinfo(
					candidate->ai_addr,
					static_cast<int>(candidate->ai_addrlen),
					resolvedHost.data(),
					static_cast<DWORD>(resolvedHost.size()),
					resolvedService.data(),
					static_cast<DWORD>(resolvedService.size()),
					NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
				resolvedDescription =
					endpoint.host + ":" + portText + " (" + resolvedHost.data() + ")";
			}
			else {
				resolvedDescription = endpoint.host + ":" + portText;
			}

			serverSocket = candidateSocket;
		}

		freeaddrinfo(resolvedAddresses);

		if (serverSocket == INVALID_SOCKET) {
			SetError(
				"Collaboration Serverへ接続できません\nHost: " + endpoint.host + ":" + portText);
			state.store(
				hasConnectedBefore ? TransportState::Reconnecting : TransportState::Connecting,
				std::memory_order_release);

			for (std::int32_t waitIndex = 0;
				waitIndex < config.reconnectIntervalMilliseconds / 50 &&
				!stopsRequested.load(std::memory_order_acquire);
				waitIndex++) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			continue;
		}

		hasConnectedBefore = true;
		SetDescription(resolvedDescription);
		SetError({});
		peerCount.store(1, std::memory_order_release);
		state.store(TransportState::Connected, std::memory_order_release);

		std::string pendingText;
		bool keepsConnection = true;

		while (keepsConnection && !stopsRequested.load(std::memory_order_acquire)) {
			const std::vector<TransportMessage> outgoing = TakeOutgoing();

			for (const TransportMessage& message : outgoing) {
				if (message.isTransferChunk) {
					sentTransferBytes.fetch_add(message.transferRawByteShare, std::memory_order_release);
				}

				if (!SendAllBytes(serverSocket, message.text)) {
					keepsConnection = false;
					break;
				}
			}

			if (keepsConnection) {
				std::array<char, 65536> receiveBuffer{};
				const int receivedByteCount = recv(
					serverSocket,
					receiveBuffer.data(),
					static_cast<int>(receiveBuffer.size()),
					0);

				if (receivedByteCount > 0) {
					pendingText.append(receiveBuffer.data(), static_cast<std::size_t>(receivedByteCount));
					keepsConnection = DrainReceiveBuffer(pendingText);
				}
				else if (receivedByteCount == 0) {
					keepsConnection = false;
				}
				else if (WSAGetLastError() != WSAEWOULDBLOCK) {
					keepsConnection = false;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}

		closesocket(serverSocket);
		peerCount.store(0, std::memory_order_release);

		if (!stopsRequested.load(std::memory_order_acquire)) {
			// 切断された。ここでScene全体を捨てたりはしない。再接続後に上位がRevision差分を取る。
			SetError("Collaboration Serverとの接続が切れました。再接続しています");
			state.store(TransportState::Reconnecting, std::memory_order_release);
		}
	}
}

TcpCollaborationTransport::TcpCollaborationTransport()
	: impl_(std::make_unique<Impl>()) {
}

TcpCollaborationTransport::~TcpCollaborationTransport() {
	Disconnect();

	if (impl_->hasWinsock) {
		ReleaseWinsock();
		impl_->hasWinsock = false;
	}
}

bool TcpCollaborationTransport::Listen(
	std::uint16_t port,
	const TransportConfig& config,
	std::string& error) {
	Disconnect();

	if (!impl_->hasWinsock) {
		if (!AcquireWinsock(error)) {
			impl_->SetError(error);
			impl_->state.store(TransportState::Error, std::memory_order_release);
			return false;
		}

		impl_->hasWinsock = true;
	}

	impl_->config = config;
	impl_->listenPort = port;
	impl_->isServer.store(true, std::memory_order_release);
	impl_->stopsRequested.store(false, std::memory_order_release);
	impl_->state.store(TransportState::Connecting, std::memory_order_release);
	Impl* impl = impl_.get();
	impl_->workerThread = std::thread([impl]() { impl->RunServerLoop(); });
	return true;
}

bool TcpCollaborationTransport::Connect(
	const TransportEndpoint& endpoint,
	const TransportConfig& config,
	std::string& error) {
	Disconnect();

	if (endpoint.host.empty() || endpoint.port == 0u) {
		error = "Collaboration ServerのHostとPortを設定してください";
		impl_->SetError(error);
		impl_->state.store(TransportState::Error, std::memory_order_release);
		return false;
	}

	if (!impl_->hasWinsock) {
		if (!AcquireWinsock(error)) {
			impl_->SetError(error);
			impl_->state.store(TransportState::Error, std::memory_order_release);
			return false;
		}

		impl_->hasWinsock = true;
	}

	impl_->config = config;
	impl_->endpoint = endpoint;
	impl_->isServer.store(false, std::memory_order_release);
	impl_->stopsRequested.store(false, std::memory_order_release);
	impl_->state.store(TransportState::Connecting, std::memory_order_release);
	impl_->SetDescription(endpoint.host + ":" + std::to_string(endpoint.port));
	Impl* impl = impl_.get();
	impl_->workerThread = std::thread([impl]() { impl->RunClientLoop(); });
	return true;
}

bool TcpCollaborationTransport::Send(const TransportMessage& message) {
	const TransportState currentState = impl_->state.load(std::memory_order_acquire);

	if (currentState == TransportState::Disconnected || currentState == TransportState::Error) {
		return false;
	}

	std::lock_guard<std::mutex> lock(impl_->queueMutex);
	impl_->outgoingMessages.push_back(message);
	return true;
}

void TcpCollaborationTransport::Poll(std::vector<std::string>& outMessages) {
	std::lock_guard<std::mutex> lock(impl_->queueMutex);

	if (impl_->incomingMessages.empty()) {
		return;
	}

	outMessages.insert(
		outMessages.end(),
		std::make_move_iterator(impl_->incomingMessages.begin()),
		std::make_move_iterator(impl_->incomingMessages.end()));
	impl_->incomingMessages.clear();
}

void TcpCollaborationTransport::Disconnect() {
	impl_->stopsRequested.store(true, std::memory_order_release);

	if (impl_->workerThread.joinable()) {
		impl_->workerThread.join();
	}

	{
		std::lock_guard<std::mutex> lock(impl_->queueMutex);
		impl_->outgoingMessages.clear();
	}

	impl_->peerCount.store(0, std::memory_order_release);
	impl_->state.store(TransportState::Disconnected, std::memory_order_release);
}

TransportState TcpCollaborationTransport::GetState() const {
	return impl_->state.load(std::memory_order_acquire);
}

std::int32_t TcpCollaborationTransport::GetPeerCount() const {
	return impl_->peerCount.load(std::memory_order_acquire);
}

std::string TcpCollaborationTransport::GetLastError() const {
	std::lock_guard<std::mutex> lock(impl_->errorMutex);
	return impl_->lastError;
}

std::string TcpCollaborationTransport::GetDescription() const {
	std::lock_guard<std::mutex> lock(impl_->errorMutex);
	return impl_->description;
}

std::uint64_t TcpCollaborationTransport::GetSentTransferBytes() const {
	return impl_->sentTransferBytes.load(std::memory_order_acquire);
}

std::uint64_t TcpCollaborationTransport::GetTotalTransferBytes() const {
	return impl_->totalTransferBytes.load(std::memory_order_acquire);
}

void TcpCollaborationTransport::ResetTransferProgress() {
	impl_->sentTransferBytes.store(0u, std::memory_order_release);
	impl_->totalTransferBytes.store(0u, std::memory_order_release);
}

}  // namespace ManoCollaboration
