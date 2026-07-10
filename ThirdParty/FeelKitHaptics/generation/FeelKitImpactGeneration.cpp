#define _CRT_SECURE_NO_WARNINGS

#include "FeelKitImpactGeneration.h"

#include "../audio/FeelKitAudio.h"
#include "../effects/FeelKitEffects.h"
#include "../haptics/FeelKitVibration.h"
#include "../screen/FeelKitScreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace FeelKit {

//================================================================
// プロバイダ登録
//================================================================

void SetImageDataProvider(ImageDataProvider provider) {
	Internal::g_imageDataProvider = std::move(provider);
}

void SetTextureDataProvider(TextureDataProvider provider) {
	Internal::g_textureDataProvider = std::move(provider);
}

void SetGifDataProvider(GifDataProvider provider) {
	Internal::g_gifDataProvider = std::move(provider);
}

void SetAnimationHapticPattern(const char* animationName,
                               const AnimationHapticPattern& pattern) {
	if (animationName && animationName[0] != '\0') {
		Internal::g_animationPatterns[animationName] = pattern;
	}
}

//================================================================
// 衝突解析（既存: そのまま）
//================================================================

ImpactAnalysisResult AnalyzeImpact(float velocity, float mass) {
	ImpactAnalysisResult result = {};
	const float momentum = std::fabs(velocity) * std::fabs(mass);
	result.power = Internal::clampFloat(momentum * 0.01f, 0.0f, 1.0f);
	result.seconds = Internal::clampFloat(result.power * 0.3f, 0.05f, 1.0f);
	return result;
}

ImpactAnalysisResult AnalyzeImpact(Vec3 velocity, float mass) {
	return AnalyzeImpact(Internal::vec3Length(velocity), mass);
}

void GenerateImpactHaptics(float velocity, float mass) {
	const ImpactAnalysisResult impact = AnalyzeImpact(velocity, mass);
	Vibrate(impact.power, impact.seconds);
}

void GenerateImpactHaptics(Vec3 velocity, float mass) {
	GenerateImpactHaptics(Internal::vec3Length(velocity), mass);
}

void GenerateImpactShake(float velocity, float mass) {
	const ImpactAnalysisResult impact = AnalyzeImpact(velocity, mass);
	Shake(impact.power * 5.0f, impact.seconds);
}

void GenerateImpactShake(Vec3 velocity, float mass) {
	GenerateImpactShake(Internal::vec3Length(velocity), mass);
}

//================================================================
// 上位自動生成（既存: そのまま）
//================================================================

ImpactFeedbackResult GenerateImpactFeedback(float velocity, float mass, float angle, MaterialType material) {
	ImpactFeedbackResult result = {};
	float speed = std::fabs(velocity);
	float safeMass = std::fabs(mass);
	float momentum = speed * safeMass;
	float intensity = Internal::clampFloat(momentum * 0.01f, 0.0f, 1.0f);

	float matMul = Internal::materialMultiplier(material);

	result.vibrationPower = Internal::clampFloat(intensity * 0.8f * matMul * Internal::g_occlusionFactor, 0.0f, 1.0f);
	result.shakePower = intensity * 5.0f * matMul * Internal::g_occlusionFactor;
	result.hitStopSeconds = intensity * 0.05f * matMul;
	result.seVolume = Internal::clampFloat((0.3f + intensity * 0.7f) * Internal::g_occlusionFactor, 0.0f, 1.0f);
	result.particleIntensity = intensity * matMul;

	float angleFactor = 1.0f - Internal::clampFloat(std::fabs(angle) / 3.14159f, 0.0f, 1.0f) * 0.5f;
	result.vibrationPower *= angleFactor;
	result.shakePower *= angleFactor;

	Vibrate(result.vibrationPower, 0.05f + intensity * 0.15f);
	Shake(result.shakePower, 0.1f + intensity * 0.2f);
	HitStop(result.hitStopSeconds);
	PlaySE("impact.wav", result.seVolume);

	return result;
}

ImpactFeedbackResult GenerateImpactFeedback(Vec3 velocity, float mass, MaterialType material) {
	float speed = Internal::vec3Length(velocity);
	float angle = std::atan2(
		std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z), velocity.y);
	return GenerateImpactFeedback(speed, mass, angle, material);
}

