#define _CRT_SECURE_NO_WARNINGS

#include "FeelKitCore.h"

#include "../audio/FeelKitAudio.h"

#include <algorithm>
#include <cmath>

namespace FeelKit {
namespace Internal {

std::vector<ActiveEffect> g_activeEffects;
SoundEffectCallback g_soundEffectCallback;
BgmCallback g_bgmCallback;
StopBgmCallback g_stopBgmCallback;
BgmSwitchCallback g_bgmSwitchCallback;
SpatialSoundCallback g_spatialSoundCallback;
VoiceCallback g_voiceCallback;
VibrationCallback g_vibrationCallback;
VibrationPatternCallback g_vibrationPatternCallback;
VibrationFromSoundCallback g_vibrationFromSoundCallback;
GamepadVibrationCallback g_gamepadVibrationCallback;
HdRumbleCallback g_hdRumbleCallback;
DualSenseHapticsCallback g_dualSenseHapticsCallback;
TriggerFeedbackCallback g_triggerFeedbackCallback;
EffectDrawCallback g_effectDrawCallback;
ExplosionCallback g_explosionCallback;
SparkCallback g_sparkCallback;
SmokeCallback g_smokeCallback;
ImpactEffectCallback g_impactEffectCallback;
ParticleCallback g_particleCallback;
ScreenShakeCallback g_screenShakeCallback;
ScreenZoomCallback g_screenZoomCallback;
HitStopFrameCallback g_hitStopFrameCallback;
ScreenFlashCallback g_screenFlashCallback;
ScreenColorChangeCallback g_screenColorChangeCallback;
SlowMotionFrameCallback g_slowMotionFrameCallback;
float g_shakePower = 0.0f;
float g_shakeRemaining = 0.0f;
float g_shakeTotal = 0.0f;
unsigned int g_flashColor = 0xFFFFFFFFu;
float g_flashRemaining = 0.0f;
float g_flashTotal = 0.0f;
float g_flashStrength = 1.0f;
float g_zoomScale = 1.0f;
float g_zoomRemaining = 0.0f;
float g_zoomTotal = 0.0f;
float g_hitStopRemaining = 0.0f;
float g_slowMoScale = 1.0f;
float g_slowMoRemaining = 0.0f;
float g_slowMoTotal = 0.0f;
float g_masterVolume = 1.0f;
int g_nextEffectId = 1;
std::unordered_map<std::string, EffectDesc> g_effectRegistry;

std::vector<DecalEntry> g_decalPool;
int g_nextDecalId = 1;
std::unordered_map<std::string, SequenceEntry> g_sequenceRegistry;
std::unordered_map<int, TimelineEntry> g_timelines;
int g_nextTimelineId = 1;
std::unordered_map<int, WaveformEntry> g_waveformRegistry;
int g_nextWaveformId = 1;
std::unordered_map<int, EmitterRecord> g_emitters;
int g_nextEmitterId = 1;
std::unordered_map<int, ActiveEffectLodRecord> g_activeEffectsLod;
int g_nextEffectLodId = 1;
RecordingBuffer g_recordingBuffer;
std::vector<std::string> g_hapticTimelineLog;
std::unordered_map<int, TrailDesc> g_trails;
std::unordered_map<int, AfterImageDesc> g_afterImages;
int g_nextTrailId = 1;
int g_nextAfterImageId = 1;
float g_globalSequenceTime = 0.0f;
FeelKitHaptics* g_hapticsBackend = nullptr;

std::vector<TextEffectData> g_textEffects;
std::vector<CollisionRect> g_collisionRects;
std::vector<FragmentData> g_fragments;
std::vector<GifFrameSet> g_gifFrames;
std::vector<ScreenEffectEntry> g_screenEffects;
std::unordered_map<int, std::vector<TrailPoint>> g_trailPoints;

ImageDataProvider g_imageDataProvider;
TextureDataProvider g_textureDataProvider;
GifDataProvider g_gifDataProvider;
std::unordered_map<std::string, AnimationHapticPattern> g_animationPatterns;
TimelineEventCallback g_timelineEventCallback;

std::unordered_map<std::string, AudioFeatureData> g_audioFeatureCache;
float g_occlusionFactor = 1.0f;
TextDrawCallback g_textDrawCallback;
FragmentDrawCallback g_fragmentDrawCallback;
ScreenEffectDrawCallback g_screenEffectDrawCallback;
DebugLogCallback g_debugLogCallback;

float clampFloat(float value, float minValue, float maxValue) {
	if (value < minValue) {
		return minValue;
	}

	if (value > maxValue) {
		return maxValue;
	}

	return value;
}

int clampInt(int value, int minValue, int maxValue) {
	if (value < minValue) {
		return minValue;
	}

	if (value > maxValue) {
		return maxValue;
	}

	return value;
}

float vec3Length(Vec3 v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void updateTimer(float& remaining, float deltaTime) {
	if (remaining <= 0.0f) {
		return;
	}

	remaining -= deltaTime;

	if (remaining < 0.0f) {
		remaining = 0.0f;
	}
}

int getCurrentFrameIndex(const ActiveEffect& effect) {
	const int frameCount = clampInt(effect.desc.frameCount, 1, 100000);

	if (effect.desc.seconds <= 0.0f) {
		return frameCount - 1;
	}

	const float normalizedTime = effect.elapsedSeconds / effect.desc.seconds;
	int frameIndex = static_cast<int>(std::floor(normalizedTime * static_cast<float>(frameCount)));

	if (effect.desc.loop) {
		frameIndex = frameIndex % frameCount;
	} else {
		frameIndex = clampInt(frameIndex, 0, frameCount - 1);
	}

	return frameIndex;
}

EffectDrawInfo makeDrawInfo(const ActiveEffect& effect) {
	EffectDrawInfo drawInfo;
	drawInfo.textureHandle = effect.desc.textureHandle;
	drawInfo.x = effect.x;
	drawInfo.y = effect.y;
	drawInfo.z = effect.z;
	drawInfo.frameIndex = getCurrentFrameIndex(effect);
	drawInfo.sourceWidth = effect.desc.frameWidth;
	drawInfo.sourceHeight = effect.desc.frameHeight;

	const int columns = clampInt(effect.desc.columns, 1, 100000);

	if (effect.desc.layout == EffectLayout::horizontal) {
		drawInfo.sourceX = drawInfo.frameIndex * effect.desc.frameWidth;
		drawInfo.sourceY = 0;
	} else if (effect.desc.layout == EffectLayout::vertical) {
		drawInfo.sourceX = 0;
		drawInfo.sourceY = drawInfo.frameIndex * effect.desc.frameHeight;
	} else if (effect.desc.layout == EffectLayout::grid) {
		drawInfo.sourceX = (drawInfo.frameIndex % columns) * effect.desc.frameWidth;
		drawInfo.sourceY = (drawInfo.frameIndex / columns) * effect.desc.frameHeight;
	} else {
		drawInfo.sourceX = 0;
		drawInfo.sourceY = 0;
	}

	return drawInfo;
}

void advanceTimelines(float deltaTime) {
	g_globalSequenceTime += deltaTime;

	for (auto& timelinePair : g_timelines) {
		TimelineEntry& timeline = timelinePair.second;
		if (!timeline.playing) continue;

		float prevElapsed = timeline.elapsed;
		timeline.elapsed += deltaTime;

		for (size_t i = 0; i < timeline.events.size(); i++) {
			if (i >= timeline.eventFired.size()) break;
			if (timeline.eventFired[i]) continue;

			const SequenceEvent& ev = timeline.events[i];
			if (prevElapsed < ev.timeSeconds && timeline.elapsed >= ev.timeSeconds) {
				timeline.eventFired[i] = true;

				if (!ev.eventName) continue;
				std::string action(ev.eventName);

				if (action == "vibrate" || action == "haptic_L" || action == "haptic_R") {
					float p = clampFloat(ev.param1, 0.0f, 1.0f);
					if (action == "haptic_L") p *= 0.9f;
					float d = (ev.param2 > 0.0f) ? ev.param2 : 0.1f;
					if (g_vibrationCallback) g_vibrationCallback(p, d);
				} else if (action == "shake") {
					float sec = (ev.param2 > 0.0f) ? ev.param2 : 0.2f;
					g_shakePower = clampFloat(ev.param1, 0.0f, 100000.0f);
					g_shakeRemaining = sec;
					g_shakeTotal = sec;
				} else if (action == "hit_stop") {
					g_hitStopRemaining = (ev.param1 > 0.0f) ? ev.param1 : 0.05f;
				} else if (action == "flash") {
					g_flashColor = static_cast<unsigned int>(ev.param3);
					g_flashRemaining = (ev.param2 > 0.0f) ? ev.param2 : 0.2f;
					g_flashTotal = g_flashRemaining;
					g_flashStrength = clampFloat(ev.param1, 0.0f, 1.0f);
				} else if (action == "zoom") {
					g_zoomScale = (ev.param1 > 0.0f) ? ev.param1 : 1.0f;
					g_zoomRemaining = (ev.param2 > 0.0f) ? ev.param2 : 0.2f;
					g_zoomTotal = g_zoomRemaining;
				} else if (action == "slow_motion") {
					g_slowMoScale = (ev.param1 > 0.0f) ? ev.param1 : 1.0f;
					g_slowMoRemaining = (ev.param2 > 0.0f) ? ev.param2 : 0.2f;
					g_slowMoTotal = g_slowMoRemaining;
				} else if (g_timelineEventCallback) {
					g_timelineEventCallback(ev.timeSeconds, ev.eventName,
						ev.param1, ev.param2, ev.param3);
				}
			}
		}
	}
}

} // namespace Internal

void Init() {
	Internal::g_activeEffects.clear();
	Internal::g_decalPool.clear();
	Internal::g_effectRegistry.clear();
	Internal::g_timelines.clear();
	Internal::g_waveformRegistry.clear();
	Internal::g_emitters.clear();
	Internal::g_activeEffectsLod.clear();
	Internal::g_trails.clear();
	Internal::g_afterImages.clear();
	Internal::g_hapticTimelineLog.clear();
	Internal::g_recordingBuffer = {};
	Internal::g_shakePower = 0.0f;
	Internal::g_shakeRemaining = 0.0f;
	Internal::g_shakeTotal = 0.0f;
	Internal::g_flashColor = 0xFFFFFFFFu;
	Internal::g_flashRemaining = 0.0f;
	Internal::g_flashTotal = 0.0f;
	Internal::g_flashStrength = 1.0f;
	Internal::g_zoomScale = 1.0f;
	Internal::g_zoomRemaining = 0.0f;
	Internal::g_zoomTotal = 0.0f;
	Internal::g_hitStopRemaining = 0.0f;
	Internal::g_slowMoScale = 1.0f;
	Internal::g_slowMoRemaining = 0.0f;
	Internal::g_slowMoTotal = 0.0f;
	Internal::g_masterVolume = 1.0f;
	Internal::g_nextEffectId = 1;
	Internal::g_nextDecalId = 1;
	Internal::g_nextTimelineId = 1;
	Internal::g_nextWaveformId = 1;
	Internal::g_nextEmitterId = 1;
	Internal::g_nextEffectLodId = 1;
	Internal::g_nextTrailId = 1;
	Internal::g_nextAfterImageId = 1;
	Internal::g_globalSequenceTime = 0.0f;
	Internal::g_textEffects.clear();
	Internal::g_collisionRects.clear();
	Internal::g_fragments.clear();
	Internal::g_gifFrames.clear();
	Internal::g_screenEffects.clear();
	Internal::g_trailPoints.clear();
	Internal::g_imageDataProvider = nullptr;
	Internal::g_textureDataProvider = nullptr;
	Internal::g_gifDataProvider = nullptr;
	Internal::g_animationPatterns.clear();
	Internal::g_timelineEventCallback = nullptr;
	Internal::g_audioFeatureCache.clear();
	Internal::g_occlusionFactor = 1.0f;
	Internal::g_textDrawCallback = nullptr;
	Internal::g_fragmentDrawCallback = nullptr;
	Internal::g_screenEffectDrawCallback = nullptr;
	Internal::g_debugLogCallback = nullptr;
}

void Update(float deltaTime) {
	const float safeDeltaTime = std::max(deltaTime, 0.0f);

	for (Internal::ActiveEffect& effect : Internal::g_activeEffects) {
		if (!effect.isActive) {
			continue;
		}

		effect.elapsedSeconds += safeDeltaTime;

		if (!effect.desc.loop && effect.desc.seconds > 0.0f &&
		    effect.elapsedSeconds >= effect.desc.seconds) {
			effect.isActive = false;
		}
	}

	Internal::g_activeEffects.erase(
		std::remove_if(
			Internal::g_activeEffects.begin(),
			Internal::g_activeEffects.end(),
			[](const Internal::ActiveEffect& effect) {
				return !effect.isActive;
			}),
		Internal::g_activeEffects.end());

	Internal::updateTimer(Internal::g_shakeRemaining, safeDeltaTime);
	Internal::updateTimer(Internal::g_flashRemaining, safeDeltaTime);
	Internal::updateTimer(Internal::g_zoomRemaining, safeDeltaTime);
	Internal::updateTimer(Internal::g_hitStopRemaining, safeDeltaTime);
	Internal::updateTimer(Internal::g_slowMoRemaining, safeDeltaTime);
	Internal::advanceTimelines(safeDeltaTime);

	// Text effects life
	for (auto& te : Internal::g_textEffects) {
		if (!te.active) continue;
		te.remainingSeconds -= safeDeltaTime;
		te.posY += safeDeltaTime * 32.0f;
		if (te.remainingSeconds <= 0.0f) te.active = false;
	}
	Internal::g_textEffects.erase(std::remove_if(
		Internal::g_textEffects.begin(), Internal::g_textEffects.end(),
		[](const Internal::TextEffectData& t) { return !t.active; }),
		Internal::g_textEffects.end());

	// Fragments life
	for (auto& f : Internal::g_fragments) {
		if (!f.active) continue;
		f.lifeSeconds -= safeDeltaTime;
		f.x += f.vx * safeDeltaTime;
		f.y += f.vy * safeDeltaTime;
		f.rotation += f.angularVelocity * safeDeltaTime;
		if (f.lifeSeconds <= 0.0f) f.active = false;
	}

	// Screen effects life
	for (auto& se : Internal::g_screenEffects) {
		if (!se.active) continue;
		se.remainingSeconds -= safeDeltaTime;
		if (se.remainingSeconds <= 0.0f) se.active = false;
	}

	// Emitter lifecycle (delay + duration)
	for (auto& pair : Internal::g_emitters) {
		auto& rec = pair.second;
		if (!rec.active) continue;
		rec.elapsedSeconds += safeDeltaTime;
		if (rec.desc.durationSeconds > 0.0f &&
			rec.elapsedSeconds >= rec.desc.delaySeconds + rec.desc.durationSeconds) {
			rec.active = false;
		}
	}

	// Trail points life
	for (auto& pair : Internal::g_trailPoints) {
		auto& points = pair.second;
		for (auto& pt : points) {
			pt.remainingSeconds -= safeDeltaTime;
		}
		points.erase(std::remove_if(points.begin(), points.end(),
			[](const Internal::TrailPoint& p) { return p.remainingSeconds <= 0.0f; }),
			points.end());
	}
}

void Draw() {
	// Draw active effects
	if (Internal::g_effectDrawCallback) {
		for (const Internal::ActiveEffect& effect : Internal::g_activeEffects) {
			if (!effect.isActive) continue;
			Internal::g_effectDrawCallback(Internal::makeDrawInfo(effect));
		}
	}

	// Draw text effects
	if (Internal::g_textDrawCallback) {
		for (const Internal::TextEffectData& te : Internal::g_textEffects) {
			if (!te.active) continue;
			Internal::g_textDrawCallback(te.text.c_str(), te.posX, te.posY, te.size, 0xFFFFFFFFu);
		}
	}

	// Draw fragments
	if (Internal::g_fragmentDrawCallback) {
		for (const Internal::FragmentData& f : Internal::g_fragments) {
			if (!f.active) continue;
			Internal::g_fragmentDrawCallback(f.x, f.y, f.vx, f.vy, f.rotation, f.color);
		}
	}

	// Draw screen overlay effects
	if (Internal::g_screenEffectDrawCallback) {
		for (const Internal::ScreenEffectEntry& se : Internal::g_screenEffects) {
			if (!se.active) continue;
			Internal::g_screenEffectDrawCallback(se.desc.effectName, se.desc.intensity,
				se.remainingSeconds, se.desc.colorRgba);
		}
	}
}

void Shutdown() {
	Internal::g_activeEffects.clear();
	Internal::g_decalPool.clear();
	Internal::g_timelines.clear();
	Internal::g_waveformRegistry.clear();
	Internal::g_emitters.clear();
	Internal::g_activeEffectsLod.clear();
}

void PlaySequence(const char* sequenceName) {
	if (sequenceName == nullptr) {
		return;
	}

	const auto sequenceIterator = Internal::g_sequenceRegistry.find(sequenceName);

	if (sequenceIterator == Internal::g_sequenceRegistry.end()) {
		return;
	}

	TimelineHandle handle = Timeline();
	Internal::TimelineEntry& entry = Internal::g_timelines[handle];

	for (const SequenceEvent& event : sequenceIterator->second.events) {
		entry.events.push_back(event);
		entry.eventFired.push_back(false);
	}

	TimelinePlay(handle);
}

void PlaySequence(const SequenceEvent* events, int eventCount) {
	if (events == nullptr || eventCount <= 0) {
		return;
	}

	TimelineHandle handle = Timeline();
	Internal::TimelineEntry& entry = Internal::g_timelines[handle];

	for (int eventIndex = 0; eventIndex < eventCount; eventIndex++) {
		entry.events.push_back(events[eventIndex]);
		entry.eventFired.push_back(false);
	}

	TimelinePlay(handle);
}

TimelineHandle Timeline() {
	const TimelineHandle handle = Internal::g_nextTimelineId;
	Internal::g_nextTimelineId++;
	Internal::g_timelines[handle] = {};
	return handle;
}

void TimelineAddEvent(TimelineHandle handle, float timeSeconds, const char* eventName) {
	auto timelineIterator = Internal::g_timelines.find(handle);

	if (timelineIterator == Internal::g_timelines.end()) {
		return;
	}

	SequenceEvent event = {};
	event.timeSeconds = timeSeconds;
	event.eventName = eventName;
	timelineIterator->second.events.push_back(event);
	timelineIterator->second.eventFired.push_back(false);
}

void TimelinePlay(TimelineHandle handle) {
	auto timelineIterator = Internal::g_timelines.find(handle);

	if (timelineIterator == Internal::g_timelines.end()) {
		return;
	}

	timelineIterator->second.playing = true;
	timelineIterator->second.elapsed = 0.0f;
}

void TimelineStop(TimelineHandle handle) {
	auto timelineIterator = Internal::g_timelines.find(handle);

	if (timelineIterator == Internal::g_timelines.end()) {
		return;
	}

	timelineIterator->second.playing = false;
}

void StartEffectRecording() {
	Internal::g_recordingBuffer.entries.clear();
	Internal::g_recordingBuffer.recording = true;
	Internal::g_recordingBuffer.startTime = Internal::g_globalSequenceTime;
}

void ReplayEffect() {
	Internal::g_recordingBuffer.recording = false;
}

void SetTextDrawCallback(TextDrawCallback cb) {
	Internal::g_textDrawCallback = cb;
}

void SetFragmentDrawCallback(FragmentDrawCallback cb) {
	Internal::g_fragmentDrawCallback = cb;
}

void SetScreenEffectDrawCallback(ScreenEffectDrawCallback cb) {
	Internal::g_screenEffectDrawCallback = cb;
}

void SetDebugLogCallback(DebugLogCallback cb) {
	Internal::g_debugLogCallback = cb;
}

float GetOcclusionFactor() {
	return Internal::g_occlusionFactor;
}

void SetOcclusionFactor(float factor) {
	Internal::g_occlusionFactor = (factor < 0.0f) ? 0.0f : factor;
}

namespace Internal {

AudioFeatureData readAudioFileFeatures(const char* path) {
	if (!path || !path[0]) return {};
	auto it = g_audioFeatureCache.find(path);
	if (it != g_audioFeatureCache.end()) return it->second;
	AudioFeatureData result = AnalyzeAudioFeatures(path);
	if (result.isValid) g_audioFeatureCache[path] = result;
	return result;
}

std::vector<AudioFrameData> extractAudioFrames(const char* path, float windowSeconds) {
	std::vector<AudioFrameData> frames;
	if (!path || !path[0] || windowSeconds <= 0.0f) return frames;

	int sampleRate = 0;
	std::vector<float> samples;
	if (!readWav(path, sampleRate, samples) || samples.empty()) return frames;

	int windowSamples = std::max(1, static_cast<int>(windowSeconds * sampleRate));
	int totalWindows = static_cast<int>(std::ceil(static_cast<float>(samples.size()) / windowSamples));

	for (int w = 0; w < totalWindows; w++) {
		int start = w * windowSamples;
		int end = std::min(start + windowSamples, static_cast<int>(samples.size()));
		float sum = 0.0f;
		float peak = 0.0f;
		int half = start + (end - start) / 2;
		float lowSum = 0.0f;
		float highSum = 0.0f;
		int lowCount = 0;
		int highCount = 0;

		for (int i = start; i < end; i++) {
			float absVal = std::abs(samples[i]);
			sum += absVal;
			if (absVal > peak) peak = absVal;
			if (i < half) { lowSum += absVal; lowCount++; }
			else { highSum += absVal; highCount++; }
		}

		AudioFrameData frame;
		frame.timeSeconds = static_cast<float>(start) / sampleRate;
		frame.amplitude = (end > start) ? (sum / (end - start)) : 0.0f;
		frame.lowBand = (lowCount > 0) ? (lowSum / lowCount) : 0.0f;
		frame.highBand = (highCount > 0) ? (highSum / highCount) : 0.0f;
		frames.push_back(frame);
	}

	return frames;
}

float materialMultiplier(MaterialType material) {
	switch (material) {
		case MaterialType::metal:  return 1.2f;
		case MaterialType::wood:   return 0.8f;
		case MaterialType::earth:  return 0.6f;
		case MaterialType::stone:  return 0.9f;
		case MaterialType::water:  return 0.3f;
		case MaterialType::flesh:  return 0.5f;
		case MaterialType::glass:  return 1.1f;
		case MaterialType::plastic: return 0.7f;
		case MaterialType::cloth:  return 0.4f;
		default: return 1.0f;
	}
}

} // namespace Internal

int GetTextEffectCount() {
	return static_cast<int>(Internal::g_textEffects.size());
}

const Internal::TextEffectData* GetTextEffect(int index) {
	if (index < 0 || index >= static_cast<int>(Internal::g_textEffects.size())) return nullptr;
	return &Internal::g_textEffects[index];
}

int GetFragmentCount() {
	return static_cast<int>(Internal::g_fragments.size());
}

const Internal::FragmentData* GetFragment(int index) {
	if (index < 0 || index >= static_cast<int>(Internal::g_fragments.size())) return nullptr;
	return &Internal::g_fragments[index];
}

int GetScreenEffectCount() {
	return static_cast<int>(Internal::g_screenEffects.size());
}

const Internal::ScreenEffectEntry* GetScreenEffect(int index) {
	if (index < 0 || index >= static_cast<int>(Internal::g_screenEffects.size())) return nullptr;
	return &Internal::g_screenEffects[index];
}

int GetTrailCount() {
	return static_cast<int>(Internal::g_trails.size());
}

TrailHandle GetTrailHandle(int index) {
	int i = 0;
	for (const auto& pair : Internal::g_trails) {
		if (i == index) return pair.first;
		i++;
	}
	return -1;
}

} // namespace FeelKit
