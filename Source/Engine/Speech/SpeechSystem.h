#pragma once

#include "ISpeechBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// SpeechSystem
//================================================================
// マイク入力 → Backend → 認識結果を 1 か所で管理し、
// SpeechRecognizerComponent ごとの Session として結果を配る。
//
// マイクと認識エンジンは 1 つしか無いため、Backend は 1 個だけ持つ。
// 複数 Component が同時に認識する場合は、
//   ・どれか 1 つでも Speech-to-Text Mode なら Dictation で認識する
//   ・全部 Keyword Mode なら、全 Session の Keyword を合わせた文法で認識する
// とし、結果の絞り込み(Keyword 一致 / Confidence しきい値)は Session ごとに行う。

class SpeechSystem {
public:
	using ResultCallback = std::function<void(int32_t gameObjectId, const SpeechResult& result)>;
	using KeywordCallback = std::function<void(int32_t gameObjectId, const std::string& keyword, const SpeechResult& result)>;
	using StateCallback = std::function<void(int32_t gameObjectId)>;
	using ErrorCallback = std::function<void(int32_t gameObjectId, const ExternalFeatureError& error)>;

	static SpeechSystem& Get();

	SpeechSystem(const SpeechSystem&) = delete;
	SpeechSystem& operator=(const SpeechSystem&) = delete;

	//============================================================
	// Backend
	//============================================================
	void SetBackend(std::unique_ptr<ISpeechBackend> backend);
	// Inspector の Backend 選択から実体を作る。使えない場合は Null Backend を入れる。
	void SelectBackend(SpeechBackendKind backendKind);
	bool Initialize();
	void Shutdown();
	void Update(float deltaTime);

	ExternalFeatureState GetState() const;
	ExternalFeatureError GetLastError() const;
	SpeechRuntimeStatus GetStatus() const;
	void EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const;

	//============================================================
	// Session(SpeechRecognizerComponent 1 つ分)
	//============================================================
	void RegisterSession(int32_t gameObjectId, const SpeechConfig& config);  // 毎フレーム最新設定で呼んでよい。
	void UnregisterSession(int32_t gameObjectId);
	void UnregisterAllSessions();
	bool StartRecognition(int32_t gameObjectId);
	bool StopRecognition(int32_t gameObjectId);
	bool IsRecognizing(int32_t gameObjectId) const;
	bool HasSession(int32_t gameObjectId) const;

	//============================================================
	// 結果の取得
	//============================================================
	bool TryGetLatestResult(int32_t gameObjectId, SpeechResult& outResult) const;  // 直近の確定結果。
	// このフレームに来た結果。Input 連携や Script 通知に使う。
	const std::vector<SpeechResult>& GetFrameResults(int32_t gameObjectId) const;
	bool WasKeywordRecognized(int32_t gameObjectId, const std::string& keyword) const;  // このフレームで一致したか。

	//============================================================
	// Event(仕様書 13 項)
	//============================================================
	void SetOnSpeechStarted(StateCallback callback);
	void SetOnSpeechEnded(StateCallback callback);
	void SetOnSpeechRecognized(ResultCallback callback);
	void SetOnKeywordRecognized(KeywordCallback callback);
	void SetOnSpeechError(ErrorCallback callback);

	// 文字列比較の共通処理。Keyword 一致判定を Editor 側からも同じ規則で使う。
	static bool IsKeywordMatch(const std::string& recognizedText, const std::string& keyword);

private:
	SpeechSystem() = default;
	~SpeechSystem() = default;

	struct Session {
		SpeechConfig config{};
		bool isRecognizing = false;
		bool hasRequestedStart = false;
		SpeechResult lastResult{};
		std::vector<SpeechResult> frameResults;
		std::vector<std::string> recognizedKeywords;  // このフレームで一致した Keyword。
	};

	SpeechConfig MakeCombinedConfig() const;  // 全 Session から Backend 用の設定を作る。
	bool ShouldBackendRun() const;

	std::unique_ptr<ISpeechBackend> backend_;
	std::unordered_map<int32_t, Session> sessions_;
	std::vector<SpeechResult> emptyResults_;
	SpeechConfig appliedConfig_{};
	SpeechRuntimeStatus status_{};
	ExternalFeatureState state_ = ExternalFeatureState::Unavailable;
	ExternalFeatureError lastError_{};
	ResultCallback onSpeechRecognized_;
	KeywordCallback onKeywordRecognized_;
	StateCallback onSpeechStarted_;
	StateCallback onSpeechEnded_;
	ErrorCallback onSpeechError_;
	float elapsedSeconds_ = 0.0f;
	bool isInitialized_ = false;
	bool isBackendRunning_ = false;
	bool hasAppliedConfig_ = false;
};

#pragma warning(pop)