ExplosionFeedbackResult GenerateExplosionFeedback(Vec3 position, float radius, float power, MaterialType material) {
	ExplosionFeedbackResult result = {};
	float safePower = Internal::clampFloat(power, 0.0f, 10.0f);
	float safeRadius = std::max(radius, 0.0f);

	float distance = Internal::vec3Length(position);
	float distanceFactor = (safeRadius > 0.0f)
		? Internal::clampFloat(1.0f - distance / safeRadius, 0.0f, 1.0f)
		: 1.0f;

	float matMul = Internal::materialMultiplier(material) * Internal::g_occlusionFactor;

	result.vibrationPower = Internal::clampFloat(safePower * 0.08f * distanceFactor * matMul, 0.0f, 1.0f);
	result.shakePower = safePower * 2.0f * distanceFactor * matMul;
	result.seVolume = Internal::clampFloat((0.3f + safePower * 0.07f) * distanceFactor * matMul, 0.0f, 1.0f);
	result.effectIntensity = Internal::clampFloat(safePower * 0.15f * distanceFactor * matMul, 0.0f, 1.0f);

	Vibrate(result.vibrationPower, 0.1f + safePower * 0.04f);
	Shake(result.shakePower, 0.2f + safePower * 0.08f);
	PlaySE("explosion.wav", result.seVolume);

	if (result.effectIntensity > 0.2f) {
		EffectDesc explosionFx;
		explosionFx.textureHandle = static_cast<int>(result.effectIntensity * 100.0f + 0.5f);
		explosionFx.layout = EffectLayout::single;
		explosionFx.frameCount = 1;
		explosionFx.seconds = 0.3f;
		PlayEffect(explosionFx, Vec3{position.x, position.y, position.z});
	}

	return result;
}

//================================================================
// アニメーション → Timeline ベースの触覚マッピング
//================================================================

namespace {

std::string toLower(const char* s) {
	if (!s) return {};
	std::string out;
	out.resize(std::strlen(s));
	for (size_t i = 0; i < out.size(); i++) {
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
	}
	return out;
}

} // namespace

void GenerateHapticsFromAnimation(const char* animationName) {
	if (!animationName || animationName[0] == '\0') return;

	float power = 0.0f;
	float duration = 0.0f;
	float interval = 0.0f;

	auto it = Internal::g_animationPatterns.find(animationName);
	if (it != Internal::g_animationPatterns.end()) {
		power = it->second.continuousPower;
		duration = it->second.continuousDuration;
		interval = it->second.footstepInterval;
	} else {
		std::string lower = toLower(animationName);

		if (lower.find("sprint") != std::string::npos ||
		    lower.find("dash") != std::string::npos) {
			power = 0.55f; duration = 0.10f; interval = 0.2f;
		} else if (lower.find("run") != std::string::npos) {
			power = 0.40f; duration = 0.08f; interval = 0.3f;
		} else if (lower.find("walk") != std::string::npos) {
			power = 0.25f; duration = 0.06f; interval = 0.5f;
		} else if (lower.find("jump") != std::string::npos) {
			power = 0.70f; duration = 0.15f;
		} else if (lower.find("land") != std::string::npos ||
		           lower.find("fall") != std::string::npos) {
			power = 0.85f; duration = 0.20f;
		} else if (lower.find("attack") != std::string::npos ||
		           lower.find("punch") != std::string::npos ||
		           lower.find("kick") != std::string::npos ||
		           lower.find("slash") != std::string::npos) {
			power = 0.90f; duration = 0.12f;
		} else if (lower.find("hit") != std::string::npos ||
		           lower.find("damage") != std::string::npos ||
		           lower.find("hurt") != std::string::npos) {
			power = 1.00f; duration = 0.20f;
		} else if (lower.find("swim") != std::string::npos) {
			power = 0.20f; duration = 0.30f;
		} else if (lower.find("fly") != std::string::npos ||
		           lower.find("float") != std::string::npos) {
			power = 0.10f; duration = 0.0f;
		} else if (lower.find("idle") != std::string::npos ||
		           lower.find("stand") != std::string::npos) {
			power = 0.0f; duration = 0.0f;
		} else {
			power = 0.30f; duration = 0.08f;
		}
	}

	if (power <= 0.0f || duration <= 0.0f) return;

	std::vector<SequenceEvent> events;

	if (interval > 0.0f) {
		int count = static_cast<int>(3.0f / interval);
		if (count > 12) count = 12;
		if (count < 1) count = 1;
		for (int i = 0; i < count; i++) {
			SequenceEvent ev;
			ev.timeSeconds = static_cast<float>(i) * interval;
			ev.eventName = (i % 2 == 0) ? "haptic_L" : "haptic_R";
			ev.param1 = (i % 2 == 0) ? power * 0.9f : power;
			ev.param2 = duration;
			events.push_back(ev);
		}
	} else {
		SequenceEvent ev;
		ev.timeSeconds = 0.0f;
		ev.eventName = "vibrate";
		ev.param1 = power;
		ev.param2 = duration;
		events.push_back(ev);
	}

	PlaySequence(events.data(), static_cast<int>(events.size()));
}

