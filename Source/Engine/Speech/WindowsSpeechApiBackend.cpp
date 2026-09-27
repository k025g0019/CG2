#include "WindowsSpeechApiBackend.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <objbase.h>
#include <sapi.h>
#include <sperror.h>
#include <wrl.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace {
	constexpr wchar_t kKeywordRuleName[] = L"ManoEngineKeywords";
	constexpr ULONG kMaxSpeechAlternatives = 16u;

	std::wstring ToWideString(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}

		const int requiredLength = MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);

		if (requiredLength <= 0) {
			return std::wstring();
		}

		std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wideText.data(), requiredLength);
		return wideText;
	}

	std::string ToUtf8String(const wchar_t* wideText) {
		if (wideText == nullptr || wideText[0] == L'\0') {
			return std::string();
		}

		const int requiredLength = WideCharToMultiByte(
			CP_UTF8, 0, wideText, -1, nullptr, 0, nullptr, nullptr);

		if (requiredLength <= 1) {
			return std::string();
		}

		std::string utf8Text(static_cast<size_t>(requiredLength - 1), '\0');
		WideCharToMultiByte(
			CP_UTF8, 0, wideText, -1, utf8Text.data(), requiredLength, nullptr, nullptr);
		return utf8Text;
	}

	// CLSID を ProgID から取る。sapi.lib をリンクせずに済ませるための経路。
	bool TryGetClassId(const wchar_t* progId, CLSID& outClassId) {
		return SUCCEEDED(CLSIDFromProgID(progId, &outClassId));
	}

	LANGID ToLanguageId(const std::string& languageName) {
		if (languageName.empty()) {
			return static_cast<LANGID>(GetUserDefaultUILanguage());
		}

		const std::wstring wideName = ToWideString(languageName);
		const LCID localeId = LocaleNameToLCID(wideName.c_str(), 0);

		if (localeId == 0) {
			return static_cast<LANGID>(GetUserDefaultUILanguage());
		}

		return LANGIDFROMLCID(localeId);
	}

	ExternalFeatureError MakeComError(const char* functionName, HRESULT result) {
		ExternalFeatureError error{};
		error.code = static_cast<int32_t>(result);
		error.message = std::string(functionName) + " が失敗しました (HRESULT=0x";

		char buffer[16] = {};
		sprintf_s(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(result));
		error.message += buffer;
		error.message += ")";
		return error;
	}
}

struct WindowsSpeechApiBackend::Impl {
	Microsoft::WRL::ComPtr<ISpRecognizer> recognizer;
	Microsoft::WRL::ComPtr<ISpRecoContext> recoContext;
	Microsoft::WRL::ComPtr<ISpRecoGrammar> grammar;
	std::string activeDeviceName;
	bool hasInitializedCom = false;
	bool isDictationLoaded = false;
};

WindowsSpeechApiBackend::~WindowsSpeechApiBackend() {
	Shutdown();
}

