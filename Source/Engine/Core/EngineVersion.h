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

EngineVersion GetCG2EngineVersion();
EngineUpdateChannel GetCG2EngineUpdateChannel();
const char* GetEngineUpdateChannelText(EngineUpdateChannel channel);
bool TryParseEngineUpdateChannel(const std::string& text, EngineUpdateChannel& channel);
std::string GetCG2EngineDisplayVersion();
std::uint32_t GetCG2ProjectFormatVersion();
std::uint32_t GetCG2SceneFormatVersion();
std::uint32_t GetCG2PrefabFormatVersion();
std::uint32_t GetCG2ScriptApiVersion();
bool CheckCG2EnginePeerCompatibility(
	const std::string& peerEngineVersion,
	std::uint32_t peerProjectFormat,
	std::uint32_t peerScriptApiVersion,
	const std::string& peerChannel,
	std::uint32_t localProjectFormat,
	EngineUpdateChannel localChannel,
	std::string& reason);
