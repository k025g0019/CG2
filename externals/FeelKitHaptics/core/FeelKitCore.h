#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class FeelKitHaptics;

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

enum class EffectLayout {
	single,
	horizontal,
	vertical,
	grid,
	sequenceFiles
};

struct EffectDesc {
	int textureHandle = 0;
	EffectLayout layout = EffectLayout::single;
	int frameWidth = 0;
	int frameHeight = 0;
	int frameCount = 1;
	int columns = 1;
	int rows = 1;
	float seconds = 0.0f;
	bool loop = false;
};

struct EffectDrawInfo {
	int textureHandle = 0;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	int frameIndex = 0;
	int sourceX = 0;
	int sourceY = 0;
	int sourceWidth = 0;
	int sourceHeight = 0;
};

struct ScreenState {
	Vec2 shakeOffset;
	float shakeRotation = 0.0f;
	float zoomScale = 1.0f;
	float slowMotionScale = 1.0f;
	unsigned int flashColor = 0xFFFFFFFFu;
	float flashAlpha = 0.0f;
	float flashStrength = 1.0f;
	bool isHitStop = false;
};

struct AudioAnalysisResult {
	bool isValid = false;
	float durationSeconds = 0.0f;
	float averageAmplitude = 0.0f;
	float peakAmplitude = 0.0f;
	float lowBandEnergy = 0.0f;
	float highBandEnergy = 0.0f;
	float sampleRate = 0.0f;
};

struct AudioFeatureData {
	bool isValid = false;
	float durationSeconds = 0.0f;
	float averageAmplitude = 0.0f;
	float peakAmplitude = 0.0f;
	float lowBandEnergy = 0.0f;
	float highBandEnergy = 0.0f;
	float sampleRate = 0.0f;
	float envelopeAverage = 0.0f;
	int peakCount = 0;
	float attackStrength = 0.0f;
};

struct AudioFrameData {
	float timeSeconds = 0.0f;
	float amplitude = 0.0f;
	float lowBand = 0.0f;
	float highBand = 0.0f;
};

struct HapticParams {
	float power = 0.0f;
	float seconds = 0.0f;
};

struct ShakeParams {
	float power = 0.0f;
	float seconds = 0.0f;
};

struct FlashParams {
	unsigned int color = 0xFFFFFFFFu;
	float seconds = 0.0f;
	float strength = 0.0f;
};

struct ParticleParams {
	float spawnRate = 1.0f;
	float size = 1.0f;
	float speed = 1.0f;
	float life = 1.0f;
	float radius = 0.5f;
	unsigned int colorRgba = 0xFFFFFFFFu;
};

struct SlowMotionParams {
	float scale = 0.0f;
	float seconds = 0.0f;
};

struct ImpactAnalysisResult {
	float power = 0.0f;
	float seconds = 0.0f;
};

using SoundEffectCallback = std::function<void(const char* path, float volume)>;
using BgmCallback = std::function<void(const char* path, bool loop)>;
using StopBgmCallback = std::function<void()>;
using VibrationCallback = std::function<void(float power, float seconds)>;
using VibrationPatternCallback = std::function<void(const char* patternName)>;
using VibrationFromSoundCallback = std::function<void(const char* soundPath)>;
using EffectDrawCallback = std::function<void(const EffectDrawInfo& drawInfo)>;

struct WaveformDesc {
	float lowFrequency = 0.0f;
	float highFrequency = 0.0f;
	float amplitude = 1.0f;
	float durationMs = 0.0f;
};

using WaveformHandle = int;

struct ParticleEmitterDesc {
	int maxParticles = 100;
	float spawnRate = 10.0f;
	float initialSpeed = 1.0f;
	float lifeSeconds = 1.0f;
	float size = 1.0f;
	float spreadAngle = 0.0f;
	unsigned int colorRgba = 0xFFFFFFFFu;
	bool useGravity = false;
};

using ParticleEmitterHandle = int;

enum class EmitterShape {
	circle,
	sector,
	sphere,
	cylinder,
	box,
	meshSurface
};

