#pragma once

#include "core/FeelKitCore.h"

namespace FeelKit {

struct ExplosionRequest {
	std::string effectName;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float power = 1.0f;
};

struct SparkRequest {
	std::string effectName;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float power = 1.0f;
};

struct SmokeRequest {
	std::string effectName;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float power = 1.0f;
	int durationMs = 0;
};

struct ImpactEffectRequest {
	std::string effectName;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	float power = 1.0f;
};

struct ParticleRequest {
	std::string particleName;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
	int count = 1;
};

// --- Callback setters ---
void SetEffectDrawCallback(const EffectDrawCallback& callback);
void SetExplosionCallback(const ExplosionCallback& callback);
void SetSparkCallback(const SparkCallback& callback);
void SetSmokeCallback(const SmokeCallback& callback);
void SetImpactEffectCallback(const ImpactEffectCallback& callback);
void SetParticleCallback(const ParticleCallback& callback);

// --- Callback-based play ---
bool PlayExplosion(const ExplosionRequest& request);
bool PlaySpark(const SparkRequest& request);
bool PlaySmoke(const SmokeRequest& request);
bool PlayImpactEffect(const ImpactEffectRequest& request);
bool PlayParticle(const ParticleRequest& request);

// --- Direct effect playback ---
int PlayEffect(int textureHandle, float x, float y, int frameCount,
               float seconds, bool loop);
int PlayEffect(int textureHandle,
               float x, float y,
               int frameWidth, int frameHeight, int frameCount,
               float seconds, bool loop);
int PlayEffect(const EffectDesc& effectDesc, Vec2 pos);
int PlayEffect(const EffectDesc& effectDesc, Vec3 pos);
bool LoadEffect(const char* effectName, const EffectDesc& effectDesc);
int PlayEffect(const char* effectName, Vec2 pos);
int PlayEffect(const char* effectName, Vec3 pos);

// --- Effect Pool ---
void EffectPoolSetMax(int maxEffects);
int EffectPoolGetActiveCount();
void EffectPoolKillAll();
void EffectPoolKill(int effectId);
bool EffectPoolIsAlive(int effectId);

// --- Decal ---
int SpawnDecal(const DecalDesc& decalDesc);

// --- Trail / AfterImage ---
TrailHandle CreateTrail(const TrailDesc& trailDesc);
void ReleaseTrail(TrailHandle handle);
AfterImageHandle CreateAfterImage(const AfterImageDesc& afterImageDesc);
void ReleaseAfterImage(AfterImageHandle handle);

// --- Emitter ---
EmitterHandle Emit(const EmitterDesc& emitterDesc);
void StopEmit(EmitterHandle handle);

// --- LOD ---
int PlayEffectLod(const char* effectName, const Vec3& position, float distance);

// --- Screen Effect ---
void AddScreenEffect(const ScreenEffectDesc& effectDesc);

} // namespace FeelKit
