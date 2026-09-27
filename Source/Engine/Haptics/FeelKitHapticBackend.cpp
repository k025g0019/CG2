#include "FeelKitHapticBackend.h"

#pragma warning(push, 0)
#include "FeelKitHaptics.h"
#include "audio/FeelKitAudio.h"
#pragma warning(pop)

#include <algorithm>

namespace {
	constexpr int32_t kMinimumDurationMs = 8;  // FeelKit へ 0ms を渡さないための下限。

	int32_t ToDurationMs(float durationSeconds) {
		const float clampedSeconds = (std::clamp)(durationSeconds, 0.0f, 10.0f);
		const int32_t durationMs = static_cast<int32_t>(clampedSeconds * 1000.0f);
		return (std::max)(durationMs, kMinimumDurationMs);
	}
}

FeelKitHapticBackend::FeelKitHapticBackend(FeelKitHaptics* haptics)
	: haptics_(haptics) {
}

bool FeelKitHapticBackend::Initialize() {
	lastError_.Clear();

	if (haptics_ == nullptr) {
		lastError_.code = 1;
		lastError_.message = "FeelKitHaptics の実体が渡されていません。";
		return false;
	}

	// Editor / Player 側が既に initialize 済みの場合は二重初期化しない。
	const FeelKitHapticsDeviceState deviceState = haptics_->getDeviceState();

	if (!deviceState.isInitialized) {
		FeelKitHapticsInitializeDesc initializeDesc{};
		initializeDesc.preferredDevice = FeelKitHapticsDeviceKind::autoSelect;
		initializeDesc.isEnabled = true;

		if (!haptics_->initialize(initializeDesc)) {
			lastError_.code = 2;
			lastError_.message = "FeelKitHaptics の初期化に失敗しました。";
			return false;
		}
	}

	haptics_->setEnabled(true);
	haptics_->setMasterScale(masterIntensity_);
	isInitialized_ = true;

	// Device が繋がっていなくても Backend としては使える扱いにする。
	// 実際の接続状態は GetDeviceInfo が返し、再生は黙って捨てられる。
	return true;
}

void FeelKitHapticBackend::Shutdown() {
	if (haptics_ != nullptr && isInitialized_) {
		haptics_->stop();
	}

	// FeelKitHaptics 自体の shutdown は所有者(Platform 側)が行う。
	isInitialized_ = false;
}

void FeelKitHapticBackend::Update(float deltaTime) {
	static_cast<void>(deltaTime);

	// FeelKitHaptics::update は Editor の Frame Input 側で毎フレーム呼ばれている。
	// ここで二重に呼ぶと Worker への指示が重複するため、状態確認だけを行う。
}

void FeelKitHapticBackend::Play(const HapticData& data) {
	if (haptics_ == nullptr || !isInitialized_) {
		return;
	}

	const float intensity = (std::clamp)(data.intensity, 0.0f, 1.0f);

	FeelKitHapticsVibrationDesc vibrationDesc{};
	vibrationDesc.leftStrength = data.channel == HapticChannel::Right ? 0.0f : intensity;
	vibrationDesc.rightStrength = data.channel == HapticChannel::Left ? 0.0f : intensity;
	vibrationDesc.durationMs = ToDurationMs(data.durationSeconds);
	vibrationDesc.isEnabled = true;

	haptics_->playOneShot(vibrationDesc);
}

void FeelKitHapticBackend::Stop() {
	if (haptics_ != nullptr) {
		haptics_->stop();
	}
}

void FeelKitHapticBackend::SetIntensity(float value) {
	masterIntensity_ = (std::clamp)(value, 0.0f, 1.0f);

	if (haptics_ != nullptr) {
		haptics_->setMasterScale(masterIntensity_);
	}
}

HapticDeviceInfo FeelKitHapticBackend::GetDeviceInfo() const {
	HapticDeviceInfo deviceInfo{};
	deviceInfo.backendName = "FeelKit";

	if (haptics_ == nullptr) {
		deviceInfo.state = HapticDeviceState::Unavailable;
		deviceInfo.deviceName = "なし";
		return deviceInfo;
	}

	const FeelKitHapticsDeviceState deviceState = haptics_->getDeviceState();

	if (!deviceState.isInitialized) {
		deviceInfo.state = HapticDeviceState::Unavailable;
		deviceInfo.deviceName = "未初期化";
		return deviceInfo;
	}

	switch (deviceState.activeDevice) {
	case FeelKitHapticsDeviceKind::xInput:
		deviceInfo.deviceName = "XInput Gamepad";
		break;
	case FeelKitHapticsDeviceKind::switch2:
		deviceInfo.deviceName = "Switch2 Controller";
		break;
	case FeelKitHapticsDeviceKind::autoSelect:
		deviceInfo.deviceName = "自動選択";
		break;
	default:
		deviceInfo.deviceName = "なし";
		break;
	}

	if (lastError_.HasError()) {
		deviceInfo.state = HapticDeviceState::Error;
	}
	else if (deviceState.isReady) {
		deviceInfo.state = HapticDeviceState::Connected;
	}
	else {
		deviceInfo.state = HapticDeviceState::Disconnected;
	}

	return deviceInfo;
}

bool FeelKitHapticBackend::RefreshDevice() {
	if (haptics_ == nullptr) {
		return false;
	}

	return haptics_->refreshDevice();
}

ExternalFeatureError FeelKitHapticBackend::GetLastError() const {
	return lastError_;
}

bool FeelKitHapticBackend::TryAnalyzeAudioFile(
	const std::string& audioFilePath,
	HapticAudioAnalysis& outAnalysis) {
	if (audioFilePath.empty()) {
		return false;
	}

	const AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures(audioFilePath.c_str());

	if (!feature.isValid) {
		return false;
	}

	outAnalysis.isValid = true;
	outAnalysis.durationSeconds = feature.durationSeconds;
	outAnalysis.averageAmplitude = feature.averageAmplitude;
	outAnalysis.peakAmplitude = feature.peakAmplitude;
	outAnalysis.lowBandEnergy = feature.lowBandEnergy;
	outAnalysis.highBandEnergy = feature.highBandEnergy;
	outAnalysis.attackStrength = feature.attackStrength;
	return true;
}

bool FeelKitHapticBackend::TryPlayFromAudioFile(
	const std::string& audioFilePath,
	const HapticData& data) {
	if (haptics_ == nullptr || !isInitialized_ || audioFilePath.empty()) {
		return false;
	}

	const float intensity = (std::clamp)(data.intensity, 0.0f, 1.0f);

	FeelKitHapticsVibrationDesc vibrationDesc{};
	vibrationDesc.leftStrength = data.channel == HapticChannel::Right ? 0.0f : intensity;
	vibrationDesc.rightStrength = data.channel == HapticChannel::Left ? 0.0f : intensity;
	vibrationDesc.durationMs = ToDurationMs(data.durationSeconds);
	vibrationDesc.isEnabled = true;

	return haptics_->vibrateSound(audioFilePath.c_str(), vibrationDesc);
}
