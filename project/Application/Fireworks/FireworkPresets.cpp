#include "Application/Fireworks/FireworkPresets.h"

#include <array>
#include <cmath>
#include <numbers>

namespace fireworks {
namespace {

Vector3 SphereDirection(int index, int count, const EffectParameters&) {
	// 緯度だけを等間隔にすると極に粒子が集まるため、Y と方位角で球面に散らす。
	constexpr float kGoldenAngle = 2.39996323f;
	const float y = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / count;
	const float radius = std::sqrt(1.0f - y * y);
	const float angle = index * kGoldenAngle;
	return {std::cos(angle) * radius, y, std::sin(angle) * radius};
}

Vector3 RingDirection(int index, int count, const EffectParameters&) {
	const float angle = 2.0f * std::numbers::pi_v<float> * index / count;
	return {std::cos(angle), std::sin(angle), 0.0f};
}

Vector3 WillowDirection(int index, int count, const EffectParameters& effect) {
	// 星を一方向の細い束として放出する。下降は発射速度ではなく重力に任せる。
	const int groups = count < effect.willowGroupCount ? count : effect.willowGroupCount;
	const int group = index % groups;
	const int strand = index / groups;
	const int strandsPerGroup = (count + groups - 1) / groups;
	const float groupAcross = 2.0f * (static_cast<float>(group) + 0.5f) / groups - 1.0f;
	const float across = strandsPerGroup > 1
		? 2.0f * strand / (strandsPerGroup - 1) - 1.0f : 0.0f;
	constexpr float kGoldenAngle = 2.39996323f;
	const float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
	const float angle = effect.willowDirectionDegrees * degreesToRadians
		+ (groupAcross + across / (2.0f * groups)) * effect.willowGroupSpread;
	return Normalize({
		std::cos(angle),
		std::sin(angle),
		std::sin(index * kGoldenAngle) * effect.willowGroupSpread
	});
}

Vector3 PalmDirection(int index, int count, const EffectParameters& effect) {
	// 少数の枝を球状に配置し、各枝の中だけを設定した幅で束ねる。
	const int armCount = count < effect.palmArmCount ? count : effect.palmArmCount;
	const int arm = index % armCount;
	const int strand = index / armCount;
	const int strandCount = (count + armCount - 1) / armCount;
	const Vector3 base = SphereDirection(arm, armCount, effect);
	const Vector3 tangent = Normalize({-base.z, 0.0f, base.x});
	const float across = strandCount > 1
		? static_cast<float>(strand) / (strandCount - 1) - 0.5f : 0.0f;
	return Normalize(base + tangent * (across * effect.palmBranchSpread));
}

// 新しい種類を追加する場所。id は保存ファイルで使うので、公開後は使い回さない。
// 打ち上げ型は菊の広がり速度15を基準にし、柳だけは低速で放出して長く垂らす。
// 開花方向の関数が空なら、吹き上げとして連続発生する。
constexpr std::array kPresets{
	FireworkPreset{
		.id = "fountain",
		.label = "吹き上げ",
		.defaults = {
			.stageSeconds = 2.5f,
			.launchSpeed = 12.0f,
			.spreadSpeed = 5.0f,
			.speedJitter = 7.5f,
			.gravity = 9.8f,
			.particleCount = 20000,
			.sparkLife = 2.0f,
			.sparkWidth = 0.1f,
			.widthJitter = 0.04f,
			.trailSeconds = 0.05f,
			.color = {1.0f, 0.5f, 0.45f, 1.0f},
			.launchTrailRate = 0,
			.launchTrailLifeSeconds = 0.0f,
			.launchTrailWidth = 0.0f,
			.launchTrailOverlap = 0.0f
		},
		.originJitter = 0.1f,
		.colorJitter = {0.0f, 0.10f, 0.15f},
		.burstDirection = nullptr
	},
	FireworkPreset{
		.id = "sphere",
		.label = "菊",
		.defaults = {
			.stageSeconds = 1.15f,
			.launchSpeed = 20.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 1.2f,
			.gravity = 6.5f,
			.particleCount = 600,
			.sparkLife = 3.0f,
			.sparkWidth = 0.3f,
			.widthJitter = 0.04f,
			.trailSeconds = 0.3f,
			.color = {0.50f, 0.74f, 1.0f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.08f, 0.06f, 0.0f},
		.burstDirection = SphereDirection
	},
	FireworkPreset{
		.id = "ring",
		.label = "リング",
		.defaults = {
			.stageSeconds = 1.1f,
			.launchSpeed = 20.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 0.25f,
			.gravity = 5.5f,
			.particleCount = 500,
			.sparkLife = 3.0f,
			.sparkWidth = 0.3f,
			.widthJitter = 0.035f,
			.trailSeconds = 0.3f,
			.color = {0.34f, 1.0f, 0.72f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.05f, 0.0f, 0.06f},
		.burstDirection = RingDirection
	},
	FireworkPreset{
		.id = "willow",
		.label = "柳",
		.defaults = {
			.stageSeconds = 1.15f,
			.launchSpeed = 21.0f,
			.spreadSpeed = 6.0f,
			.speedJitter = 0.5f,
			.gravity = 2.0f,
			.particleCount = 500,
			.sparkLife = 5.2f,
			.sparkWidth = 0.16f,
			.widthJitter = 0.04f,
			.trailSeconds = 0.8f,
			.color = {1.0f, 0.85f, 0.45f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f,
			.willowTrailLifeSeconds = 2.5f,
			.willowGroupCount = 9,
			.willowGroupSpread = 0.12f,
			.willowDirectionDegrees = 20.0f,
			.willowGlowHoldSeconds = 3.8f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.0f, 0.09f, 0.10f},
		.burstDirection = WillowDirection,
		.pattern = FireworkPattern::Willow
	},
	FireworkPreset{
		.id = "peony",
		.label = "牡丹",
		.defaults = {
			.stageSeconds = 1.2f,
			.launchSpeed = 21.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 0.7f,
			.gravity = 6.0f,
			.particleCount = 800,
			.sparkLife = 4.0f,
			.sparkWidth = 0.5f,
			.widthJitter = 0.035f,
			.trailSeconds = 0.005f,
			.color = {1.0f, 0.47f, 0.38f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.0f, 0.04f, 0.03f},
		.burstDirection = SphereDirection
	},
	FireworkPreset{
		.id = "thousand_chrysanthemum",
		.label = "千輪菊",
		.defaults = {
			.stageSeconds = 1.15f,
			.launchSpeed = 20.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 0.45f,
			.gravity = 6.5f,
			.particleCount = 800,
			.sparkLife = 3.0f,
			.sparkWidth = 0.3f,
			.widthJitter = 0.018f,
			.trailSeconds = 0.3f,
			.color = {1.0f, 0.88f, 0.55f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.0f, 0.08f, 0.12f},
		.burstDirection = SphereDirection,
		.pattern = FireworkPattern::ThousandChrysanthemum,
		.childDefaults = {
			.count = 24,
			.travelSpeed = 15.0f,
			.fuseSeconds = 1.5f,
			.fuseSpreadSeconds = 0.05f,
			.shellWidth = 0.22f,
			.shellTrailSeconds = 0.22f,
			.particleCount = 300,
			.burstSpeed = 8.0f,
			.sparkLife = 2.0f,
			.sparkWidth = 0.2f,
			.trailSeconds = 0.14f,
			.gravity = 5.0f,
			.color = {1.0f, 0.82f, 0.44f, 1.0f},
			.colorVariation = 0.18f
		}
	},
	FireworkPreset{
		.id = "palm",
		.label = "椰子",
		.defaults = {
			.stageSeconds = 1.15f,
			.launchSpeed = 21.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 0.55f,
			.gravity = 6.5f,
			.particleCount = 96,
			.sparkLife = 3.5f,
			.sparkWidth = 0.5f,
			.widthJitter = 0.06f,
			.trailSeconds = 0.5f,
			.color = {1.0f, 0.85f, 0.45f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f,
			.palmBranchLifeSeconds = 2.0f,
			.palmBranchSpread = 0.08f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.0f, 0.05f, 0.06f},
		.burstDirection = PalmDirection,
		.pattern = FireworkPattern::Palm
	},
	FireworkPreset{
		.id = "crown",
		.label = "冠菊",
		.defaults = {
			.stageSeconds = 1.25f,
			.launchSpeed = 21.0f,
			.spreadSpeed = 15.0f,
			.speedJitter = 0.9f,
			.gravity = 6.5f,
			.particleCount = 600,
			.sparkLife = 4.2f,
			.sparkWidth = 0.3f,
			.widthJitter = 0.025f,
			.trailSeconds = 0.42f,
			.color = {1.0f, 0.83f, 0.42f, 1.0f},
			.launchTrailRate = kFramesPerSecond,
			.launchTrailLifeSeconds = 1.0f,
			.launchTrailWidth = 0.5f,
			.launchTrailOverlap = 0.7f,
			.crownDroopStartSeconds = 0.8f,
			.crownHorizontalDrag = 2.0f
		},
		.originJitter = 0.0f,
		.colorJitter = {0.0f, 0.09f, 0.08f},
		.burstDirection = SphereDirection,
		.pattern = FireworkPattern::CrownChrysanthemum
	}
};

} // namespace

std::span<const FireworkPreset> GetPresets() {
	return kPresets;
}

const FireworkPreset* FindPreset(std::string_view id) {
	for (const FireworkPreset& preset : GetPresets()) {
		if (preset.id == id) {
			return &preset;
		}
	}
	return nullptr;
}

FireworkCue MakeCue(const FireworkPreset& preset, int startFrame, std::uint32_t seed) {
	FireworkCue cue{};
	cue.presetId = preset.id;
	cue.startFrame = startFrame;
	cue.seed = seed;
	cue.effect = preset.defaults;
	cue.child = preset.childDefaults;
	return cue;
}

} // namespace fireworks
