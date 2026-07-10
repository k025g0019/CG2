#include "FeelKitEffects.h"

#include "../screen/FeelKitScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace FeelKit {

namespace {
void dbgLog(const char* msg) {
	if (Internal::g_debugLogCallback) Internal::g_debugLogCallback(msg);
	else std::printf("%s", msg);
}
} // namespace

//================================================================
// Callback setters
//================================================================

void SetEffectDrawCallback(const EffectDrawCallback& callback) {
	Internal::g_effectDrawCallback = callback;
}

void SetExplosionCallback(const ExplosionCallback& callback) {
	Internal::g_explosionCallback = callback;
}

void SetSparkCallback(const SparkCallback& callback) {
	Internal::g_sparkCallback = callback;
}

void SetSmokeCallback(const SmokeCallback& callback) {
	Internal::g_smokeCallback = callback;
}

void SetImpactEffectCallback(const ImpactEffectCallback& callback) {
	Internal::g_impactEffectCallback = callback;
}

void SetParticleCallback(const ParticleCallback& callback) {
	Internal::g_particleCallback = callback;
}

//================================================================
// Callback-based play
//================================================================

bool PlayExplosion(const ExplosionRequest& request) {
	if (request.effectName.empty()) return false;
	if (Internal::g_explosionCallback) {
		Internal::g_explosionCallback(
			request.effectName.c_str(),
			request.positionX, request.positionY, request.positionZ,
			request.power);
		return true;
	}
	return PlayEffect(request.effectName.c_str(),
		Vec3{request.positionX, request.positionY, request.positionZ}) >= 0;
}

bool PlaySpark(const SparkRequest& request) {
	if (request.effectName.empty()) return false;
	if (Internal::g_sparkCallback) {
		Internal::g_sparkCallback(
			request.effectName.c_str(),
			request.positionX, request.positionY, request.positionZ,
			request.power);
		return true;
	}
	return PlayEffect(request.effectName.c_str(),
		Vec3{request.positionX, request.positionY, request.positionZ}) >= 0;
}

bool PlaySmoke(const SmokeRequest& request) {
	if (request.effectName.empty()) return false;
	if (Internal::g_smokeCallback) {
		Internal::g_smokeCallback(
			request.effectName.c_str(),
			request.positionX, request.positionY, request.positionZ,
			request.power, request.durationMs);
		return true;
	}
	return PlayEffect(request.effectName.c_str(),
		Vec3{request.positionX, request.positionY, request.positionZ}) >= 0;
}

bool PlayImpactEffect(const ImpactEffectRequest& request) {
	if (request.effectName.empty()) return false;
	if (Internal::g_impactEffectCallback) {
		Internal::g_impactEffectCallback(
			request.effectName.c_str(),
			request.positionX, request.positionY, request.positionZ,
			request.power);
		return true;
	}
	return PlayEffect(request.effectName.c_str(),
		Vec3{request.positionX, request.positionY, request.positionZ}) >= 0;
}

bool PlayParticle(const ParticleRequest& request) {
	if (request.particleName.empty()) return false;
	if (!Internal::g_particleCallback) return false;
	Internal::g_particleCallback(
		request.particleName.c_str(),
		request.positionX, request.positionY, request.positionZ,
		request.count);
	return true;
}

//================================================================
// Direct effect playback
//================================================================

int PlayEffect(int textureHandle, float x, float y,
               int frameCount, float seconds, bool loop) {
	EffectDesc desc = {};
	desc.textureHandle = textureHandle;
	desc.layout = frameCount <= 1 ? EffectLayout::single : EffectLayout::horizontal;
	desc.frameCount = Internal::clampInt(frameCount, 1, 100000);
	desc.columns = desc.frameCount;
	desc.rows = 1;
	desc.seconds = (seconds > 0.0f) ? seconds : 0.0f;
	desc.loop = loop;
	Internal::ActiveEffect effect = {};
	effect.id = Internal::g_nextEffectId++;
	effect.desc = desc;
	effect.x = x;
	effect.y = y;
	effect.z = 0.0f;
	effect.isActive = true;
	Internal::g_activeEffects.push_back(effect);
	return effect.id;
}

int PlayEffect(int textureHandle,
               float x, float y,
               int frameWidth, int frameHeight, int frameCount,
               float seconds, bool loop) {
	EffectDesc desc = {};
	desc.textureHandle = textureHandle;
	desc.layout = frameCount <= 1 ? EffectLayout::single : EffectLayout::horizontal;
	desc.frameWidth = frameWidth;
	desc.frameHeight = frameHeight;
	desc.frameCount = Internal::clampInt(frameCount, 1, 100000);
	desc.columns = desc.frameCount;
	desc.rows = 1;
	desc.seconds = (seconds > 0.0f) ? seconds : 0.0f;
	desc.loop = loop;
	return PlayEffect(desc, Vec2{x, y});
}