bool WindowsSpeechApiBackend::Initialize() {
	if (isInitialized_) {
		return true;
	}

	lastError_.Clear();
	impl_ = new Impl();

	// Editor 本体が既に COM を初期化している場合は RPC_E_CHANGED_MODE になるため、
	// その場合は初期化済みとして扱い、Uninitialize も行わない。
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	if (SUCCEEDED(comResult)) {
		impl_->hasInitializedCom = true;
	}
	else if (comResult != RPC_E_CHANGED_MODE) {
		lastError_ = MakeComError("CoInitializeEx", comResult);
		delete impl_;
		impl_ = nullptr;
		return false;
	}

	// 既定マイクを使う共有 Recognizer を優先する。Device 指定がある場合だけ
	// In-Proc Recognizer を作り、そこへ Audio Input Token を割り当てる。
	bool wasRecognizerCreated = false;

	if (!config_.microphoneDeviceName.empty()) {
		CLSID inprocClassId{};
		CLSID tokenCategoryClassId{};
		CLSID tokenClassId{};

		if (TryGetClassId(L"SAPI.SpInprocRecognizer", inprocClassId) &&
			TryGetClassId(L"SAPI.SpObjectTokenCategory", tokenCategoryClassId) &&
			TryGetClassId(L"SAPI.SpObjectToken", tokenClassId)) {
			Microsoft::WRL::ComPtr<ISpRecognizer> inprocRecognizer;
			HRESULT result = CoCreateInstance(
				inprocClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&inprocRecognizer));

			if (SUCCEEDED(result)) {
				// 既定の認識エンジンを割り当てる。
				Microsoft::WRL::ComPtr<ISpObjectTokenCategory> recognizerCategory;

				if (SUCCEEDED(CoCreateInstance(
						tokenCategoryClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&recognizerCategory))) &&
					SUCCEEDED(recognizerCategory->SetId(SPCAT_RECOGNIZERS, FALSE))) {
					LPWSTR defaultTokenId = nullptr;

					if (SUCCEEDED(recognizerCategory->GetDefaultTokenId(&defaultTokenId)) &&
						defaultTokenId != nullptr) {
						Microsoft::WRL::ComPtr<ISpObjectToken> recognizerToken;

						if (SUCCEEDED(CoCreateInstance(
								tokenClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&recognizerToken))) &&
							SUCCEEDED(recognizerToken->SetId(nullptr, defaultTokenId, FALSE))) {
							inprocRecognizer->SetRecognizer(recognizerToken.Get());
						}

						CoTaskMemFree(defaultTokenId);
					}
				}

				// 指定名に一致する Audio Input Token を探して割り当てる。
				Microsoft::WRL::ComPtr<ISpObjectTokenCategory> audioCategory;

				if (SUCCEEDED(CoCreateInstance(
						tokenCategoryClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&audioCategory))) &&
					SUCCEEDED(audioCategory->SetId(SPCAT_AUDIOIN, FALSE))) {
					Microsoft::WRL::ComPtr<IEnumSpObjectTokens> tokenEnumerator;

					if (SUCCEEDED(audioCategory->EnumTokens(nullptr, nullptr, &tokenEnumerator))) {
						Microsoft::WRL::ComPtr<ISpObjectToken> audioToken;
						ULONG fetchedCount = 0;

						while (SUCCEEDED(tokenEnumerator->Next(1, audioToken.ReleaseAndGetAddressOf(), &fetchedCount)) &&
							fetchedCount == 1) {
							LPWSTR description = nullptr;

							if (SUCCEEDED(audioToken->GetStringValue(nullptr, &description)) && description != nullptr) {
								const std::string deviceName = ToUtf8String(description);
								CoTaskMemFree(description);

								if (deviceName.find(config_.microphoneDeviceName) != std::string::npos) {
									if (SUCCEEDED(inprocRecognizer->SetInput(audioToken.Get(), TRUE))) {
										impl_->activeDeviceName = deviceName;
										impl_->recognizer = inprocRecognizer;
										wasRecognizerCreated = true;
									}

									break;
								}
							}
						}
					}
				}
			}
		}

		if (!wasRecognizerCreated) {
			ExternalFeatureLog::Warning(
				ExternalFeatureCategory::Speech,
				"指定マイクを使えないため既定マイクへ切り替えます: " + config_.microphoneDeviceName);
		}
	}

	if (!wasRecognizerCreated) {
		CLSID sharedClassId{};

		if (!TryGetClassId(L"SAPI.SpSharedRecognizer", sharedClassId)) {
			lastError_.code = -1;
			lastError_.message = "Windows Speech API (SAPI) がこの環境に見つかりません。";
			Shutdown();
			return false;
		}

		const HRESULT result = CoCreateInstance(
			sharedClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->recognizer));

		if (FAILED(result)) {
			lastError_ = MakeComError("CoCreateInstance(SpSharedRecognizer)", result);
			Shutdown();
			return false;
		}

		impl_->activeDeviceName = "既定のマイク";
	}

	HRESULT result = impl_->recognizer->CreateRecoContext(&impl_->recoContext);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecognizer::CreateRecoContext", result);
		Shutdown();
		return false;
	}

	// Command Grammarでも2位以下を取得できるよう、Contextへ候補保持数を明示する。
	// EngineがCommand候補を提供しない場合は0件のままなので、Runtime側は2位Score=0として扱う。
	result = impl_->recoContext->SetMaxAlternates(kMaxSpeechAlternatives);
	if (FAILED(result)) {
		ExternalFeatureLog::Warning(
			ExternalFeatureCategory::Speech,
			"SAPIの音声候補数を設定できませんでした。2位候補なしで認識を続けます。");
	}

	// 認識結果、途中結果、音量、発話開始/終了、認識失敗を受け取る。
	const ULONGLONG eventInterest =
		SPFEI(SPEI_RECOGNITION) |
		SPFEI(SPEI_HYPOTHESIS) |
		SPFEI(SPEI_FALSE_RECOGNITION) |
		SPFEI(SPEI_SR_AUDIO_LEVEL) |
		SPFEI(SPEI_SOUND_START) |
		SPFEI(SPEI_SOUND_END);

	result = impl_->recoContext->SetInterest(eventInterest, eventInterest);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecoContext::SetInterest", result);
		Shutdown();
		return false;
	}

	impl_->recoContext->SetNotifyWin32Event();

	if (!RebuildGrammar()) {
		Shutdown();
		return false;
	}

	// StartRecognition が呼ばれるまでマイクを掴まない。
	impl_->recognizer->SetRecoState(SPRST_INACTIVE);
	isInitialized_ = true;
	return true;
}

