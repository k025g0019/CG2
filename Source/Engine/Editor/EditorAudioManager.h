#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

struct IUnknown;
struct IXAudio2;
struct IXAudio2SourceVoice;
struct IXAudio2SubmixVoice;

struct AudioClip {
	void* pBuffer = nullptr;
	uint32_t bufferSize = 0;
	uint16_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;
};

enum class AudioFilterMode : uint8_t {
	None = 0,
	LowPass,
	HighPass,
};

struct ActiveAudio {
	int32_t gameObjectId = -1;
	AudioClip* clip = nullptr;
	IXAudio2SourceVoice* voice = nullptr;
	float volume = 1.0f;
	float spatialBlend = 0.0f;
	float minDistance = 1.0f;
	float maxDistance = 50.0f;
	bool loop = false;
	AudioFilterMode filterMode = AudioFilterMode::None;
	float filterCutoff = 1.0f;
	int32_t filterComponentId = -1;
};

class EditorAudioManager {
public:
	void Initialize(EditorScene* editorScene);
	void Start();
	void Update(float deltaTime);
	void Stop();
	void Draw();

private:
	EditorScene* editorScene_ = nullptr;
	IXAudio2* xAudio2_ = nullptr;
	IXAudio2SubmixVoice* reverbSubmix_ = nullptr;
	IUnknown* reverbEffect_ = nullptr;
	std::unordered_map<std::string, AudioClip> clips_;
	std::vector<ActiveAudio> activeAudios_;

	AudioClip* LoadClip(const std::string& path);
	void StopClip(ActiveAudio& audio);
	void ApplyVoiceFilter(ActiveAudio& audio, int32_t filterGameObjectId) const;
	Vector3 GetListenerPosition() const;
};

#pragma warning(pop)
