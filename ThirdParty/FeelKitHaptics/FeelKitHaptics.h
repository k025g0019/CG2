#pragma once

#include <memory>

//================================================================
// FeelKitHaptics
//================================================================
// ゲーム側から include して使うための振動入口です。

constexpr unsigned int kFeelKitHapticsAnyXInputUser = 0xFFFFFFFFu;

enum class FeelKitHapticsDeviceKind {
	none,
	autoSelect,
	xInput,
	switch2
};

struct FeelKitHapticsInitializeDesc {
	FeelKitHapticsDeviceKind preferredDevice = FeelKitHapticsDeviceKind::autoSelect;
	unsigned int preferredXInputUserIndex = kFeelKitHapticsAnyXInputUser;
	bool isEnabled = true;
	bool startsWorkerThread = true;
	int workerIntervalMs = 4;
	float masterScale = 1.0f;
};

struct FeelKitHapticsVibrationDesc {
	float leftStrength = 1.0f;
	float rightStrength = 1.0f;
	int durationMs = 120;
	bool isEnabled = true;
};

struct FeelKitHapticsDeviceState {
	bool isInitialized = false;
	bool isReady = false;
	bool isEnabled = false;
	FeelKitHapticsDeviceKind activeDevice = FeelKitHapticsDeviceKind::none;
	unsigned int activeXInputUserIndex = kFeelKitHapticsAnyXInputUser;
};

class FeelKitHaptics {
public:
	FeelKitHaptics();
	~FeelKitHaptics();

	FeelKitHaptics(const FeelKitHaptics&) = delete;
	FeelKitHaptics& operator=(const FeelKitHaptics&) = delete;

	FeelKitHaptics(FeelKitHaptics&&) noexcept;
	FeelKitHaptics& operator=(FeelKitHaptics&&) noexcept;

	bool initialize(const FeelKitHapticsInitializeDesc& initializeDesc = {});
	void shutdown();
	void update();

	bool refreshDevice();
	FeelKitHapticsDeviceState getDeviceState() const;

	void setEnabled(bool isEnabled);
	bool isVibrationEnabled() const;
	bool isReady() const;

	void setMasterScale(float masterScale);
	float getMasterScale() const;

	bool registerSoundVibration(const char* soundPath,
	                            const FeelKitHapticsVibrationDesc& vibrationDesc);
	bool registerSoundVibration(const wchar_t* soundPath,
	                            const FeelKitHapticsVibrationDesc& vibrationDesc);
	void clearSoundVibrations();

	bool vibrateSound(const char* soundPath, bool shouldVibrate = true);
	bool vibrateSound(const wchar_t* soundPath, bool shouldVibrate = true);
	bool vibrateSound(const char* soundPath,
	                  const FeelKitHapticsVibrationDesc& vibrationDesc);
	bool vibrateSound(const wchar_t* soundPath,
	                  const FeelKitHapticsVibrationDesc& vibrationDesc);

	bool playOneShot(float leftStrength,
	                 float rightStrength,
	                 int durationMs,
	                 bool shouldVibrate = true);
	bool playOneShot(const FeelKitHapticsVibrationDesc& vibrationDesc);

	void stop();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