struct EmitterDesc {
	EmitterShape shape = EmitterShape::circle;
	float radius = 1.0f;
	float angle = 0.0f;
	float height = 0.0f;
	float delaySeconds = 0.0f;
	float durationSeconds = 0.0f; // 0 = infinite
	ParticleEmitterDesc particleDesc;
};

using EmitterHandle = int;

struct TrailDesc {
	float lifeSeconds = 0.5f;
	float width = 1.0f;
	unsigned int colorRgba = 0xFFFFFFFFu;
	int maxPoints = 100;
};

using TrailHandle = int;

struct AfterImageDesc {
	float intervalSeconds = 0.05f;
	float lifeSeconds = 0.3f;
	float fadeSpeed = 1.0f;
	unsigned int colorRgba = 0xFFFFFFFFu;
};

using AfterImageHandle = int;

struct DecalDesc {
	const char* textureName = nullptr;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float scale = 1.0f;
	float rotation = 0.0f;
	float lifeSeconds = 5.0f;
	int maxDecals = 1000;
};

struct MarkDesc {
	unsigned int colorRgba = 0xFFFFFFFFu;
	float size = 1.0f;
	float lifeSeconds = 10.0f;
};

struct ScreenEffectDesc {
	const char* effectName = nullptr;
	float intensity = 1.0f;
	float durationSeconds = 1.0f;
	unsigned int colorRgba = 0xFFFFFFFFu;
};

enum class MaterialType {
	metal,
	wood,
	earth,
	stone,
	water,
	flesh,
	glass,
	plastic,
	cloth
};

struct SequenceEvent {
	float timeSeconds = 0.0f;
	const char* eventName = nullptr;
	float param1 = 0.0f;
	float param2 = 0.0f;
	float param3 = 0.0f;
};

using TimelineHandle = int;

struct AISenseDebugDesc {
	Vec3 position;
	float sightRange = 0.0f;
	float hearingRange = 0.0f;
	int alertLevel = 0;
};

struct ImpactFeedbackResult {
	float vibrationPower = 0.0f;
	float shakePower = 0.0f;
	float hitStopSeconds = 0.0f;
	float seVolume = 1.0f;
	float particleIntensity = 0.0f;
};

using ImageDataProvider =
	std::function<bool(const char* path,
	                   int& outWidth,
	                   int& outHeight,
	                   std::vector<unsigned char>& outRGBA)>;

using TextureDataProvider =
	std::function<bool(int textureHandle,
	                   int& outWidth,
	                   int& outHeight,
	                   std::vector<unsigned char>& outRGBA)>;

using GifDataProvider =
	std::function<bool(const char* path,
	                   int& outFrameCount,
	                   int& outFrameWidth,
	                   int& outFrameHeight,
	                   std::vector<std::vector<unsigned char>>& outFrames)>;

struct AnimationHapticPattern {
	float continuousPower = 0.0f;
	float continuousDuration = 0.0f;
	float footstepInterval = 0.0f;
	float footstepPower = 0.3f;
	const char* footstepSe = "footstep.wav";
};

struct ExplosionFeedbackResult {
	float vibrationPower = 0.0f;
	float shakePower = 0.0f;
	float seVolume = 1.0f;
	float effectIntensity = 0.0f;
};

using TextDrawCallback = std::function<void(const char* text, float x, float y, float size, unsigned int colorRgba)>;
using FragmentDrawCallback = std::function<void(float x, float y, float vx, float vy, float rotation, unsigned int colorRgba)>;
using ScreenEffectDrawCallback = std::function<void(const char* effectName, float intensity, float durationSeconds, unsigned int colorRgba)>;
using DebugLogCallback = std::function<void(const char* message)>;

