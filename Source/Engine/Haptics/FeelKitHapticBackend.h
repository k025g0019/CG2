#pragma once

#include "IHapticBackend.h"

class FeelKitHaptics;

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// FeelKitHaptics を IHapticBackend へ載せた Backend(仕様書 76 項)
//================================================================
// FeelKitHaptics の実体は Editor / Player 側が 1 つだけ持つ。ここは
// その参照を受け取って CG2Engine の型へ橋渡しするだけにする。
// Device が無い場合も Play を黙って捨てるだけで、ゲームロジックは止めない
// (仕様書 78 項)。

class FeelKitHapticBackend final : public IHapticBackend {
public:
	explicit FeelKitHapticBackend(FeelKitHaptics* haptics);
	~FeelKitHapticBackend() override = default;

	FeelKitHapticBackend(const FeelKitHapticBackend&) = delete;
	FeelKitHapticBackend& operator=(const FeelKitHapticBackend&) = delete;

	bool Initialize() override;
	void Shutdown() override;
	void Update(float deltaTime) override;

	void Play(const HapticData& data) override;
	void Stop() override;
	void SetIntensity(float value) override;

	HapticDeviceInfo GetDeviceInfo() const override;
	bool RefreshDevice() override;
	ExternalFeatureError GetLastError() const override;

	bool TryAnalyzeAudioFile(const std::string& audioFilePath, HapticAudioAnalysis& outAnalysis) override;
	bool TryPlayFromAudioFile(const std::string& audioFilePath, const HapticData& data) override;

private:
	FeelKitHaptics* haptics_ = nullptr;  // 実体は外部所有。ここでは破棄しない。
	ExternalFeatureError lastError_{};
	float masterIntensity_ = 1.0f;
	bool isInitialized_ = false;
};

#pragma warning(pop)
