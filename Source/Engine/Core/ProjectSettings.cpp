#include "ProjectSettings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
	constexpr const char* kProjectSettingsPath = "ProjectSettings/ProjectSettings.cg2";
	constexpr const char* kHeaderLine = "CG2EngineProjectSettings|1";

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
}

ProjectSettings& ProjectSettings::Get() {
	static ProjectSettings instance;
	return instance;
}

void ProjectSettings::Load() {
	if (isLoaded_) {
		return;
	}

	isLoaded_ = true;
	std::ifstream file(kProjectSettingsPath, std::ios::binary);

	if (!file.is_open()) {
		return;
	}

	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (isFirstLine) {
			isFirstLine = false;

			// UTF-8 BOM を取り除いてからヘッダーを判定する。
			if (line.size() >= 3u &&
				static_cast<unsigned char>(line[0]) == 0xEFu &&
				static_cast<unsigned char>(line[1]) == 0xBBu &&
				static_cast<unsigned char>(line[2]) == 0xBFu) {
				line.erase(0, 3);
			}

			if (line.rfind("CG2EngineProjectSettings", 0) == 0) {
				continue;
			}
		}

		const std::size_t separatorPosition = line.find('|');

		if (separatorPosition == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0u, separatorPosition);
		const std::string value = line.substr(separatorPosition + 1u);

		if (key == "GameWidth") {
			data_.gameWidth = (std::clamp)(ToInt(value, data_.gameWidth), 320, 7680);
		}
		else if (key == "GameHeight") {
			data_.gameHeight = (std::clamp)(ToInt(value, data_.gameHeight), 240, 4320);
		}
		else if (key == "WindowMode") {
			data_.windowMode = ToInt(value, 0) == 1
				? ProjectWindowMode::BorderlessFullscreen
				: ProjectWindowMode::Windowed;
		}
		else if (key == "VSync") {
			data_.vsyncEnabled = ToInt(value, 1) != 0;
		}
		else if (key == "MaxScenePhysicsDebris") {
			data_.maxScenePhysicsDebris = (std::clamp)(ToInt(value, data_.maxScenePhysicsDebris), 0, 100000);
		}
		else if (key == "FrameRateLimit") {
			data_.frameRateLimit = (std::clamp)(ToInt(value, data_.frameRateLimit), 0, 1000);
		}
		else if (key == "MasterVolume") {
			data_.masterVolume = (std::clamp)(ToFloat(value, data_.masterVolume), 0.0f, 1.0f);
		}
		else if (key == "SfxVolume") {
			data_.sfxVolume = (std::clamp)(ToFloat(value, data_.sfxVolume), 0.0f, 1.0f);
		}
		else if (key == "BgmVolume") {
			data_.bgmVolume = (std::clamp)(ToFloat(value, data_.bgmVolume), 0.0f, 1.0f);
		}
		else if (key == "AmbienceVolume") {
			data_.ambienceVolume = (std::clamp)(ToFloat(value, data_.ambienceVolume), 0.0f, 1.0f);
		}
		else if (key == "UiVolume") {
			data_.uiVolume = (std::clamp)(ToFloat(value, data_.uiVolume), 0.0f, 1.0f);
		}
		else if (key == "VoiceVolume") {
			data_.voiceVolume = (std::clamp)(ToFloat(value, data_.voiceVolume), 0.0f, 1.0f);
		}
		else if (key == "GamepadStickDeadZone") {
			data_.gamepadStickDeadZone = (std::clamp)(ToFloat(value, data_.gamepadStickDeadZone), 0.0f, 0.9f);
		}
		else if (key == "GamepadTriggerThreshold") {
			data_.gamepadTriggerThreshold = (std::clamp)(ToFloat(value, data_.gamepadTriggerThreshold), 0.0f, 1.0f);
		}
		else if (key == "GamepadLookSensitivity") {
			data_.gamepadLookSensitivity = (std::clamp)(ToFloat(value, data_.gamepadLookSensitivity), 0.05f, 10.0f);
		}
		else if (key == "OnlineServicesEnabled") {
			data_.onlineServicesEnabled = ToInt(value, 0) != 0;
		}
		else if (key == "OnlineProviderName") {
			data_.onlineProviderName = value;
		}
		else if (key == "OnlineApiBaseUrl") {
			data_.onlineApiBaseUrl = value;
		}
		else if (key == "OnlineDevelopmentBaseUrl") {
			data_.onlineDevelopmentBaseUrl = value;
		}
		else if (key == "OnlineGameId") {
			data_.onlineGameId = value;
		}
		else if (key == "OnlineClientKey") {
			data_.onlineClientKey = value;
		}
		else if (key == "OnlineEnvironment") {
			data_.onlineEnvironment = (std::clamp)(ToInt(value, data_.onlineEnvironment), 0, 1);
		}
		else if (key == "OnlineTimeoutSeconds") {
			data_.onlineTimeoutSeconds = (std::clamp)(ToInt(value, data_.onlineTimeoutSeconds), 1, 60);
		}
		else if (key == "OnlineMaximumPendingRequests") {
			data_.onlineMaximumPendingRequests =
				(std::clamp)(ToInt(value, data_.onlineMaximumPendingRequests), 1, 1024);
		}
		else if (key == "HapticMasterIntensity") {
			data_.hapticMasterIntensity = (std::clamp)(ToFloat(value, data_.hapticMasterIntensity), 0.0f, 1.0f);
		}
	}
}

