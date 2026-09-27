#include "SpeechSystem.h"

#include "NullSpeechBackend.h"
#include "WhisperSpeechBackend.h"
#include "WindowsSpeechApiBackend.h"

#include <algorithm>
#include <cctype>

namespace {
	// 比較用に前後の空白を落とし、英字を小文字へ寄せる。日本語はそのまま比較する。
	std::string NormalizeForMatch(const std::string& text) {
		std::string normalizedText;
		normalizedText.reserve(text.size());

		for (const char character : text) {
			const unsigned char rawCharacter = static_cast<unsigned char>(character);

			if (rawCharacter < 0x80u) {
				if (std::isspace(rawCharacter) != 0) {
					continue;
				}

				normalizedText.push_back(
					static_cast<char>(std::tolower(rawCharacter)));
				continue;
			}

			normalizedText.push_back(character);
		}

		return normalizedText;
	}
}

const char* ToDisplayString(SpeechRecognitionMode mode) {
	return mode == SpeechRecognitionMode::Keyword ? "キーワード" : "文字起こし";
}

const char* ToDisplayString(SpeechBackendKind backendKind) {
	switch (backendKind) {
	case SpeechBackendKind::Auto:
		return "自動選択";
	case SpeechBackendKind::WindowsSpeechApi:
		return "Windows Speech API";
	case SpeechBackendKind::Whisper:
		return "Whisper";
	case SpeechBackendKind::OnnxLocalModel:
		return "ONNX ローカルモデル";
	case SpeechBackendKind::None:
		return "使用しない";
	default:
		return "自動選択";
	}
}

SpeechSystem& SpeechSystem::Get() {
	static SpeechSystem instance;
	return instance;
}

void SpeechSystem::SetBackend(std::unique_ptr<ISpeechBackend> backend) {
	if (isInitialized_) {
		Shutdown();
	}

	backend_ = std::move(backend);
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
	hasAppliedConfig_ = false;
}

void SpeechSystem::SelectBackend(SpeechBackendKind backendKind) {
	switch (backendKind) {
	case SpeechBackendKind::Auto:
	case SpeechBackendKind::WindowsSpeechApi:
		SetBackend(std::make_unique<WindowsSpeechApiBackend>());
		break;
	case SpeechBackendKind::Whisper:
		SetBackend(std::make_unique<WhisperSpeechBackend>());
		break;
	case SpeechBackendKind::OnnxLocalModel:
		SetBackend(std::make_unique<NullSpeechBackend>(
			"ONNX 音声認識 Backend はまだ実装されていません。Windows Speech API を選んでください。"));
		break;
	case SpeechBackendKind::None:
	default:
		SetBackend(std::make_unique<NullSpeechBackend>("音声認識 Backend が無効です。"));
		break;
	}
}

bool SpeechSystem::Initialize() {
	if (backend_ == nullptr) {
		state_ = ExternalFeatureState::Unavailable;
		lastError_.code = 1;
		lastError_.message = "音声認識 Backend が設定されていません。";
		return false;
	}

	if (isInitialized_) {
		return true;
	}

	// Backend の初期化前に、いま登録されている Session の設定を渡しておく。
	if (!sessions_.empty()) {
		appliedConfig_ = MakeCombinedConfig();
		backend_->ApplyConfig(appliedConfig_);
		hasAppliedConfig_ = true;
	}

	if (!backend_->Initialize()) {
		lastError_ = backend_->GetLastError();

		if (!lastError_.HasError()) {
			lastError_.code = 2;
			lastError_.message = "音声認識 Backend の初期化に失敗しました。";
		}

		state_ = ExternalFeatureState::Unavailable;
		status_.state = state_;
		status_.lastError = lastError_;
		ExternalFeatureLog::Error(ExternalFeatureCategory::Speech, lastError_);
		return false;
	}

	isInitialized_ = true;
	state_ = ExternalFeatureState::Ready;
	lastError_.Clear();
	status_ = SpeechRuntimeStatus{};
	status_.state = state_;
	status_.backendName = backend_->GetName();
	status_.deviceName = backend_->GetActiveDeviceName();

	ExternalFeatureLog::Info(
		ExternalFeatureCategory::Speech,
		std::string("Backend 初期化: ") + backend_->GetName() +
			" / Device: " + backend_->GetActiveDeviceName());
	return true;
}

void SpeechSystem::Shutdown() {
	if (backend_ != nullptr) {
		backend_->StopRecognition();

		if (isInitialized_) {
			backend_->Shutdown();
		}
	}

	sessions_.clear();
	isInitialized_ = false;
	isBackendRunning_ = false;
	hasAppliedConfig_ = false;
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
	status_ = SpeechRuntimeStatus{};
	status_.state = state_;
}

