#include "Application/Fireworks/FireworkPlayer.h"
#include "Application/Fireworks/FireworkPresets.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "3d/ParticleEffect.h"

namespace fireworks {
namespace {

bool StillAlive(int elapsedSteps, float lifeSeconds) {
	return static_cast<double>(elapsedSteps) * kStepSeconds < lifeSeconds;
}

std::mt19937 MakeRandom(const FireworkCue& cue, int relativeFrame) {
	// キューと発生フレームから乱数を作るので、シークしても同じ火花を復元できる。
	constexpr std::uint32_t kFrameSalt = 0x9E3779B9u;
	const auto frame = static_cast<std::uint32_t>(relativeFrame);
	return std::mt19937(cue.seed ^ (kFrameSalt * (frame + 1)));
}

Vector3 LaunchVelocity(const FireworkCue& cue) {
	// 左右と前後の角度から上向きの単位方向を作り、打上速度を掛ける。
	const float radians = std::numbers::pi_v<float> / 180.0f;
	const Vector3 direction = Normalize({
		std::tan(cue.effect.launchSideAngleDegrees * radians),
		1.0f,
		std::tan(cue.effect.launchDepthAngleDegrees * radians)
	});
	return direction * cue.effect.launchSpeed;
}

ParticleEffect::ParticleParameters MakeSpark(const FireworkCue& cue, const Vector3& position) {
	ParticleEffect::ParticleParameters particle{};
	particle.position = position;
	particle.acceleration = {0.0f, -cue.effect.gravity, 0.0f};
	particle.scale = {cue.effect.sparkWidth, cue.effect.sparkWidth, cue.effect.sparkWidth};
	particle.color = cue.effect.color;
	particle.lifeTime = cue.effect.sparkLife;
	particle.trailSeconds = cue.effect.trailSeconds;
	return particle;
}

void EmitFountain(ParticleEffect& particles, const FireworkCue& cue,
                  const FireworkPreset& preset, int relativeFrame, int elapsedSteps) {
	const EffectParameters& effect = cue.effect;
	if (!StillAlive(elapsedSteps, effect.sparkLife)) {
		return;
	}

	// 端数を次フレームに持ち越し、60で割り切れない発生数にも対応する。
	const int rate = effect.particleCount;
	const int count = ((relativeFrame + 1) * rate) / kFramesPerSecond
		- (relativeFrame * rate) / kFramesPerSecond;
	if (count == 0) {
		return;
	}

	ParticleEffect::EmitParameters emission{};
	emission.count = static_cast<std::size_t>(count);
	emission.particle = MakeSpark(cue, cue.position);
	emission.particle.velocity = LaunchVelocity(cue);
	emission.positionJitter = {preset.originJitter, preset.originJitter, preset.originJitter};
	emission.velocityJitter = {effect.spreadSpeed, effect.speedJitter, effect.spreadSpeed};
	emission.scaleJitter = {effect.widthJitter, effect.widthJitter, effect.widthJitter};
	emission.colorJitter = {preset.colorJitter.x, preset.colorJitter.y, preset.colorJitter.z, 0.0f};
	auto random = MakeRandom(cue, relativeFrame);
	particles.Emit(emission, random, elapsedSteps, kStepSeconds);
}

void EmitShell(ParticleEffect& particles, const FireworkCue& cue, int riseFrames, int elapsedSteps) {
	if (elapsedSteps >= riseFrames) {
		return;
	}

	auto shell = MakeSpark(cue, cue.position);
	shell.velocity = LaunchVelocity(cue);
	shell.scale = {cue.effect.launchTrailWidth,
		cue.effect.launchTrailWidth, cue.effect.launchTrailWidth};
	shell.color = cue.effect.launchTrailColor;
	shell.lifeTime = riseFrames * kStepSeconds;
	particles.Spawn(shell, elapsedSteps, kStepSeconds);
}

Vector3 ShellPosition(const FireworkCue& cue, int relativeFrame) {
	// ParticleEffect::Advance と同じ積分式を使い、火花と開花位置を発射体に合わせる。
	const float seconds = relativeFrame * kStepSeconds;
	Vector3 position = cue.position + LaunchVelocity(cue) * seconds;
	position.y -= 0.5f * cue.effect.gravity * seconds * (seconds + kStepSeconds);
	return position;
}

void EmitLaunchTrail(ParticleEffect& particles, const FireworkCue& cue,
	                 int relativeFrame, int elapsedSteps) {
	const EffectParameters& effect = cue.effect;
	if (relativeFrame == 0 || effect.launchTrailRate == 0
		|| effect.launchTrailLifeSeconds <= 0.0f || effect.launchTrailWidth <= 0.0f
		|| !StillAlive(elapsedSteps, effect.launchTrailLifeSeconds)) {
		return;
	}

	const int emittedBefore = (relativeFrame - 1) * effect.launchTrailRate / kFramesPerSecond;
	if (relativeFrame * effect.launchTrailRate / kFramesPerSecond == emittedBefore) {
		return;
	}
	// 線を間引いたときも、前に描いた位置からつないで隙間を作らない。
	const int previousFrame = emittedBefore == 0 ? 0
		: (emittedBefore * kFramesPerSecond + effect.launchTrailRate - 1) / effect.launchTrailRate;
	const Vector3 previous = ShellPosition(cue, previousFrame);
	const Vector3 current = ShellPosition(cue, relativeFrame);
	const Vector3 step = current - previous;
	// 1フレームの移動区間を1本の板ポリにする。位置と向きを固定して光の線だけを残す。
	ParticleEffect::ParticleParameters segment = MakeSpark(cue, previous + step * 0.5f);
	segment.acceleration = {};
	segment.scale = {
		effect.launchTrailWidth, effect.launchTrailWidth, effect.launchTrailWidth
	};
	segment.lifeTime = effect.launchTrailLifeSeconds;
	segment.color = effect.launchTrailColor;
	segment.lineDirection = step;
	segment.lineLength = Length(step) + effect.launchTrailOverlap;
	particles.Spawn(segment, elapsedSteps, kStepSeconds);
}

ParticleEffect::ParticleParameters MakeBurstSpark(const FireworkCue& cue,
	const FireworkPreset& preset, int riseFrames, int index,
	std::mt19937& random, std::uniform_real_distribution<float>& variation) {
	const EffectParameters& effect = cue.effect;
	auto spark = MakeSpark(cue, ShellPosition(cue, riseFrames));
	if (preset.pattern == FireworkPattern::CrownChrysanthemum) {
		// 開花直後は菊と同じ球状。後半だけ横方向を減速させて光を垂らす。
		spark.dragStartSeconds = effect.crownDroopStartSeconds;
		spark.horizontalDrag = effect.crownHorizontalDrag;
	}
	const auto vary = [&random, &variation](float value, float jitter) {
		return value + variation(random) * jitter;
	};
	const Vector3 direction = preset.burstDirection(index, effect.particleCount, effect);
	if (preset.pattern == FireworkPattern::Willow) {
		// 柳は弱い放出速度を基準に、各軸を独立に少しずらして枝の落ち方を変える。
		spark.velocity = {
			direction.x * effect.spreadSpeed + variation(random) * effect.speedJitter,
			direction.y * effect.spreadSpeed + variation(random) * effect.speedJitter,
			direction.z * effect.spreadSpeed + variation(random) * effect.speedJitter
		};
		spark.fadeStartSeconds = effect.willowGlowHoldSeconds;
	} else {
		const float speed = vary(effect.spreadSpeed, effect.speedJitter);
		spark.velocity = direction * speed;
	}
	const float width = vary(effect.sparkWidth, effect.widthJitter);
	spark.scale = {width, width, width};
	spark.color = {
		std::clamp(vary(effect.color.x, preset.colorJitter.x), 0.0f, 1.0f),
		std::clamp(vary(effect.color.y, preset.colorJitter.y), 0.0f, 1.0f),
		std::clamp(vary(effect.color.z, preset.colorJitter.z), 0.0f, 1.0f),
		effect.color.w
	};
	return spark;
}

void EmitBurst(ParticleEffect& particles, const FireworkCue& cue, const FireworkPreset& preset,
               int riseFrames, int elapsedSteps) {
	if (!StillAlive(elapsedSteps, cue.effect.sparkLife)) {
		return;
	}
	auto random = MakeRandom(cue, riseFrames);
	std::uniform_real_distribution<float> variation(-1.0f, 1.0f);
	for (int index = 0; index < cue.effect.particleCount; ++index) {
		const auto spark = MakeBurstSpark(cue, preset, riseFrames, index, random, variation);
		if (!particles.Spawn(spark, elapsedSteps, kStepSeconds)) {
			break;
		}
	}
}

Vector3 BurstPosition(const ParticleEffect::ParticleParameters& spark, int elapsedFrames) {
	const float seconds = elapsedFrames * kStepSeconds;
	return spark.position + spark.velocity * seconds
		+ spark.acceleration * (0.5f * seconds * (seconds + kStepSeconds));
}

void EmitFallingTrails(ParticleEffect& particles, const FireworkCue& cue,
	const FireworkPreset& preset, int riseFrames, int relativeFrame,
	int elapsedSteps, float trailLifeSeconds) {
	const int ageFrames = relativeFrame - riseFrames;
	if (ageFrames <= 0 || ageFrames > ToFrames(cue.effect.sparkLife)
		|| !StillAlive(elapsedSteps, trailLifeSeconds)) {
		return;
	}

	// 椰子の太い枝と柳の細い枝を、各星が通った曲線として残す。
	auto random = MakeRandom(cue, riseFrames);
	std::uniform_real_distribution<float> variation(-1.0f, 1.0f);
	for (int index = 0; index < cue.effect.particleCount; ++index) {
		const auto spark = MakeBurstSpark(cue, preset, riseFrames, index, random, variation);
		const Vector3 previous = BurstPosition(spark, ageFrames - 1);
		const Vector3 current = BurstPosition(spark, ageFrames);
		const Vector3 step = current - previous;
		if (Length(step) <= 0.0f) {
			continue;
		}
		auto segment = MakeSpark(cue, previous + step * 0.5f);
		segment.acceleration = {};
		segment.scale = spark.scale;
		segment.color = spark.color;
		segment.lifeTime = trailLifeSeconds;
		segment.trailSeconds = 0.0f;
		segment.lineDirection = step;
		segment.lineLength = Length(step);
		if (!particles.Spawn(segment, elapsedSteps, kStepSeconds)) {
			break;
		}
	}
}

int ChildFuseFrames(const FireworkCue& cue, int riseFrames, int childIndex) {
	// シーク後も同じ小玉が同じ時刻に開くよう、親玉と小玉番号で固定する。
	auto random = MakeRandom(cue, riseFrames + childIndex + 1);
	std::uniform_real_distribution<float> offset(-cue.child.fuseSpreadSeconds,
		cue.child.fuseSpreadSeconds);
	return ToFrames(cue.child.fuseSeconds + offset(random));
}

Vector4 ChildColor(const FireworkCue& cue, int riseFrames, int childIndex) {
	auto random = MakeRandom(cue, riseFrames + childIndex + 1);
	std::uniform_real_distribution<float> variation(-cue.child.colorVariation,
		cue.child.colorVariation);
	return {
		std::clamp(cue.child.color.x + variation(random), 0.0f, 1.0f),
		std::clamp(cue.child.color.y + variation(random), 0.0f, 1.0f),
		std::clamp(cue.child.color.z + variation(random), 0.0f, 1.0f),
		cue.child.color.w
	};
}

Vector3 ChildVelocity(const FireworkCue& cue, const FireworkPreset& preset, int childIndex) {
	return preset.burstDirection(childIndex, cue.child.count, cue.effect) * cue.child.travelSpeed;
}

Vector3 ChildPosition(const FireworkCue& cue, const FireworkPreset& preset,
                      int riseFrames, int childIndex, int fuseFrames) {
	const float seconds = fuseFrames * kStepSeconds;
	Vector3 position = ShellPosition(cue, riseFrames)
		+ ChildVelocity(cue, preset, childIndex) * seconds;
	position.y -= 0.5f * cue.child.gravity * seconds * (seconds + kStepSeconds);
	return position;
}

void EmitChildShells(ParticleEffect& particles, const FireworkCue& cue,
                     const FireworkPreset& preset, int riseFrames, int elapsedSteps) {
	// 親玉が割れた瞬間に小玉を飛ばし、開花まで光の尾を見せる。
	for (int childIndex = 0; childIndex < cue.child.count; ++childIndex) {
		const int fuseFrames = ChildFuseFrames(cue, riseFrames, childIndex);
		auto shell = MakeSpark(cue, ShellPosition(cue, riseFrames));
		shell.velocity = ChildVelocity(cue, preset, childIndex);
		shell.acceleration = {0.0f, -cue.child.gravity, 0.0f};
		shell.scale = {cue.child.shellWidth, cue.child.shellWidth, cue.child.shellWidth};
		shell.color = ChildColor(cue, riseFrames, childIndex);
		shell.lifeTime = fuseFrames * kStepSeconds;
		shell.trailSeconds = cue.child.shellTrailSeconds;
		particles.Spawn(shell, elapsedSteps, kStepSeconds);
	}
}

void EmitChildBursts(ParticleEffect& particles, const FireworkCue& cue,
                     const FireworkPreset& preset, int riseFrames,
                     int relativeFrame, int elapsedSteps) {
	// 各小玉が移動した位置を中心に、その小玉自身の設定で球状に開く。
	if (!StillAlive(elapsedSteps, cue.child.sparkLife)) {
		return;
	}
	for (int childIndex = 0; childIndex < cue.child.count; ++childIndex) {
		const int fuseFrames = ChildFuseFrames(cue, riseFrames, childIndex);
		if (relativeFrame != riseFrames + fuseFrames) {
			continue;
		}
		ParticleEffect::ParticleParameters spark{};
		spark.position = ChildPosition(cue, preset, riseFrames, childIndex, fuseFrames);
		spark.acceleration = {0.0f, -cue.child.gravity, 0.0f};
		spark.scale = {cue.child.sparkWidth, cue.child.sparkWidth, cue.child.sparkWidth};
		spark.color = ChildColor(cue, riseFrames, childIndex);
		spark.lifeTime = cue.child.sparkLife;
		spark.trailSeconds = cue.child.trailSeconds;
		for (int index = 0; index < cue.child.particleCount; ++index) {
			spark.velocity = preset.burstDirection(index, cue.child.particleCount, cue.effect)
				* cue.child.burstSpeed;
			if (!particles.Spawn(spark, elapsedSteps, kStepSeconds)) {
				return;
			}
		}
	}
}

void EmitFrame(ParticleEffect& particles, const FireworkShow& show, int frame, int elapsedSteps) {
	if (frame >= show.durationFrames) {
		return;
	}

	for (const FireworkCue& cue : show.cues) {
		if (particles.GetParticleCount() >= kMaxLiveParticles) {
			break;
		}
		const int relativeFrame = frame - cue.startFrame;
		const int stageFrames = ToFrames(cue.effect.stageSeconds);
		const FireworkPreset* preset = FindPreset(cue.presetId);
		if (preset == nullptr || relativeFrame < 0) {
			continue;
		}
		int extraFrames = 0;
		if (preset->pattern == FireworkPattern::ThousandChrysanthemum) {
			extraFrames = ToFrames(cue.child.fuseSeconds + cue.child.fuseSpreadSeconds);
		} else if (preset->pattern == FireworkPattern::Palm
			|| preset->pattern == FireworkPattern::Willow) {
			extraFrames = ToFrames(cue.effect.sparkLife);
		}
		if (relativeFrame > stageFrames + extraFrames) {
			continue;
		}

		if (!preset->IsAerial()) {
			if (relativeFrame < stageFrames) {
				EmitFountain(particles, cue, *preset, relativeFrame, elapsedSteps);
			}
		} else {
			if (relativeFrame <= stageFrames) {
				if (relativeFrame == 0) {
					EmitShell(particles, cue, stageFrames, elapsedSteps);
				}
				EmitLaunchTrail(particles, cue, relativeFrame, elapsedSteps);
			}
			if (preset->pattern == FireworkPattern::ThousandChrysanthemum) {
				if (relativeFrame == stageFrames) {
					EmitBurst(particles, cue, *preset, stageFrames, elapsedSteps);
					EmitChildShells(particles, cue, *preset, stageFrames, elapsedSteps);
				}
				EmitChildBursts(particles, cue, *preset,
					stageFrames, relativeFrame, elapsedSteps);
			} else if (preset->pattern == FireworkPattern::Palm
				|| preset->pattern == FireworkPattern::Willow) {
				if (relativeFrame == stageFrames) {
					EmitBurst(particles, cue, *preset, stageFrames, elapsedSteps);
				}
				const float trailLifeSeconds = preset->pattern == FireworkPattern::Palm
					? cue.effect.palmBranchLifeSeconds : cue.effect.willowTrailLifeSeconds;
				EmitFallingTrails(particles, cue, *preset,
					stageFrames, relativeFrame, elapsedSteps, trailLifeSeconds);
			} else if (relativeFrame == stageFrames) {
				EmitBurst(particles, cue, *preset, stageFrames, elapsedSteps);
			}
		}
	}
}

} // namespace

FireworkPlayer::FireworkPlayer(ParticleEffect& particles) : particles_(particles) {
}

bool FireworkPlayer::Update(const FireworkShow& show, double deltaSeconds) {
	soundCues_.clear();
	timelineJumped_ = timelineJumpPending_;
	timelineJumpPending_ = false;
	bool changed = needsRebuild_;
	if (needsRebuild_) {
		Rebuild(show);
	}
	if (playing_ && startSoundPending_) {
		QueueSoundsAtFrame(show, currentFrame_);
		startSoundPending_ = false;
	}
	if (!playing_) {
		return changed;
	}

	// 長時間中断したときも、1フレームの更新量を抑えて操作に戻れるようにする。
	accumulatedSeconds_ += std::clamp(deltaSeconds, 0.0, 0.25);
	while (accumulatedSeconds_ >= kStepSeconds) {
		accumulatedSeconds_ -= kStepSeconds;
		if (currentFrame_ >= show.durationFrames) {
			if (!loop_) {
				SetPlaying(false);
				break;
			}
			currentFrame_ = 0;
			particles_.Clear();
			timelineJumped_ = true;
		} else {
			++currentFrame_;
			particles_.Advance(kStepSeconds);
		}
		EmitFrame(particles_, show, currentFrame_, 0);
		QueueSoundsAtFrame(show, currentFrame_);
		changed = true;
		if (!loop_ && currentFrame_ == show.durationFrames) {
			SetPlaying(false);
			break;
		}
	}
	return changed;
}

void FireworkPlayer::Seek(int frame) {
	if (currentFrame_ != frame) {
		currentFrame_ = frame;
		accumulatedSeconds_ = 0.0;
		startSoundPending_ = true;
		Refresh();
	}
}

void FireworkPlayer::Refresh() {
	needsRebuild_ = true;
	timelineJumpPending_ = true;
}

void FireworkPlayer::SetPlaying(bool playing) {
	playing_ = playing;
	accumulatedSeconds_ = 0.0;
}

void FireworkPlayer::TogglePlayback(const FireworkShow& show) {
	if (!playing_ && currentFrame_ >= show.durationFrames) {
		Seek(0);
	}
	SetPlaying(!playing_);
}

void FireworkPlayer::Restart() {
	Seek(0);
	timelineJumpPending_ = true;
	startSoundPending_ = true;
	SetPlaying(true);
}

void FireworkPlayer::QueueSoundsAtFrame(const FireworkShow& show, int frame) {
	if (frame >= show.durationFrames) {
		return;
	}
	for (std::size_t index = 0; index < show.cues.size(); ++index) {
		const FireworkCue& cue = show.cues[index];
		if (!cue.launchSound.path.empty()
			&& cue.startFrame + ToFrames(cue.launchSound.delaySeconds) == frame) {
			soundCues_.push_back({index, false});
		}
		if (!cue.burstSound.path.empty()
			&& cue.startFrame + ToFrames(cue.effect.stageSeconds)
				+ ToFrames(cue.burstSound.delaySeconds) == frame) {
			soundCues_.push_back({index, true});
		}
	}
}

void FireworkPlayer::Rebuild(const FireworkShow& show) {
	particles_.Clear();
	currentFrame_ = std::clamp(currentFrame_, 0, show.durationFrames);

	// すでに寿命が切れた発生は再生しない。残っている粒子だけを現在の年齢で生成する。
	const int historyFrames = ToFrames((std::max)(kMaxSparkLife
		+ (std::max)(kMaxPalmBranchLifeSeconds, kMaxWillowTrailLifeSeconds),
		kMaxRiseSeconds));
	const int firstFrame = (std::max)(0, currentFrame_ - historyFrames);
	for (int frame = firstFrame; frame <= currentFrame_; ++frame) {
		EmitFrame(particles_, show, frame, currentFrame_ - frame);
		if (particles_.GetParticleCount() >= kMaxLiveParticles) {
			break;
		}
	}
	needsRebuild_ = false;
}

} // namespace fireworks
