#include "HapticSystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
	constexpr float kDeviceRefreshInterval = 0.08f;  // 同じ強度でも Device へ出し直す間隔(秒)。
	constexpr float kDeviceLevelEpsilon = 0.02f;     // この差未満の変化は Device へ送らない。
	constexpr const char* kHapticClipHeader = "ManoEngineHapticClip|1";

	float ToNormalized01(float value) {
		return (std::clamp)(value, 0.0f, 1.0f);
	}

	int32_t ToInt(const std::string& text, int32_t fallbackValue) {
		try {
			return text.empty() ? fallbackValue : std::stoi(text);
		}
		catch (...) {
			return fallbackValue;
		}
	}

	float ToFloat(const std::string& text, float fallbackValue) {
		try {
			return text.empty() ? fallbackValue : std::stof(text);
		}
		catch (...) {
			return fallbackValue;
		}
	}

	HapticPattern ToPattern(int32_t value) {
		const int32_t clampedValue = (std::clamp)(value, 0, 4);
		return static_cast<HapticPattern>(clampedValue);
	}

	HapticChannel ToChannel(int32_t value) {
		const int32_t clampedValue = (std::clamp)(value, 0, 2);
		return static_cast<HapticChannel>(clampedValue);
	}
}

const char* ToDisplayString(HapticPattern pattern) {
	switch (pattern) {
	case HapticPattern::Constant:
		return "一定";
	case HapticPattern::Pulse:
		return "パルス";
	case HapticPattern::RampUp:
		return "立ち上がり";
	case HapticPattern::RampDown:
		return "減衰";
	case HapticPattern::Burst:
		return "衝撃";
	default:
		return "一定";
	}
}

const char* ToDisplayString(HapticChannel channel) {
	switch (channel) {
	case HapticChannel::Both:
		return "両方";
	case HapticChannel::Left:
		return "左";
	case HapticChannel::Right:
		return "右";
	default:
		return "両方";
	}
}

const char* ToDisplayString(HapticDeviceState state) {
	switch (state) {
	case HapticDeviceState::Unavailable:
		return "利用不可";
	case HapticDeviceState::Disconnected:
		return "未接続";
	case HapticDeviceState::Connected:
		return "接続";
	case HapticDeviceState::Error:
		return "エラー";
	default:
		return "利用不可";
	}
}

HapticSystem& HapticSystem::Get() {
	static HapticSystem instance;
	return instance;
}

void HapticSystem::SetBackend(std::unique_ptr<IHapticBackend> backend) {
	if (isInitialized_) {
		Shutdown();
	}

	backend_ = std::move(backend);
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
	lastError_.Clear();
}

bool HapticSystem::Initialize() {
	if (backend_ == nullptr) {
		state_ = ExternalFeatureState::Unavailable;
		lastError_.code = 1;
		lastError_.message = "Haptic Backend が設定されていません。";
		return false;
	}

	if (isInitialized_) {
		return true;
	}

	if (!backend_->Initialize()) {
		lastError_ = backend_->GetLastError();

		if (!lastError_.HasError()) {
			lastError_.code = 2;
			lastError_.message = "Haptic Backend の初期化に失敗しました。";
		}

		state_ = ExternalFeatureState::Error;
		ExternalFeatureLog::Error(ExternalFeatureCategory::Haptics, lastError_);
		return false;
	}

	backend_->SetIntensity(masterIntensity_);
	isInitialized_ = true;
	state_ = ExternalFeatureState::Ready;
	lastError_.Clear();

	const HapticDeviceInfo deviceInfo = backend_->GetDeviceInfo();
	ExternalFeatureLog::Info(
		ExternalFeatureCategory::Haptics,
		std::string("Backend 初期化: ") + deviceInfo.backendName + " / Device: " + deviceInfo.deviceName);
	return true;
}

void HapticSystem::Shutdown() {
	StopAll();

	if (backend_ != nullptr && isInitialized_) {
		backend_->Shutdown();
	}

	isInitialized_ = false;
	currentOutputIntensity_ = 0.0f;
	state_ = backend_ == nullptr ? ExternalFeatureState::Unavailable : ExternalFeatureState::Ready;
}