void SpeechSystem::RegisterSession(int32_t gameObjectId, const SpeechConfig& config) {
	Session& session = sessions_[gameObjectId];
	session.config = config;
}

void SpeechSystem::UnregisterSession(int32_t gameObjectId) {
	const auto sessionIt = sessions_.find(gameObjectId);

	if (sessionIt == sessions_.end()) {
		return;
	}

	if (sessionIt->second.isRecognizing && onSpeechEnded_) {
		onSpeechEnded_(gameObjectId);
	}

	sessions_.erase(sessionIt);
}

void SpeechSystem::UnregisterAllSessions() {
	sessions_.clear();
}

bool SpeechSystem::StartRecognition(int32_t gameObjectId) {
	const auto sessionIt = sessions_.find(gameObjectId);

	if (sessionIt == sessions_.end()) {
		return false;
	}

	if (!isInitialized_ && !Initialize()) {
		if (onSpeechError_) {
			onSpeechError_(gameObjectId, lastError_);
		}

		return false;
	}

	Session& session = sessionIt->second;

	if (!session.isRecognizing) {
		session.isRecognizing = true;
		session.hasRequestedStart = true;

		if (onSpeechStarted_) {
			onSpeechStarted_(gameObjectId);
		}
	}

	return true;
}

bool SpeechSystem::StopRecognition(int32_t gameObjectId) {
	const auto sessionIt = sessions_.find(gameObjectId);

	if (sessionIt == sessions_.end()) {
		return false;
	}

	Session& session = sessionIt->second;

	if (session.isRecognizing) {
		session.isRecognizing = false;

		if (onSpeechEnded_) {
			onSpeechEnded_(gameObjectId);
		}
	}

	return true;
}

bool SpeechSystem::IsRecognizing(int32_t gameObjectId) const {
	const auto sessionIt = sessions_.find(gameObjectId);
	return sessionIt != sessions_.end() && sessionIt->second.isRecognizing;
}

bool SpeechSystem::HasSession(int32_t gameObjectId) const {
	return sessions_.find(gameObjectId) != sessions_.end();
}

bool SpeechSystem::ShouldBackendRun() const {
	for (const std::pair<const int32_t, Session>& sessionPair : sessions_) {
		if (sessionPair.second.isRecognizing) {
			return true;
		}
	}

	return false;
}

SpeechConfig SpeechSystem::MakeCombinedConfig() const {
	SpeechConfig combinedConfig{};
	bool hasFirstSession = false;

	for (const std::pair<const int32_t, Session>& sessionPair : sessions_) {
		const SpeechConfig& sessionConfig = sessionPair.second.config;

		if (!hasFirstSession) {
			combinedConfig = sessionConfig;
			combinedConfig.keywords.clear();
			hasFirstSession = true;
		}

		// どれか 1 つでも文字起こしを求めていれば Dictation 側に合わせる。
		if (sessionConfig.mode == SpeechRecognitionMode::SpeechToText) {
			combinedConfig.mode = SpeechRecognitionMode::SpeechToText;
		}

		// Keyword は全 Session 分を合わせて 1 つの文法にする。
		for (const std::string& keyword : sessionConfig.keywords) {
			if (keyword.empty()) {
				continue;
			}

			if (std::find(combinedConfig.keywords.begin(), combinedConfig.keywords.end(), keyword) ==
				combinedConfig.keywords.end()) {
				combinedConfig.keywords.push_back(keyword);
			}
		}

		// しきい値は Session ごとに判定するため、Backend へは最小値を渡す。
		combinedConfig.confidenceThreshold =
			(std::min)(combinedConfig.confidenceThreshold, sessionConfig.confidenceThreshold);
		// Whisper Backend は共有なので、複数 Session では最も早く応答する設定へ合わせる。
		combinedConfig.whisperEndOnSilence =
			combinedConfig.whisperEndOnSilence || sessionConfig.whisperEndOnSilence;
		combinedConfig.whisperMaximumCaptureSeconds =
			(std::min)(combinedConfig.whisperMaximumCaptureSeconds, sessionConfig.whisperMaximumCaptureSeconds);
		combinedConfig.whisperSilenceSeconds =
			(std::min)(combinedConfig.whisperSilenceSeconds, sessionConfig.whisperSilenceSeconds);
		combinedConfig.whisperVoiceThreshold =
			(std::min)(combinedConfig.whisperVoiceThreshold, sessionConfig.whisperVoiceThreshold);
	}

	return combinedConfig;
}