void ProjectSettings::Save() const {
	const std::filesystem::path filePath(kProjectSettingsPath);
	std::error_code directoryError;
	std::filesystem::create_directories(filePath.parent_path(), directoryError);

	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		return;
	}

	// 既存の ProjectSettings 系テキストと同じく UTF-8 BOM + CRLF で書き出す。
	file.write("\xEF\xBB\xBF", 3);
	file << kHeaderLine << "\r\n";
	file << "GameWidth|" << data_.gameWidth << "\r\n";
	file << "GameHeight|" << data_.gameHeight << "\r\n";
	file << "WindowMode|" << static_cast<int32_t>(data_.windowMode) << "\r\n";
	file << "VSync|" << (data_.vsyncEnabled ? 1 : 0) << "\r\n";
	file << "FrameRateLimit|" << data_.frameRateLimit << "\r\n";
	file << "MaxScenePhysicsDebris|" << data_.maxScenePhysicsDebris << "\r\n";
	file << "MasterVolume|" << data_.masterVolume << "\r\n";
	file << "SfxVolume|" << data_.sfxVolume << "\r\n";
	file << "BgmVolume|" << data_.bgmVolume << "\r\n";
	file << "AmbienceVolume|" << data_.ambienceVolume << "\r\n";
	file << "UiVolume|" << data_.uiVolume << "\r\n";
	file << "VoiceVolume|" << data_.voiceVolume << "\r\n";
	file << "GamepadStickDeadZone|" << data_.gamepadStickDeadZone << "\r\n";
	file << "GamepadTriggerThreshold|" << data_.gamepadTriggerThreshold << "\r\n";
	file << "GamepadLookSensitivity|" << data_.gamepadLookSensitivity << "\r\n";
	file << "OnlineServicesEnabled|" << (data_.onlineServicesEnabled ? 1 : 0) << "\r\n";
	file << "OnlineProviderName|" << data_.onlineProviderName << "\r\n";
	file << "OnlineApiBaseUrl|" << data_.onlineApiBaseUrl << "\r\n";
	file << "OnlineDevelopmentBaseUrl|" << data_.onlineDevelopmentBaseUrl << "\r\n";
	file << "OnlineGameId|" << data_.onlineGameId << "\r\n";
	file << "OnlineClientKey|" << data_.onlineClientKey << "\r\n";
	file << "OnlineEnvironment|" << data_.onlineEnvironment << "\r\n";
	file << "OnlineTimeoutSeconds|" << data_.onlineTimeoutSeconds << "\r\n";
	file << "OnlineMaximumPendingRequests|" << data_.onlineMaximumPendingRequests << "\r\n";
	file << "HapticMasterIntensity|" << data_.hapticMasterIntensity << "\r\n";
}

ProjectSettingsData& ProjectSettings::GetMutableData() {
	Load();
	return data_;
}

const ProjectSettingsData& ProjectSettings::GetData() const {
	const_cast<ProjectSettings*>(this)->Load();
	return data_;
}
