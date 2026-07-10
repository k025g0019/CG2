#define _CRT_SECURE_NO_WARNINGS

#include "FeelKitAudio.h"

#include "../effects/FeelKitEffects.h"
#include "../haptics/FeelKitVibration.h"
#include "../screen/FeelKitScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace FeelKit {
namespace {

struct WavInfo {
	bool valid = false;
	int channels = 0;
	int sampleRate = 0;
	int bitsPerSample = 0;
	int totalSamples = 0;
	std::vector<float> samples;
};

static WavInfo readWavInternal(const char* path) {
	WavInfo info;

	if (path == nullptr) {
		return info;
	}

	FILE* fileHandle = std::fopen(path, "rb");

	if (fileHandle == nullptr) {
		return info;
	}

	auto read4 = [&]() -> unsigned int {
		unsigned char bytes[4] = {};

		if (std::fread(bytes, 1, 4, fileHandle) != 4) {
			return 0u;
		}

		return static_cast<unsigned int>(bytes[0]) |
		       (static_cast<unsigned int>(bytes[1]) << 8) |
		       (static_cast<unsigned int>(bytes[2]) << 16) |
		       (static_cast<unsigned int>(bytes[3]) << 24);
	};

	auto read2 = [&]() -> unsigned short {
		unsigned char bytes[2] = {};

		if (std::fread(bytes, 1, 2, fileHandle) != 2) {
			return 0u;
		}

		return static_cast<unsigned short>(bytes[0]) |
		       (static_cast<unsigned short>(bytes[1]) << 8);
	};

	char riff[5] = {};

	if (std::fread(riff, 1, 4, fileHandle) != 4 || std::memcmp(riff, "RIFF", 4) != 0) {
		std::fclose(fileHandle);
		return info;
	}

	read4();

	char wave[5] = {};

	if (std::fread(wave, 1, 4, fileHandle) != 4 || std::memcmp(wave, "WAVE", 4) != 0) {
		std::fclose(fileHandle);
		return info;
	}

	int formatChannels = 0;
	int formatSampleRate = 0;
	int formatBitsPerSample = 0;
	bool foundFormat = false;

	while (true) {
		char chunkId[5] = {};

		if (std::fread(chunkId, 1, 4, fileHandle) != 4) {
			break;
		}

		const unsigned int chunkSize = read4();

		if (std::memcmp(chunkId, "fmt ", 4) == 0) {
			read2();
			formatChannels = read2();
			formatSampleRate = static_cast<int>(read4());
			read4();
			read2();
			formatBitsPerSample = read2();
			foundFormat = true;

			if (chunkSize > 16u) {
				const unsigned short extraSize = read2();
				if (std::fseek(fileHandle, static_cast<long>(extraSize) - 2L, SEEK_CUR) != 0) {
					break;
				}
			}
		} else if (std::memcmp(chunkId, "data", 4) == 0) {
			if (!foundFormat) {
				break;
			}

			info.channels = formatChannels;
			info.sampleRate = formatSampleRate;
			info.bitsPerSample = formatBitsPerSample;
			const long dataBytes = static_cast<long>(chunkSize);
			const long dataSamples = dataBytes / (formatBitsPerSample / 8);
			info.totalSamples = dataSamples / formatChannels;
			info.samples.reserve(static_cast<size_t>(info.totalSamples));

			for (long sampleIndex = 0; sampleIndex < dataSamples; sampleIndex++) {
				float sample = 0.0f;

				if (formatBitsPerSample == 8) {
					const int byteValue = std::fgetc(fileHandle);

					if (byteValue == EOF) {
						break;
					}

					sample = (static_cast<float>(byteValue) - 128.0f) / 128.0f;
				} else if (formatBitsPerSample == 16) {
					const unsigned char lowByte = static_cast<unsigned char>(std::fgetc(fileHandle));
					const unsigned char highByte = static_cast<unsigned char>(std::fgetc(fileHandle));

					if (std::feof(fileHandle)) {
						break;
					}

					const short sampleValue = static_cast<short>(
						static_cast<unsigned short>(lowByte) |
						(static_cast<unsigned short>(highByte) << 8));
					sample = static_cast<float>(sampleValue) / 32768.0f;
				} else {
					std::fseek(fileHandle, formatBitsPerSample / 8, SEEK_CUR);
				}

				if ((sampleIndex % formatChannels) == 0) {
					info.samples.push_back(sample);
				} else {
					info.samples.back() = (info.samples.back() + sample) * 0.5f;
				}
			}

			info.valid = true;
			break;
		} else if (chunkSize > 0u) {
			std::fseek(fileHandle, static_cast<long>(chunkSize), SEEK_CUR);
		}
	}

	std::fclose(fileHandle);
	return info;
}

static WavInfo cachedRead(const char* path) {
	return readWavInternal(path);
}

} // namespace

