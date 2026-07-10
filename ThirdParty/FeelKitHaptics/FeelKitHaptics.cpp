#include "FeelKitHaptics.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "xinput.lib")

namespace {

//================================================================
// 内部ユーティリティ
//================================================================

struct ActiveVibration {
	float leftStrength = 0.0f;
	float rightStrength = 0.0f;
	unsigned long long endTimeMs = 0;
};

float clampFloat(float value, float minValue, float maxValue) {
	if (value < minValue) {
		return minValue;
	}

	if (value > maxValue) {
		return maxValue;
	}

	return value;
}

int clampInt(int value, int minValue, int maxValue) {
	if (value < minValue) {
		return minValue;
	}

	if (value > maxValue) {
		return maxValue;
	}

	return value;
}

unsigned short strengthToXInputMotor(float strength) {
	const float clampedStrength = clampFloat(strength, 0.0f, 1.0f);
	const float motorValue = clampedStrength * 65535.0f;
	return static_cast<unsigned short>(std::lround(motorValue));
}

std::wstring makeWideTextFromUtf8(const char* text) {
	if (text == nullptr) {
		return L"";
	}

	const int wideTextLength =
		MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);

	if (wideTextLength <= 0) {
		std::wstring fallbackText;

		while (*text != '\0') {
			fallbackText.push_back(static_cast<unsigned char>(*text));
			text++;
		}

		return fallbackText;
	}

	std::wstring wideText(static_cast<size_t>(wideTextLength), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		text,
		-1,
		&wideText[0],
		wideTextLength);

	if (!wideText.empty() && wideText.back() == L'\0') {
		wideText.pop_back();
	}

	return wideText;
}

} // namespace

//================================================================
// 実装本体
//================================================================

struct FeelKitHaptics::Impl {
	mutable std::mutex mutex;
	std::vector<ActiveVibration> activeVibrations;
	std::unordered_map<std::wstring, FeelKitHapticsVibrationDesc> soundVibrations;
	std::thread workerThread;
	std::atomic_bool shouldRunWorker = false;
	FeelKitHapticsInitializeDesc initializeDesc = {};
	FeelKitHapticsDeviceState deviceState = {};
	float lastSentLeftStrength = 0.0f;
	float lastSentRightStrength = 0.0f;
	unsigned long long lastDeviceRefreshTimeMs = 0;

	~Impl() {
		shutdown();
	}

	bool initialize(const FeelKitHapticsInitializeDesc& newInitializeDesc) {
		shutdown();

		{
			std::lock_guard<std::mutex> lock(mutex);
			initializeDesc = newInitializeDesc;
			initializeDesc.workerIntervalMs =
				clampInt(initializeDesc.workerIntervalMs, 1, 100);
			initializeDesc.masterScale =
				clampFloat(initializeDesc.masterScale, 0.0f, 1.0f);
			deviceState.isInitialized = true;
			deviceState.isEnabled = initializeDesc.isEnabled;
			deviceState.isReady = false;
			deviceState.activeDevice = FeelKitHapticsDeviceKind::none;
			deviceState.activeXInputUserIndex = kFeelKitHapticsAnyXInputUser;
			lastSentLeftStrength = 0.0f;
			lastSentRightStrength = 0.0f;
			lastDeviceRefreshTimeMs = 0;
			activeVibrations.clear();
		}

		refreshDevice();

		if (newInitializeDesc.startsWorkerThread) {
			startWorker();
		}

		return true;
	}

	void shutdown() {
		stopWorker();
		stop();

		std::lock_guard<std::mutex> lock(mutex);
		activeVibrations.clear();
		deviceState = {};
		lastSentLeftStrength = 0.0f;
		lastSentRightStrength = 0.0f;
		lastDeviceRefreshTimeMs = 0;
	}

	void startWorker() {
		if (shouldRunWorker.load()) {
			return;
		}

		shouldRunWorker.store(true);
		workerThread = std::thread([this]() {
			while (shouldRunWorker.load()) {
				update();
				std::this_thread::sleep_for(
					std::chrono::milliseconds(initializeDesc.workerIntervalMs));
			}
		});
	}

	void stopWorker() {
		shouldRunWorker.store(false);

		if (workerThread.joinable()) {
			workerThread.join();
		}
	}

