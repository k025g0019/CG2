#include "EngineVersion.h"

#include "EngineVersion.generated.h"
#include "EditorScriptApi.h"

#include <charconv>
#include <vector>

namespace {
	bool TryParsePart(const std::string& text, std::uint32_t& value) {
		if (text.empty()) return false;
		const char* begin = text.data();
		const char* end = begin + text.size();
		const std::from_chars_result result = std::from_chars(begin, end, value);
		return result.ec == std::errc{} && result.ptr == end;
	}
}

std::string EngineVersion::ToString() const {
	return std::to_string(major) + "." + std::to_string(minor) + "." +
		std::to_string(patch) + "+" + std::to_string(build);
}

bool EngineVersion::TryParse(const std::string& text, EngineVersion& version) {
	const std::size_t firstDot = text.find('.');
	const std::size_t secondDot = firstDot == std::string::npos
		? std::string::npos : text.find('.', firstDot + 1U);
	const std::size_t plus = secondDot == std::string::npos
		? std::string::npos : text.find('+', secondDot + 1U);
	if (firstDot == std::string::npos || secondDot == std::string::npos) return false;

	EngineVersion parsed{};
	if (!TryParsePart(text.substr(0U, firstDot), parsed.major) ||
		!TryParsePart(text.substr(firstDot + 1U, secondDot - firstDot - 1U), parsed.minor)) return false;
	const std::string patchText = plus == std::string::npos
		? text.substr(secondDot + 1U) : text.substr(secondDot + 1U, plus - secondDot - 1U);
	if (!TryParsePart(patchText, parsed.patch)) return false;
	if (plus != std::string::npos && !TryParsePart(text.substr(plus + 1U), parsed.build)) return false;
	version = parsed;
	return true;
}

EngineVersion GetManoEngineVersion() {
	return {ManoEngineVersionGenerated::kMajor, ManoEngineVersionGenerated::kMinor,
		ManoEngineVersionGenerated::kPatch, ManoEngineVersionGenerated::kBuild};
}

EngineUpdateChannel GetManoEngineUpdateChannel() {
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	TryParseEngineUpdateChannel(ManoEngineVersionGenerated::kChannel, channel);
	return channel;
}

const char* GetEngineUpdateChannelText(EngineUpdateChannel channel) {
	switch (channel) {
	case EngineUpdateChannel::Beta: return "Beta";
	case EngineUpdateChannel::Dev: return "Dev";
	default: return "Stable";
	}
}

bool TryParseEngineUpdateChannel(const std::string& text, EngineUpdateChannel& channel) {
	if (text == "Stable") channel = EngineUpdateChannel::Stable;
	else if (text == "Beta") channel = EngineUpdateChannel::Beta;
	else if (text == "Dev") channel = EngineUpdateChannel::Dev;
	else return false;
	return true;
}

std::string GetManoEngineDisplayVersion() { return GetManoEngineVersion().ToString(); }
std::uint32_t GetManoProjectFormatVersion() { return ManoEngineVersionGenerated::kProjectFormat; }
std::uint32_t GetManoSceneFormatVersion() { return ManoEngineVersionGenerated::kSceneFormat; }
std::uint32_t GetManoPrefabFormatVersion() { return ManoEngineVersionGenerated::kPrefabFormat; }
std::uint32_t GetManoScriptApiVersion() { return kEditorScriptApiVersion; }

bool CheckManoEnginePeerCompatibility(const std::string& peerEngineVersion,
	std::uint32_t peerProjectFormat, std::uint32_t peerScriptApiVersion,
	const std::string& peerChannel, std::uint32_t localProjectFormat,
	EngineUpdateChannel localChannel, std::string& reason) {
	reason.clear();
	if (peerEngineVersion != GetManoEngineDisplayVersion())
		reason = "Engine Version不一致 (Local " + GetManoEngineDisplayVersion() + " / Peer " + peerEngineVersion + ")";
	else if (peerProjectFormat != localProjectFormat) reason = "Project Format不一致";
	else if (peerScriptApiVersion != GetManoScriptApiVersion()) reason = "Script API Version不一致";
	else if (peerChannel != GetEngineUpdateChannelText(localChannel)) reason = "Update Channel不一致";
	return reason.empty();
}
