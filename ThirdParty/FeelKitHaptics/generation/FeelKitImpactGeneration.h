#pragma once

#include "core/FeelKitCore.h"

namespace FeelKit {

// --- 衝突解析 ---
ImpactAnalysisResult AnalyzeImpact(float velocity, float mass);
ImpactAnalysisResult AnalyzeImpact(Vec3 velocity, float mass);
void GenerateImpactHaptics(float velocity, float mass);
void GenerateImpactHaptics(Vec3 velocity, float mass);
void GenerateImpactShake(float velocity, float mass);
void GenerateImpactShake(Vec3 velocity, float mass);

// --- 上位自動生成 ---
ImpactFeedbackResult GenerateImpactFeedback(float velocity, float mass, float angle, MaterialType material = MaterialType::metal);
ImpactFeedbackResult GenerateImpactFeedback(Vec3 velocity, float mass, MaterialType material = MaterialType::metal);
ExplosionFeedbackResult GenerateExplosionFeedback(Vec3 position, float radius, float power, MaterialType material = MaterialType::metal);

void GenerateHapticsFromAnimation(const char* animationName);
void GenerateFootsteps(const char* animationName);

// --- 素材→演出変換 ---
void CreateParticlesFromAudio(const char* soundPath);
void CreateParticlesFromImage(const char* imagePath);
void CreateCollisionFromImage(const char* imagePath);
void BreakImage(int textureHandle);
void ImportGif(const char* gifPath);
void SpawnTextEffect(const char* text);

// --- プロバイダ登録 ---
void SetImageDataProvider(ImageDataProvider provider);
void SetTextureDataProvider(TextureDataProvider provider);
void SetGifDataProvider(GifDataProvider provider);
void SetAnimationHapticPattern(const char* animationName,
                               const AnimationHapticPattern& pattern);

// --- 統合演出 ---
void PlayImpact(const Vec3& position, float intensity);
void PlayExplosion(const Vec3& position, float radius, float power);

} // namespace FeelKit