void HapticSystem::Update(float deltaTime) {
	if (backend_ == nullptr || !isInitialized_) {
		return;
	}

	const float advanceSeconds = (std::max)(deltaTime, 0.0f);
	backend_->Update(advanceSeconds);

	float leftLevel = 0.0f;
	float rightLevel = 0.0f;

	for (Voice& voice : voices_) {
		if (voice.isFinished) {
			continue;
		}

		voice.elapsedSeconds += advanceSeconds * (std::max)(voice.data.playbackSpeed, 0.0f);

		const float durationSeconds = (std::max)(voice.data.durationSeconds, 0.001f);

		if (!voice.data.isLooping && voice.elapsedSeconds >= durationSeconds) {
			voice.isFinished = true;
			continue;
		}

		const float level = EvaluateVoiceLevel(voice);

		if (voice.data.channel != HapticChannel::Right) {
			leftLevel = (std::max)(leftLevel, level);
		}

		if (voice.data.channel != HapticChannel::Left) {
			rightLevel = (std::max)(rightLevel, level);
		}
	}

	voices_.erase(
		std::remove_if(
			voices_.begin(),
			voices_.end(),
			[](const Voice& voice) { return voice.isFinished; }),
		voices_.end());

	state_ = voices_.empty() ? ExternalFeatureState::Ready : ExternalFeatureState::Running;
	SendToDevice(leftLevel, rightLevel, advanceSeconds);
}

float HapticSystem::EvaluateVoiceLevel(const Voice& voice) const {
	const float durationSeconds = (std::max)(voice.data.durationSeconds, 0.001f);
	const float loopedElapsed = voice.data.isLooping
		? std::fmod(voice.elapsedSeconds, durationSeconds)
		: voice.elapsedSeconds;
	const float normalizedTime = (std::clamp)(loopedElapsed / durationSeconds, 0.0f, 1.0f);
	const float intensity = ToNormalized01(voice.data.intensity);
	const float frequency = voice.data.frequency > 0.0f ? voice.data.frequency : 12.0f;

	switch (voice.data.pattern) {
	case HapticPattern::Constant:
		return intensity;
	case HapticPattern::Pulse: {
		// 1 周期の前半だけ振動させる矩形波。
		const float phase = std::fmod(loopedElapsed * frequency, 1.0f);
		return phase < 0.5f ? intensity : 0.0f;
	}
	case HapticPattern::RampUp:
		return intensity * normalizedTime;
	case HapticPattern::RampDown:
		return intensity * (1.0f - normalizedTime);
	case HapticPattern::Burst: {
		// 立ち上がり最大から指数減衰させ、着弾・衝突の手触りへ寄せる。
		const float decay = std::exp(-6.0f * normalizedTime);
		return intensity * decay;
	}
	default:
		return intensity;
	}
}

void HapticSystem::SendToDevice(float leftLevel, float rightLevel, float deltaTime) {
	const float mixedLevel = ToNormalized01((std::max)(leftLevel, rightLevel));
	deviceRefreshTimer_ -= deltaTime;

	const bool hasLevelChanged = std::fabs(mixedLevel - currentOutputIntensity_) > kDeviceLevelEpsilon;
	const bool shouldRefresh = mixedLevel > 0.0f && deviceRefreshTimer_ <= 0.0f;

	if (!hasLevelChanged && !shouldRefresh) {
		return;
	}

	currentOutputIntensity_ = mixedLevel;
	deviceRefreshTimer_ = kDeviceRefreshInterval;

	if (mixedLevel <= 0.0f) {
		backend_->Stop();
		return;
	}

	HapticData deviceData{};
	deviceData.intensity = mixedLevel;
	deviceData.durationSeconds = kDeviceRefreshInterval * 2.0f;  // 次の送信まで途切れない長さにする。
	deviceData.pattern = HapticPattern::Constant;

	// FeelKit 系 Device は左右 2 Motor しか無いため、片側だけが鳴っている時は
	// その側の Channel として出し、それ以外は両側へ同じ強度を出す。
	if (leftLevel > 0.0f && rightLevel <= 0.0f) {
		deviceData.channel = HapticChannel::Left;
	}
	else if (rightLevel > 0.0f && leftLevel <= 0.0f) {
		deviceData.channel = HapticChannel::Right;
	}
	else {
		deviceData.channel = HapticChannel::Both;
	}

	backend_->Play(deviceData);
}