namespace Internal {
bool readWav(const char* path, int& outSampleRate, std::vector<float>& outSamples) {
	WavInfo info = cachedRead(path);
	if (!info.valid || info.samples.empty()) return false;
	outSampleRate = info.sampleRate;
	outSamples = info.samples;
	return true;
}
} // namespace Internal

WaveformHandle LoadWaveform(const WaveformDesc& waveformDesc) {
	int id = Internal::g_nextWaveformId++;
	Internal::WaveformEntry entry;
	entry.desc = waveformDesc;
	entry.valid = true;
	Internal::g_waveformRegistry[id] = entry;
	return id;
}

void SetSoundEffectCallback(const SoundEffectCallback& callback) {
	Internal::g_soundEffectCallback = callback;
}

void SetBgmCallback(const BgmCallback& callback) {
	Internal::g_bgmCallback = callback;
}

void SetStopBgmCallback(const StopBgmCallback& callback) {
	Internal::g_stopBgmCallback = callback;
}

void SetBgmSwitchCallback(const BgmSwitchCallback& callback) {
	Internal::g_bgmSwitchCallback = callback;
}

void SetSpatialSoundCallback(const SpatialSoundCallback& callback) {
	Internal::g_spatialSoundCallback = callback;
}

void SetVoiceCallback(const VoiceCallback& callback) {
	Internal::g_voiceCallback = callback;
}

AudioAnalysisResult AnalyzeAudioFile(const char* soundPath) {
	AudioAnalysisResult result = {};
	const WavInfo wavInfo = readWavInternal(soundPath);

	if (!wavInfo.valid) {
		return result;
	}

	result.isValid = true;
	result.sampleRate = static_cast<float>(wavInfo.sampleRate);
	result.durationSeconds =
		static_cast<float>(wavInfo.totalSamples) / static_cast<float>(wavInfo.sampleRate);

	if (wavInfo.samples.empty()) {
		return result;
	}

	double sumAbs = 0.0;
	double peak = 0.0;
	double lowSum = 0.0;
	double highSum = 0.0;

	for (size_t sampleIndex = 0; sampleIndex < wavInfo.samples.size(); sampleIndex++) {
		const float sample = wavInfo.samples[sampleIndex];
		const float absoluteSample = std::fabs(sample);
		sumAbs += static_cast<double>(absoluteSample);

		if (static_cast<double>(absoluteSample) > peak) {
			peak = static_cast<double>(absoluteSample);
		}

		const int window = static_cast<int>(result.sampleRate * 0.02f);
		const int start = std::max(0, static_cast<int>(sampleIndex) - window);
		const int end = std::min(
			static_cast<int>(wavInfo.samples.size()) - 1,
			static_cast<int>(sampleIndex) + window);
		float localAverage = 0.0f;
		int sampleCount = 0;

		for (int windowIndex = start; windowIndex <= end; windowIndex++) {
			localAverage += std::fabs(wavInfo.samples[windowIndex]);
			sampleCount++;
		}

		if (sampleCount > 0) {
			localAverage /= static_cast<float>(sampleCount);
		}

		lowSum += static_cast<double>(localAverage);
		const float difference =
			(sampleIndex > 0u) ? std::fabs(sample - wavInfo.samples[sampleIndex - 1u]) : 0.0f;
		highSum += static_cast<double>(difference);
	}

	const double sampleTotal = static_cast<double>(wavInfo.samples.size());
	result.averageAmplitude = static_cast<float>(sumAbs / sampleTotal);
	result.peakAmplitude = static_cast<float>(peak);
	result.lowBandEnergy = static_cast<float>(lowSum / sampleTotal);
	result.highBandEnergy = static_cast<float>(highSum / sampleTotal);
	return result;
}

