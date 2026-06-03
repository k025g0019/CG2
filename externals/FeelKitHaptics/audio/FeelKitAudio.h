#pragma once

#include "core/FeelKitCore.h"

namespace FeelKit {

struct SoundEffectRequest {
	std::string soundPath;
	float volume = 1.0f;
};

struct BgmSwitchRequest {
	std::string bgmPath;
	int fadeMs = 0;
	bool shouldLoop = true;
};

struct SpatialSoundRequest {
	std::string soundPath;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float volume = 1.0f;
};

struct VoiceRequest {
	std::string voicePath;
	std::string speakerName;
	float volume = 1.0f;
};

void SetSoundEffectCallback(const SoundEffectCallback& callback);
void SetBgmCallback(const BgmCallback& callback);
void SetStopBgmCallback(const StopBgmCallback& callback);
void SetBgmSwitchCallback(const BgmSwitchCallback& callback);
void SetSpatialSoundCallback(const SpatialSoundCallback& callback);
void SetVoiceCallback(const VoiceCallback& callback);

AudioAnalysisResult AnalyzeAudioFile(const char* soundPath);
AudioFeatureData AnalyzeAudioFeatures(const char* soundPath);
WaveformHandle LoadWaveform(const WaveformDesc& waveformDesc);

namespace Internal {
bool readWav(const char* path, int& outSampleRate, std::vector<float>& outSamples);
} // namespace Internal

// 素材特徴 → 用途別パラメータ変換
HapticParams MakeHapticsFromAudio(const AudioFeatureData& feature);
ShakeParams MakeShakeFromAudio(const AudioFeatureData& feature);
FlashParams MakeFlashFromAudio(const AudioFeatureData& feature);
ParticleParams MakeParticlesFromAudio(const AudioFeatureData& feature);
SlowMotionParams MakeSlowMotionFromAudio(const AudioFeatureData& feature);

bool PlaySoundEffect(const SoundEffectRequest& request);
bool PlayBgmSwitch(const BgmSwitchRequest& request);
bool PlaySpatialSound(const SpatialSoundRequest& request);
bool PlayVoice(const VoiceRequest& request);

void PlaySE(const char* path);
void PlaySE(const char* path, float volume);
void PlayBGM(const char* path, bool loop);
void StopBGM();
void SetMasterVolume(float volume);
float GetMasterVolume();

void PlayHapticFromAudio(const char* soundPath);
void PlayShakeFromAudio(const char* soundPath);
void PlayFlashFromAudio(const char* soundPath, unsigned int color = 0xFFFFFFFFu);
void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec2 pos);
void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec3 pos);
void CreateLightFromAudio(const char* soundPath);
void CreateCameraMotionFromAudio(const char* soundPath);
void CreateSlowMotionFromAudio(const char* soundPath);

} // namespace FeelKit
