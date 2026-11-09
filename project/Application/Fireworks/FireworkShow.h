#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "math/Vector.h"

namespace fireworks {

inline constexpr int kFramesPerSecond = 60;
inline constexpr float kStepSeconds = 1.0f / kFramesPerSecond;
inline constexpr int kMaxShowSeconds = 120;
inline constexpr std::size_t kMaxCues = 128;
inline constexpr std::size_t kMaxLiveParticles = 200000;
inline constexpr char kSavedShowPath[] = "resources/fireworks/show.fwshow";
inline constexpr char kExampleShowPath[] = "resources/fireworks/example.fwshow";
inline constexpr char kEffectPresetDirectory[] = "resources/fireworks/presets";

// 編集画面とファイル読み込みで同じ範囲を使う。
inline constexpr float kPositionLimit = 60.0f;
inline constexpr float kMinStageSeconds = 0.05f;
inline constexpr float kMaxFountainSeconds = 20.0f;
inline constexpr float kMaxRiseSeconds = 5.0f;
inline constexpr float kMaxSpeed = 50.0f;
inline constexpr float kMaxLaunchTiltDegrees = 60.0f;
inline constexpr float kMaxGravity = 30.0f;
inline constexpr int kMaxFountainRate = 20000;
inline constexpr int kMaxBurstCount = 5000;
inline constexpr int kMaxChildFireworks = 64;
inline constexpr int kMaxChildSparkCount = 300;
inline constexpr int kMinPalmArms = 4;
inline constexpr int kMaxPalmArms = 24;
inline constexpr float kMaxPalmBranchLifeSeconds = 3.0f;
inline constexpr float kMaxPalmBranchSpread = 0.5f;
inline constexpr float kMaxWillowTrailLifeSeconds = 3.0f;
inline constexpr int kMaxWillowGroups = 16;
inline constexpr float kMaxWillowGroupSpread = 0.6f;
inline constexpr float kMinWillowDirectionDegrees = -180.0f;
inline constexpr float kMaxWillowDirectionDegrees = 180.0f;
inline constexpr float kMaxChildFuseSeconds = 2.0f;
inline constexpr float kMaxChildFuseSpreadSeconds = 0.5f;
inline constexpr float kMinSparkLife = 0.05f;
inline constexpr float kMaxSparkLife = 6.0f;
inline constexpr float kMinWidth = 0.005f;
inline constexpr float kMaxWidth = 1.0f;
inline constexpr float kMaxTrailSeconds = 0.5f;
inline constexpr int kMaxLaunchTrailRate = kFramesPerSecond;
inline constexpr float kMaxLaunchTrailLifeSeconds = 2.0f;
inline constexpr float kMaxLaunchTrailWidth = 0.5f;
inline constexpr float kMaxLaunchTrailOverlap = 2.0f;
inline constexpr float kMaxCrownDroopStartSeconds = 2.0f;
inline constexpr float kMaxCrownHorizontalDrag = 10.0f;
inline constexpr float kMaxSoundDelaySeconds = 5.0f;

struct EffectParameters {
	// 吹き上げでは発生時間、打ち上げでは開花までの時間。
	float stageSeconds = 1.0f;
	float launchSpeed = 12.0f;
	// 0 度で真上。左右は X 方向、前後は Z 方向への傾き。
	float launchSideAngleDegrees = 0.0f;
	float launchDepthAngleDegrees = 0.0f;
	float spreadSpeed = 5.0f;
	float speedJitter = 0.0f;
	float gravity = 9.8f;
	// 吹き上げでは1秒の発生数、打ち上げでは開花時の発生数。
	int particleCount = 200;
	float sparkLife = 1.0f;
	float sparkWidth = 0.05f;
	float widthJitter = 0.0f;
	float trailSeconds = 0.05f;
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	// 打ち上げ時の光の線。0 本/秒で軌跡を消せる。
	int launchTrailRate = kFramesPerSecond;
	float launchTrailLifeSeconds = 1.0f;
	float launchTrailWidth = 0.5f;
	float launchTrailOverlap = 0.7f;
	// 開花色とは独立した、上昇中の発射体と軌跡の色。
	Vector4 launchTrailColor = {1.0f, 0.95f, 0.72f, 1.0f};
	int palmArmCount = 12;
	// 椰子の太い枝を、移動する星が通った位置に残す。
	float palmBranchLifeSeconds = 2.0f;
	float palmBranchSpread = 0.08f;
	// 柳の細い枝が、星の通過後も光として残る時間。
	float willowTrailLifeSeconds = 2.3f;
	int willowGroupCount = 9;
	float willowGroupSpread = 0.12f;
	float willowDirectionDegrees = 20.0f;
	// 柳の星を明るいまま表示し、終盤だけ徐々に暗くする境界時刻。
	float willowGlowHoldSeconds = 0.0f;
	// 冠菊は菊と同じ球状に開き、指定時刻から横の勢いが弱まる。
	float crownDroopStartSeconds = 0.0f;
	float crownHorizontalDrag = 0.0f;
};

// 千輪菊の親玉から飛び出す小玉と、その後に開く小花火の設定。
struct ChildFireworkSettings {
	int count = 24;
	float travelSpeed = 15.0f;
	float fuseSeconds = 1.5f;
	float fuseSpreadSeconds = 0.05f;
	float shellWidth = 0.22f;
	float shellTrailSeconds = 0.22f;
	int particleCount = 300;
	float burstSpeed = 8.0f;
	float sparkLife = 2.0f;
	float sparkWidth = 0.2f;
	float trailSeconds = 0.14f;
	float gravity = 5.0f;
	Vector4 color = {1.0f, 0.82f, 0.44f, 1.0f};
	float colorVariation = 0.18f;
};

struct FireworkSound {
	std::string path;
	float delaySeconds = 0.0f;
	float volume = 1.0f;
};

struct FireworkCue {
	std::string presetId;
	int startFrame = 0;
	Vector3 position{};
	std::uint32_t seed = 1;
	EffectParameters effect{};
	ChildFireworkSettings child{};
	FireworkSound launchSound;
	FireworkSound burstSound;
};

struct FireworkShow {
	int durationFrames = 40 * kFramesPerSecond;
	std::vector<FireworkCue> cues;
};

constexpr float ToSeconds(int frames) {
	return static_cast<float>(frames) / kFramesPerSecond;
}

int ToFrames(float seconds);
bool LoadShow(const std::filesystem::path& path, FireworkShow& show, std::string& error);
bool SaveShow(const std::filesystem::path& path, const FireworkShow& show, std::string& error);
// 1件の花火から見た目の種類と設定だけを保存・適用する。位置と開始時刻は保持する。
bool SaveEffectPreset(const std::filesystem::path& path, const FireworkCue& cue, std::string& error);
bool LoadEffectPreset(const std::filesystem::path& path, FireworkCue& cue, std::string& error);

} // namespace fireworks