int PlayEffect(const EffectDesc& effectDesc, Vec2 pos) {
	return PlayEffect(effectDesc, Vec3{pos.x, pos.y, 0.0f});
}

int PlayEffect(const EffectDesc& effectDesc, Vec3 pos) {
	Internal::ActiveEffect effect = {};
	effect.id = Internal::g_nextEffectId++;
	effect.desc = effectDesc;
	effect.desc.frameCount = Internal::clampInt(effect.desc.frameCount, 1, 100000);
	effect.desc.columns = Internal::clampInt(effect.desc.columns, 1, 100000);
	effect.desc.rows = Internal::clampInt(effect.desc.rows, 1, 100000);
	effect.desc.seconds = (effect.desc.seconds > 0.0f) ? effect.desc.seconds : 0.0f;
	effect.x = pos.x;
	effect.y = pos.y;
	effect.z = pos.z;
	effect.isActive = true;
	Internal::g_activeEffects.push_back(effect);
	return effect.id;
}

bool LoadEffect(const char* effectName, const EffectDesc& effectDesc) {
	if (effectName == nullptr || effectName[0] == '\0') return false;
	Internal::g_effectRegistry[effectName] = effectDesc;
	return true;
}

int PlayEffect(const char* effectName, Vec2 pos) {
	return PlayEffect(effectName, Vec3{pos.x, pos.y, 0.0f});
}

int PlayEffect(const char* effectName, Vec3 pos) {
	const auto it = Internal::g_effectRegistry.find(effectName ? effectName : "");
	if (it == Internal::g_effectRegistry.end()) return -1;
	return PlayEffect(it->second, pos);
}

//================================================================
// Effect Pool
//================================================================

namespace {
constexpr int kDefaultMaxEffects = 256;
int g_effectPoolMax = kDefaultMaxEffects;
}

void EffectPoolSetMax(int maxEffects) {
	g_effectPoolMax = (maxEffects > 0) ? maxEffects : kDefaultMaxEffects;
}

int EffectPoolGetActiveCount() {
	return static_cast<int>(Internal::g_activeEffects.size());
}

void EffectPoolKillAll() {
	Internal::g_activeEffects.clear();
}

void EffectPoolKill(int effectId) {
	for (auto& effect : Internal::g_activeEffects) {
		if (effect.id == effectId) {
			effect.isActive = false;
			return;
		}
	}
}

bool EffectPoolIsAlive(int effectId) {
	for (const auto& effect : Internal::g_activeEffects) {
		if (effect.id == effectId && effect.isActive) return true;
	}
	return false;
}

//================================================================
// Decal
//================================================================

int SpawnDecal(const DecalDesc& decalDesc) {
	Internal::DecalEntry entry;
	entry.desc = decalDesc;
	entry.lifeRemaining = std::max(decalDesc.lifeSeconds, 0.0f);
	entry.id = Internal::g_nextDecalId++;
	entry.active = true;

	if (static_cast<int>(Internal::g_decalPool.size()) >= decalDesc.maxDecals) {
		Internal::g_decalPool.erase(Internal::g_decalPool.begin());
	}
	Internal::g_decalPool.push_back(entry);

	char buf[256];
	std::snprintf(buf, sizeof(buf),
		"[FeelKit] SpawnDecal: id=%d tex=%s pos=(%.1f,%.1f,%.1f) life=%.1f\n",
		entry.id,
		decalDesc.textureName ? decalDesc.textureName : "null",
		decalDesc.positionX, decalDesc.positionY, decalDesc.positionZ,
		decalDesc.lifeSeconds);
	dbgLog(buf);
	return entry.id;
}

//================================================================
// Trail / AfterImage（実用最低限）
//================================================================

TrailHandle CreateTrail(const TrailDesc& trailDesc) {
	int id = Internal::g_nextTrailId++;
	Internal::g_trails[id] = trailDesc;
	Internal::g_trailPoints[id] = {};

	// Seed with one point at origin as starter
	Internal::TrailPoint pt;
	pt.remainingSeconds = trailDesc.lifeSeconds;
	Internal::g_trailPoints[id].push_back(pt);

	char buf[128];
	std::snprintf(buf, sizeof(buf),
		"[FeelKit] CreateTrail: id=%d life=%.2f width=%.1f maxPts=%d\n",
		id, trailDesc.lifeSeconds, trailDesc.width, trailDesc.maxPoints);
	dbgLog(buf);
	return id;
}

void ReleaseTrail(TrailHandle handle) {
	Internal::g_trails.erase(handle);
	Internal::g_trailPoints.erase(handle);

	char buf[128];
	std::snprintf(buf, sizeof(buf),
		"[FeelKit] ReleaseTrail: id=%d\n", handle);
	dbgLog(buf);
}

