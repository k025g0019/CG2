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

// Audio Import SettingsのUI表示用。AudioClip自体は再生に必要な生Bufferまで持つため、
// 表示専用の軽量な構造体を別に用意する。
struct AudioClipInfo {
	bool isLoaded = false;
	int32_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;
	float durationSeconds = 0.0f;
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
	Voice,  // セリフ・ナレーション用。既存のSfx/Bgm/Ambience/Uiのindexは変えずに追加している
	        // (Scene保存はaudioBusを素のintで書くため、既存Sceneの再生バス指定を壊さないため)。
	Count,
};

// Script APIが個別Voiceを指すための安定Handle。値0は無効。
// 内部のIXAudio2SourceVoice*をScriptへ渡さないため、再生ごとに発番した通し番号で参照する。
using AudioVoiceHandle = uint64_t;
constexpr AudioVoiceHandle kInvalidAudioVoiceHandle = 0ULL;

struct ActiveAudio {
	// Voiceの寿命はこのActiveAudio自身が持つ(voice != nullptrの間だけ有効。破棄はStopClipに一本化)。
	// clipはEditorAudioManager::clips_内の実体を指す非所有ポインタ。unordered_mapは要素を
	// erase()しない限り参照・ポインタが無効化されないため、InvalidateClipが「先に該当Voiceを
	// 全停止してからclips_.erase()する」順序さえ守れば、ここがダングリングになることはない。
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
	AudioVoiceHandle handle = kInvalidAudioVoiceHandle;
	// AudioSource Componentを持たない単発再生(Scriptの位置指定・2D再生)。
	// trueの間はUpdateがComponentを参照せず、detachedPositionだけで距離減衰とPanを決める。
	bool isDetached = false;
	Vector3 detachedPosition{0.0f, 0.0f, 0.0f};
	bool isVoicePaused = false;  // Script個別のPause。全体Pause(isPaused_)とは独立に保持する
	// Scriptが明示指定した値はComponent値で毎フレーム上書きしない。
	bool hasScriptVolume = false;
	bool hasScriptPitch = false;
	float fadeRemainingSeconds = 0.0f;
	float fadeTotalSeconds = 0.0f;
	float fadeStartVolume = 1.0f;
	float fadeTargetVolume = 1.0f;
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

