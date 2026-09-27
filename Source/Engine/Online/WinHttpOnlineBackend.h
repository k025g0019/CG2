#pragma once

#include "IOnlineBackend.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Cloudflare Workers 向け HTTPS Backend(WinHTTP 実装)
//================================================================
// Worker Thread 1 本で送信を直列処理し、Main Thread は Send と
// TryTakeResponse だけを呼ぶ。Main Thread を止めない(仕様書 46 項)。
// Endpoint は Project Settings の API Base URL からの相対パスで渡す。

class WinHttpOnlineBackend final : public IOnlineBackend {
public:
	WinHttpOnlineBackend() = default;
	~WinHttpOnlineBackend() override;

	WinHttpOnlineBackend(const WinHttpOnlineBackend&) = delete;
	WinHttpOnlineBackend& operator=(const WinHttpOnlineBackend&) = delete;

	bool Initialize(const OnlineConfig& config) override;
	void Shutdown() override;
	void UpdateConfig(const OnlineConfig& config) override;

	OnlineRequestHandle Send(const OnlineRequest& request) override;
	bool TryTakeResponse(OnlineRequestHandle& outHandle, OnlineResponse& outResponse) override;

	int32_t GetInFlightCount() const override;
	const char* GetName() const override;
	ExternalFeatureError GetLastError() const override;

private:
	struct QueuedRequest {
		OnlineRequestHandle handle = kInvalidOnlineRequestHandle;
		OnlineRequest request;
	};

	struct QueuedResponse {
		OnlineRequestHandle handle = kInvalidOnlineRequestHandle;
		OnlineResponse response;
	};

	void WorkerMain();  // Worker Thread の本体。
	bool ExecuteRequest(const OnlineRequest& request, OnlineResponse& outResponse);  // Worker Thread から呼ぶ同期通信。
	OnlineConfig CopyConfig() const;

	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::thread workerThread_;
	std::vector<QueuedRequest> requestQueue_;
	std::vector<QueuedResponse> responseQueue_;
	OnlineConfig config_{};
	ExternalFeatureError lastError_{};
	std::atomic<int32_t> inFlightCount_{0};
	std::atomic<bool> isStopRequested_{false};
	OnlineRequestHandle nextHandle_ = 1u;
	void* sessionHandle_ = nullptr;  // HINTERNET。winhttp.h を Header へ持ち込まないため void* で持つ。
	bool isInitialized_ = false;
};

#pragma warning(pop)