ExternalFeatureState HapticSystem::GetState() const {
	return state_;
}

ExternalFeatureError HapticSystem::GetLastError() const {
	return lastError_;
}

HapticDeviceInfo HapticSystem::GetDeviceInfo() const {
	if (backend_ == nullptr) {
		HapticDeviceInfo deviceInfo{};
		deviceInfo.state = HapticDeviceState::Unavailable;
		deviceInfo.backendName = "なし";
		deviceInfo.deviceName = "なし";
		return deviceInfo;
	}

	return backend_->GetDeviceInfo();
}

bool HapticSystem::RefreshDevice() {
	return backend_ != nullptr && backend_->RefreshDevice();
}

const char* HapticSystem::GetBackendName() const {
	return backend_ == nullptr ? "なし" : "FeelKit";
}

HapticHandle HapticSystem::Play(
	const HapticData& data,
	int32_t ownerGameObjectId,
	const std::string& displayName) {
	if (backend_ == nullptr || !isInitialized_) {
		// Device が無くてもゲームロジックは止めない。無効 Handle を返すだけにする。
		return kInvalidHapticHandle;
	}

	Voice voice{};
	voice.handle = nextHandle_;
	voice.data = data;
	voice.data.intensity = ToNormalized01(data.intensity);
	voice.data.durationSeconds = (std::max)(data.durationSeconds, 0.001f);
	voice.data.playbackSpeed = (std::clamp)(data.playbackSpeed, 0.0f, 8.0f);
	voice.displayName = displayName;
	voice.ownerGameObjectId = ownerGameObjectId;
	voices_.push_back(voice);

	nextHandle_ = nextHandle_ + 1u == kInvalidHapticHandle ? 1u : nextHandle_ + 1u;
	state_ = ExternalFeatureState::Running;
	return voice.handle;
}

HapticHandle HapticSystem::PlayClip(const HapticClipData& clip, int32_t ownerGameObjectId) {
	const std::string displayName = clip.name.empty() ? clip.assetPath : clip.name;
	return Play(clip.ToHapticData(), ownerGameObjectId, displayName);
}

HapticHandle HapticSystem::PlayClipAsset(const std::string& clipAssetPath, int32_t ownerGameObjectId) {
	HapticClipData clip{};

	if (!LoadClip(clipAssetPath, clip)) {
		return kInvalidHapticHandle;
	}

	return PlayClip(clip, ownerGameObjectId);
}

HapticHandle HapticSystem::PlayFromAudioFile(
	const std::string& audioFilePath,
	const HapticData& data,
	int32_t ownerGameObjectId) {
	if (backend_ == nullptr || !isInitialized_) {
		return kInvalidHapticHandle;
	}

	// Backend が音源から直接振動を作れる場合はそちらを使い、合成 Voice も同時に持たせない。
	if (backend_->TryPlayFromAudioFile(audioFilePath, data)) {
		return kInvalidHapticHandle;
	}

	return Play(data, ownerGameObjectId, audioFilePath);
}

bool HapticSystem::Stop(HapticHandle handle) {
	Voice* voice = FindVoice(handle);

	if (voice == nullptr) {
		return false;
	}

	voice->isFinished = true;
	return true;
}

void HapticSystem::StopGameObject(int32_t ownerGameObjectId) {
	for (Voice& voice : voices_) {
		if (voice.ownerGameObjectId == ownerGameObjectId) {
			voice.isFinished = true;
		}
	}
}

void HapticSystem::StopAll() {
	voices_.clear();
	currentOutputIntensity_ = 0.0f;

	if (backend_ != nullptr && isInitialized_) {
		backend_->Stop();
	}

	if (state_ == ExternalFeatureState::Running) {
		state_ = ExternalFeatureState::Ready;
	}
}

bool HapticSystem::IsPlaying(HapticHandle handle) const {
	const Voice* voice = FindVoice(handle);
	return voice != nullptr && !voice->isFinished;
}