//================================================================
// AnalyzeAudioFeatures: 拡張特徴量解析
//================================================================

AudioFeatureData AnalyzeAudioFeatures(const char* soundPath) {
	AudioFeatureData data = {};

	AudioAnalysisResult base = AnalyzeAudioFile(soundPath);
	if (!base.isValid) return data;

	data.isValid = true;
	data.durationSeconds = base.durationSeconds;
	data.averageAmplitude = base.averageAmplitude;
	data.peakAmplitude = base.peakAmplitude;
	data.lowBandEnergy = base.lowBandEnergy;
	data.highBandEnergy = base.highBandEnergy;
	data.sampleRate = base.sampleRate;

	// 追加特徴量の計算のために再度 WAV を読む
	const WavInfo wavInfo = readWavInternal(soundPath);
	if (!wavInfo.valid || wavInfo.samples.empty()) return data;

	// エンベロープ平均
	double envSum = 0.0;
	for (size_t i = 0; i < wavInfo.samples.size(); i++) {
		float env = std::fabs(wavInfo.samples[i]);
		// 簡易平滑化
		if (i > 0) {
			env = (env + std::fabs(wavInfo.samples[i - 1])) * 0.5f;
		}
		envSum += static_cast<double>(env);
	}
	data.envelopeAverage = static_cast<float>(envSum / static_cast<double>(wavInfo.samples.size()));

	// ピーク検出（単純なしきい値ベース）
	int windowSize = static_cast<int>(wavInfo.sampleRate * 0.05f);
	if (windowSize < 1) windowSize = 1;
	int peakCount = 0;
	float threshold = data.averageAmplitude * 1.5f;
	for (int i = windowSize; i + windowSize < static_cast<int>(wavInfo.samples.size()); i += windowSize) {
		float localMax = 0.0f;
		for (int j = -windowSize; j <= windowSize; j++) {
			float absVal = std::fabs(wavInfo.samples[i + j]);
			if (absVal > localMax) localMax = absVal;
		}
		if (localMax > threshold) {
			peakCount++;
		}
	}
	data.peakCount = peakCount;

	// アタック強度（短時間のエネルギー上昇率の平均）
	double attackSum = 0.0;
	int attackCount = 0;
	int attackWindow = static_cast<int>(wavInfo.sampleRate * 0.01f);
	if (attackWindow < 1) attackWindow = 1;
	for (int i = attackWindow; i + attackWindow < static_cast<int>(wavInfo.samples.size()); i++) {
		float before = 0.0f, after = 0.0f;
		for (int j = 0; j < attackWindow; j++) {
			before += std::fabs(wavInfo.samples[i - attackWindow + j]);
			after += std::fabs(wavInfo.samples[i + j]);
		}
		before /= static_cast<float>(attackWindow);
		after /= static_cast<float>(attackWindow);
		if (after > before) {
			attackSum += static_cast<double>(after - before);
			attackCount++;
		}
	}
	data.attackStrength = (attackCount > 0)
		? static_cast<float>(attackSum / static_cast<double>(attackCount))
		: 0.0f;

	return data;
}

//================================================================
// 用途別パラメータ変換
//================================================================