void WindowsSpeechApiBackend::Shutdown() {
	if (impl_ == nullptr) {
		isInitialized_ = false;
		isRecognizing_ = false;
		return;
	}

	if (impl_->recognizer) {
		impl_->recognizer->SetRecoState(SPRST_INACTIVE);
	}

	impl_->grammar.Reset();
	impl_->recoContext.Reset();
	impl_->recognizer.Reset();

	if (impl_->hasInitializedCom) {
		CoUninitialize();
	}

	delete impl_;
	impl_ = nullptr;
	isInitialized_ = false;
	isRecognizing_ = false;
	isSpeaking_ = false;
	audioLevel_ = 0.0f;
}

bool WindowsSpeechApiBackend::RebuildGrammar() {
	if (impl_ == nullptr || !impl_->recoContext) {
		return false;
	}

	impl_->grammar.Reset();
	impl_->isDictationLoaded = false;

	HRESULT result = impl_->recoContext->CreateGrammar(1, &impl_->grammar);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecoContext::CreateGrammar", result);
		return false;
	}

	if (config_.mode == SpeechRecognitionMode::SpeechToText) {
		// 発話内容全体を文字列へ変換する(仕様書 8 項)。
		result = impl_->grammar->LoadDictation(nullptr, SPLO_STATIC);

		if (FAILED(result)) {
			lastError_ = MakeComError("ISpRecoGrammar::LoadDictation", result);
			return false;
		}

		impl_->isDictationLoaded = true;
		impl_->grammar->SetDictationState(isRecognizing_ ? SPRS_ACTIVE : SPRS_INACTIVE);
		lastError_.Clear();
		return true;
	}

	// Keyword Mode。登録語だけを認識する(仕様書 7 項)。
	const LANGID languageId = ToLanguageId(config_.language);
	result = impl_->grammar->ResetGrammar(languageId);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecoGrammar::ResetGrammar", result);
		return false;
	}

	SPSTATEHANDLE ruleState = nullptr;
	result = impl_->grammar->GetRule(
		kKeywordRuleName,
		0,
		SPRAF_TopLevel | SPRAF_Active,
		TRUE,
		&ruleState);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecoGrammar::GetRule", result);
		return false;
	}

	int32_t addedKeywordCount = 0;

	for (const std::string& keyword : config_.keywords) {
		if (keyword.empty()) {
			continue;
		}

		const std::wstring wideKeyword = ToWideString(keyword);
		result = impl_->grammar->AddWordTransition(
			ruleState,
			nullptr,
			wideKeyword.c_str(),
			L" ",
			SPWT_LEXICAL,
			1.0f,
			nullptr);

		if (SUCCEEDED(result)) {
			++addedKeywordCount;
		}
	}

	if (addedKeywordCount == 0) {
		// 登録語が無い状態で Commit すると認識器が空文法を掴むため、ここで止める。
		lastError_.code = -2;
		lastError_.message = "Keyword Mode の登録語が空です。Keyword List を設定してください。";
		return false;
	}

	result = impl_->grammar->Commit(0);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecoGrammar::Commit", result);
		return false;
	}

	impl_->grammar->SetRuleState(nullptr, nullptr, isRecognizing_ ? SPRS_ACTIVE : SPRS_INACTIVE);
	lastError_.Clear();
	return true;
}

