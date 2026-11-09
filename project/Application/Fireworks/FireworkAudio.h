#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Windows.h>
#include <mmreg.h>
#include <xaudio2.h>

#include "Application/Fireworks/FireworkShow.h"

namespace fireworks {

// 演出中の短い効果音を重ねて再生する。XAudio2 本体の所有権は main にある。
class FireworkAudio {
public:
	~FireworkAudio();
	void Initialize(IXAudio2* engine);
	void Play(const FireworkSound& sound);
	void SetPaused(bool paused);
	void UpdateFinished();
	void StopAll();
	std::string TakeError();

private:
	struct WaveData {
		WAVEFORMATEXTENSIBLE format{};
		std::vector<std::uint8_t> samples;
	};

	const WaveData* LoadWave(const std::string& path);

	IXAudio2* engine_ = nullptr;
	std::map<std::string, WaveData> waves_;
	std::vector<IXAudio2SourceVoice*> voices_;
	std::string error_;
	bool paused_ = false;
};

} // namespace fireworks
