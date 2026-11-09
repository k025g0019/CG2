#pragma once

#include <cstddef>
#include <vector>

#include "Application/Fireworks/FireworkShow.h"

class ParticleEffect;

namespace fireworks {

class FireworkPlayer {
public:
	struct SoundCue {
		std::size_t cueIndex;
		bool burst;
	};

	explicit FireworkPlayer(ParticleEffect& particles);

	bool Update(const FireworkShow& show, double deltaSeconds);
	void Seek(int frame);
	void Refresh();
	void SetPlaying(bool playing);
	void TogglePlayback(const FireworkShow& show);
	void Restart();

	int CurrentFrame() const { return currentFrame_; }
	bool IsPlaying() const { return playing_; }
	bool& Loop() { return loop_; }
	const std::vector<SoundCue>& SoundCues() const { return soundCues_; }
	bool TimelineJumped() const { return timelineJumped_; }

private:
	void Rebuild(const FireworkShow& show);
	void QueueSoundsAtFrame(const FireworkShow& show, int frame);

	ParticleEffect& particles_;
	int currentFrame_ = 0;
	double accumulatedSeconds_ = 0.0;
	bool playing_ = true;
	bool loop_ = true;
	bool needsRebuild_ = true;
	bool startSoundPending_ = true;
	bool timelineJumpPending_ = false;
	bool timelineJumped_ = false;
	std::vector<SoundCue> soundCues_;
};

} // namespace fireworks
