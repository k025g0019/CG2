#include "Application/Fireworks/FireworkShow.h"
#include "Application/Fireworks/FireworkPresets.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <string_view>
#include <system_error>
#include <utility>

#include <Windows.h>

namespace fireworks {
namespace {

constexpr char kFileMagic[] = "FIREWORK_SHOW";
constexpr int kFileVersion = 12;
constexpr char kUtf8Bom[] = "\xEF\xBB\xBF";
constexpr std::uintmax_t kMaxFileBytes = 512 * 1024;

bool InRange(float value, float minimum, float maximum) {
	return value >= minimum && value <= maximum;
}

bool ValidDuration(int frames) {
	return frames >= kFramesPerSecond && frames <= kMaxShowSeconds * kFramesPerSecond;
}

bool ValidateSound(const FireworkSound& sound) {
	return sound.path.size() <= 512
		&& InRange(sound.delaySeconds, 0.0f, kMaxSoundDelaySeconds)
		&& InRange(sound.volume, 0.0f, 1.0f);
}

bool ValidateChild(const ChildFireworkSettings& child) {
	return child.count >= 1 && child.count <= kMaxChildFireworks
		&& InRange(child.travelSpeed, 0.0f, kMaxSpeed)
		&& InRange(child.fuseSeconds, 0.1f, kMaxChildFuseSeconds)
		&& InRange(child.fuseSpreadSeconds, 0.0f, kMaxChildFuseSpreadSeconds)
		&& child.fuseSpreadSeconds <= child.fuseSeconds - 0.05f
		&& InRange(child.shellWidth, kMinWidth, kMaxWidth)
		&& InRange(child.shellTrailSeconds, 0.0f, kMaxTrailSeconds)
		&& child.particleCount >= 1 && child.particleCount <= kMaxChildSparkCount
		&& InRange(child.burstSpeed, 0.0f, kMaxSpeed)
		&& InRange(child.sparkLife, kMinSparkLife, kMaxSparkLife)
		&& InRange(child.sparkWidth, kMinWidth, kMaxWidth)
		&& InRange(child.trailSeconds, 0.0f, kMaxTrailSeconds)
		&& InRange(child.gravity, 0.0f, kMaxGravity)
		&& InRange(child.color.x, 0.0f, 1.0f)
		&& InRange(child.color.y, 0.0f, 1.0f)
		&& InRange(child.color.z, 0.0f, 1.0f)
		&& InRange(child.color.w, 0.0f, 1.0f)
		&& InRange(child.colorVariation, 0.0f, 1.0f);
}

bool ValidateCue(const FireworkCue& cue, int durationFrames) {
	const FireworkPreset* preset = FindPreset(cue.presetId);
	if (preset == nullptr || cue.startFrame < 0 || cue.startFrame >= durationFrames) {
		return false;
	}

	const EffectParameters& effect = cue.effect;
	const float maxStageSeconds = preset->IsAerial() ? kMaxRiseSeconds : kMaxFountainSeconds;
	const int maxCount = preset->IsAerial() ? kMaxBurstCount : kMaxFountainRate;
	return InRange(cue.position.x, -kPositionLimit, kPositionLimit)
		&& InRange(cue.position.y, -kPositionLimit, kPositionLimit)
		&& InRange(cue.position.z, -kPositionLimit, kPositionLimit)
		&& InRange(effect.stageSeconds, kMinStageSeconds, maxStageSeconds)
		&& InRange(effect.launchSpeed, 0.0f, kMaxSpeed)
		&& InRange(effect.launchSideAngleDegrees,
			-kMaxLaunchTiltDegrees, kMaxLaunchTiltDegrees)
		&& InRange(effect.launchDepthAngleDegrees,
			-kMaxLaunchTiltDegrees, kMaxLaunchTiltDegrees)
		&& InRange(effect.spreadSpeed, 0.0f, kMaxSpeed)
		&& InRange(effect.speedJitter, 0.0f, kMaxSpeed)
		&& InRange(effect.gravity, 0.0f, kMaxGravity)
		&& effect.particleCount >= 1 && effect.particleCount <= maxCount
		&& InRange(effect.sparkLife, kMinSparkLife, kMaxSparkLife)
		&& InRange(effect.sparkWidth, kMinWidth, kMaxWidth)
		&& InRange(effect.widthJitter, 0.0f, effect.sparkWidth - kMinWidth)
		&& InRange(effect.trailSeconds, 0.0f, kMaxTrailSeconds)
		&& InRange(effect.color.x, 0.0f, 1.0f)
		&& InRange(effect.color.y, 0.0f, 1.0f)
		&& InRange(effect.color.z, 0.0f, 1.0f)
		&& InRange(effect.color.w, 0.0f, 1.0f)
		&& effect.launchTrailRate >= 0 && effect.launchTrailRate <= kMaxLaunchTrailRate
		&& InRange(effect.launchTrailLifeSeconds, 0.0f, kMaxLaunchTrailLifeSeconds)
		&& InRange(effect.launchTrailWidth, 0.0f, kMaxLaunchTrailWidth)
		&& InRange(effect.launchTrailOverlap, 0.0f, kMaxLaunchTrailOverlap)
		&& InRange(effect.launchTrailColor.x, 0.0f, 1.0f)
		&& InRange(effect.launchTrailColor.y, 0.0f, 1.0f)
		&& InRange(effect.launchTrailColor.z, 0.0f, 1.0f)
		&& InRange(effect.launchTrailColor.w, 0.0f, 1.0f)
		&& effect.palmArmCount >= kMinPalmArms && effect.palmArmCount <= kMaxPalmArms
		&& InRange(effect.palmBranchLifeSeconds, 0.0f, kMaxPalmBranchLifeSeconds)
		&& InRange(effect.palmBranchSpread, 0.0f, kMaxPalmBranchSpread)
		&& InRange(effect.willowTrailLifeSeconds, 0.0f, kMaxWillowTrailLifeSeconds)
		&& effect.willowGroupCount >= 1 && effect.willowGroupCount <= kMaxWillowGroups
		&& InRange(effect.willowGroupSpread, 0.0f, kMaxWillowGroupSpread)
		&& InRange(effect.willowDirectionDegrees,
			kMinWillowDirectionDegrees, kMaxWillowDirectionDegrees)
		&& InRange(effect.willowGlowHoldSeconds, 0.0f,
			(std::max)(0.0f, effect.sparkLife - kMinSparkLife))
		&& InRange(effect.crownDroopStartSeconds, 0.0f, kMaxCrownDroopStartSeconds)
		&& InRange(effect.crownHorizontalDrag, 0.0f, kMaxCrownHorizontalDrag)
		&& (preset->pattern != FireworkPattern::CrownChrysanthemum
			|| effect.crownDroopStartSeconds < effect.sparkLife)
		&& ValidateChild(cue.child)
		&& ValidateSound(cue.launchSound)
		&& ValidateSound(cue.burstSound);
}

bool ReadCue(std::istream& input, FireworkCue& cue, int version) {
	EffectParameters& e = cue.effect;
	std::int64_t seed = 0;
	if (!(input >> cue.presetId >> cue.startFrame >> seed
		>> cue.position.x >> cue.position.y >> cue.position.z
		>> e.stageSeconds >> e.launchSpeed >> e.spreadSpeed >> e.speedJitter >> e.gravity
		>> e.particleCount >> e.sparkLife >> e.sparkWidth >> e.widthJitter >> e.trailSeconds
		>> e.color.x >> e.color.y >> e.color.z >> e.color.w)
		|| seed < 0 || seed > (std::numeric_limits<std::uint32_t>::max)()) {
		return false;
	}
	if (version == 1) {
		// 旧ファイルには軌跡の設定がない。種類ごとの初期値を補う。
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			e.launchTrailRate = preset->defaults.launchTrailRate;
			e.launchTrailLifeSeconds = preset->defaults.launchTrailLifeSeconds;
			e.launchTrailWidth = preset->defaults.launchTrailWidth;
			e.launchTrailOverlap = preset->defaults.launchTrailOverlap;
		}
	} else if (!(input >> e.launchTrailRate >> e.launchTrailLifeSeconds
		>> e.launchTrailWidth >> e.launchTrailOverlap)) {
		return false;
	}
	if (version >= 4) {
		if (!(input >> std::quoted(cue.launchSound.path)
			>> cue.launchSound.delaySeconds >> cue.launchSound.volume
			>> std::quoted(cue.burstSound.path)
			>> cue.burstSound.delaySeconds >> cue.burstSound.volume)) {
			return false;
		}
	} else if (version == 3) {
		// 旧形式の効果音1件を、元の再生タイミングの枠へ移す。
		FireworkSound sound;
		int trigger = 0;
		if (!(input >> std::quoted(sound.path) >> trigger
			>> sound.delaySeconds >> sound.volume)
			|| (trigger != 0 && trigger != 1)) {
			return false;
		}
		(trigger == 1 ? cue.burstSound : cue.launchSound) = std::move(sound);
	}
	if (version >= 5) {
		ChildFireworkSettings& child = cue.child;
		if (!(input >> e.launchTrailColor.x >> e.launchTrailColor.y
			>> e.launchTrailColor.z >> e.launchTrailColor.w
			>> child.count >> child.travelSpeed >> child.fuseSeconds
			>> child.fuseSpreadSeconds >> child.shellWidth >> child.shellTrailSeconds
			>> child.particleCount >> child.burstSpeed
			>> child.sparkLife >> child.sparkWidth >> child.trailSeconds
			>> child.gravity >> child.color.x >> child.color.y
			>> child.color.z >> child.color.w >> child.colorVariation
			>> e.palmArmCount)) {
			return false;
		}
	} else {
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			cue.child = preset->childDefaults;
		}
	}
	if (cue.presetId == "niagara") {
		// 廃止した仕掛花火を、今回置き換えた冠菊として読み込む。
		const FireworkPreset* crown = FindPreset("crown");
		if (crown == nullptr) {
			return false;
		}
		cue.presetId = "crown";
		cue.effect = crown->defaults;
		cue.child = crown->childDefaults;
	}
	if (version >= 6) {
		if (!(input >> e.crownDroopStartSeconds >> e.crownHorizontalDrag)) {
			return false;
		}
	} else {
		// 旧形式には冠菊の垂れ方がない。保存済みの寿命は保って設定だけ補う。
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			e.crownDroopStartSeconds = (std::min)(preset->defaults.crownDroopStartSeconds,
				(std::max)(0.0f, e.sparkLife - 0.05f));
			e.crownHorizontalDrag = preset->defaults.crownHorizontalDrag;
		}
	}
	if (version >= 7) {
		if (!(input >> e.palmBranchLifeSeconds >> e.palmBranchSpread)) {
			return false;
		}
	} else {
		// 旧形式の椰子には枝の残像設定がない。種類ごとの初期値を補う。
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			e.palmBranchLifeSeconds = preset->defaults.palmBranchLifeSeconds;
			e.palmBranchSpread = preset->defaults.palmBranchSpread;
		}
	}
	if (version >= 8) {
		if (!(input >> e.willowTrailLifeSeconds)) {
			return false;
		}
	} else {
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			e.willowTrailLifeSeconds = preset->defaults.willowTrailLifeSeconds;
		}
	}
	if (version >= 9) {
		float directionOrLegacyBias = 0.0f;
		if (!(input >> e.willowGroupCount >> e.willowGroupSpread
			>> directionOrLegacyBias)) {
			return false;
		}
		if (version >= 10) {
			e.willowDirectionDegrees = directionOrLegacyBias;
		} else {
			// 旧形式の値は下向き初速度の割合。新しい放出角度とは単位が違う。
			const FireworkPreset* preset = FindPreset(cue.presetId);
			if (preset != nullptr) {
				e.willowDirectionDegrees = preset->defaults.willowDirectionDegrees;
			}
		}
	} else {
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			e.willowGroupCount = preset->defaults.willowGroupCount;
			e.willowGroupSpread = preset->defaults.willowGroupSpread;
			e.willowDirectionDegrees = preset->defaults.willowDirectionDegrees;
		}
	}
	if (version >= 11) {
		if (!(input >> e.willowGlowHoldSeconds)) {
			return false;
		}
	} else {
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset != nullptr) {
			// 旧データの火花寿命は保持し、明るく保つ時間だけ範囲内で補う。
			e.willowGlowHoldSeconds = (std::min)(preset->defaults.willowGlowHoldSeconds,
				(std::max)(0.0f, e.sparkLife - kMinSparkLife));
		}
	}
	if (version <= 10 && cue.presetId == "willow"
		&& e.spreadSpeed == 15.0f && e.speedJitter == 1.5f
		&& e.gravity == 5.5f && e.particleCount == 360
		&& e.sparkLife == 4.4f && e.sparkWidth == 0.16f
		&& e.widthJitter == 0.04f && e.trailSeconds == 0.5f
		&& e.willowGroupCount == 9 && e.willowGroupSpread == 0.12f
		&& (version < 8 || e.willowTrailLifeSeconds == 2.3f)
		&& (version < 10 || e.willowDirectionDegrees == 10.0f)) {
		// 旧プリセットをそのまま使った柳だけ、新しい低速の柳へ移す。
		// 形や動きを調整した柳は利用者の値を優先する。
		const FireworkPreset* preset = FindPreset("willow");
		if (preset != nullptr) {
			e.spreadSpeed = preset->defaults.spreadSpeed;
			e.speedJitter = preset->defaults.speedJitter;
			e.gravity = preset->defaults.gravity;
			e.particleCount = preset->defaults.particleCount;
			e.sparkLife = preset->defaults.sparkLife;
			e.trailSeconds = preset->defaults.trailSeconds;
			e.willowTrailLifeSeconds = preset->defaults.willowTrailLifeSeconds;
			e.willowDirectionDegrees = preset->defaults.willowDirectionDegrees;
			e.willowGlowHoldSeconds = preset->defaults.willowGlowHoldSeconds;
		}
	}
	if (version >= 12) {
		if (!(input >> e.launchSideAngleDegrees >> e.launchDepthAngleDegrees)) {
			return false;
		}
	} else {
		// 既存の演出は従来どおり真上に打ち上げる。
		e.launchSideAngleDegrees = 0.0f;
		e.launchDepthAngleDegrees = 0.0f;
	}
	cue.seed = static_cast<std::uint32_t>(seed);
	return true;
}