	bool refreshDevice() {
		std::lock_guard<std::mutex> lock(mutex);
		lastDeviceRefreshTimeMs = GetTickCount64();

		if (!deviceState.isInitialized) {
			return false;
		}

		if (initializeDesc.preferredDevice == FeelKitHapticsDeviceKind::switch2) {
			deviceState.isReady = false;
			deviceState.activeDevice = FeelKitHapticsDeviceKind::none;
			deviceState.activeXInputUserIndex = kFeelKitHapticsAnyXInputUser;
			return false;
		}

		if (initializeDesc.preferredDevice == FeelKitHapticsDeviceKind::xInput &&
		    initializeDesc.preferredXInputUserIndex != kFeelKitHapticsAnyXInputUser) {
			return refreshSpecificXInputDevice(initializeDesc.preferredXInputUserIndex);
		}

		for (unsigned int userIndex = 0; userIndex < XUSER_MAX_COUNT; userIndex++) {
			if (initializeDesc.preferredXInputUserIndex != kFeelKitHapticsAnyXInputUser &&
			    userIndex != initializeDesc.preferredXInputUserIndex) {
				continue;
			}

			if (refreshSpecificXInputDevice(userIndex)) {
				return true;
			}
		}

		deviceState.isReady = false;
		deviceState.activeDevice = FeelKitHapticsDeviceKind::none;
		deviceState.activeXInputUserIndex = kFeelKitHapticsAnyXInputUser;
		return false;
	}

	bool refreshSpecificXInputDevice(unsigned int userIndex) {
		XINPUT_STATE xInputState = {};
		const DWORD result = XInputGetState(userIndex, &xInputState);

		if (result != ERROR_SUCCESS) {
			return false;
		}

		deviceState.isReady = true;
		deviceState.activeDevice = FeelKitHapticsDeviceKind::xInput;
		deviceState.activeXInputUserIndex = userIndex;
		return true;
	}

	void update() {
		float mixedLeftStrength = 0.0f;
		float mixedRightStrength = 0.0f;
		bool shouldSendStop = false;
		bool shouldSendMotor = false;
		unsigned int xInputUserIndex = kFeelKitHapticsAnyXInputUser;

		{
			std::lock_guard<std::mutex> lock(mutex);

			if (!deviceState.isInitialized) {
				return;
			}

			if (!deviceState.isReady) {
				const unsigned long long currentTimeMs = GetTickCount64();

				if (currentTimeMs - lastDeviceRefreshTimeMs >= 1000) {
					// ロック中に直接 XInput を確認して、ゲーム側の手間を減らします。
					lastDeviceRefreshTimeMs = currentTimeMs;

					for (unsigned int userIndex = 0; userIndex < XUSER_MAX_COUNT; userIndex++) {
						if (initializeDesc.preferredXInputUserIndex != kFeelKitHapticsAnyXInputUser &&
						    userIndex != initializeDesc.preferredXInputUserIndex) {
							continue;
						}

						if (refreshSpecificXInputDevice(userIndex)) {
							break;
						}
					}
				}
			}

			if (!deviceState.isEnabled || !deviceState.isReady) {
				activeVibrations.clear();
				shouldSendStop = lastSentLeftStrength > 0.0f || lastSentRightStrength > 0.0f;
				xInputUserIndex = deviceState.activeXInputUserIndex;
			} else {
				const unsigned long long currentTimeMs = GetTickCount64();

				activeVibrations.erase(
					std::remove_if(
						activeVibrations.begin(),
						activeVibrations.end(),
						[currentTimeMs](const ActiveVibration& vibration) {
							return vibration.endTimeMs <= currentTimeMs;
						}),
					activeVibrations.end());

				for (const ActiveVibration& vibration : activeVibrations) {
					mixedLeftStrength =
						std::max(mixedLeftStrength, vibration.leftStrength);
					mixedRightStrength =
						std::max(mixedRightStrength, vibration.rightStrength);
				}

				mixedLeftStrength *= initializeDesc.masterScale;
				mixedRightStrength *= initializeDesc.masterScale;
				mixedLeftStrength = clampFloat(mixedLeftStrength, 0.0f, 1.0f);
				mixedRightStrength = clampFloat(mixedRightStrength, 0.0f, 1.0f);

				shouldSendMotor =
					std::fabs(mixedLeftStrength - lastSentLeftStrength) > 0.001f ||
					std::fabs(mixedRightStrength - lastSentRightStrength) > 0.001f;

				shouldSendStop =
					activeVibrations.empty() &&
					(lastSentLeftStrength > 0.0f || lastSentRightStrength > 0.0f);
				xInputUserIndex = deviceState.activeXInputUserIndex;
			}
		}

		if (shouldSendStop) {
			sendXInputMotor(xInputUserIndex, 0.0f, 0.0f);
			saveLastSentStrength(0.0f, 0.0f);
			return;
		}

		if (shouldSendMotor) {
			sendXInputMotor(xInputUserIndex, mixedLeftStrength, mixedRightStrength);
			saveLastSentStrength(mixedLeftStrength, mixedRightStrength);
		}
	}