HapticParams MakeHapticsFromAudio(const AudioFeatureData& feature) {
	HapticParams p;
	p.power = Internal::clampFloat(
		feature.averageAmplitude * 1.5f +
			feature.peakAmplitude * 0.5f +
			feature.lowBandEnergy * 0.8f,
		0.0f, 1.0f);
	p.seconds = Internal::clampFloat(feature.durationSeconds * 0.8f, 0.05f, 5.0f);
	return p;
}

ShakeParams MakeShakeFromAudio(const AudioFeatureData& feature) {
	ShakeParams p;
	p.power = Internal::clampFloat(
		feature.lowBandEnergy * 2.0f + feature.averageAmplitude * 0.5f,
		0.0f, 10.0f);
	p.seconds = Internal::clampFloat(feature.durationSeconds * 0.6f, 0.05f, 3.0f);
	return p;
}

FlashParams MakeFlashFromAudio(const AudioFeatureData& feature) {
	FlashParams p;
	p.color = 0xFFFFFFFFu;
	p.strength = Internal::clampFloat(feature.peakAmplitude * 1.2f, 0.0f, 1.0f);
	p.seconds = Internal::clampFloat(feature.durationSeconds * 0.3f, 0.05f, 1.0f);
	return p;
}

ParticleParams MakeParticlesFromAudio(const AudioFeatureData& feature) {
	ParticleParams p;
	p.spawnRate = 5.0f + feature.averageAmplitude * 30.0f;
	p.size = 4.0f + feature.lowBandEnergy * 16.0f;
	p.speed = 1.0f + feature.highBandEnergy * 4.0f;
	p.life = std::max(feature.durationSeconds * 0.5f, 0.1f);
	p.radius = 0.5f + feature.averageAmplitude * 2.0f;
	int r = Internal::clampInt(static_cast<int>(128.0f + feature.lowBandEnergy * 127.0f), 0, 255);
	int g = Internal::clampInt(static_cast<int>(100.0f + feature.highBandEnergy * 155.0f), 0, 255);
	int b = Internal::clampInt(static_cast<int>(200.0f - feature.averageAmplitude * 100.0f), 0, 255);
	p.colorRgba = 0xFF000000u | (static_cast<unsigned int>(r) << 16) |
	              (static_cast<unsigned int>(g) << 8) | static_cast<unsigned int>(b);
	return p;
}

SlowMotionParams MakeSlowMotionFromAudio(const AudioFeatureData& feature) {
	SlowMotionParams p;
	p.scale = 1.0f - Internal::clampFloat(
		feature.peakAmplitude * 0.5f + feature.averageAmplitude * 0.2f,
		0.0f, 0.8f);
	p.seconds = Internal::clampFloat(feature.durationSeconds * 0.5f, 0.1f, 3.0f);
	return p;
}

//================================================================
// 従来の Play* / Create*（共通解析 + 変換 + 実行 の合成）
//================================================================

void PlayHapticFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	HapticParams p = MakeHapticsFromAudio(feature);
	Vibrate(p.power, p.seconds);
}

void PlayShakeFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	auto frames = Internal::extractAudioFrames(soundPath, 0.1f);
	if (frames.empty()) {
		ShakeParams p = MakeShakeFromAudio(feature);
		Shake(p.power, p.seconds);
		return;
	}
	TimelineHandle tl = Timeline();
	for (size_t i = 0; i < frames.size(); i++) {
		float power = Internal::clampFloat(frames[i].lowBand * 3.0f + frames[i].amplitude, 0.0f, 10.0f);
		if (power < 0.3f) continue;
		float dur = (i + 1 < frames.size()) ? (frames[i + 1].timeSeconds - frames[i].timeSeconds) : 0.1f;
		SequenceEvent ev;
		ev.timeSeconds = frames[i].timeSeconds;
		ev.eventName = "shake";
		ev.param1 = power;
		ev.param2 = dur;
		TimelineAddEvent(tl, ev.timeSeconds, ev.eventName);
		auto& entry = Internal::g_timelines[tl];
		if (!entry.events.empty()) {
			entry.events.back().param1 = power;
			entry.events.back().param2 = dur;
		}
	}
	TimelinePlay(tl);
}

