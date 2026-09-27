#pragma once

#include "ISpeechBackend.h"

#include <memory>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// whisper.cpp CLI を使うローカル音声認識 Backend
//================================================================
// マイク入力は Engine 側で 16 kHz / mono / PCM16 として収録し、短い WAV に
// 分割して whisper-cli.exe へ渡す。推論は Worker Thread で行うため、Editor の
// Main Thread を止めない。モデルと CLI が無い場合は別 Backend へ代替しない。

class WhisperSpeechBackend final : public ISpeechBackend {
public:
	WhisperSpeechBackend();
	~WhisperSpeechBackend() override;

	WhisperSpeechBackend(const WhisperSpeechBackend&) = delete;
	WhisperSpeechBackend& operator=(const WhisperSpeechBackend&) = delete;

	bool Initialize() override;
	void Shutdown() override;
	void StartRecognition() override;
	void StopRecognition() override;
	void Update() override;
	std::vector<SpeechResult> GetResults() override;

	bool ApplyConfig(const SpeechConfig& config) override;
	bool IsRecognizing() const override;
	bool IsSpeaking() const override;
	bool IsProcessing() const override;
	float GetAudioLevel() const override;
	void EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const override;
	std::string GetActiveDeviceName() const override;
	const char* GetName() const override;
	ExternalFeatureError GetLastError() const override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#pragma warning(pop)
