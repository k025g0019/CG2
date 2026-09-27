#include "OnlineService.h"

#include "OnlineJson.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {
	constexpr const char* kPendingQueuePath = "SaveData/OnlinePendingQueue.cg2";
	constexpr const char* kPendingQueueHeader = "ManoEngineOnlinePendingQueue|1";
	constexpr float kRetryIntervalSeconds = 8.0f;  // 未送信 Queue を再送する間隔。
	constexpr int32_t kMaximumRetryCount = 8;      // これを超えた Request は捨てる。
	constexpr size_t kDebugBodyLimit = 512u;       // Debug Window へ残す本文の長さ。

	std::string TrimForDebug(const std::string& text) {
		if (text.size() <= kDebugBodyLimit) {
			return text;
		}

		return text.substr(0u, kDebugBodyLimit) + " ...";
	}
}

const char* ToDisplayString(OnlineConnectionState state) {
	switch (state) {
	case OnlineConnectionState::Offline:
		return "オフライン";
	case OnlineConnectionState::Connecting:
		return "接続中";
	case OnlineConnectionState::Online:
		return "オンライン";
	case OnlineConnectionState::Error:
		return "エラー";
	default:
		return "オフライン";
	}
}

const char* ToDisplayString(OnlineEnvironment environment) {
	return environment == OnlineEnvironment::Production ? "本番" : "開発";
}

const char* ToRequestString(LeaderboardScope scope) {
	switch (scope) {
	case LeaderboardScope::Global:
		return "global";
	case LeaderboardScope::Daily:
		return "daily";
	case LeaderboardScope::Weekly:
		return "weekly";
	case LeaderboardScope::Season:
		return "season";
	case LeaderboardScope::Custom:
		return "custom";
	default:
		return "global";
	}
}

const char* ToDisplayString(LeaderboardScope scope) {
	switch (scope) {
	case LeaderboardScope::Global:
		return "全体";
	case LeaderboardScope::Daily:
		return "デイリー";
	case LeaderboardScope::Weekly:
		return "ウィークリー";
	case LeaderboardScope::Season:
		return "シーズン";
	case LeaderboardScope::Custom:
		return "カスタム";
	default:
		return "全体";
	}
}

OnlineService& OnlineService::Get() {
	static OnlineService instance;
	return instance;
}

void OnlineService::SetBackend(std::unique_ptr<IOnlineBackend> backend) {
	if (isInitialized_) {
		Shutdown();
	}

	backend_ = std::move(backend);
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
}

void OnlineService::Configure(const OnlineConfig& config) {
	config_ = config;
	debugInfo_.environment = config_.environment;
	debugInfo_.activeBaseUrl = config_.GetActiveBaseUrl();

	if (backend_ != nullptr && isInitialized_) {
		backend_->UpdateConfig(config_);
	}
}

const OnlineConfig& OnlineService::GetConfig() const {
	return config_;
}

void OnlineService::SetPlayerIdentity(const std::string& playerId, const std::string& playerName) {
	config_.playerId = playerId;
	config_.playerName = playerName;

	if (backend_ != nullptr && isInitialized_) {
		backend_->UpdateConfig(config_);
	}
}

bool OnlineService::Initialize() {
	if (!config_.isEnabled) {
		state_ = ExternalFeatureState::Unavailable;
		debugInfo_.state = OnlineConnectionState::Offline;
		return false;
	}

	if (backend_ == nullptr) {
		state_ = ExternalFeatureState::Unavailable;
		lastError_.code = 1;
		lastError_.message = "Online Backend が設定されていません。";
		debugInfo_.lastError = lastError_;
		return false;
	}

	if (config_.GetActiveBaseUrl().empty()) {
		state_ = ExternalFeatureState::Error;
		lastError_.code = 2;
		lastError_.message = "API Base URL が未設定です。Project Settings で設定してください。";
		debugInfo_.lastError = lastError_;
		ExternalFeatureLog::Error(ExternalFeatureCategory::Online, lastError_);
		return false;
	}

	if (isInitialized_) {
		return true;
	}

	if (!backend_->Initialize(config_)) {
		lastError_ = backend_->GetLastError();

		if (!lastError_.HasError()) {
			lastError_.code = 3;
			lastError_.message = "Online Backend の初期化に失敗しました。";
		}

		state_ = ExternalFeatureState::Error;
		debugInfo_.state = OnlineConnectionState::Error;
		debugInfo_.lastError = lastError_;
		ExternalFeatureLog::Error(ExternalFeatureCategory::Online, lastError_);
		return false;
	}

	isInitialized_ = true;
	state_ = ExternalFeatureState::Ready;
	lastError_.Clear();
	debugInfo_.state = OnlineConnectionState::Offline;
	debugInfo_.activeBaseUrl = config_.GetActiveBaseUrl();
	debugInfo_.environment = config_.environment;
	LoadPendingQueue();

	ExternalFeatureLog::Info(
		ExternalFeatureCategory::Online,
		std::string("Backend 初期化: ") + backend_->GetName() + " / " +
			ToDisplayString(config_.environment) + " / " + config_.GetActiveBaseUrl());
	return true;
}

