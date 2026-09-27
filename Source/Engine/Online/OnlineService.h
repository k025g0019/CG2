#pragma once

#include "IOnlineBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// OnlineService(仕様書 35〜53 項)
//================================================================
// ゲーム側から HTTP の詳細を扱わずにオンライン機能を使うための入口。
// Leaderboard / PlayerData / CloudSave / 共有データはすべてここを通す。
// 通信失敗でゲームを止めず、送信系は Pending Queue へ残して再送する。

class OnlineService {
public:
	using ResponseCallback = std::function<void(const OnlineResponse&)>;
	using LeaderboardCallback = std::function<void(bool isSuccess, const std::vector<LeaderboardEntry>& entries)>;
	using PlayerDataCallback = std::function<void(bool isSuccess, const std::vector<PlayerDataEntry>& entries)>;
	using CloudSaveCallback = std::function<void(bool isSuccess, const std::string& saveText)>;

	static OnlineService& Get();

	OnlineService(const OnlineService&) = delete;
	OnlineService& operator=(const OnlineService&) = delete;

	//============================================================
	// 初期化 / 設定
	//============================================================
	void SetBackend(std::unique_ptr<IOnlineBackend> backend);
	void Configure(const OnlineConfig& config);  // Project Settings から呼ぶ。
	const OnlineConfig& GetConfig() const;
	void SetPlayerIdentity(const std::string& playerId, const std::string& playerName);  // 仕様書 50 項。

	bool Initialize();  // Enabled かつ Base URL があれば Backend を開く。
	void Shutdown();
	void Update(float deltaTime);  // Response 受け取り、Callback 呼び出し、再送を行う。

	bool IsEnabled() const;
	ExternalFeatureState GetState() const;
	OnlineConnectionState GetConnectionState() const;
	ExternalFeatureError GetLastError() const;
	OnlineDebugInfo GetDebugInfo() const;

	//============================================================
	// 汎用通信(仕様書 43〜46 項)
	//============================================================
	OnlineRequestHandle RequestAsync(const OnlineRequest& request, ResponseCallback callback = ResponseCallback());

	//============================================================
	// Leaderboard(仕様書 37〜39 項)
	//============================================================
	OnlineRequestHandle SubmitScore(
		const std::string& boardName,
		int64_t score,
		LeaderboardScope scope = LeaderboardScope::Global,
		ResponseCallback callback = ResponseCallback());
	OnlineRequestHandle GetTopScores(
		const std::string& boardName,
		int32_t entryCount,
		LeaderboardScope scope,
		LeaderboardCallback callback);

	//============================================================
	// Player Data(仕様書 40 項)
	//============================================================
	OnlineRequestHandle GetPlayerData(PlayerDataCallback callback);
	OnlineRequestHandle SetPlayerValue(
		const std::string& key,
		const std::string& value,
		ResponseCallback callback = ResponseCallback());
	OnlineRequestHandle SetPlayerData(
		const std::vector<PlayerDataEntry>& entries,
		ResponseCallback callback = ResponseCallback());

	//============================================================
	// Cloud Save(仕様書 41〜42 項)
	//============================================================
	OnlineRequestHandle UploadCloudSave(
		const std::string& slotName,
		const std::string& saveText,
		ResponseCallback callback = ResponseCallback());
	OnlineRequestHandle DownloadCloudSave(const std::string& slotName, CloudSaveCallback callback);

	//============================================================
	// 共有データ / メッセージ / イベント / デイリー / マッチ(仕様書 36 項)
	//============================================================
	OnlineRequestHandle GetSharedData(const std::string& key, ResponseCallback callback);
	OnlineRequestHandle SetSharedData(
		const std::string& key,
		const std::string& value,
		ResponseCallback callback = ResponseCallback());
	OnlineRequestHandle GetMessages(ResponseCallback callback);
	OnlineRequestHandle GetGlobalEvents(ResponseCallback callback);
	OnlineRequestHandle GetDailyInfo(ResponseCallback callback);
	OnlineRequestHandle GetMatchInfo(const std::string& matchGroup, ResponseCallback callback);

	//============================================================
	// 取得結果の保持(Script API 用)
	//============================================================
	// Script は Callback を持てないため、取得系は「要求 → 次以降のフレームで参照」
	// の形で使えるように結果を保持しておく。
	void RequestTopScoresCached(const std::string& boardName, int32_t entryCount, LeaderboardScope scope);
	const std::vector<LeaderboardEntry>& GetCachedLeaderboard() const;
	bool IsLeaderboardRequestPending() const;

	void RequestPlayerDataCached();
	const std::vector<PlayerDataEntry>& GetCachedPlayerData() const;
	bool TryGetCachedPlayerValue(const std::string& key, std::string& outValue) const;
	bool IsPlayerDataRequestPending() const;

	void RequestCloudSaveCached(const std::string& slotName);
	const std::string& GetCachedCloudSave() const;
	bool IsCloudSaveRequestPending() const;

	//============================================================
	// 再送 Queue(仕様書 48 項)
	//============================================================
	int32_t GetPendingQueueCount() const;
	void ClearPendingQueue();
	void RetryPendingNow();  // 次の Update で Queue 先頭を送る。
	bool LoadPendingQueue();  // SaveData から前回終了時の Queue を復元する。
	bool SavePendingQueue() const;

	//============================================================
	// Response 解析(Worker の返り値 JSON → Engine 型)
	//============================================================
	static bool ParseLeaderboard(const std::string& responseBody, std::vector<LeaderboardEntry>& outEntries);
	static bool ParsePlayerData(const std::string& responseBody, std::vector<PlayerDataEntry>& outEntries);
	static bool ParseCloudSave(const std::string& responseBody, std::string& outSaveText);

private:
	OnlineService() = default;
	~OnlineService() = default;

	struct InFlightRequest {
		OnlineRequest request;
		ResponseCallback callback;
		std::string summary;  // "POST /leaderboard/submit"。Debug Window 表示用。
	};

	struct PendingRequest {
		OnlineRequest request;
		int32_t retryCount = 0;
	};

	OnlineRequestHandle SendInternal(const OnlineRequest& request, ResponseCallback callback);
	void EnqueuePending(const OnlineRequest& request);
	void HandleResponse(OnlineRequestHandle handle, const OnlineResponse& response);
	std::string MakeGameScopedBody(const std::string& jsonObjectText) const;  // gameId / playerId を必ず添える。
	static std::string EncodeQueueField(const std::string& text);
	static std::string DecodeQueueField(const std::string& text);

	std::unique_ptr<IOnlineBackend> backend_;
	std::vector<LeaderboardEntry> cachedLeaderboard_;
	std::vector<PlayerDataEntry> cachedPlayerData_;
	std::string cachedCloudSaveText_;
	bool isLeaderboardRequestPending_ = false;
	bool isPlayerDataRequestPending_ = false;
	bool isCloudSaveRequestPending_ = false;
	std::unordered_map<OnlineRequestHandle, InFlightRequest> inFlightRequests_;
	std::vector<PendingRequest> pendingRequests_;
	OnlineConfig config_{};
	OnlineDebugInfo debugInfo_{};
	ExternalFeatureState state_ = ExternalFeatureState::Unavailable;
	ExternalFeatureError lastError_{};
	float retryTimerSeconds_ = 0.0f;
	bool isInitialized_ = false;
	bool hasPendingQueueChanged_ = false;
};

#pragma warning(pop)