void SpeechSystem::Update(float deltaTime) {
	elapsedSeconds_ += (std::max)(deltaTime, 0.0f);

	for (std::pair<const int32_t, Session>& sessionPair : sessions_) {
		sessionPair.second.frameResults.clear();
		sessionPair.second.recognizedKeywords.clear();
	}

	if (backend_ == nullptr) {
		state_ = ExternalFeatureState::Unavailable;
		status_.state = state_;
		return;
	}

	const bool shouldRun = ShouldBackendRun();

	if (shouldRun && !isInitialized_ && !Initialize()) {
		return;
	}

	if (!isInitialized_) {
		return;
	}

	// Session 構成が変わっていたら Backend の文法を作り直す。
	const SpeechConfig combinedConfig = MakeCombinedConfig();
	const bool needsApply =
		!hasAppliedConfig_ ||
		combinedConfig.mode != appliedConfig_.mode ||
		combinedConfig.language != appliedConfig_.language ||
		combinedConfig.microphoneDeviceName != appliedConfig_.microphoneDeviceName ||
		combinedConfig.modelAssetPath != appliedConfig_.modelAssetPath ||
		combinedConfig.whisperEndOnSilence != appliedConfig_.whisperEndOnSilence ||
		combinedConfig.whisperMaximumCaptureSeconds != appliedConfig_.whisperMaximumCaptureSeconds ||
		combinedConfig.whisperSilenceSeconds != appliedConfig_.whisperSilenceSeconds ||
		combinedConfig.whisperVoiceThreshold != appliedConfig_.whisperVoiceThreshold ||
		combinedConfig.keywords != appliedConfig_.keywords;

	if (needsApply && !sessions_.empty()) {
		if (!backend_->ApplyConfig(combinedConfig)) {
			lastError_ = backend_->GetLastError();
			status_.lastError = lastError_;
			state_ = ExternalFeatureState::Error;
			status_.state = state_;

			if (lastError_.HasError()) {
				ExternalFeatureLog::Error(ExternalFeatureCategory::Speech, lastError_);
			}
		}

		appliedConfig_ = combinedConfig;
		hasAppliedConfig_ = true;
	}

	if (shouldRun && !isBackendRunning_) {
		backend_->StartRecognition();
		isBackendRunning_ = backend_->IsRecognizing();

		if (!isBackendRunning_) {
			lastError_ = backend_->GetLastError();

			if (lastError_.HasError()) {
				status_.lastError = lastError_;
				state_ = ExternalFeatureState::Error;
				status_.state = state_;
				ExternalFeatureLog::Error(ExternalFeatureCategory::Speech, lastError_);

				for (const std::pair<const int32_t, Session>& sessionPair : sessions_) {
					if (sessionPair.second.isRecognizing && onSpeechError_) {
						onSpeechError_(sessionPair.first, lastError_);
					}
				}
			}
		}
	}
	else if (!shouldRun && isBackendRunning_) {
		backend_->StopRecognition();
		isBackendRunning_ = false;
	}

	backend_->Update();
	status_.audioLevel = backend_->GetAudioLevel();
	status_.isMicrophoneActive = isBackendRunning_;
	status_.isRecognizing = isBackendRunning_;
	status_.isSpeaking = isBackendRunning_ && backend_->IsSpeaking();
	status_.isProcessing = backend_->IsProcessing();
	status_.backendName = backend_->GetName();
	status_.deviceName = backend_->GetActiveDeviceName();
	state_ = isBackendRunning_ ? ExternalFeatureState::Running : ExternalFeatureState::Ready;
	status_.state = state_;

	// 非同期 Backend では Update 後に推論Errorが確定する。
	// 同じErrorを毎Frame通知せず、新しいErrorだけをSessionへ配る。
	const ExternalFeatureError backendError = backend_->GetLastError();

	if (backendError.HasError()) {
		const bool isNewError =
			backendError.code != lastError_.code || backendError.message != lastError_.message;
		lastError_ = backendError;
		status_.lastError = backendError;
		state_ = ExternalFeatureState::Error;
		status_.state = state_;

		if (isNewError) {
			ExternalFeatureLog::Error(ExternalFeatureCategory::Speech, backendError);

			for (const std::pair<const int32_t, Session>& sessionPair : sessions_) {
				if (sessionPair.second.isRecognizing && onSpeechError_) {
					onSpeechError_(sessionPair.first, backendError);
				}
			}
		}
	}
	else {
		lastError_.Clear();
		status_.lastError.Clear();
	}

	const std::vector<SpeechResult> backendResults = backend_->GetResults();

	for (const SpeechResult& backendResult : backendResults) {
		SpeechResult result = backendResult;
		result.endSeconds = elapsedSeconds_;

		if (result.startSeconds <= 0.0f) {
			result.startSeconds = elapsedSeconds_;
		}

		status_.lastText = result.text;
		status_.lastConfidence = result.confidence;

		if (result.isFinal) {
			status_.recognizedCount += 1;
		}

		for (std::pair<const int32_t, Session>& sessionPair : sessions_) {
			Session& session = sessionPair.second;

			if (!session.isRecognizing) {
				continue;
			}

			if (result.confidence < session.config.confidenceThreshold && result.isFinal) {
				// しきい値未満の確定結果は捨てる。途中結果は Confidence が低いため対象外。
				continue;
			}

			SpeechResult sessionResult = result;
			bool hasKeywordMatch = false;

			for (const std::string& keyword : session.config.keywords) {
				if (IsKeywordMatch(result.text, keyword)) {
					sessionResult.matchedKeyword = keyword;
					hasKeywordMatch = true;
					break;
				}
			}

			// Keyword Mode では登録語に一致した時だけ通知する(仕様書 7 項)。
			if (session.config.mode == SpeechRecognitionMode::Keyword && !hasKeywordMatch) {
				continue;
			}

			session.frameResults.push_back(sessionResult);

			if (sessionResult.isFinal) {
				session.lastResult = sessionResult;
			}

			if (hasKeywordMatch && sessionResult.isFinal) {
				session.recognizedKeywords.push_back(sessionResult.matchedKeyword);
				status_.lastKeyword = sessionResult.matchedKeyword;

				if (onKeywordRecognized_) {
					onKeywordRecognized_(sessionPair.first, sessionResult.matchedKeyword, sessionResult);
				}
			}

			if (onSpeechRecognized_) {
				onSpeechRecognized_(sessionPair.first, sessionResult);
			}

			// 連続認識しない設定なら、確定結果 1 件で止める(仕様書 12 項)。
			if (sessionResult.isFinal && !session.config.isContinuous) {
				session.isRecognizing = false;

				if (onSpeechEnded_) {
					onSpeechEnded_(sessionPair.first);
				}
			}
		}
	}
}