void WindowsSpeechApiBackend::StartRecognition() {
	if (impl_ == nullptr || !isInitialized_ || !impl_->recognizer) {
		return;
	}

	const HRESULT result = impl_->recognizer->SetRecoState(SPRST_ACTIVE_ALWAYS);

	if (FAILED(result)) {
		lastError_ = MakeComError("ISpRecognizer::SetRecoState", result);
		return;
	}

	if (impl_->grammar) {
		if (impl_->isDictationLoaded) {
			impl_->grammar->SetDictationState(SPRS_ACTIVE);
		}
		else {
			impl_->grammar->SetRuleState(nullptr, nullptr, SPRS_ACTIVE);
		}
	}

	isRecognizing_ = true;
	lastError_.Clear();
}

void WindowsSpeechApiBackend::StopRecognition() {
	if (impl_ == nullptr || !isInitialized_) {
		isRecognizing_ = false;
		isSpeaking_ = false;
		return;
	}

	if (impl_->grammar) {
		if (impl_->isDictationLoaded) {
			impl_->grammar->SetDictationState(SPRS_INACTIVE);
		}
		else {
			impl_->grammar->SetRuleState(nullptr, nullptr, SPRS_INACTIVE);
		}
	}

	if (impl_->recognizer) {
		impl_->recognizer->SetRecoState(SPRST_INACTIVE);
	}

	isRecognizing_ = false;
	isSpeaking_ = false;
	audioLevel_ = 0.0f;
}