bool HapticSystem::SetIntensity(HapticHandle handle, float intensity) {
	Voice* voice = FindVoice(handle);

	if (voice == nullptr) {
		return false;
	}

	voice->data.intensity = ToNormalized01(intensity);
	return true;
}

bool HapticSystem::SetFrequency(HapticHandle handle, float frequency) {
	Voice* voice = FindVoice(handle);

	if (voice == nullptr) {
		return false;
	}

	voice->data.frequency = (std::clamp)(frequency, 0.0f, 200.0f);
	return true;
}

bool HapticSystem::SetPlaybackSpeed(HapticHandle handle, float playbackSpeed) {
	Voice* voice = FindVoice(handle);

	if (voice == nullptr) {
		return false;
	}

	voice->data.playbackSpeed = (std::clamp)(playbackSpeed, 0.0f, 8.0f);
	return true;
}

bool HapticSystem::SetLooping(HapticHandle handle, bool isLooping) {
	Voice* voice = FindVoice(handle);

	if (voice == nullptr) {
		return false;
	}

	voice->data.isLooping = isLooping;
	return true;
}

void HapticSystem::SetMasterIntensity(float masterIntensity) {
	masterIntensity_ = ToNormalized01(masterIntensity);

	if (backend_ != nullptr) {
		backend_->SetIntensity(masterIntensity_);
	}
}

float HapticSystem::GetMasterIntensity() const {
	return masterIntensity_;
}

HapticSystem::Voice* HapticSystem::FindVoice(HapticHandle handle) {
	if (handle == kInvalidHapticHandle) {
		return nullptr;
	}

	for (Voice& voice : voices_) {
		if (voice.handle == handle) {
			return &voice;
		}
	}

	return nullptr;
}

const HapticSystem::Voice* HapticSystem::FindVoice(HapticHandle handle) const {
	if (handle == kInvalidHapticHandle) {
		return nullptr;
	}

	for (const Voice& voice : voices_) {
		if (voice.handle == handle) {
			return &voice;
		}
	}

	return nullptr;
}

bool HapticSystem::LoadClip(const std::string& clipAssetPath, HapticClipData& outClip) {
	if (clipAssetPath.empty()) {
		return false;
	}

	const auto cachedIt = clipCache_.find(clipAssetPath);

	if (cachedIt != clipCache_.end()) {
		outClip = cachedIt->second;
		return true;
	}

	std::ifstream file(clipAssetPath, std::ios::binary);

	if (!file.is_open()) {
		ExternalFeatureLog::Warning(
			ExternalFeatureCategory::Haptics,
			"Haptic Clip を開けません: " + clipAssetPath);
		return false;
	}

	HapticClipData clip{};
	clip.assetPath = clipAssetPath;
	clip.name = std::filesystem::path(clipAssetPath).stem().string();

	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (isFirstLine) {
			isFirstLine = false;

			if (line.size() >= 3u &&
				static_cast<unsigned char>(line[0]) == 0xEFu &&
				static_cast<unsigned char>(line[1]) == 0xBBu &&
				static_cast<unsigned char>(line[2]) == 0xBFu) {
				line.erase(0, 3);
			}

			if (line.rfind("ManoEngineHapticClip", 0) == 0) {
				continue;
			}
		}

		const std::size_t separatorPosition = line.find('|');

		if (separatorPosition == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0u, separatorPosition);
		const std::string value = line.substr(separatorPosition + 1u);

		if (key == "Name") {
			clip.name = value;
		}
		else if (key == "Duration") {
			clip.durationSeconds = (std::clamp)(ToFloat(value, clip.durationSeconds), 0.01f, 10.0f);
		}
		else if (key == "Intensity") {
			clip.intensity = ToNormalized01(ToFloat(value, clip.intensity));
		}
		else if (key == "Frequency") {
			clip.frequency = (std::clamp)(ToFloat(value, clip.frequency), 0.0f, 200.0f);
		}
		else if (key == "Pattern") {
			clip.pattern = ToPattern(ToInt(value, 0));
		}
		else if (key == "Loop") {
			clip.isLooping = ToInt(value, 0) != 0;
		}
		else if (key == "Channel") {
			clip.channel = ToChannel(ToInt(value, 0));
		}
	}

	clipCache_[clipAssetPath] = clip;
	outClip = clip;
	return true;
}