void PlayFlashFromAudio(const char* soundPath, unsigned int color) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	auto frames = Internal::extractAudioFrames(soundPath, 0.1f);
	if (frames.empty()) {
		FlashParams p = MakeFlashFromAudio(feature);
		if (color != 0xFFFFFFFFu) p.color = color;
		Flash(p.color, p.seconds, p.strength);
		return;
	}
	TimelineHandle tl = Timeline();
	for (size_t i = 0; i < frames.size(); i++) {
		float strength = Internal::clampFloat(frames[i].amplitude * 2.0f, 0.0f, 1.0f);
		if (strength < 0.2f) continue;
		float dur = (i + 1 < frames.size()) ? (frames[i + 1].timeSeconds - frames[i].timeSeconds) : 0.1f;
		SequenceEvent ev;
		ev.timeSeconds = frames[i].timeSeconds;
		ev.eventName = "flash";
		ev.param1 = strength;
		ev.param2 = dur;
		ev.param3 = (color != 0xFFFFFFFFu) ? static_cast<float>(color) : 0xFFFFFFFFu;
		TimelineAddEvent(tl, ev.timeSeconds, ev.eventName);
		auto& entry = Internal::g_timelines[tl];
		if (!entry.events.empty()) {
			entry.events.back().param1 = strength;
			entry.events.back().param2 = dur;
			entry.events.back().param3 = ev.param3;
		}
	}
	TimelinePlay(tl);
}

void CreateLightFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	FlashParams p = MakeFlashFromAudio(feature);
	float brightness = Internal::clampFloat(
		feature.peakAmplitude * 1.5f + feature.highBandEnergy * 0.5f,
		0.0f, 1.0f);
	Flash(0xFFFFFFFFu, p.seconds, brightness);
}

void CreateCameraMotionFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	auto frames = Internal::extractAudioFrames(soundPath, 0.1f);
	if (frames.empty()) {
		ShakeParams shakeP = MakeShakeFromAudio(feature);
		Shake(shakeP.power, shakeP.seconds);
		float zoomScale = 1.0f + Internal::clampFloat(feature.peakAmplitude * 0.3f, 0.0f, 0.5f);
		Zoom(zoomScale, shakeP.seconds);
		return;
	}
	TimelineHandle tl = Timeline();
	for (size_t i = 0; i < frames.size(); i++) {
		float power = Internal::clampFloat(frames[i].lowBand * 3.0f + frames[i].amplitude, 0.0f, 10.0f);
		if (power < 0.3f) continue;
		float dur = (i + 1 < frames.size()) ? (frames[i + 1].timeSeconds - frames[i].timeSeconds) : 0.1f;
		SequenceEvent evShake;
		evShake.timeSeconds = frames[i].timeSeconds;
		evShake.eventName = "shake";
		evShake.param1 = power;
		evShake.param2 = dur;
		TimelineAddEvent(tl, evShake.timeSeconds, evShake.eventName);
		auto& entry = Internal::g_timelines[tl];
		entry.events.back().param1 = power;
		entry.events.back().param2 = dur;
		// add zoom event at same time
		float zoomScale = 1.0f + Internal::clampFloat(frames[i].highBand * 0.5f, 0.0f, 0.5f);
		SequenceEvent evZoom;
		evZoom.timeSeconds = frames[i].timeSeconds;
		evZoom.eventName = "zoom";
		evZoom.param1 = zoomScale;
		evZoom.param2 = dur;
		TimelineAddEvent(tl, evZoom.timeSeconds, evZoom.eventName);
		entry.events.back().param1 = zoomScale;
		entry.events.back().param2 = dur;
	}
	TimelinePlay(tl);
}

void CreateSlowMotionFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	auto frames = Internal::extractAudioFrames(soundPath, 0.1f);
	if (frames.empty()) {
		SlowMotionParams p = MakeSlowMotionFromAudio(feature);
		SlowMotion(p.scale, p.seconds);
		return;
	}
	TimelineHandle tl = Timeline();
	for (size_t i = 0; i < frames.size(); i++) {
		float scale = 1.0f - Internal::clampFloat(frames[i].amplitude * 1.5f + frames[i].lowBand * 0.5f, 0.0f, 0.8f);
		float dur = (i + 1 < frames.size()) ? (frames[i + 1].timeSeconds - frames[i].timeSeconds) : 0.1f;
		SequenceEvent ev;
		ev.timeSeconds = frames[i].timeSeconds;
		ev.eventName = "slow_motion";
		ev.param1 = scale;
		ev.param2 = dur;
		TimelineAddEvent(tl, ev.timeSeconds, ev.eventName);
		auto& entry = Internal::g_timelines[tl];
		if (!entry.events.empty()) {
			entry.events.back().param1 = scale;
			entry.events.back().param2 = dur;
		}
	}
	TimelinePlay(tl);
}

void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec2 pos) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;
	EffectDesc adjusted = desc;
	adjusted.seconds = std::max(desc.seconds, feature.durationSeconds * 0.5f);
	adjusted.loop = false;
	PlayEffect(adjusted, pos);
}

void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec3 pos) {
	SpawnEffectFromAudio(desc, soundPath, Vec2{pos.x, pos.y});
}

bool PlaySoundEffect(const SoundEffectRequest& request) {
	if (request.soundPath.empty()) {
		return false;
	}

	PlaySE(request.soundPath.c_str(), request.volume);
	return true;
}

bool PlayBgmSwitch(const BgmSwitchRequest& request) {
	if (request.bgmPath.empty()) {
		return false;
	}

	if (Internal::g_bgmSwitchCallback) {
		Internal::g_bgmSwitchCallback(
			request.bgmPath.c_str(),
			request.fadeMs,
			request.shouldLoop);
		return true;
	}

	PlayBGM(request.bgmPath.c_str(), request.shouldLoop);
	return true;
}

bool PlaySpatialSound(const SpatialSoundRequest& request) {
	if (request.soundPath.empty()) {
		return false;
	}

	if (!Internal::g_spatialSoundCallback) {
		return false;
	}

	Internal::g_spatialSoundCallback(
		request.soundPath.c_str(),
		request.positionX,
		request.positionY,
		request.positionZ,
		Internal::clampFloat(request.volume, 0.0f, 1.0f) * Internal::g_masterVolume);
	return true;
}

bool PlayVoice(const VoiceRequest& request) {
	if (request.voicePath.empty()) {
		return false;
	}

	if (!Internal::g_voiceCallback) {
		return false;
	}

	Internal::g_voiceCallback(
		request.voicePath.c_str(),
		request.speakerName.c_str(),
		Internal::clampFloat(request.volume, 0.0f, 1.0f) * Internal::g_masterVolume);
	return true;
}

void PlaySE(const char* path) {
	PlaySE(path, 1.0f);
}

void PlaySE(const char* path, float volume) {
	if (!Internal::g_soundEffectCallback) {
		return;
	}

	Internal::g_soundEffectCallback(
		path,
		Internal::clampFloat(volume, 0.0f, 1.0f) * Internal::g_masterVolume);
}

void PlayBGM(const char* path, bool loop) {
	if (!Internal::g_bgmCallback) {
		return;
	}

	Internal::g_bgmCallback(path, loop);
}

void StopBGM() {
	if (!Internal::g_stopBgmCallback) {
		return;
	}

	Internal::g_stopBgmCallback();
}

void SetMasterVolume(float volume) {
	Internal::g_masterVolume = Internal::clampFloat(volume, 0.0f, 1.0f);
}

float GetMasterVolume() {
	return Internal::g_masterVolume;
}

} // namespace FeelKit