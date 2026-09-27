#pragma once

#include "ISpeechBackend.h"

#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Windows Speech API (SAPI) を使う音声認識 Backend
//================================================================
// Keyword Mode は文法(Grammar)、Speech-to-Text Mode は Dictation を使う。
// SAPI が使えない環境では Initialize が false を返し、SpeechSystem は
// Unavailable を返すだけにする(勝手に別機能で代替しない / 仕様書 85 項)。
// COM Interface を Header へ出さないため、実体は Impl 側へ隠す。

class WindowsSpeechApiBackend final : public ISpeechBackend {
public:
	WindowsSpeechApiBackend() = default;
	~WindowsSpeechApiBackend() override;

	WindowsSpeechApiBackend(const WindowsSpeechApiBackend&) = delete;
	WindowsSpeechApiBackend& operator=(const WindowsSpeechApiBackend&) = delete;

	bool Initialize() override;
	void Shutdown() override;
	void StartRecognition() override;
	void StopRecognition() override;
	void Update() override;
	std::vector<SpeechResult> GetResults() override;

	bool ApplyConfig(const SpeechConfig& config) override;
	bool IsRecognizing() const override;
	bool IsSpeaking() const override;
	float GetAudioLevel() const override;
	void EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const override;
	std::string GetActiveDeviceName() const override;
	const char* GetName() const override;
	ExternalFeatureError GetLastError() const override;

private:
	struct Impl;  // SAPI の COM Interface をまとめて隠す。

	bool RebuildGrammar();  // Mode / Keyword / 言語から文法を作り直す。

	Impl* impl_ = nullptr;
	SpeechConfig config_{};
	std::vector<SpeechResult> results_;
	ExternalFeatureError lastError_{};
	float audioLevel_ = 0.0f;
	float elapsedSeconds_ = 0.0f;
	bool isInitialized_ = false;
	bool isRecognizing_ = false;
	bool isSpeaking_ = false;
};

#pragma warning(pop)