bool HapticSystem::SaveClip(const std::string& clipAssetPath, const HapticClipData& clip) {
	if (clipAssetPath.empty()) {
		return false;
	}

	const std::filesystem::path filePath(clipAssetPath);
	std::error_code directoryError;

	if (filePath.has_parent_path()) {
		std::filesystem::create_directories(filePath.parent_path(), directoryError);
	}

	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		ExternalFeatureLog::Error(
			ExternalFeatureCategory::Haptics,
			"Haptic Clip を保存できません: " + clipAssetPath);
		return false;
	}

	// 他の ManoEngine テキスト Asset と同じく UTF-8 BOM + CRLF で書き出す。
	file.write("\xEF\xBB\xBF", 3);
	file << kHapticClipHeader << "\r\n";
	file << "Name|" << clip.name << "\r\n";
	file << "Duration|" << clip.durationSeconds << "\r\n";
	file << "Intensity|" << clip.intensity << "\r\n";
	file << "Frequency|" << clip.frequency << "\r\n";
	file << "Pattern|" << static_cast<int32_t>(clip.pattern) << "\r\n";
	file << "Loop|" << (clip.isLooping ? 1 : 0) << "\r\n";
	file << "Channel|" << static_cast<int32_t>(clip.channel) << "\r\n";

	HapticClipData savedClip = clip;
	savedClip.assetPath = clipAssetPath;
	clipCache_[clipAssetPath] = savedClip;
	return true;
}

void HapticSystem::ClearClipCache() {
	clipCache_.clear();
}

bool HapticSystem::TryAnalyzeAudioFile(
	const std::string& audioFilePath,
	HapticAudioAnalysis& outAnalysis) {
	return backend_ != nullptr && backend_->TryAnalyzeAudioFile(audioFilePath, outAnalysis);
}

float HapticSystem::MakeIntensityFromAudio(
	const HapticAudioAnalysis& analysis,
	int32_t frequencyRange,
	float sensitivity,
	float intensityScale) {
	if (!analysis.isValid) {
		return 0.0f;
	}

	float bandEnergy = analysis.averageAmplitude;

	if (frequencyRange == 0) {
		bandEnergy = analysis.lowBandEnergy;
	}
	else if (frequencyRange == 2) {
		bandEnergy = analysis.highBandEnergy;
	}

	const float attackBoost = analysis.attackStrength * 0.5f;
	const float rawIntensity = (bandEnergy + attackBoost) * (std::max)(sensitivity, 0.0f);
	return ToNormalized01(rawIntensity * (std::max)(intensityScale, 0.0f));
}

float HapticSystem::MakeIntensityFromImpulse(float impulse, float maximumImpulse) {
	const float safeMaximum = (std::max)(maximumImpulse, 0.0001f);
	return (std::clamp)(impulse / safeMaximum, 0.0f, 1.0f);
}

float HapticSystem::GetCurrentOutputIntensity() const {
	return currentOutputIntensity_;
}

void HapticSystem::GetPlaybackStatus(std::vector<HapticPlaybackStatus>& outStatus) const {
	outStatus.clear();
	outStatus.reserve(voices_.size());

	for (const Voice& voice : voices_) {
		HapticPlaybackStatus status{};
		status.handle = voice.handle;
		status.clipName = voice.displayName.empty() ? "(手動再生)" : voice.displayName;
		status.intensity = EvaluateVoiceLevel(voice);
		status.frequency = voice.data.frequency;
		status.isLooping = voice.data.isLooping;
		status.ownerGameObjectId = voice.ownerGameObjectId;
		status.remainingSeconds = voice.data.isLooping
			? -1.0f
			: (std::max)(voice.data.durationSeconds - voice.elapsedSeconds, 0.0f);
		outStatus.push_back(status);
	}
}

int32_t HapticSystem::GetActiveVoiceCount() const {
	return static_cast<int32_t>(voices_.size());
}
