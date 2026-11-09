#include "Application/Fireworks/FireworkAudio.h"
#include "motion/StringUtility.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace fireworks {
namespace {

constexpr std::streamoff kMaxWaveBytes = 100 * 1024 * 1024;

bool IsChunk(const char* id, const char* expected) {
	return std::memcmp(id, expected, 4) == 0;
}

} // namespace

FireworkAudio::~FireworkAudio() {
	StopAll();
}

void FireworkAudio::Initialize(IXAudio2* engine) {
	engine_ = engine;
}

const FireworkAudio::WaveData* FireworkAudio::LoadWave(const std::string& path) {
	if (auto found = waves_.find(path); found != waves_.end()) {
		return &found->second;
	}
	const std::wstring widePath = StringUtility::ConvertString(path);
	if (widePath.empty()) {
		error_ = "効果音のファイル名が正しくありません";
		return nullptr;
	}
	std::ifstream input(std::filesystem::path(widePath), std::ios::binary | std::ios::ate);
	const std::streamoff fileBytes = input ? static_cast<std::streamoff>(input.tellg()) : -1;
	if (fileBytes < 12 || fileBytes > kMaxWaveBytes) {
		error_ = "効果音のWAVを読み込めませんでした";
		return nullptr;
	}
	input.seekg(0);
	char riff[12]{};
	if (!input.read(riff, sizeof(riff)) || !IsChunk(riff, "RIFF")
		|| !IsChunk(riff + 8, "WAVE")) {
		error_ = "効果音はRIFF形式のWAVを指定してください";
		return nullptr;
	}

	WaveData wave;
	bool foundFormat = false;
	std::uint32_t formatBytes = 0;
	while (input) {
		char id[4]{};
		std::uint32_t bytes = 0;
		if (!input.read(id, sizeof(id))
			|| !input.read(reinterpret_cast<char*>(&bytes), sizeof(bytes))) {
			break;
		}
		if (bytes > static_cast<std::uint32_t>(kMaxWaveBytes)) {
			break;
		}
		if (IsChunk(id, "fmt ")) {
			if (bytes < 16 || bytes > sizeof(wave.format)
				|| !input.read(reinterpret_cast<char*>(&wave.format), bytes)) {
				break;
			}
			foundFormat = true;
			formatBytes = bytes;
		} else if (IsChunk(id, "data")) {
			wave.samples.resize(bytes);
			if (bytes == 0 || !input.read(reinterpret_cast<char*>(wave.samples.data()), bytes)) {
				break;
			}
		} else {
			input.seekg(bytes, std::ios::cur);
		}
		if ((bytes & 1u) != 0) {
			input.seekg(1, std::ios::cur);
		}
		if (foundFormat && !wave.samples.empty()) {
			break;
		}
	}
	const WAVEFORMATEX& format = wave.format.Format;
	const bool supported = format.wFormatTag == WAVE_FORMAT_PCM
		|| format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE
			&& formatBytes >= sizeof(WAVEFORMATEXTENSIBLE) && format.cbSize >= 22);
	if (!foundFormat || wave.samples.empty() || !supported || format.nChannels == 0
		|| format.nSamplesPerSec == 0 || format.nBlockAlign == 0
		|| wave.samples.size() % format.nBlockAlign != 0) {
		error_ = "効果音のWAV形式に対応していません";
		return nullptr;
	}
	return &waves_.emplace(path, std::move(wave)).first->second;
}

void FireworkAudio::Play(const FireworkSound& sound) {
	if (engine_ == nullptr || paused_ || sound.path.empty()) {
		return;
	}
	const WaveData* wave = LoadWave(sound.path);
	if (wave == nullptr) {
		return;
	}
	IXAudio2SourceVoice* voice = nullptr;
	if (FAILED(engine_->CreateSourceVoice(&voice, &wave->format.Format)) || voice == nullptr) {
		error_ = "効果音の再生準備に失敗しました";
		return;
	}
	voice->SetVolume(sound.volume);
	XAUDIO2_BUFFER buffer{};
	buffer.AudioBytes = static_cast<UINT32>(wave->samples.size());
	buffer.pAudioData = wave->samples.data();
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0))) {
		voice->DestroyVoice();
		error_ = "効果音を再生できませんでした";
		return;
	}
	voices_.push_back(voice);
}

void FireworkAudio::SetPaused(bool paused) {
	if (paused_ == paused) {
		return;
	}
	paused_ = paused;
	for (IXAudio2SourceVoice* voice : voices_) {
		if (paused) {
			voice->Stop(0);
		} else {
			voice->Start(0);
		}
	}
}

void FireworkAudio::UpdateFinished() {
	for (auto it = voices_.begin(); it != voices_.end();) {
		XAUDIO2_VOICE_STATE state{};
		(*it)->GetState(&state);
		if (state.BuffersQueued == 0) {
			(*it)->DestroyVoice();
			it = voices_.erase(it);
		} else {
			++it;
		}
	}
}

void FireworkAudio::StopAll() {
	for (IXAudio2SourceVoice* voice : voices_) {
		voice->Stop(0);
		voice->DestroyVoice();
	}
	voices_.clear();
}

std::string FireworkAudio::TakeError() {
	return std::exchange(error_, {});
}

} // namespace fireworks