void OnlineService::Shutdown() {
	if (hasPendingQueueChanged_) {
		SavePendingQueue();
	}

	if (backend_ != nullptr && isInitialized_) {
		backend_->Shutdown();
	}

	inFlightRequests_.clear();
	isInitialized_ = false;
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
	debugInfo_.state = OnlineConnectionState::Offline;
	debugInfo_.inFlightCount = 0;
}

void OnlineService::Update(float deltaTime) {
	if (backend_ == nullptr || !isInitialized_) {
		return;
	}

	// 完了した Response を Main Thread で受け取り、Callback を呼ぶ。
	OnlineRequestHandle handle = kInvalidOnlineRequestHandle;
	OnlineResponse response{};

	while (backend_->TryTakeResponse(handle, response)) {
		HandleResponse(handle, response);
	}

	debugInfo_.inFlightCount = backend_->GetInFlightCount();
	debugInfo_.pendingQueueCount = static_cast<int32_t>(pendingRequests_.size());

	if (pendingRequests_.empty()) {
		retryTimerSeconds_ = 0.0f;

		if (hasPendingQueueChanged_) {
			SavePendingQueue();
			hasPendingQueueChanged_ = false;
		}

		return;
	}

	// 送れなかった Request を一定間隔で 1 件ずつ再送する(仕様書 48 項)。
	retryTimerSeconds_ -= (std::max)(deltaTime, 0.0f);

	if (retryTimerSeconds_ > 0.0f || debugInfo_.inFlightCount > 0) {
		return;
	}

	retryTimerSeconds_ = kRetryIntervalSeconds;
	PendingRequest pendingRequest = pendingRequests_.front();
	pendingRequests_.erase(pendingRequests_.begin());
	pendingRequest.retryCount += 1;

	if (pendingRequest.retryCount > kMaximumRetryCount) {
		ExternalFeatureLog::Warning(
			ExternalFeatureCategory::Online,
			"再送回数の上限に達したため破棄しました: " + pendingRequest.request.endpoint);
		hasPendingQueueChanged_ = true;
		return;
	}

	const int32_t retryCount = pendingRequest.retryCount;
	const OnlineRequest retryRequest = pendingRequest.request;
	SendInternal(retryRequest, [this, retryRequest, retryCount](const OnlineResponse& retryResponse) {
		if (retryResponse.success) {
			return;
		}

		// まだ失敗するなら retryCount を保ったまま Queue の末尾へ戻す。
		PendingRequest requeued{};
		requeued.request = retryRequest;
		requeued.retryCount = retryCount;
		pendingRequests_.push_back(requeued);
		hasPendingQueueChanged_ = true;
	});
}

bool OnlineService::IsEnabled() const {
	return config_.isEnabled;
}

ExternalFeatureState OnlineService::GetState() const {
	return state_;
}

OnlineConnectionState OnlineService::GetConnectionState() const {
	return debugInfo_.state;
}

ExternalFeatureError OnlineService::GetLastError() const {
	return lastError_;
}