void GenerateFootsteps(const char* animationName) {
	if (!animationName || animationName[0] == '\0') return;

	float power = 0.30f;
	float interval = 0.40f;

	auto it = Internal::g_animationPatterns.find(animationName);
	if (it != Internal::g_animationPatterns.end() &&
	    it->second.footstepInterval > 0.0f) {
		power = it->second.footstepPower;
		interval = it->second.footstepInterval;
	} else {
		std::string lower = toLower(animationName);

		if (lower.find("sprint") != std::string::npos ||
		    lower.find("dash") != std::string::npos) {
			power = 0.40f; interval = 0.18f;
		} else if (lower.find("run") != std::string::npos) {
			power = 0.35f; interval = 0.28f;
		} else if (lower.find("walk") != std::string::npos) {
			power = 0.20f; interval = 0.45f;
		} else if (lower.find("stomp") != std::string::npos ||
		           lower.find("heavy") != std::string::npos) {
			power = 0.70f; interval = 0.60f;
		} else if (lower.find("tip") != std::string::npos ||
		           lower.find("creep") != std::string::npos ||
		           lower.find("stealth") != std::string::npos) {
			power = 0.08f; interval = 0.65f;
		}
	}

	std::vector<SequenceEvent> events;
	int count = static_cast<int>(3.0f / interval);
	if (count > 16) count = 16;
	if (count < 2) count = 2;

	for (int i = 0; i < count; i++) {
		SequenceEvent ev;
		ev.timeSeconds = static_cast<float>(i) * interval;
		ev.eventName = (i % 2 == 0) ? "haptic_L" : "haptic_R";
		ev.param1 = (i % 2 == 0) ? power * 0.95f : power;
		ev.param2 = 0.04f + power * 0.06f;
		events.push_back(ev);
	}

	PlaySequence(events.data(), static_cast<int>(events.size()));
}

//================================================================
// CreateParticlesFromAudio（共有解析 + 変換）
//================================================================

void CreateParticlesFromAudio(const char* soundPath) {
	AudioFeatureData feature = Internal::readAudioFileFeatures(soundPath);
	if (!feature.isValid) return;

	auto frames = Internal::extractAudioFrames(soundPath, 0.1f);
	if (frames.empty()) {
		ParticleParams pp = MakeParticlesFromAudio(feature);
		EmitterDesc desc;
		desc.shape = EmitterShape::circle;
		desc.radius = pp.radius;
		desc.particleDesc.spawnRate = pp.spawnRate;
		desc.particleDesc.size = pp.size;
		desc.particleDesc.initialSpeed = pp.speed;
		desc.particleDesc.lifeSeconds = pp.life;
		desc.particleDesc.maxParticles = static_cast<int>(pp.spawnRate * pp.life);
		desc.particleDesc.colorRgba = pp.colorRgba;
		Emit(desc);
		return;
	}

	for (size_t i = 0; i < frames.size(); i++) {
		float intensity = Internal::clampFloat(frames[i].amplitude * 3.0f, 0.0f, 1.0f);
		if (intensity < 0.12f) continue;

		ParticleParams pp = MakeParticlesFromAudio(feature);
		float spawnScale = 0.3f + intensity * 2.5f;
		float windowDur = (i + 1 < frames.size()) ?
			(frames[i + 1].timeSeconds - frames[i].timeSeconds) : 0.1f;

		EmitterDesc desc;
		desc.shape = EmitterShape::circle;
		desc.radius = pp.radius * (0.3f + intensity * 1.2f);
		desc.delaySeconds = frames[i].timeSeconds;
		desc.durationSeconds = windowDur;
		desc.particleDesc.spawnRate = pp.spawnRate * spawnScale;
		desc.particleDesc.size = pp.size * (0.7f + intensity * 0.6f);
		desc.particleDesc.initialSpeed = pp.speed * (0.6f + intensity * 0.8f);
		desc.particleDesc.lifeSeconds = pp.life * (0.4f + intensity * 0.8f);
		desc.particleDesc.maxParticles = std::max(1, static_cast<int>(desc.particleDesc.spawnRate * desc.particleDesc.lifeSeconds));
		desc.particleDesc.colorRgba = pp.colorRgba;
		Emit(desc);
	}
}