	void saveLastSentStrength(float leftStrength, float rightStrength) {
		std::lock_guard<std::mutex> lock(mutex);
		lastSentLeftStrength = leftStrength;
		lastSentRightStrength = rightStrength;
	}

	bool sendXInputMotor(unsigned int userIndex, float leftStrength, float rightStrength) {
		if (userIndex == kFeelKitHapticsAnyXInputUser) {
			return false;
		}

		XINPUT_VIBRATION vibration = {};
		vibration.wLeftMotorSpeed = strengthToXInputMotor(leftStrength);
		vibration.wRightMotorSpeed = strengthToXInputMotor(rightStrength);
		const DWORD result = XInputSetState(userIndex, &vibration);
		return result == ERROR_SUCCESS;
	}

	FeelKitHapticsDeviceState getDeviceState() const {
		std::lock_guard<std::mutex> lock(mutex);
		return deviceState;
	}

	void setEnabled(bool isEnabled) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			deviceState.isEnabled = isEnabled;

			if (!isEnabled) {
				activeVibrations.clear();
			}
		}

		if (!isEnabled) {
			stop();
		}
	}

	bool isVibrationEnabled() const {
		std::lock_guard<std::mutex> lock(mutex);
		return deviceState.isEnabled;
	}

	bool isReady() const {
		std::lock_guard<std::mutex> lock(mutex);
		return deviceState.isReady;
	}

	void setMasterScale(float masterScale) {
		std::lock_guard<std::mutex> lock(mutex);
		initializeDesc.masterScale = clampFloat(masterScale, 0.0f, 1.0f);
	}

	float getMasterScale() const {
		std::lock_guard<std::mutex> lock(mutex);
		return initializeDesc.masterScale;
	}

	bool registerSoundVibration(const std::wstring& soundPath,
	                            const FeelKitHapticsVibrationDesc& vibrationDesc) {
		if (soundPath.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(mutex);
		soundVibrations[soundPath] = vibrationDesc;
		return true;
	}

	void clearSoundVibrations() {
		std::lock_guard<std::mutex> lock(mutex);
		soundVibrations.clear();
	}

	bool vibrateSound(const std::wstring& soundPath, bool shouldVibrate) {
		if (!shouldVibrate) {
			return true;
		}

		FeelKitHapticsVibrationDesc vibrationDesc = {};

		{
			std::lock_guard<std::mutex> lock(mutex);
			const auto soundIterator = soundVibrations.find(soundPath);

			if (soundIterator != soundVibrations.end()) {
				vibrationDesc = soundIterator->second;
			}
		}

		return playOneShot(vibrationDesc);
	}

	bool playOneShot(const FeelKitHapticsVibrationDesc& vibrationDesc) {
		if (!vibrationDesc.isEnabled) {
			return true;
		}

		const float leftStrength = clampFloat(vibrationDesc.leftStrength, 0.0f, 1.0f);
		const float rightStrength = clampFloat(vibrationDesc.rightStrength, 0.0f, 1.0f);
		const int durationMs = clampInt(vibrationDesc.durationMs, 1, 60000);

		std::lock_guard<std::mutex> lock(mutex);

		if (!deviceState.isInitialized || !deviceState.isEnabled) {
			return false;
		}

		const unsigned long long currentTimeMs = GetTickCount64();
		activeVibrations.push_back({
			leftStrength,
			rightStrength,
			currentTimeMs + static_cast<unsigned long long>(durationMs)
		});

		return true;
	}

	void stop() {
		unsigned int xInputUserIndex = kFeelKitHapticsAnyXInputUser;

		{
			std::lock_guard<std::mutex> lock(mutex);
			activeVibrations.clear();
			xInputUserIndex = deviceState.activeXInputUserIndex;
			lastSentLeftStrength = 0.0f;
			lastSentRightStrength = 0.0f;
		}

		sendXInputMotor(xInputUserIndex, 0.0f, 0.0f);
	}
};