namespace FeelKit {
using ::AISenseDebugDesc;
using ::AfterImageDesc;
using ::AudioAnalysisResult;
using ::AudioFeatureData;
using ::AudioFrameData;
using ::FlashParams;
using ::HapticParams;
using ::MaterialType;
using ::ParticleParams;
using ::ShakeParams;
using ::SlowMotionParams;
using ::DecalDesc;
using ::EffectDesc;
using ::EffectDrawInfo;
using ::EffectLayout;
using ::EmitterDesc;
using ::ExplosionFeedbackResult;
using ::ImpactAnalysisResult;
using ::ImpactFeedbackResult;
using ::MarkDesc;
using ::ParticleEmitterDesc;
using ::ScreenEffectDesc;
using ::ScreenState;
using ::SequenceEvent;
using ::AnimationHapticPattern;
using ::GifDataProvider;
using ::ImageDataProvider;
using ::TextureDataProvider;
using ::TrailDesc;
using ::Vec2;
using ::Vec3;
using ::WaveformDesc;
using ::TextDrawCallback;
using ::FragmentDrawCallback;
using ::ScreenEffectDrawCallback;
using ::DebugLogCallback;

struct SoundEffectRequest;
struct BgmSwitchRequest;
struct SpatialSoundRequest;
struct VoiceRequest;
struct ExplosionRequest;
struct SparkRequest;
struct SmokeRequest;
struct ImpactEffectRequest;
struct ParticleRequest;
struct ScreenShakeRequest;
struct ScreenZoomRequest;
struct HitStopRequest;
struct ScreenFlashRequest;
struct ScreenColorChangeRequest;
struct SlowMotionRequest;
struct GamepadVibrationRequest;
struct HdRumbleRequest;
struct DualSenseHapticsRequest;
struct TriggerFeedbackRequest;

using BgmSwitchCallback = std::function<void(const char* bgmPath, int fadeMs, bool shouldLoop)>;
using SpatialSoundCallback =
	std::function<void(const char* soundPath,
	                   float positionX,
	                   float positionY,
	                   float positionZ,
	                   float volume)>;
using VoiceCallback =
	std::function<void(const char* voicePath, const char* speakerName, float volume)>;
using ExplosionCallback =
	std::function<void(const char* effectName,
	                   float positionX,
	                   float positionY,
	                   float positionZ,
	                   float power)>;
using SparkCallback = ExplosionCallback;
using SmokeCallback =
	std::function<void(const char* effectName,
	                   float positionX,
	                   float positionY,
	                   float positionZ,
	                   float power,
	                   int durationMs)>;
using ImpactEffectCallback = ExplosionCallback;
using ParticleCallback =
	std::function<void(const char* particleName,
	                   float positionX,
	                   float positionY,
	                   float positionZ,
	                   int count)>;
using ScreenShakeCallback = std::function<void(float power, int durationMs)>;
using ScreenZoomCallback = std::function<void(float zoomScale, int durationMs)>;
using HitStopFrameCallback = std::function<void(int frameCount)>;
using ScreenFlashCallback =
	std::function<void(unsigned int colorRgba, float strength, int durationMs)>;
using ScreenColorChangeCallback =
	std::function<void(unsigned int colorRgba, float strength, int durationMs)>;
using SlowMotionFrameCallback = std::function<void(float timeScale, int durationMs)>;
using GamepadVibrationCallback =
	std::function<void(float leftStrength, float rightStrength, int durationMs)>;
using HdRumbleCallback =
	std::function<void(float lowFrequency,
	                   float highFrequency,
	                   float amplitude,
	                   int durationMs)>;
using DualSenseHapticsCallback =
	std::function<void(float leftStrength, float rightStrength, int durationMs)>;
using TriggerFeedbackCallback =
	std::function<void(float leftResistance, float rightResistance, int durationMs)>;

namespace Internal {

struct ActiveEffect {
	int id = 0;
	EffectDesc desc;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float elapsedSeconds = 0.0f;
	bool isActive = false;
};

struct DecalEntry {
	DecalDesc desc;
	float lifeRemaining = 0.0f;
	int id = 0;
	bool active = false;
};

struct SequenceEntry {
	std::string name;
	std::vector<SequenceEvent> events;
};

struct TimelineEntry {
	std::vector<SequenceEvent> events;
	std::vector<bool> eventFired;
	bool playing = false;
	float elapsed = 0.0f;
};

struct WaveformEntry {
	WaveformDesc desc;
	bool valid = false;
};

struct EmitterRecord {
	EmitterDesc desc;
	bool active = false;
	float elapsedSeconds = 0.0f;
};

struct ActiveEffectLodRecord {
	int id = 0;
	std::string effectName;
	Vec3 position;
	float distance = 0.0f;
};

struct RecordingBuffer {
	std::vector<std::pair<float, std::string>> entries;
	bool recording = false;
	float startTime = 0.0f;
};

struct CharEffectData {
	char ch = ' ';
	float x = 0.0f;
	float y = 0.0f;
	float rot = 0.0f;
	float scale = 1.0f;
	unsigned int color = 0xFFFFFFFFu;
};

struct TextEffectData {
	std::string text;
	float size = 24.0f;
	float scatter = 0.0f;
	float posX = 0.0f;
	float posY = 0.0f;
	float remainingSeconds = 0.0f;
	float totalSeconds = 0.0f;
	bool active = false;
	std::vector<CharEffectData> chars;
};

struct CollisionRect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

struct FragmentData {
	float x = 0.0f;
	float y = 0.0f;
	float vx = 0.0f;
	float vy = 0.0f;
	float rotation = 0.0f;
	float angularVelocity = 0.0f;
	float lifeSeconds = 0.0f;
	unsigned int color = 0xFFFFFFFFu;
	bool active = false;
};

struct GifFrameSet {
	int textureHandle = 0;
	int frameWidth = 0;
	int frameHeight = 0;
	int frameCount = 0;
	float frameInterval = 0.0f;
	EffectLayout layout = EffectLayout::horizontal;
};

struct TrailPoint {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float remainingSeconds = 0.0f;
};

struct ScreenEffectEntry {
	ScreenEffectDesc desc;
	float remainingSeconds = 0.0f;
	bool active = false;
	int order = 0;
};

extern std::vector<ActiveEffect> g_activeEffects;
extern SoundEffectCallback g_soundEffectCallback;
extern BgmCallback g_bgmCallback;
extern StopBgmCallback g_stopBgmCallback;
extern BgmSwitchCallback g_bgmSwitchCallback;
extern SpatialSoundCallback g_spatialSoundCallback;
extern VoiceCallback g_voiceCallback;
extern VibrationCallback g_vibrationCallback;
extern VibrationPatternCallback g_vibrationPatternCallback;
extern VibrationFromSoundCallback g_vibrationFromSoundCallback;
extern GamepadVibrationCallback g_gamepadVibrationCallback;
extern HdRumbleCallback g_hdRumbleCallback;
extern DualSenseHapticsCallback g_dualSenseHapticsCallback;
extern TriggerFeedbackCallback g_triggerFeedbackCallback;
extern EffectDrawCallback g_effectDrawCallback;
extern ExplosionCallback g_explosionCallback;
extern SparkCallback g_sparkCallback;
extern SmokeCallback g_smokeCallback;
extern ImpactEffectCallback g_impactEffectCallback;
extern ParticleCallback g_particleCallback;
extern ScreenShakeCallback g_screenShakeCallback;
extern ScreenZoomCallback g_screenZoomCallback;
extern HitStopFrameCallback g_hitStopFrameCallback;
extern ScreenFlashCallback g_screenFlashCallback;
extern ScreenColorChangeCallback g_screenColorChangeCallback;
extern SlowMotionFrameCallback g_slowMotionFrameCallback;
extern float g_shakePower;
extern float g_shakeRemaining;
extern float g_shakeTotal;
extern unsigned int g_flashColor;
extern float g_flashRemaining;
extern float g_flashTotal;
extern float g_flashStrength;
extern float g_zoomScale;
extern float g_zoomRemaining;
extern float g_zoomTotal;
extern float g_hitStopRemaining;
extern float g_slowMoScale;
extern float g_slowMoRemaining;
extern float g_slowMoTotal;
extern float g_masterVolume;
extern int g_nextEffectId;
extern std::unordered_map<std::string, EffectDesc> g_effectRegistry;

extern std::vector<DecalEntry> g_decalPool;
extern int g_nextDecalId;
extern std::unordered_map<std::string, SequenceEntry> g_sequenceRegistry;
extern std::unordered_map<int, TimelineEntry> g_timelines;
extern int g_nextTimelineId;
extern std::unordered_map<int, WaveformEntry> g_waveformRegistry;
extern int g_nextWaveformId;
extern std::unordered_map<int, EmitterRecord> g_emitters;
extern int g_nextEmitterId;
extern std::unordered_map<int, ActiveEffectLodRecord> g_activeEffectsLod;
extern int g_nextEffectLodId;
extern RecordingBuffer g_recordingBuffer;
extern std::vector<std::string> g_hapticTimelineLog;

using TimelineEventCallback = std::function<void(float timeSeconds, const char* eventName, float param1, float param2, float param3)>;
extern TimelineEventCallback g_timelineEventCallback;
extern std::unordered_map<int, TrailDesc> g_trails;
extern std::unordered_map<int, AfterImageDesc> g_afterImages;
extern int g_nextTrailId;
extern int g_nextAfterImageId;
extern float g_globalSequenceTime;
extern FeelKitHaptics* g_hapticsBackend;

// Enhanced state for new systems
extern std::vector<TextEffectData> g_textEffects;
extern std::vector<CollisionRect> g_collisionRects;
extern std::vector<FragmentData> g_fragments;
extern std::vector<GifFrameSet> g_gifFrames;
extern std::vector<ScreenEffectEntry> g_screenEffects;
extern std::unordered_map<int, std::vector<TrailPoint>> g_trailPoints;

extern ImageDataProvider g_imageDataProvider;
extern TextureDataProvider g_textureDataProvider;
extern GifDataProvider g_gifDataProvider;
extern std::unordered_map<std::string, AnimationHapticPattern> g_animationPatterns;

extern std::unordered_map<std::string, AudioFeatureData> g_audioFeatureCache;
extern float g_occlusionFactor;
extern TextDrawCallback g_textDrawCallback;
extern FragmentDrawCallback g_fragmentDrawCallback;
extern ScreenEffectDrawCallback g_screenEffectDrawCallback;
extern DebugLogCallback g_debugLogCallback;

float clampFloat(float value, float minValue, float maxValue);
int clampInt(int value, int minValue, int maxValue);
float vec3Length(Vec3 v);
void updateTimer(float& remaining, float deltaTime);
int getCurrentFrameIndex(const ActiveEffect& effect);
EffectDrawInfo makeDrawInfo(const ActiveEffect& effect);
void advanceTimelines(float deltaTime);

AudioFeatureData readAudioFileFeatures(const char* path);
std::vector<AudioFrameData> extractAudioFrames(const char* path, float windowSeconds = 0.1f);
float materialMultiplier(MaterialType material);

} // namespace Internal

void Init();
void Update(float deltaTime);
void Draw();
void Shutdown();

void PlaySequence(const char* sequenceName);
void PlaySequence(const SequenceEvent* events, int eventCount);
TimelineHandle Timeline();
void TimelineAddEvent(TimelineHandle handle, float timeSeconds, const char* eventName);
void TimelinePlay(TimelineHandle handle);
void TimelineStop(TimelineHandle handle);

void StartEffectRecording();
void ReplayEffect();

// New setters for draw and debug callbacks
void SetTextDrawCallback(TextDrawCallback cb);
void SetFragmentDrawCallback(FragmentDrawCallback cb);
void SetScreenEffectDrawCallback(ScreenEffectDrawCallback cb);
void SetDebugLogCallback(DebugLogCallback cb);

// Occlusion factor
float GetOcclusionFactor();
void SetOcclusionFactor(float factor);

// Query APIs for active state
int GetTextEffectCount();
const Internal::TextEffectData* GetTextEffect(int index);
int GetFragmentCount();
const Internal::FragmentData* GetFragment(int index);
int GetScreenEffectCount();
const Internal::ScreenEffectEntry* GetScreenEffect(int index);
int GetTrailCount();
TrailHandle GetTrailHandle(int index);

} // namespace FeelKit