//================================================================
// 画像 → パーティクル／コリジョン変換（ImageDataProvider 必須）
//================================================================

void CreateParticlesFromImage(const char* imagePath) {
	if (!imagePath || !Internal::g_imageDataProvider) return;

	int imgW = 0, imgH = 0;
	std::vector<unsigned char> pixels;
	if (!Internal::g_imageDataProvider(imagePath, imgW, imgH, pixels)) return;
	if (imgW <= 0 || imgH <= 0 || pixels.empty()) return;

	Internal::g_collisionRects.clear();

	const int cellW = 16, cellH = 16;
	const int cols = imgW / cellW;
	const int rows = imgH / cellH;
	int contentCells = 0;
	unsigned long sumR = 0, sumG = 0, sumB = 0;
	int sampleCount = 0;

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			bool hasContent = false;
			for (int py = y * cellH; py < (y + 1) * cellH && py < imgH; py++) {
				for (int px = x * cellW; px < (x + 1) * cellW && px < imgW; px++) {
					int idx = (py * imgW + px) * 4;
					if (idx + 3 < static_cast<int>(pixels.size())) {
						sumR += pixels[idx];
						sumG += pixels[idx + 1];
						sumB += pixels[idx + 2];
						sampleCount++;
						if (pixels[idx + 3] > 32) {
							hasContent = true;
						}
					}
				}
			}
			if (hasContent) {
				Internal::CollisionRect rect;
				rect.x = static_cast<float>(x * cellW);
				rect.y = static_cast<float>(y * cellH);
				rect.width = static_cast<float>(std::min(cellW, imgW - x * cellW));
				rect.height = static_cast<float>(std::min(cellH, imgH - y * cellH));
				Internal::g_collisionRects.push_back(rect);
				contentCells++;
			}
		}
	}

	unsigned int color = 0xFFFFFFFFu;
	if (sampleCount > 0) {
		unsigned char r = static_cast<unsigned char>(sumR / sampleCount);
		unsigned char g = static_cast<unsigned char>(sumG / sampleCount);
		unsigned char b = static_cast<unsigned char>(sumB / sampleCount);
		color = 0xFF000000u | (static_cast<unsigned int>(r) << 16) |
		        (static_cast<unsigned int>(g) << 8) |
		        static_cast<unsigned int>(b);
	}

	EmitterDesc desc;
	desc.shape = EmitterShape::box;
	desc.radius = static_cast<float>(std::max(imgW, imgH)) * 0.5f;
	desc.particleDesc.spawnRate = std::max(1.0f, static_cast<float>(contentCells) * 0.3f);
	desc.particleDesc.size = static_cast<float>(cellW + cellH) * 0.25f;
	desc.particleDesc.initialSpeed = 1.0f + static_cast<float>(imgW) * 0.005f;
	desc.particleDesc.lifeSeconds = 2.0f;
	desc.particleDesc.colorRgba = color;
	desc.particleDesc.maxParticles = contentCells * 4;

	Emit(desc);
}

