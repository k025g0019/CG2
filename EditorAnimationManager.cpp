#include "EditorAnimationManager.h"
#include "EditorComponentUtility.h"

namespace {
	constexpr float kPi = 3.14159265f;
}

void EditorAnimationManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
}

void EditorAnimationManager::Start() {
	animationTimes_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) return;

	for (const auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;
		const auto* anim = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Animation);
		if (anim != nullptr && anim->isActive && anim->animationPlayOnAwake) {
			animationTimes_[gameObject.id] = 0.0f;
		}
	}
}

void EditorAnimationManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) return;

	for (auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;

		auto* anim = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Animation);
		if (anim != nullptr && anim->isActive) {
			float& time = animationTimes_[gameObject.id];
			time += deltaTime * anim->animationSpeed;
			UpdateAnimation(gameObject, *anim, time);
		}

		auto* particle = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ParticleSystem);
		if (particle != nullptr && particle->isActive && particle->animationPlayOnAwake) {
			float& time = animationTimes_[gameObject.id | 0x20000];
			time += deltaTime;
			float interval = 1.0f / (std::max)(particle->particleRate, 0.1f);
			if (time >= interval) {
				time -= interval;
				gameObject.translate.y += std::sin(time * 100.0f) * 0.001f;
			}
		}

		auto* animator = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Animator);
		if (animator != nullptr && animator->isActive) {
			float& time = animationTimes_[gameObject.id | 0x10000];
			time += deltaTime * animator->animationSpeed;
			EditorComponent proxy = *animator;
			proxy.animationType = animator->animatorState + 1;
			proxy.animationAmplitude = 0.5f;
			proxy.animationLoop = true;
			UpdateAnimation(gameObject, proxy, time);
		}
	}
}

void EditorAnimationManager::Stop() {
	animationTimes_.clear();
	isStarted_ = false;
}

bool EditorAnimationManager::IsAnimationPlaying(int32_t gameObjectId) const {
	return animationTimes_.find(gameObjectId) != animationTimes_.end();
}

float EditorAnimationManager::GetAnimationTime(int32_t gameObjectId) const {
	const auto timeIterator = animationTimes_.find(gameObjectId);
	if (timeIterator == animationTimes_.end()) {
		return 0.0f;
	}

	return timeIterator->second;
}

void EditorAnimationManager::UpdateAnimation(EditorGameObject& gameObject, EditorComponent& component, float deltaTime) {
	float t = deltaTime;

	switch (component.animationType) {
	case 0:
		break;

	case 1: {
		float offset = std::sin(t * kPi * 2.0f) * component.animationAmplitude;
		gameObject.translate.y += offset;
		break;
	}

	case 2: {
		float angle = t * 2.0f;
		gameObject.rotate.y = angle;
		break;
	}

	case 3: {
		float scale = 1.0f + std::sin(t * kPi * 2.0f) * component.animationAmplitude * 0.5f;
		gameObject.scale = {scale, scale, scale};
		break;
	}

	case 4: {
		float offsetX = std::sin(t * kPi * 1.3f) * component.animationAmplitude;
		float offsetY = std::sin(t * kPi * 2.0f) * component.animationAmplitude * 0.5f;
		float offsetZ = std::sin(t * kPi * 1.7f) * component.animationAmplitude;
		gameObject.translate.x += offsetX;
		gameObject.translate.y += offsetY;
		gameObject.translate.z += offsetZ;
		break;
	}

	default:
		break;
	}
}
