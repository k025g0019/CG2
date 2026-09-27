#pragma once

#include "ISpeechBackend.h"

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 使える Backend が無い時に入れる空 Backend
//================================================================
// 別機能へ勝手に置き換えず、Unavailable を返し続ける(仕様書 85 項)。

class NullSpeechBackend final : public ISpeechBackend {
public:
	explicit NullSpeechBackend(const std::string& reasonMessage)
		: reasonMessage_(reasonMessage) {
	}

	bool Initialize() override {
		return false;
	}

	void Shutdown() override {
	}

	void StartRecognition() override {
	}

	void StopRecognition() override {
	}

	void Update() override {
	}

	std::vector<SpeechResult> GetResults() override {
		return std::vector<SpeechResult>();
	}

	bool ApplyConfig(const SpeechConfig& config) override {
		static_cast<void>(config);
		return false;
	}

	bool IsRecognizing() const override {
		return false;
	}

	float GetAudioLevel() const override {
		return 0.0f;
	}

	void EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const override {
		outDevices.clear();
	}

	std::string GetActiveDeviceName() const override {
		return std::string();
	}

	const char* GetName() const override {
		return "なし";
	}

	ExternalFeatureError GetLastError() const override {
		ExternalFeatureError error{};
		error.code = -100;
		error.message = reasonMessage_;
		return error;
	}

private:
	std::string reasonMessage_;
};

#pragma warning(pop)
