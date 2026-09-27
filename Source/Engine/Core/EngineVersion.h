#pragma once

#include <cstdint>
#include <compare>
#include <string>

enum class EngineUpdateChannel : std::uint32_t {
	Stable = 0U,
	Beta,
	Dev,
};

struct EngineVersion {
	std::uint32_t major = 0U;
	std::uint32_t minor = 0U;
	std::uint32_t patch = 0U;
	std::uint32_t build = 0U;

	std::string ToString() const;
	static bool TryParse(const std::string& text, EngineVersion& version);
	auto operator<=>(const EngineVersion&) const = default;
};

EngineVersion GetManoEngineVersion();
EngineUpdateChannel GetManoEngineUpdateChannel();
const char* GetEngineUpdateChannelText(EngineUpdateChannel channel);
bool TryParseEngineUpdateChannel(const std::string& text, EngineUpdateChannel& channel);
std::string GetManoEngineDisplayVersion();
std::uint32_t GetManoProjectFormatVersion();
std::uint32_t GetManoSceneFormatVersion();
std::uint32_t GetManoPrefabFormatVersion();
std::uint32_t GetManoScriptApiVersion();
bool CheckManoEnginePeerCompatibility(
	const std::string& peerEngineVersion,
	std::uint32_t peerProjectFormat,
	std::uint32_t peerScriptApiVersion,
	const std::string& peerChannel,
	std::uint32_t localProjectFormat,
	EngineUpdateChannel localChannel,
	std::string& reason);