void WriteCue(std::ostream& output, const FireworkCue& cue) {
	const EffectParameters& e = cue.effect;
	output << cue.presetId << ' ' << cue.startFrame << ' ' << cue.seed << ' '
		<< cue.position.x << ' ' << cue.position.y << ' ' << cue.position.z << ' '
		<< e.stageSeconds << ' ' << e.launchSpeed << ' ' << e.spreadSpeed << ' '
		<< e.speedJitter << ' ' << e.gravity << ' ' << e.particleCount << ' '
		<< e.sparkLife << ' ' << e.sparkWidth << ' ' << e.widthJitter << ' '
		<< e.trailSeconds << ' ' << e.color.x << ' ' << e.color.y << ' '
		<< e.color.z << ' ' << e.color.w << ' '
		<< e.launchTrailRate << ' ' << e.launchTrailLifeSeconds << ' '
		<< e.launchTrailWidth << ' ' << e.launchTrailOverlap << ' '
		<< std::quoted(cue.launchSound.path) << ' '
		<< cue.launchSound.delaySeconds << ' ' << cue.launchSound.volume << ' '
		<< std::quoted(cue.burstSound.path) << ' '
		<< cue.burstSound.delaySeconds << ' ' << cue.burstSound.volume << ' '
		<< e.launchTrailColor.x << ' ' << e.launchTrailColor.y << ' '
		<< e.launchTrailColor.z << ' ' << e.launchTrailColor.w << ' '
		<< cue.child.count << ' ' << cue.child.travelSpeed << ' '
		<< cue.child.fuseSeconds << ' ' << cue.child.fuseSpreadSeconds << ' '
		<< cue.child.shellWidth << ' ' << cue.child.shellTrailSeconds << ' '
		<< cue.child.particleCount << ' ' << cue.child.burstSpeed << ' '
		<< cue.child.sparkLife << ' ' << cue.child.sparkWidth << ' '
		<< cue.child.trailSeconds << ' ' << cue.child.gravity << ' '
		<< cue.child.color.x << ' ' << cue.child.color.y << ' '
		<< cue.child.color.z << ' ' << cue.child.color.w << ' '
		<< cue.child.colorVariation << ' ' << e.palmArmCount << ' '
		<< e.crownDroopStartSeconds << ' ' << e.crownHorizontalDrag << ' '
		<< e.palmBranchLifeSeconds << ' ' << e.palmBranchSpread << ' '
		<< e.willowTrailLifeSeconds << ' ' << e.willowGroupCount << ' '
		<< e.willowGroupSpread << ' ' << e.willowDirectionDegrees << ' '
		<< e.willowGlowHoldSeconds << ' ' << e.launchSideAngleDegrees << ' '
		<< e.launchDepthAngleDegrees << '\n';
}

} // namespace