void CreateCollisionFromImage(const char* imagePath) {
	if (!imagePath || !Internal::g_imageDataProvider) return;

	int imgW = 0, imgH = 0;
	std::vector<unsigned char> pixels;
	if (!Internal::g_imageDataProvider(imagePath, imgW, imgH, pixels)) return;
	if (imgW <= 0 || imgH <= 0 || pixels.empty()) return;

	Internal::g_collisionRects.clear();

	const int cellW = 16, cellH = 16;
	const int cols = imgW / cellW;
	const int rows = imgH / cellH;

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			bool hasContent = false;
			for (int py = y * cellH; py < (y + 1) * cellH && py < imgH; py++) {
				for (int px = x * cellW; px < (x + 1) * cellW && px < imgW; px++) {
					int idx = (py * imgW + px) * 4;
					if (idx + 3 < static_cast<int>(pixels.size()) &&
					    pixels[idx + 3] > 32) {
						hasContent = true;
						break;
					}
				}
				if (hasContent) break;
			}
			if (hasContent) {
				Internal::CollisionRect rect;
				rect.x = static_cast<float>(x * cellW);
				rect.y = static_cast<float>(y * cellH);
				rect.width = static_cast<float>(std::min(cellW, imgW - x * cellW));
				rect.height = static_cast<float>(std::min(cellH, imgH - y * cellH));
				Internal::g_collisionRects.push_back(rect);
			}
		}
	}
}

//================================================================
// BreakImage（TextureDataProvider 必須）
//================================================================

void BreakImage(int textureHandle) {
	if (!Internal::g_textureDataProvider) return;

	int w = 0, h = 0;
	std::vector<unsigned char> pixels;
	if (!Internal::g_textureDataProvider(textureHandle, w, h, pixels)) return;
	if (w <= 0 || h <= 0 || pixels.empty()) return;

	Internal::g_fragments.clear();

	const int cellW = 24, cellH = 24;
	const int cols = std::max(1, w / cellW);
	const int rows = std::max(1, h / cellH);

	int seed = textureHandle * 13;
	std::srand(seed);

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			Internal::FragmentData frag;
			frag.x = (static_cast<float>(x) + 0.5f) * static_cast<float>(cellW);
			frag.y = (static_cast<float>(y) + 0.5f) * static_cast<float>(cellH);

			float cx = frag.x - static_cast<float>(w) * 0.5f;
			float cy = frag.y - static_cast<float>(h) * 0.5f;
			float dist = std::sqrt(cx * cx + cy * cy) + 0.001f;
			float speed = 60.0f + dist * 0.8f;
			float rv = (static_cast<float>(std::rand() % 200) - 100.0f) * 0.3f;
			float rh = (static_cast<float>(std::rand() % 200) - 100.0f) * 0.3f;
			frag.vx = (cx / dist) * speed + rv;
			frag.vy = (cy / dist) * speed - 100.0f + rh;

			frag.rotation = static_cast<float>(std::rand() % 628) * 0.01f;
			frag.angularVelocity = (static_cast<float>(std::rand() % 400) - 200.0f) * 0.01f;
			frag.lifeSeconds = Internal::clampFloat(0.5f + dist * 0.008f, 0.3f, 3.0f);

			int idx = (static_cast<int>(frag.y) * w + static_cast<int>(frag.x)) * 4;
			if (idx + 3 < static_cast<int>(pixels.size())) {
				frag.color = 0xFF000000u |
					(static_cast<unsigned int>(pixels[idx]) << 16) |
					(static_cast<unsigned int>(pixels[idx + 1]) << 8) |
					static_cast<unsigned int>(pixels[idx + 2]);
			} else {
				frag.color = 0xFFFFFFFFu;
			}
			frag.active = true;

			Internal::g_fragments.push_back(frag);
		}
	}
}

//================================================================
// ImportGif（GifDataProvider 必須）
//================================================================