ExternalFeatureState SpeechSystem::GetState() const {
	return state_;
}

ExternalFeatureError SpeechSystem::GetLastError() const {
	return lastError_;
}

SpeechRuntimeStatus SpeechSystem::GetStatus() const {
	return status_;
}

void SpeechSystem::EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const {
	outDevices.clear();

	if (backend_ != nullptr) {
		backend_->EnumerateDevices(outDevices);
		return;
	}

	// Play 前でも Inspector からマイク一覧を出せるよう、一時 Backend で列挙する。
	const WindowsSpeechApiBackend enumerationBackend;
	enumerationBackend.EnumerateDevices(outDevices);
}

bool SpeechSystem::TryGetLatestResult(int32_t gameObjectId, SpeechResult& outResult) const {
	const auto sessionIt = sessions_.find(gameObjectId);

	if (sessionIt == sessions_.end() || sessionIt->second.lastResult.text.empty()) {
		return false;
	}

	outResult = sessionIt->second.lastResult;
	return true;
}

const std::vector<SpeechResult>& SpeechSystem::GetFrameResults(int32_t gameObjectId) const {
	const auto sessionIt = sessions_.find(gameObjectId);
	return sessionIt == sessions_.end() ? emptyResults_ : sessionIt->second.frameResults;
}

bool SpeechSystem::WasKeywordRecognized(int32_t gameObjectId, const std::string& keyword) const {
	const auto sessionIt = sessions_.find(gameObjectId);

	if (sessionIt == sessions_.end()) {
		return false;
	}

	for (const std::string& recognizedKeyword : sessionIt->second.recognizedKeywords) {
		if (IsKeywordMatch(recognizedKeyword, keyword)) {
			return true;
		}
	}

	return false;
}

void SpeechSystem::SetOnSpeechStarted(StateCallback callback) {
	onSpeechStarted_ = std::move(callback);
}

void SpeechSystem::SetOnSpeechEnded(StateCallback callback) {
	onSpeechEnded_ = std::move(callback);
}

void SpeechSystem::SetOnSpeechRecognized(ResultCallback callback) {
	onSpeechRecognized_ = std::move(callback);
}

void SpeechSystem::SetOnKeywordRecognized(KeywordCallback callback) {
	onKeywordRecognized_ = std::move(callback);
}

void SpeechSystem::SetOnSpeechError(ErrorCallback callback) {
	onSpeechError_ = std::move(callback);
}

bool SpeechSystem::IsKeywordMatch(const std::string& recognizedText, const std::string& keyword) {
	if (recognizedText.empty() || keyword.empty()) {
		return false;
	}

	const std::string normalizedText = NormalizeForMatch(recognizedText);
	const std::string normalizedKeyword = NormalizeForMatch(keyword);

	if (normalizedText.empty() || normalizedKeyword.empty()) {
		return false;
	}

	// 文字起こし結果の中に登録語が含まれていれば一致とする。
	return normalizedText.find(normalizedKeyword) != std::string::npos;
}