int ToFrames(float seconds) {
	if (!std::isfinite(seconds)) {
		return 0;
	}
	const float boundedSeconds = std::clamp(seconds, 0.0f, static_cast<float>(kMaxShowSeconds));
	return static_cast<int>(std::lround(boundedSeconds * kFramesPerSecond));
}

bool LoadShow(const std::filesystem::path& path, FireworkShow& show, std::string& error) {
	std::error_code fileError;
	const std::uintmax_t bytes = std::filesystem::file_size(path, fileError);
	if (fileError || bytes > kMaxFileBytes) {
		error = "演出ファイルがないか、読み込めないか、サイズが大きすぎます。";
		return false;
	}

	std::ifstream input(path, std::ios::binary);
	input.imbue(std::locale::classic());
	if (input.peek() == 0xEF) {
		char bom[3]{};
		input.read(bom, sizeof(bom));
		if (std::string_view(bom, sizeof(bom)) != kUtf8Bom) {
			error = "UTF-8 の署名が正しくありません。";
			return false;
		}
	}

	// すべて読めたときだけ編集中のデータを入れ替える。
	FireworkShow loaded;
	std::string magic;
	int version = 0;
	int count = 0;
	if (!(input >> magic >> version >> loaded.durationFrames >> count)
		|| magic != kFileMagic || version < 1 || version > kFileVersion
		|| !ValidDuration(loaded.durationFrames)
		|| count < 0 || static_cast<std::size_t>(count) > kMaxCues) {
		error = "演出ファイルの形式かバージョンが正しくありません。";
		return false;
	}

	loaded.cues.reserve(static_cast<std::size_t>(count));
	for (int index = 0; index < count; ++index) {
		FireworkCue cue;
		if (!ReadCue(input, cue, version) || !ValidateCue(cue, loaded.durationFrames)) {
			error = "花火 " + std::to_string(index + 1) + " の設定か種類が正しくありません。";
			return false;
		}
		loaded.cues.push_back(std::move(cue));
	}
	input >> std::ws;
	if (!input.eof() || input.bad()) {
		error = "演出ファイルの末尾に余分なデータがあります。";
		return false;
	}

	show = std::move(loaded);
	error.clear();
	return true;
}