void WindowsSpeechApiBackend::Update() {
	if (impl_ == nullptr || !isInitialized_ || !impl_->recoContext) {
		return;
	}

	elapsedSeconds_ += 1.0f / 60.0f;  // Event へ載せる概算時刻。実時間は SpeechSystem が補正する。

	while (true) {
		SPEVENT speechEvent{};
		ULONG fetchedCount = 0;
		const HRESULT result = impl_->recoContext->GetEvents(1, &speechEvent, &fetchedCount);

		if (FAILED(result) || fetchedCount == 0) {
			break;
		}

		switch (speechEvent.eEventId) {
		case SPEI_SR_AUDIO_LEVEL:
			audioLevel_ = (std::clamp)(static_cast<float>(speechEvent.wParam) / 100.0f, 0.0f, 1.0f);
			break;
		case SPEI_SOUND_START:
			isSpeaking_ = true;
			audioLevel_ = (std::max)(audioLevel_, 0.05f);
			break;
		case SPEI_SOUND_END:
			isSpeaking_ = false;
			audioLevel_ = 0.0f;
			break;
		case SPEI_RECOGNITION:
		case SPEI_HYPOTHESIS:
		case SPEI_FALSE_RECOGNITION: {
			ISpRecoResult* recoResult = reinterpret_cast<ISpRecoResult*>(speechEvent.lParam);

			if (recoResult != nullptr) {
				LPWSTR recognizedText = nullptr;

				if (SUCCEEDED(recoResult->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &recognizedText, nullptr)) &&
					recognizedText != nullptr) {
					SpeechResult speechResult{};
					speechResult.text = ToUtf8String(recognizedText);
					speechResult.isFinal = speechEvent.eEventId == SPEI_RECOGNITION;
					speechResult.language = config_.language;
					speechResult.backendName = GetName();
					speechResult.endSeconds = elapsedSeconds_;
					speechResult.startSeconds = elapsedSeconds_;
					CoTaskMemFree(recognizedText);

					// 認識エンジンの信頼度は要素ごとに入るため平均を取る。
					SPPHRASE* phrase = nullptr;

					if (SUCCEEDED(recoResult->GetPhrase(&phrase)) && phrase != nullptr) {
						float confidenceSum = 0.0f;
						uint32_t confidenceCount = 0u;

						for (uint32_t elementIndex = 0u; elementIndex < phrase->Rule.ulCountOfElements; ++elementIndex) {
							if (phrase->pElements == nullptr) {
								break;
							}

							confidenceSum += phrase->pElements[elementIndex].SREngineConfidence;
							++confidenceCount;
						}

						speechResult.confidence = confidenceCount > 0u
							? (std::clamp)(confidenceSum / static_cast<float>(confidenceCount), 0.0f, 1.0f)
							: 0.0f;
						CoTaskMemFree(phrase);
					}

					// 限定語彙の音声Commandでは1位だけでなく2位との差も必要になる。
					// SAPIが返す音響候補を保持し、上位候補が拮抗した時は呼び出し側で棄却できるようにする。
					std::array<ISpPhraseAlt*, kMaxSpeechAlternatives> phraseAlternatives{};
					ULONG alternativeCount = 0u;

					if (SUCCEEDED(recoResult->GetAlternates(
							0u,
							SP_GETWHOLEPHRASE,
							static_cast<ULONG>(phraseAlternatives.size()),
							phraseAlternatives.data(),
							&alternativeCount))) {
						for (ULONG alternativeIndex = 0u; alternativeIndex < alternativeCount; ++alternativeIndex) {
							ISpPhraseAlt* alternative = phraseAlternatives[alternativeIndex];
							if (alternative == nullptr) continue;

							LPWSTR alternativeText = nullptr;
							if (SUCCEEDED(alternative->GetText(
									SP_GETWHOLEPHRASE,
									SP_GETWHOLEPHRASE,
									TRUE,
									&alternativeText,
									nullptr)) && alternativeText != nullptr) {
								SpeechAlternative candidate{};
								candidate.text = ToUtf8String(alternativeText);
								CoTaskMemFree(alternativeText);

								SPPHRASE* alternativePhrase = nullptr;
								if (SUCCEEDED(alternative->GetPhrase(&alternativePhrase)) && alternativePhrase != nullptr) {
									float confidenceSum = 0.0f;
									uint32_t confidenceCount = 0u;
									for (uint32_t elementIndex = 0u;
										elementIndex < alternativePhrase->Rule.ulCountOfElements;
										++elementIndex) {
										if (alternativePhrase->pElements == nullptr) break;
										confidenceSum += alternativePhrase->pElements[elementIndex].SREngineConfidence;
										++confidenceCount;
									}
									candidate.confidence = confidenceCount > 0u
										? (std::clamp)(confidenceSum / static_cast<float>(confidenceCount), 0.0f, 1.0f)
										: 0.0f;
									CoTaskMemFree(alternativePhrase);
								}

								if (!candidate.text.empty() && candidate.text != speechResult.text) {
									speechResult.alternatives.push_back(std::move(candidate));
								}
							}

							alternative->Release();
						}
					}

					if (speechEvent.eEventId == SPEI_FALSE_RECOGNITION) {
						speechResult.confidence = 0.0f;
						speechResult.isFinal = false;
					}

					if (!speechResult.text.empty()) {
						results_.push_back(speechResult);
					}
				}
			}

			break;
		}
		default:
			break;
		}

		// SPEVENT の lParam は種類に応じて解放方法が変わる。
		if (speechEvent.elParamType == SPET_LPARAM_IS_OBJECT && speechEvent.lParam != 0) {
			reinterpret_cast<IUnknown*>(speechEvent.lParam)->Release();
		}
		else if ((speechEvent.elParamType == SPET_LPARAM_IS_POINTER ||
				  speechEvent.elParamType == SPET_LPARAM_IS_STRING) &&
				 speechEvent.lParam != 0) {
			CoTaskMemFree(reinterpret_cast<void*>(speechEvent.lParam));
		}
	}
}