void ImportGif(const char* gifPath) {
	if (!gifPath || !Internal::g_gifDataProvider) return;

	int frameCount = 0, frameW = 0, frameH = 0;
	std::vector<std::vector<unsigned char>> framePixels;
	if (!Internal::g_gifDataProvider(gifPath, frameCount, frameW, frameH, framePixels)) return;
	if (frameCount <= 0 || frameW <= 0 || frameH <= 0) return;

	Internal::GifFrameSet frameSet;
	frameSet.textureHandle = 100 + static_cast<int>(Internal::g_gifFrames.size());
	frameSet.frameWidth = frameW;
	frameSet.frameHeight = frameH;
	frameSet.frameCount = frameCount;
	frameSet.frameInterval = (frameCount > 16) ? 0.033f : 0.066f;
	frameSet.layout = EffectLayout::horizontal;
	Internal::g_gifFrames.push_back(frameSet);

	EffectDesc gifDesc;
	gifDesc.textureHandle = frameSet.textureHandle;
	gifDesc.layout = frameSet.layout;
	gifDesc.frameWidth = frameW;
	gifDesc.frameHeight = frameH;
	gifDesc.frameCount = frameCount;
	gifDesc.columns = frameCount;
	gifDesc.rows = 1;
	gifDesc.seconds = static_cast<float>(frameCount) * frameSet.frameInterval;
	gifDesc.loop = true;

	char effectName[128];
	std::snprintf(effectName, sizeof(effectName), "_gif_%s", gifPath);
	LoadEffect(effectName, gifDesc);
}

//================================================================
// SpawnTextEffect（テキストのみで完結 → そのまま）
//================================================================

void SpawnTextEffect(const char* text) {
	if (!text || text[0] == '\0') return;

	Internal::TextEffectData entry;
	entry.text = text;
	entry.size = Internal::clampFloat(
		14.0f + static_cast<float>(entry.text.size()) * 1.5f, 12.0f, 72.0f);

	float punct = 0.0f, upper = 0.0f;
	for (size_t i = 0; i < entry.text.size(); i++) {
		unsigned char c = static_cast<unsigned char>(entry.text[i]);
		if (std::ispunct(c)) punct += 1.0f;
		if (std::isupper(c)) upper += 1.0f;
	}
	entry.scatter = Internal::clampFloat(
		(punct / std::max(1.0f, static_cast<float>(entry.text.size()))) * 2.0f +
		(upper / std::max(1.0f, static_cast<float>(entry.text.size()))) * 0.5f,
		0.0f, 1.0f);

	entry.posX = 400.0f;
	entry.posY = 300.0f;
	entry.totalSeconds = Internal::clampFloat(
		1.0f + static_cast<float>(entry.text.size()) * 0.05f + entry.scatter * 1.0f,
		0.5f, 5.0f);
	entry.remainingSeconds = entry.totalSeconds;
	entry.active = true;

	// Populate char-level data for per-character rendering
	float charSpacing = entry.size * 0.6f;
	float totalWidth = static_cast<float>(entry.text.size()) * charSpacing;
	float startX = entry.posX - totalWidth * 0.5f;
	entry.chars.clear();
	entry.chars.reserve(entry.text.size());
	for (size_t i = 0; i < entry.text.size(); i++) {
		Internal::CharEffectData cd;
		cd.ch = entry.text[i];
		cd.x = startX + static_cast<float>(i) * charSpacing;
		cd.y = entry.posY + (static_cast<float>(std::rand() % 100) - 50.0f) * entry.scatter * 0.1f;
		cd.rot = (static_cast<float>(std::rand() % 628) - 314.0f) * entry.scatter * 0.005f;
		cd.scale = 1.0f + (static_cast<float>(std::rand() % 50) - 25.0f) * 0.01f * entry.scatter;
		cd.color = 0xFFFFFFFFu;
		entry.chars.push_back(cd);
	}

	Internal::g_textEffects.push_back(entry);
}

//================================================================
// 統合演出
//================================================================

void PlayImpact(const Vec3& position, float intensity) {
	float safeIntensity = Internal::clampFloat(intensity, 0.0f, 1.0f);

	Vibrate(safeIntensity * 0.8f, 0.05f + safeIntensity * 0.15f);
	Shake(safeIntensity * 8.0f, 0.1f + safeIntensity * 0.2f);
	HitStop(safeIntensity * 0.05f);
	PlaySE("impact.wav", 0.5f + safeIntensity * 0.5f);
}

void PlayExplosion(const Vec3& position, float radius, float power) {
	GenerateExplosionFeedback(position, radius, power);
}

} // namespace FeelKit