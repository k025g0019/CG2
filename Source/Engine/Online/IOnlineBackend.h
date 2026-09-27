#pragma once

#include "OnlineTypes.h"

//================================================================
// オンライン通信 Backend 抽象(仕様書 83 項)
//================================================================
// Cloudflare Workers を前提にするが、別 Provider へ差し替えても
// OnlineService から上の API は変えない。
// 送信は必ず非同期で、Main Thread を止めない(仕様書 46 項)。

class IOnlineBackend {
public:
	virtual ~IOnlineBackend() = default;

	virtual bool Initialize(const OnlineConfig& config) = 0;
	virtual void Shutdown() = 0;
	virtual void UpdateConfig(const OnlineConfig& config) = 0;  // Player ID や環境切替を反映する。

	// Request を送信キューへ積み、Handle を返す。0 は受け付けられなかった場合。
	virtual OnlineRequestHandle Send(const OnlineRequest& request) = 0;

	// 完了した Response を 1 件取り出す。無ければ false。Main Thread から呼ぶ。
	virtual bool TryTakeResponse(OnlineRequestHandle& outHandle, OnlineResponse& outResponse) = 0;

	virtual int32_t GetInFlightCount() const = 0;
	virtual const char* GetName() const = 0;
	virtual ExternalFeatureError GetLastError() const = 0;
};
