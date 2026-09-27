#pragma once

#include "Source/Engine/External/ExternalFeature.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Online モジュールの公開型(仕様書 33〜58 項)
//================================================================
// Game 側は HTTP の詳細を扱わず、この型と OnlineService だけを使う。
// Cloudflare / WinHTTP など実装側の型はここへ持ち込まない。

using OnlineRequestHandle = uint32_t;
constexpr OnlineRequestHandle kInvalidOnlineRequestHandle = 0u;

// 接続状態(仕様書 47 項)。通信失敗でもゲームは止めず、この状態を持つだけにする。
enum class OnlineConnectionState : int32_t {
	Offline = 0,
	Connecting,
	Online,
	Error,
};

const char* ToDisplayString(OnlineConnectionState state);

// 開発用と本番用の Backend を分ける(仕様書 52 項)。
enum class OnlineEnvironment : int32_t {
	Development = 0,
	Production,
};

const char* ToDisplayString(OnlineEnvironment environment);

// ランキング種類(仕様書 39 項)。
enum class LeaderboardScope : int32_t {
	Global = 0,
	Daily,
	Weekly,
	Season,
	Custom,
};

const char* ToRequestString(LeaderboardScope scope);  // Worker へ渡す scope 文字列。
const char* ToDisplayString(LeaderboardScope scope);

// 共通 Request(仕様書 44 項)。
struct OnlineRequest {
	std::string endpoint;  // "/leaderboard/submit" のように API Base URL からの相対パス。
	std::string method;    // "GET" / "POST"。空なら GET 扱い。
	std::string body;      // POST の本文。JSON を想定する。

	// 送信できなかった時に Pending Queue へ積み直してよい Request なら true。
	// GET のような取得系は積まず、Submit / Save のような送信系だけ積む。
	bool isQueueable = false;
};

// 共通 Response(仕様書 45 項)。
struct OnlineResponse {
	int32_t statusCode = 0;
	std::string body;
	bool success = false;
	ExternalFeatureError error{};  // 通信そのものが失敗した理由。
	float elapsedMilliseconds = 0.0f;
};

// Leaderboard の 1 行(仕様書 38 項)。
struct LeaderboardEntry {
	std::string playerId;
	std::string playerName;
	int64_t score = 0;
	int32_t rank = 0;
};

// Player Data の 1 項目(仕様書 40 項)。値は文字列で持ち、数値は呼び出し側で変換する。
struct PlayerDataEntry {
	std::string key;
	std::string value;
};

// Project Settings から渡す設定(仕様書 51 項)。
struct OnlineConfig {
	bool isEnabled = false;
	std::string providerName = "Cloudflare";
	std::string apiBaseUrl;         // 例: https://example.workers.dev
	std::string developmentBaseUrl;  // Development 用 URL。空なら apiBaseUrl を使う。
	std::string gameId;
	OnlineEnvironment environment = OnlineEnvironment::Development;
	// クライアントが持ってよい公開鍵だけを置く。重要な認証情報は Worker 側が持つ(仕様書 49 項)。
	std::string clientKey;
	int32_t timeoutSeconds = 10;
	int32_t maximumPendingRequests = 64;

	// 実行中に決まる Player 識別子(仕様書 50 項)。生成方式はゲーム側が選ぶ。
	std::string playerId;
	std::string playerName;

	const std::string& GetActiveBaseUrl() const {
		if (environment == OnlineEnvironment::Development && !developmentBaseUrl.empty()) {
			return developmentBaseUrl;
		}

		return apiBaseUrl;
	}
};

// Debug Window 表示用(仕様書 53 項)。
struct OnlineDebugInfo {
	OnlineConnectionState state = OnlineConnectionState::Offline;
	std::string lastRequestSummary;   // "POST /leaderboard/submit" など。
	std::string lastRequestBody;
	std::string lastResponseBody;
	int32_t lastStatusCode = 0;
	float lastElapsedMilliseconds = 0.0f;
	int32_t pendingQueueCount = 0;
	int32_t inFlightCount = 0;
	int32_t completedRequestCount = 0;
	int32_t failedRequestCount = 0;
	ExternalFeatureError lastError{};
	std::string activeBaseUrl;
	OnlineEnvironment environment = OnlineEnvironment::Development;
};

#pragma warning(pop)