OnlineDebugInfo OnlineService::GetDebugInfo() const {
	OnlineDebugInfo debugInfo = debugInfo_;
	debugInfo.pendingQueueCount = static_cast<int32_t>(pendingRequests_.size());
	debugInfo.inFlightCount = backend_ != nullptr ? backend_->GetInFlightCount() : 0;
	debugInfo.activeBaseUrl = config_.GetActiveBaseUrl();
	debugInfo.environment = config_.environment;
	return debugInfo;
}

OnlineRequestHandle OnlineService::RequestAsync(const OnlineRequest& request, ResponseCallback callback) {
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::SendInternal(const OnlineRequest& request, ResponseCallback callback) {
	if (!config_.isEnabled) {
		// 機能が無効な時は勝手に別手段で代替しない(仕様書 85 項)。
		if (callback) {
			OnlineResponse response{};
			response.error.code = -10;
			response.error.message = "Online Services が無効です。";
			callback(response);
		}

		return kInvalidOnlineRequestHandle;
	}

	if (backend_ == nullptr || !isInitialized_) {
		if (request.isQueueable) {
			EnqueuePending(request);
		}

		if (callback) {
			OnlineResponse response{};
			response.error.code = -11;
			response.error.message = "Online Backend が初期化されていません。";
			callback(response);
		}

		return kInvalidOnlineRequestHandle;
	}

	const OnlineRequestHandle handle = backend_->Send(request);

	if (handle == kInvalidOnlineRequestHandle) {
		if (request.isQueueable) {
			EnqueuePending(request);
		}

		if (callback) {
			OnlineResponse response{};
			response.error.code = -12;
			response.error.message = "Request を送信キューへ積めませんでした。";
			callback(response);
		}

		return kInvalidOnlineRequestHandle;
	}

	InFlightRequest inFlight{};
	inFlight.request = request;
	inFlight.callback = std::move(callback);
	inFlight.summary = (request.method.empty() ? std::string("GET") : request.method) + " " + request.endpoint;
	inFlightRequests_[handle] = std::move(inFlight);

	debugInfo_.lastRequestSummary = inFlightRequests_[handle].summary;
	debugInfo_.lastRequestBody = TrimForDebug(request.body);

	if (debugInfo_.state != OnlineConnectionState::Online) {
		debugInfo_.state = OnlineConnectionState::Connecting;
	}

	state_ = ExternalFeatureState::Running;
	return handle;
}

void OnlineService::HandleResponse(OnlineRequestHandle handle, const OnlineResponse& response) {
	const auto inFlightIt = inFlightRequests_.find(handle);
	InFlightRequest inFlight{};

	if (inFlightIt != inFlightRequests_.end()) {
		inFlight = inFlightIt->second;
		inFlightRequests_.erase(inFlightIt);
	}

	debugInfo_.lastStatusCode = response.statusCode;
	debugInfo_.lastResponseBody = TrimForDebug(response.body);
	debugInfo_.lastElapsedMilliseconds = response.elapsedMilliseconds;

	if (response.success) {
		debugInfo_.state = OnlineConnectionState::Online;
		debugInfo_.completedRequestCount += 1;
		debugInfo_.lastError.Clear();
		lastError_.Clear();
	}
	else {
		debugInfo_.failedRequestCount += 1;
		debugInfo_.lastError = response.error;
		lastError_ = response.error;

		// HTTP 応答があった場合は通信自体は届いているため Online のまま扱う。
		debugInfo_.state = response.statusCode > 0
			? OnlineConnectionState::Online
			: OnlineConnectionState::Offline;

		ExternalFeatureLog::Warning(
			ExternalFeatureCategory::Online,
			inFlight.summary + " が失敗しました: " + response.error.message);

		// 通信自体が届かなかった送信系だけ Queue へ残す(取得系は残さない)。
		if (inFlight.request.isQueueable && response.statusCode <= 0) {
			EnqueuePending(inFlight.request);
		}
	}

	if (inFlight.callback) {
		inFlight.callback(response);
	}

	state_ = inFlightRequests_.empty() ? ExternalFeatureState::Ready : ExternalFeatureState::Running;
}

void OnlineService::EnqueuePending(const OnlineRequest& request) {
	if (static_cast<int32_t>(pendingRequests_.size()) >= (std::max)(config_.maximumPendingRequests, 1)) {
		ExternalFeatureLog::Warning(
			ExternalFeatureCategory::Online,
			"Pending Queue が上限に達したため最古の Request を破棄しました。");
		pendingRequests_.erase(pendingRequests_.begin());
	}

	PendingRequest pendingRequest{};
	pendingRequest.request = request;
	pendingRequests_.push_back(pendingRequest);
	hasPendingQueueChanged_ = true;
	debugInfo_.pendingQueueCount = static_cast<int32_t>(pendingRequests_.size());
}

std::string OnlineService::MakeGameScopedBody(const std::string& jsonObjectText) const {
	// Worker 側が Game / Player を必ず判定できるよう、共通項目を足した JSON へ包み直す。
	OnlineJsonValue parsedValue{};
	OnlineJson::ObjectWriter writer;
	writer.AddString("gameId", config_.gameId);
	writer.AddString("playerId", config_.playerId);
	writer.AddString("playerName", config_.playerName);
	writer.AddString("environment", config_.environment == OnlineEnvironment::Production ? "production" : "development");

	if (!jsonObjectText.empty() && OnlineJson::Parse(jsonObjectText, parsedValue) &&
		parsedValue.type == OnlineJsonType::Object) {
		writer.AddRaw("payload", jsonObjectText);
	}
	else if (!jsonObjectText.empty()) {
		writer.AddString("payload", jsonObjectText);
	}

	return writer.Build();
}

//================================================================
// Leaderboard
//================================================================

OnlineRequestHandle OnlineService::SubmitScore(
	const std::string& boardName,
	int64_t score,
	LeaderboardScope scope,
	ResponseCallback callback) {
	OnlineJson::ObjectWriter payloadWriter;
	payloadWriter.AddString("board", boardName);
	payloadWriter.AddInt64("score", score);
	payloadWriter.AddString("scope", ToRequestString(scope));

	OnlineRequest request{};
	request.endpoint = "/leaderboard/submit";
	request.method = "POST";
	request.body = MakeGameScopedBody(payloadWriter.Build());
	request.isQueueable = true;  // 送信系は再送対象(仕様書 48 項)。
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::GetTopScores(
	const std::string& boardName,
	int32_t entryCount,
	LeaderboardScope scope,
	LeaderboardCallback callback) {
	const int32_t clampedCount = (std::clamp)(entryCount, 1, 500);

	OnlineRequest request{};
	request.endpoint = "/leaderboard/top?board=" + boardName +
		"&scope=" + ToRequestString(scope) +
		"&count=" + std::to_string(clampedCount) +
		"&gameId=" + config_.gameId;
	request.method = "GET";

	return SendInternal(request, [callback](const OnlineResponse& response) {
		std::vector<LeaderboardEntry> entries;
		const bool isSuccess = response.success && ParseLeaderboard(response.body, entries);

		if (callback) {
			callback(isSuccess, entries);
		}
	});
}

bool OnlineService::ParseLeaderboard(
	const std::string& responseBody,
	std::vector<LeaderboardEntry>& outEntries) {
	outEntries.clear();
	OnlineJsonValue rootValue{};

	if (!OnlineJson::Parse(responseBody, rootValue)) {
		return false;
	}

	const OnlineJsonValue* arrayValue = nullptr;

	if (rootValue.type == OnlineJsonType::Array) {
		arrayValue = &rootValue;
	}
	else {
		arrayValue = rootValue.FindArray("entries");

		if (arrayValue == nullptr) {
			arrayValue = rootValue.FindArray("results");
		}
	}

	if (arrayValue == nullptr) {
		return false;
	}

	int32_t fallbackRank = 1;

	for (const OnlineJsonValue& elementValue : arrayValue->arrayValues) {
		if (elementValue.type != OnlineJsonType::Object) {
			continue;
		}

		LeaderboardEntry entry{};
		entry.playerId = elementValue.GetString("playerId");
		entry.playerName = elementValue.GetString("playerName");
		entry.score = elementValue.GetInt64("score", 0);
		entry.rank = elementValue.GetInt32("rank", fallbackRank);
		outEntries.push_back(entry);
		++fallbackRank;
	}

	return true;
}

//================================================================
// Player Data
//================================================================

OnlineRequestHandle OnlineService::GetPlayerData(PlayerDataCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/player?gameId=" + config_.gameId + "&playerId=" + config_.playerId;
	request.method = "GET";

	return SendInternal(request, [callback](const OnlineResponse& response) {
		std::vector<PlayerDataEntry> entries;
		const bool isSuccess = response.success && ParsePlayerData(response.body, entries);

		if (callback) {
			callback(isSuccess, entries);
		}
	});
}

OnlineRequestHandle OnlineService::SetPlayerValue(
	const std::string& key,
	const std::string& value,
	ResponseCallback callback) {
	std::vector<PlayerDataEntry> entries;
	PlayerDataEntry entry{};
	entry.key = key;
	entry.value = value;
	entries.push_back(entry);
	return SetPlayerData(entries, std::move(callback));
}

OnlineRequestHandle OnlineService::SetPlayerData(
	const std::vector<PlayerDataEntry>& entries,
	ResponseCallback callback) {
	std::string valuesText = "{";

	for (size_t entryIndex = 0u; entryIndex < entries.size(); ++entryIndex) {
		if (entryIndex > 0u) {
			valuesText.push_back(',');
		}

		valuesText += OnlineJson::EscapeString(entries[entryIndex].key);
		valuesText.push_back(':');
		valuesText += OnlineJson::EscapeString(entries[entryIndex].value);
	}

	valuesText.push_back('}');

	OnlineJson::ObjectWriter payloadWriter;
	payloadWriter.AddRaw("values", valuesText);

	OnlineRequest request{};
	request.endpoint = "/player";
	request.method = "POST";
	request.body = MakeGameScopedBody(payloadWriter.Build());
	request.isQueueable = true;
	return SendInternal(request, std::move(callback));
}

bool OnlineService::ParsePlayerData(
	const std::string& responseBody,
	std::vector<PlayerDataEntry>& outEntries) {
	outEntries.clear();
	OnlineJsonValue rootValue{};

	if (!OnlineJson::Parse(responseBody, rootValue) || rootValue.type != OnlineJsonType::Object) {
		return false;
	}

	const OnlineJsonValue* valuesValue = rootValue.Find("values");

	if (valuesValue == nullptr || valuesValue->type != OnlineJsonType::Object) {
		// values を持たない応答も、そのまま Key-Value として読めるなら受け付ける。
		valuesValue = &rootValue;
	}

	for (const std::pair<std::string, OnlineJsonValue>& member : valuesValue->objectValues) {
		PlayerDataEntry entry{};
		entry.key = member.first;

		switch (member.second.type) {
		case OnlineJsonType::String:
			entry.value = member.second.stringValue;
			break;
		case OnlineJsonType::Number:
			entry.value = valuesValue->GetString(member.first);
			break;
		case OnlineJsonType::Bool:
			entry.value = member.second.boolValue ? "1" : "0";
			break;
		default:
			continue;
		}

		outEntries.push_back(entry);
	}

	return true;
}

//================================================================
// Cloud Save
//================================================================

OnlineRequestHandle OnlineService::UploadCloudSave(
	const std::string& slotName,
	const std::string& saveText,
	ResponseCallback callback) {
	OnlineJson::ObjectWriter payloadWriter;
	payloadWriter.AddString("slot", slotName);
	payloadWriter.AddString("data", saveText);
	payloadWriter.AddInt64("size", static_cast<int64_t>(saveText.size()));

	OnlineRequest request{};
	request.endpoint = "/save";
	request.method = "POST";
	request.body = MakeGameScopedBody(payloadWriter.Build());
	request.isQueueable = true;
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::DownloadCloudSave(
	const std::string& slotName,
	CloudSaveCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/save?gameId=" + config_.gameId +
		"&playerId=" + config_.playerId +
		"&slot=" + slotName;
	request.method = "GET";

	return SendInternal(request, [callback](const OnlineResponse& response) {
		std::string saveText;
		const bool isSuccess = response.success && ParseCloudSave(response.body, saveText);

		if (callback) {
			callback(isSuccess, saveText);
		}
	});
}

bool OnlineService::ParseCloudSave(const std::string& responseBody, std::string& outSaveText) {
	outSaveText.clear();
	OnlineJsonValue rootValue{};

	if (!OnlineJson::Parse(responseBody, rootValue) || rootValue.type != OnlineJsonType::Object) {
		return false;
	}

	const OnlineJsonValue* dataValue = rootValue.Find("data");

	if (dataValue == nullptr || dataValue->type != OnlineJsonType::String) {
		return false;
	}

	outSaveText = dataValue->stringValue;
	return true;
}

//================================================================
// 共有データ / メッセージ / イベント / デイリー / マッチ
//================================================================

OnlineRequestHandle OnlineService::GetSharedData(const std::string& key, ResponseCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/shared?gameId=" + config_.gameId + "&key=" + key;
	request.method = "GET";
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::SetSharedData(
	const std::string& key,
	const std::string& value,
	ResponseCallback callback) {
	OnlineJson::ObjectWriter payloadWriter;
	payloadWriter.AddString("key", key);
	payloadWriter.AddString("value", value);

	OnlineRequest request{};
	request.endpoint = "/shared";
	request.method = "POST";
	request.body = MakeGameScopedBody(payloadWriter.Build());
	request.isQueueable = true;
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::GetMessages(ResponseCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/messages?gameId=" + config_.gameId;
	request.method = "GET";
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::GetGlobalEvents(ResponseCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/events?gameId=" + config_.gameId;
	request.method = "GET";
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::GetDailyInfo(ResponseCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/daily?gameId=" + config_.gameId;
	request.method = "GET";
	return SendInternal(request, std::move(callback));
}

OnlineRequestHandle OnlineService::GetMatchInfo(const std::string& matchGroup, ResponseCallback callback) {
	OnlineRequest request{};
	request.endpoint = "/match?gameId=" + config_.gameId + "&group=" + matchGroup;
	request.method = "GET";
	return SendInternal(request, std::move(callback));
}

//================================================================
// 取得結果の保持(Script API 用)
//================================================================

void OnlineService::RequestTopScoresCached(
	const std::string& boardName,
	int32_t entryCount,
	LeaderboardScope scope) {
	isLeaderboardRequestPending_ = true;
	GetTopScores(
		boardName,
		entryCount,
		scope,
		[this](bool isSuccess, const std::vector<LeaderboardEntry>& entries) {
			isLeaderboardRequestPending_ = false;

			if (isSuccess) {
				cachedLeaderboard_ = entries;
			}
		});
}

const std::vector<LeaderboardEntry>& OnlineService::GetCachedLeaderboard() const {
	return cachedLeaderboard_;
}

bool OnlineService::IsLeaderboardRequestPending() const {
	return isLeaderboardRequestPending_;
}

void OnlineService::RequestPlayerDataCached() {
	isPlayerDataRequestPending_ = true;
	GetPlayerData([this](bool isSuccess, const std::vector<PlayerDataEntry>& entries) {
		isPlayerDataRequestPending_ = false;

		if (isSuccess) {
			cachedPlayerData_ = entries;
		}
	});
}

const std::vector<PlayerDataEntry>& OnlineService::GetCachedPlayerData() const {
	return cachedPlayerData_;
}

bool OnlineService::TryGetCachedPlayerValue(const std::string& key, std::string& outValue) const {
	for (const PlayerDataEntry& entry : cachedPlayerData_) {
		if (entry.key == key) {
			outValue = entry.value;
			return true;
		}
	}

	return false;
}

bool OnlineService::IsPlayerDataRequestPending() const {
	return isPlayerDataRequestPending_;
}

void OnlineService::RequestCloudSaveCached(const std::string& slotName) {
	isCloudSaveRequestPending_ = true;
	DownloadCloudSave(slotName, [this](bool isSuccess, const std::string& saveText) {
		isCloudSaveRequestPending_ = false;

		if (isSuccess) {
			cachedCloudSaveText_ = saveText;
		}
	});
}

const std::string& OnlineService::GetCachedCloudSave() const {
	return cachedCloudSaveText_;
}

bool OnlineService::IsCloudSaveRequestPending() const {
	return isCloudSaveRequestPending_;
}

//================================================================
// Pending Queue の保存 / 復元
//================================================================

int32_t OnlineService::GetPendingQueueCount() const {
	return static_cast<int32_t>(pendingRequests_.size());
}

void OnlineService::ClearPendingQueue() {
	pendingRequests_.clear();
	hasPendingQueueChanged_ = true;
	SavePendingQueue();
}

void OnlineService::RetryPendingNow() {
	retryTimerSeconds_ = 0.0f;
}

std::string OnlineService::EncodeQueueField(const std::string& text) {
	// 1 行 1 Request のテキストへ収めるため、区切り文字と改行を退避する。
	std::string encodedText;
	encodedText.reserve(text.size());

	for (const char character : text) {
		switch (character) {
		case '\\':
			encodedText += "\\\\";
			break;
		case '|':
			encodedText += "\\p";
			break;
		case '\r':
			encodedText += "\\r";
			break;
		case '\n':
			encodedText += "\\n";
			break;
		default:
			encodedText.push_back(character);
			break;
		}
	}

	return encodedText;
}

std::string OnlineService::DecodeQueueField(const std::string& text) {
	std::string decodedText;
	decodedText.reserve(text.size());

	for (size_t characterIndex = 0u; characterIndex < text.size(); ++characterIndex) {
		if (text[characterIndex] != '\\' || characterIndex + 1u >= text.size()) {
			decodedText.push_back(text[characterIndex]);
			continue;
		}

		++characterIndex;

		switch (text[characterIndex]) {
		case '\\':
			decodedText.push_back('\\');
			break;
		case 'p':
			decodedText.push_back('|');
			break;
		case 'r':
			decodedText.push_back('\r');
			break;
		case 'n':
			decodedText.push_back('\n');
			break;
		default:
			decodedText.push_back(text[characterIndex]);
			break;
		}
	}

	return decodedText;
}

bool OnlineService::LoadPendingQueue() {
	std::ifstream file(kPendingQueuePath, std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	pendingRequests_.clear();
	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (isFirstLine) {
			isFirstLine = false;

			if (line.rfind("ManoEngineOnlinePendingQueue", 0) == 0) {
				continue;
			}
		}

		// Request|<method>|<endpoint>|<retryCount>|<body>
		if (line.rfind("Request|", 0) != 0) {
			continue;
		}

		std::vector<std::string> elements;
		size_t searchPosition = 0u;

		while (elements.size() < 4u) {
			const size_t separatorPosition = line.find('|', searchPosition);

			if (separatorPosition == std::string::npos) {
				break;
			}

			elements.push_back(line.substr(searchPosition, separatorPosition - searchPosition));
			searchPosition = separatorPosition + 1u;
		}

		if (elements.size() < 4u) {
			continue;
		}

		PendingRequest pendingRequest{};
		pendingRequest.request.method = DecodeQueueField(elements[1]);
		pendingRequest.request.endpoint = DecodeQueueField(elements[2]);
		pendingRequest.request.isQueueable = true;
		pendingRequest.request.body = DecodeQueueField(line.substr(searchPosition));

		try {
			pendingRequest.retryCount = std::stoi(elements[3]);
		}
		catch (...) {
			pendingRequest.retryCount = 0;
		}

		pendingRequests_.push_back(pendingRequest);
	}

	if (!pendingRequests_.empty()) {
		ExternalFeatureLog::Info(
			ExternalFeatureCategory::Online,
			"未送信 Request を " + std::to_string(pendingRequests_.size()) + " 件復元しました。");
	}

	debugInfo_.pendingQueueCount = static_cast<int32_t>(pendingRequests_.size());
	return true;
}

bool OnlineService::SavePendingQueue() const {
	const std::filesystem::path filePath(kPendingQueuePath);
	std::error_code directoryError;

	if (filePath.has_parent_path()) {
		std::filesystem::create_directories(filePath.parent_path(), directoryError);
	}

	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		return false;
	}

	file << kPendingQueueHeader << "\r\n";

	for (const PendingRequest& pendingRequest : pendingRequests_) {
		file << "Request|"
		     << EncodeQueueField(pendingRequest.request.method) << "|"
		     << EncodeQueueField(pendingRequest.request.endpoint) << "|"
		     << pendingRequest.retryCount << "|"
		     << EncodeQueueField(pendingRequest.request.body) << "\r\n";
	}

	return true;
}