bool SaveShow(const std::filesystem::path& path, const FireworkShow& show, std::string& error) {
	if (!ValidDuration(show.durationFrames) || show.cues.size() > kMaxCues) {
		error = "演出時間か花火の数が正しくありません。";
		return false;
	}
	for (std::size_t index = 0; index < show.cues.size(); ++index) {
		if (!ValidateCue(show.cues[index], show.durationFrames)) {
			error = "花火 " + std::to_string(index + 1) + " の設定が正しくありません。";
			return false;
		}
	}

	std::error_code fileError;
	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path(), fileError);
	}
	if (fileError) {
		error = "演出の保存先フォルダーを作成できませんでした。";
		return false;
	}

	// 書き込みに失敗しても前回の保存を残すため、同じフォルダに書いてから置き換える。
	std::filesystem::path temporary = path;
	temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	output.imbue(std::locale::classic());
	output.write(kUtf8Bom, 3);
	output << std::setprecision(std::numeric_limits<float>::max_digits10);
	output << kFileMagic << ' ' << kFileVersion << '\n';
	output << show.durationFrames << ' ' << show.cues.size() << '\n';
	for (const FireworkCue& cue : show.cues) {
		WriteCue(output, cue);
	}
	output.close();

	if (!output || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		std::filesystem::remove(temporary, fileError);
		error = "演出ファイルを書き込めませんでした。";
		return false;
	}
	error.clear();
	return true;
}

bool SaveEffectPreset(const std::filesystem::path& path, const FireworkCue& cue, std::string& error) {
	FireworkShow single;
	FireworkCue settings = cue;
	settings.startFrame = 0;
	settings.position = {};
	settings.seed = 1;
	single.cues.push_back(std::move(settings));
	return SaveShow(path, single, error);
}

bool LoadEffectPreset(const std::filesystem::path& path, FireworkCue& cue, std::string& error) {
	FireworkShow single;
	if (!LoadShow(path, single, error)) {
		return false;
	}
	if (single.cues.size() != 1) {
		error = "設定プリセットには花火が1件必要です。";
		return false;
	}
	cue.presetId = single.cues.front().presetId;
	cue.effect = single.cues.front().effect;
	cue.child = single.cues.front().child;
	cue.launchSound = single.cues.front().launchSound;
	cue.burstSound = single.cues.front().burstSound;
	error.clear();
	return true;
}

} // namespace fireworks