std::vector<SpeechResult> WindowsSpeechApiBackend::GetResults() {
	std::vector<SpeechResult> takenResults;
	takenResults.swap(results_);
	return takenResults;
}

bool WindowsSpeechApiBackend::ApplyConfig(const SpeechConfig& config) {
	const bool needsGrammarRebuild =
		config.mode != config_.mode ||
		config.language != config_.language ||
		config.keywords != config_.keywords;

	config_ = config;

	if (!isInitialized_ || !needsGrammarRebuild) {
		return true;
	}

	return RebuildGrammar();
}

bool WindowsSpeechApiBackend::IsRecognizing() const {
	return isRecognizing_;
}

bool WindowsSpeechApiBackend::IsSpeaking() const {
	return isSpeaking_;
}

float WindowsSpeechApiBackend::GetAudioLevel() const {
	return audioLevel_;
}

void WindowsSpeechApiBackend::EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const {
	outDevices.clear();

	CLSID tokenCategoryClassId{};

	if (!TryGetClassId(L"SAPI.SpObjectTokenCategory", tokenCategoryClassId)) {
		return;
	}

	Microsoft::WRL::ComPtr<ISpObjectTokenCategory> audioCategory;

	if (FAILED(CoCreateInstance(
			tokenCategoryClassId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&audioCategory))) ||
		FAILED(audioCategory->SetId(SPCAT_AUDIOIN, FALSE))) {
		return;
	}

	std::string defaultTokenIdText;
	LPWSTR defaultTokenId = nullptr;

	if (SUCCEEDED(audioCategory->GetDefaultTokenId(&defaultTokenId)) && defaultTokenId != nullptr) {
		defaultTokenIdText = ToUtf8String(defaultTokenId);
		CoTaskMemFree(defaultTokenId);
	}

	Microsoft::WRL::ComPtr<IEnumSpObjectTokens> tokenEnumerator;

	if (FAILED(audioCategory->EnumTokens(nullptr, nullptr, &tokenEnumerator))) {
		return;
	}

	Microsoft::WRL::ComPtr<ISpObjectToken> audioToken;
	ULONG fetchedCount = 0;

	while (SUCCEEDED(tokenEnumerator->Next(1, audioToken.ReleaseAndGetAddressOf(), &fetchedCount)) &&
		fetchedCount == 1) {
		SpeechDeviceInfo deviceInfo{};
		LPWSTR description = nullptr;

		if (SUCCEEDED(audioToken->GetStringValue(nullptr, &description)) && description != nullptr) {
			deviceInfo.deviceName = ToUtf8String(description);
			CoTaskMemFree(description);
		}

		LPWSTR tokenId = nullptr;

		if (SUCCEEDED(audioToken->GetId(&tokenId)) && tokenId != nullptr) {
			deviceInfo.deviceId = ToUtf8String(tokenId);
			CoTaskMemFree(tokenId);
		}

		deviceInfo.isDefault = !defaultTokenIdText.empty() && deviceInfo.deviceId == defaultTokenIdText;

		if (!deviceInfo.deviceName.empty()) {
			outDevices.push_back(deviceInfo);
		}
	}
}

std::string WindowsSpeechApiBackend::GetActiveDeviceName() const {
	return impl_ != nullptr ? impl_->activeDeviceName : std::string();
}

const char* WindowsSpeechApiBackend::GetName() const {
	return "Windows Speech API";
}

ExternalFeatureError WindowsSpeechApiBackend::GetLastError() const {
	return lastError_;
}
