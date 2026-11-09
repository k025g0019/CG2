#pragma once

#include <span>
#include <string_view>

#include "Application/Fireworks/FireworkShow.h"

namespace fireworks {

// 開花の1粒ごとの単位方向。設定値を使う種類もここで形状を決める。
using BurstDirection = Vector3 (*)(int index, int particleCount,
	const EffectParameters& effect);

enum class FireworkPattern { Standard, ThousandChrysanthemum, CrownChrysanthemum, Palm, Willow };

struct FireworkPreset {
	std::string_view id;
	const char* label;
	EffectParameters defaults;
	float originJitter;
	Vector3 colorJitter;
	BurstDirection burstDirection;
	FireworkPattern pattern = FireworkPattern::Standard;
	ChildFireworkSettings childDefaults{};

	bool IsAerial() const { return burstDirection != nullptr; }
};

std::span<const FireworkPreset> GetPresets();
const FireworkPreset* FindPreset(std::string_view id);
FireworkCue MakeCue(const FireworkPreset& preset, int startFrame, std::uint32_t seed);

} // namespace fireworks