//================================================================
// 公開クラス
//================================================================

FeelKitHaptics::FeelKitHaptics()
	: impl_(std::make_unique<Impl>()) {
}

FeelKitHaptics::~FeelKitHaptics() = default;

FeelKitHaptics::FeelKitHaptics(FeelKitHaptics&&) noexcept = default;

FeelKitHaptics& FeelKitHaptics::operator=(FeelKitHaptics&&) noexcept = default;

bool FeelKitHaptics::initialize(const FeelKitHapticsInitializeDesc& initializeDesc) {
	return impl_->initialize(initializeDesc);
}

void FeelKitHaptics::shutdown() {
	impl_->shutdown();
}

void FeelKitHaptics::update() {
	impl_->update();
}

bool FeelKitHaptics::refreshDevice() {
	return impl_->refreshDevice();
}

FeelKitHapticsDeviceState FeelKitHaptics::getDeviceState() const {
	return impl_->getDeviceState();
}

void FeelKitHaptics::setEnabled(bool isEnabled) {
	impl_->setEnabled(isEnabled);
}

bool FeelKitHaptics::isVibrationEnabled() const {
	return impl_->isVibrationEnabled();
}

bool FeelKitHaptics::isReady() const {
	return impl_->isReady();
}

void FeelKitHaptics::setMasterScale(float masterScale) {
	impl_->setMasterScale(masterScale);
}

float FeelKitHaptics::getMasterScale() const {
	return impl_->getMasterScale();
}

bool FeelKitHaptics::registerSoundVibration(
	const char* soundPath,
	const FeelKitHapticsVibrationDesc& vibrationDesc) {
	return impl_->registerSoundVibration(makeWideTextFromUtf8(soundPath), vibrationDesc);
}

bool FeelKitHaptics::registerSoundVibration(
	const wchar_t* soundPath,
	const FeelKitHapticsVibrationDesc& vibrationDesc) {
	return impl_->registerSoundVibration(soundPath != nullptr ? soundPath : L"", vibrationDesc);
}

void FeelKitHaptics::clearSoundVibrations() {
	impl_->clearSoundVibrations();
}

bool FeelKitHaptics::vibrateSound(const char* soundPath, bool shouldVibrate) {
	return impl_->vibrateSound(makeWideTextFromUtf8(soundPath), shouldVibrate);
}

bool FeelKitHaptics::vibrateSound(const wchar_t* soundPath, bool shouldVibrate) {
	return impl_->vibrateSound(soundPath != nullptr ? soundPath : L"", shouldVibrate);
}

bool FeelKitHaptics::vibrateSound(
	const char* soundPath,
	const FeelKitHapticsVibrationDesc& vibrationDesc) {
	registerSoundVibration(soundPath, vibrationDesc);
	return playOneShot(vibrationDesc);
}

bool FeelKitHaptics::vibrateSound(
	const wchar_t* soundPath,
	const FeelKitHapticsVibrationDesc& vibrationDesc) {
	registerSoundVibration(soundPath, vibrationDesc);
	return playOneShot(vibrationDesc);
}

bool FeelKitHaptics::playOneShot(
	float leftStrength,
	float rightStrength,
	int durationMs,
	bool shouldVibrate) {
	if (!shouldVibrate) {
		return true;
	}

	FeelKitHapticsVibrationDesc vibrationDesc = {};
	vibrationDesc.leftStrength = leftStrength;
	vibrationDesc.rightStrength = rightStrength;
	vibrationDesc.durationMs = durationMs;
	vibrationDesc.isEnabled = true;
	return impl_->playOneShot(vibrationDesc);
}

bool FeelKitHaptics::playOneShot(const FeelKitHapticsVibrationDesc& vibrationDesc) {
	return impl_->playOneShot(vibrationDesc);
}

void FeelKitHaptics::stop() {
	impl_->stop();
}