	// --- Script向け個別Voice操作 ---
	// 無効Handle、再生終了済みHandleは全てfalse(または無効値)を返すだけで、Crashしない。
	AudioVoiceHandle PlayWithHandle(int32_t gameObjectId);  // Play と同じ再生を行い、操作用Handleを返す。
	// AudioSource Componentを介さずClipを直接再生する。spatialBlend>0なら位置で距離減衰する。
	AudioVoiceHandle PlayClipAtPosition(
		const std::string& assetPath,
		const Vector3& position,
		int32_t audioBus,
		float volume,
		bool loop,
		float spatialBlend);
	bool StopVoice(AudioVoiceHandle handle);
	bool SetVoicePaused(AudioVoiceHandle handle, bool isPaused);
	bool IsVoicePlaying(AudioVoiceHandle handle) const;  // Handleが生存し、かつPause中でなければ true。
	bool IsVoiceValid(AudioVoiceHandle handle) const;  // Pause中も含め、Voiceがまだ生存しているか。
	bool SetVoiceVolume(AudioVoiceHandle handle, float volume);
	bool GetVoiceVolume(AudioVoiceHandle handle, float& volume) const;
	bool SetVoicePitch(AudioVoiceHandle handle, float pitch);
	bool GetVoicePitch(AudioVoiceHandle handle, float& pitch) const;
	bool SetVoiceLoop(AudioVoiceHandle handle, bool loop);
	bool GetVoiceLoop(AudioVoiceHandle handle, bool& loop) const;
	bool SetVoicePosition(AudioVoiceHandle handle, const Vector3& position);  // 位置指定再生のWorld座標を更新する。
	bool SetVoiceBus(AudioVoiceHandle handle, int32_t audioBus);
	bool GetVoicePlaybackPosition(AudioVoiceHandle handle, float& seconds) const;
	bool SetVoicePlaybackPosition(AudioVoiceHandle handle, float seconds);
	bool GetVoiceDuration(AudioVoiceHandle handle, float& seconds) const;
	// durationSeconds秒かけてtargetVolumeへ音量を寄せる。0以下なら即時反映。
	// targetVolume=0のFade Outでは、到達時にVoiceを停止する。
	bool FadeVoiceTo(AudioVoiceHandle handle, float targetVolume, float durationSeconds);
	void InvalidateClip(const std::string& assetPath);  // 外部更新された音声を停止・破棄し、次回再生時に新しい内容を読む
	// Audio Import Settingsの表示用。読み込み済みで無ければ一度読み込みを試みる(失敗時はisLoaded=false)。
	AudioClipInfo GetClipInfo(const std::string& path);
	// Import Settingsのpreload指定を実際に反映する。Playと違い音を鳴らさず、Clipを事前に読み込むだけ。
	bool PreloadClip(const std::string& path);
	void SetPaused(bool isPaused);  // GamePause中は再生位置を維持したまま全Voiceを停止・再開する
	bool IsPaused() const;  // Audio Pause状態を返す
	void SetMasterVolume(float volume);  // 全 AudioSource へ掛ける最終音量を設定する。
	float GetMasterVolume() const;  // 現在の Master 音量を返す。
	void SetBusVolume(EditorAudioBus audioBus, float volume);  // SFX / BGM / Ambience / UI / Voice の音量を設定する。
	float GetBusVolume(EditorAudioBus audioBus) const;  // 指定バスの現在音量を返す。
	void SetMasterMute(bool isMuted);  // Master単位でのMute。Volumeの値自体は保持したまま無音にする。
	bool IsMasterMuted() const;
	void SetBusMute(EditorAudioBus audioBus, bool isMuted);  // 指定バスをMuteする。
	bool IsBusMuted(EditorAudioBus audioBus) const;
	// 全体で同時に鳴らせるVoice数の上限。同一SEの大量再生でVoiceが無制限に増えるのを防ぐ。
	// ハードコードせずInspectorから調整できるようにしている。
	void SetMaxGlobalVoiceCount(int32_t maxVoiceCount);
	int32_t GetMaxGlobalVoiceCount() const;
	// Diagnostics 表示用。今フレームで実際に鳴っている Voice 数と、読み込み済み Clip 数。
	int32_t GetActiveVoiceCount() const;
	int32_t GetLoadedClipCount() const;

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
		1.0f,
		1.0f};
	std::array<bool, static_cast<size_t>(EditorAudioBus::Count)> busMutes_ = {};
	float masterVolume_ = 1.0f;
	bool masterMuted_ = false;
	float playbackClock_ = 0.0f;
	bool isPaused_ = false;  // Stopとは異なりVoiceと再生位置を保持する
	// 既定値はUnityの"Real Voices"やFMODの既定同時発音数を参考にした一般的な上限。
	// GameObjectごとのaudioMaxVoicesとは別に、Scene全体で鳴っているVoice総数を制限する。
	int32_t maxGlobalVoiceCount_ = 64;
	AudioVoiceHandle nextVoiceHandle_ = 1ULL;  // 0を無効値として予約するため1から発番する

	ActiveAudio* FindVoice(AudioVoiceHandle handle);
	const ActiveAudio* FindVoice(AudioVoiceHandle handle) const;
	// ActiveAudioの設定済み内容からIXAudio2 Voiceを作り、activeAudios_へ登録してHandleを返す。
	AudioVoiceHandle StartVoice(ActiveAudio& prepared, float initialPitch);
	void UpdateDetachedVoice(ActiveAudio& audio);  // Componentを持たないVoiceの距離減衰とPanを更新する。
	void UpdateVoiceFade(ActiveAudio& audio, float deltaTime);  // Fade進行を反映し、Fade Out完了Voiceを停止対象にする。
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