AfterImageHandle CreateAfterImage(const AfterImageDesc& afterImageDesc) {
	int id = Internal::g_nextAfterImageId++;
	Internal::g_afterImages[id] = afterImageDesc;

	char logBuf[128];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] CreateAfterImage: id=%d interval=%.3f life=%.2f fade=%.1f\n",
		id, afterImageDesc.intervalSeconds, afterImageDesc.lifeSeconds,
		afterImageDesc.fadeSpeed);
	std::printf("%s", logBuf);
	return id;
}

void ReleaseAfterImage(AfterImageHandle handle) {
	Internal::g_afterImages.erase(handle);

	char logBuf[128];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] ReleaseAfterImage: id=%d\n", handle);
	std::printf("%s", logBuf);
}

//================================================================
// Emitter
//================================================================

EmitterHandle Emit(const EmitterDesc& emitterDesc) {
	int id = Internal::g_nextEmitterId++;
	Internal::EmitterRecord record;
	record.desc = emitterDesc;
	record.active = true;
	Internal::g_emitters[id] = record;

	const char* shapeNames[] = {
		"circle", "sector", "sphere", "cylinder", "box", "meshSurface"
	};
	int shapeIndex = static_cast<int>(emitterDesc.shape);
	const char* shapeName = (shapeIndex >= 0 && shapeIndex < 6)
		? shapeNames[shapeIndex] : "unknown";

	char logBuf[256];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] Emit: id=%d shape=%s radius=%.1f rate=%.0f/s life=%.1f\n",
		id, shapeName, emitterDesc.radius,
		emitterDesc.particleDesc.spawnRate,
		emitterDesc.particleDesc.lifeSeconds);
	std::printf("%s", logBuf);
	return id;
}

void StopEmit(EmitterHandle handle) {
	auto it = Internal::g_emitters.find(handle);
	if (it == Internal::g_emitters.end()) return;
	it->second.active = false;

	char logBuf[128];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] StopEmit: id=%d\n", handle);
	std::printf("%s", logBuf);
}

//================================================================
// LOD（実用最低限）
//================================================================

int PlayEffectLod(const char* effectName, const Vec3& position, float distance) {
	if (!effectName) return -1;

	// LOD switching by name suffix
	const char* lodSuffix = "";
	if (distance > 500.0f) {
		lodSuffix = "_far";
	} else if (distance > 200.0f) {
		lodSuffix = "_mid";
	}

	// Try LOD-specific name first, fall back to original
	char lodName[128];
	std::snprintf(lodName, sizeof(lodName), "%s%s", effectName, lodSuffix);

	auto it = Internal::g_effectRegistry.find(lodName);
	if (it == Internal::g_effectRegistry.end()) {
		it = Internal::g_effectRegistry.find(effectName);
		if (it == Internal::g_effectRegistry.end()) return -1;
	}

	EffectDesc lodDesc = it->second;
	if (distance > 500.0f) {
		lodDesc.frameCount = std::max(1, lodDesc.frameCount / 4);
		lodDesc.seconds *= 0.5f;
	} else if (distance > 200.0f) {
		lodDesc.frameCount = std::max(1, lodDesc.frameCount / 2);
		lodDesc.seconds *= 0.75f;
	}

	int effectId = PlayEffect(lodDesc, Vec3{position.x, position.y, position.z});

	int lodId = Internal::g_nextEffectLodId++;
	Internal::ActiveEffectLodRecord record;
	record.id = effectId;
	record.effectName = (distance > 200.0f) ? lodName : effectName;
	record.position = position;
	record.distance = distance;
	Internal::g_activeEffectsLod[lodId] = record;

	char logBuf[256];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] PlayEffectLod: name=%s dist=%.0f -> %s id=%d\n",
		effectName, distance, record.effectName.c_str(), effectId);
	std::printf("%s", logBuf);

	return effectId;
}

//================================================================
// Screen Effect（実用最低限）
//================================================================

void AddScreenEffect(const ScreenEffectDesc& effectDesc) {
	Internal::ScreenEffectEntry entry;
	entry.desc = effectDesc;
	entry.remainingSeconds = std::max(effectDesc.durationSeconds, 0.0f);
	entry.active = true;
	entry.order = static_cast<int>(Internal::g_screenEffects.size());
	Internal::g_screenEffects.push_back(entry);

	const char* name = effectDesc.effectName ? effectDesc.effectName : "null";
	char logBuf[256];
	std::snprintf(logBuf, sizeof(logBuf),
		"[FeelKit] AddScreenEffect: name=%s intensity=%.2f duration=%.1f order=%d\n",
		name, effectDesc.intensity, effectDesc.durationSeconds, entry.order);
	std::printf("%s", logBuf);
}

} // namespace FeelKit