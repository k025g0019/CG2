#pragma once

#include "EditorScene.h"

#include <array>
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
class EditorPhysicsManager;

struct AudioClip {
	void* pBuffer = nullptr;
	uint32_t bufferSize = 0;
	uint16_t formatTag = 0;
	uint16_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint32_t averageBytesPerSecond = 0;
	uint16_t blockAlign = 0;
	uint16_t bitsPerSample = 0;
};

enum class AudioFilterMode : uint8_t {
	None = 0,
	LowPass,
	HighPass,
};

enum class EditorAudioBus : int32_t {
	Sfx = 0,
	Bgm,
	Ambience,
	Ui,
	Count,
};

struct ActiveAudio {
	int32_t gameObjectId = -1;
	AudioClip* clip = nullptr;
	IXAudio2SourceVoice* voice = nullptr;
	float volume = 1.0f;
	float spatialBlend = 0.0f;
	float minDistance = 1.0f;
	float maxDistance = 50.0f;
	float playbackAge = 0.0f;
	float pitch = 1.0f;
	float occlusion = 0.0f;
	Vector3 previousSourcePosition = {0.0f, 0.0f, 0.0f};
	Vector3 previousListenerPosition = {0.0f, 0.0f, 0.0f};
	int32_t audioBus = static_cast<int32_t>(EditorAudioBus::Sfx);
	bool loop = false;
	bool hasPreviousSpatialPosition = false;
	AudioFilterMode filterMode = AudioFilterMode::None;
	float filterCutoff = 1.0f;
	int32_t filterComponentId = -1;
};

class EditorAudioManager {
public:
	~EditorAudioManager();

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager);
	void Start();
	void Update(float deltaTime);
	void Stop();
	void Stop(int32_t gameObjectId);  // 指定 AudioSource が鳴らしている Voice だけを停止する。
	void Draw();
	bool Play(int32_t gameObjectId);  // AudioSource をイベントから一度再生する。
	void SetPaused(bool isPaused);  // GamePause中は再生位置を維持したまま全Voiceを停止・再開する
	bool IsPaused() const;  // Audio Pause状態を返す
	void SetMasterVolume(float volume);  // 全 AudioSource へ掛ける最終音量を設定する。
	float GetMasterVolume() const;  // 現在の Master 音量を返す。
	void SetBusVolume(EditorAudioBus audioBus, float volume);  // SFX / BGM / Ambience / UI の音量を設定する。
	float GetBusVolume(EditorAudioBus audioBus) const;  // 指定バスの現在音量を返す。

private:
	EditorScene* editorScene_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	IXAudio2* xAudio2_ = nullptr;
	IXAudio2SubmixVoice* reverbSubmix_ = nullptr;
	IUnknown* reverbEffect_ = nullptr;
	std::unordered_map<std::string, AudioClip> clips_;
	std::vector<ActiveAudio> activeAudios_;
	std::unordered_map<int32_t, float> lastPlaybackTimeByGameObjectId_;
	std::array<float, static_cast<size_t>(EditorAudioBus::Count)> busVolumes_ = {
		1.0f,
		0.70f,
		0.80f,
		1.0f};
	float masterVolume_ = 1.0f;
	float playbackClock_ = 0.0f;
	bool isPaused_ = false;  // Stopとは異なりVoiceと再生位置を保持する

	AudioClip* LoadClip(const std::string& path);
	AudioClip* CreateBuiltinClip(const std::string& path);  // builtin:// URI の軽量 PCM 音源を生成する。
	void StopClip(ActiveAudio& audio);
	void ApplyVoiceFilter(ActiveAudio& audio, int32_t filterGameObjectId) const;
	int32_t GetListenerGameObjectId() const;
	Vector3 GetListenerPosition() const;
	Vector3 GetListenerRight() const;  // AudioListener の向きから右方向を返す。
	float GetListenerReverbAmount() const;  // Listenerが入っているReverb Zoneの残響量を返す。
	float GetMixedVolume(const ActiveAudio& audio) const;  // Source、Master、Bus を合成した音量を返す。
};

#pragma warning(pop)
