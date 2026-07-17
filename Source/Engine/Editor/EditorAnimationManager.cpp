#include "EditorAnimationManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kPi = 3.14159265f;
	constexpr float kMinimumScale = 0.00001f;

	std::string GetAnimationAssetPath(
		const EditorGameObject& gameObject,
		const EditorComponent& animationComponent) {
		if (!animationComponent.assetPath.empty()) {
			return animationComponent.assetPath;
		}

		const EditorComponent* modelRenderer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ModelRenderer);

		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* meshFilter = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MeshFilter);

		if (meshFilter != nullptr) {
			return meshFilter->assetPath;
		}

		return "";
	}

	float LerpFloat(float startValue, float endValue, float interpolationRate) {
		return startValue + (endValue - startValue) * interpolationRate;
	}

	float LerpAngle(float startAngle, float endAngle, float interpolationRate) {
		const float angleDifference = std::remainder(endAngle - startAngle, kPi * 2.0f);
		return startAngle + angleDifference * interpolationRate;
	}

	Vector3 LerpVector3(const Vector3& startValue, const Vector3& endValue, float interpolationRate) {
		return {
			LerpFloat(startValue.x, endValue.x, interpolationRate),
			LerpFloat(startValue.y, endValue.y, interpolationRate),
			LerpFloat(startValue.z, endValue.z, interpolationRate)};
	}

	Vector3 LerpRotation(const Vector3& startValue, const Vector3& endValue, float interpolationRate) {
		return {
			LerpAngle(startValue.x, endValue.x, interpolationRate),
			LerpAngle(startValue.y, endValue.y, interpolationRate),
			LerpAngle(startValue.z, endValue.z, interpolationRate)};
	}

	float SafeScaleRatio(float animatedScale, float referenceScale) {
		if (std::abs(referenceScale) <= kMinimumScale) {
			return 1.0f;
		}

		return animatedScale / referenceScale;
	}
}

void EditorAnimationManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
}

void EditorAnimationManager::Start() {
	animationTimes_.clear();
	baseTransforms_.clear();
	animationClips_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* animationComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Animation);

		if (animationComponent == nullptr || !animationComponent->isActive) {
			continue;
		}

		baseTransforms_[gameObject.id] = {
			gameObject.translate,
			gameObject.rotate,
			gameObject.scale};

		if (animationComponent->animationType == 0) {
			CacheAnimationClip(gameObject, *animationComponent);
		}

		if (animationComponent->animationPlayOnAwake) {
			animationTimes_[gameObject.id] = 0.0f;
		}
	}
}

void EditorAnimationManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* animationComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Animation);
		auto animationTimeIterator = animationTimes_.find(gameObject.id);

		if (animationComponent != nullptr &&
			animationComponent->isActive &&
			animationTimeIterator != animationTimes_.end()) {
			float& playbackTime = animationTimeIterator->second;
			playbackTime += deltaTime * animationComponent->animationSpeed;
			UpdateAnimation(gameObject, *animationComponent, playbackTime);
		}

		EditorComponent* particleComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ParticleSystem);

		if (particleComponent != nullptr &&
			particleComponent->isActive &&
			particleComponent->animationPlayOnAwake) {
			float& particleTime = animationTimes_[gameObject.id | 0x20000];
			particleTime += deltaTime;
			const float particleInterval = 1.0f / (std::max)(particleComponent->particleRate, 0.1f);

			if (particleTime >= particleInterval) {
				particleTime -= particleInterval;
			}
		}

		EditorComponent* animatorComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Animator);

		if (animatorComponent != nullptr && animatorComponent->isActive) {
			float& animatorTime = animationTimes_[gameObject.id | 0x10000];
			animatorTime += deltaTime * animatorComponent->animationSpeed;
			EditorComponent animationProxy = *animatorComponent;
			animationProxy.animationType = animatorComponent->animatorState + 1;
			animationProxy.animationAmplitude = 0.5f;
			animationProxy.animationLoop = true;
			UpdateAnimation(gameObject, animationProxy, animatorTime);
		}
	}
}

void EditorAnimationManager::Stop() {
	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const auto baseTransformIterator = baseTransforms_.find(gameObject.id);

			if (baseTransformIterator == baseTransforms_.end()) {
				continue;
			}

			gameObject.translate = baseTransformIterator->second.translate;
			gameObject.rotate = baseTransformIterator->second.rotate;
			gameObject.scale = baseTransformIterator->second.scale;
		}
	}

	animationTimes_.clear();
	baseTransforms_.clear();
	animationClips_.clear();
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

void EditorAnimationManager::CacheAnimationClip(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	const std::string animationAssetPath = GetAnimationAssetPath(gameObject, component);
	ModelData modelData{};

	if (animationAssetPath.empty() ||
		!EditorAssetUtility::LoadModelAsset(animationAssetPath, modelData) ||
		modelData.animationClips.empty()) {
		return;
	}

	const int32_t maximumClipIndex = static_cast<int32_t>(modelData.animationClips.size()) - 1;
	const int32_t clipIndex = (std::clamp)(component.animationClipIndex, 0, maximumClipIndex);
	const ModelAnimationClipData& animationClip = modelData.animationClips[static_cast<size_t>(clipIndex)];

	if (!animationClip.keyframes.empty()) {
		animationClips_[gameObject.id] = animationClip;
	}
}

void EditorAnimationManager::UpdateAnimation(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float& playbackTime) {
	const auto baseTransformIterator = baseTransforms_.find(gameObject.id);

	if (baseTransformIterator == baseTransforms_.end()) {
		baseTransforms_[gameObject.id] = {
			gameObject.translate,
			gameObject.rotate,
			gameObject.scale};
	}

	if (component.animationType == 0) {
		UpdateFbxAnimation(gameObject, component, playbackTime);
		return;
	}

	const BaseTransform& baseTransform = baseTransforms_[gameObject.id];
	const float animationTime = playbackTime;
	gameObject.translate = baseTransform.translate;
	gameObject.rotate = baseTransform.rotate;
	gameObject.scale = baseTransform.scale;

	switch (component.animationType) {
	case 1:
		gameObject.translate.y = baseTransform.translate.y +
			std::sin(animationTime * kPi * 2.0f) * component.animationAmplitude;
		break;

	case 2:
		gameObject.rotate.y = baseTransform.rotate.y + animationTime * 2.0f;
		break;

	case 3: {
		const float scaleRate = 1.0f +
			std::sin(animationTime * kPi * 2.0f) * component.animationAmplitude * 0.5f;
		gameObject.scale = {
			baseTransform.scale.x * scaleRate,
			baseTransform.scale.y * scaleRate,
			baseTransform.scale.z * scaleRate};
		break;
	}

	case 4:
		gameObject.translate = {
			baseTransform.translate.x + std::sin(animationTime * kPi * 1.3f) * component.animationAmplitude,
			baseTransform.translate.y + std::sin(animationTime * kPi * 2.0f) * component.animationAmplitude * 0.5f,
			baseTransform.translate.z + std::sin(animationTime * kPi * 1.7f) * component.animationAmplitude};
		break;

	default:
		break;
	}
}

void EditorAnimationManager::UpdateFbxAnimation(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float& playbackTime) {
	const auto animationClipIterator = animationClips_.find(gameObject.id);

	if (animationClipIterator == animationClips_.end()) {
		return;
	}

	const ModelAnimationClipData& animationClip = animationClipIterator->second;
	if (animationClip.keyframes.empty() || animationClip.durationSeconds <= 0.0f) {
		return;
	}

	if (component.animationLoop) {
		playbackTime = std::fmod(playbackTime, animationClip.durationSeconds);

		if (playbackTime < 0.0f) {
			playbackTime += animationClip.durationSeconds;
		}
	}
	else {
		playbackTime = (std::clamp)(playbackTime, 0.0f, animationClip.durationSeconds);
	}

	const auto nextKeyframeIterator = std::lower_bound(
		animationClip.keyframes.begin(),
		animationClip.keyframes.end(),
		playbackTime,
		[](const ModelAnimationKeyframeData& keyframe, float targetTime) {
			return keyframe.timeSeconds < targetTime;
		});

	const ModelAnimationKeyframeData* previousKeyframe = &animationClip.keyframes.front();
	const ModelAnimationKeyframeData* nextKeyframe = previousKeyframe;

	if (nextKeyframeIterator == animationClip.keyframes.end()) {
		previousKeyframe = &animationClip.keyframes.back();
		nextKeyframe = previousKeyframe;
	}
	else {
		nextKeyframe = &(*nextKeyframeIterator);

		if (nextKeyframeIterator != animationClip.keyframes.begin()) {
			previousKeyframe = &(*(nextKeyframeIterator - 1));
		}
	}

	const float keyframeDuration = nextKeyframe->timeSeconds - previousKeyframe->timeSeconds;
	const float interpolationRate = keyframeDuration > 0.00001f
		? (std::clamp)(
			(playbackTime - previousKeyframe->timeSeconds) / keyframeDuration,
			0.0f,
			1.0f)
		: 0.0f;
	const Vector3 animatedTranslation = LerpVector3(
		previousKeyframe->translation,
		nextKeyframe->translation,
		interpolationRate);
	const Vector3 animatedRotation = LerpRotation(
		previousKeyframe->rotation,
		nextKeyframe->rotation,
		interpolationRate);
	const Vector3 animatedScale = LerpVector3(
		previousKeyframe->scale,
		nextKeyframe->scale,
		interpolationRate);
	const ModelAnimationKeyframeData& referenceKeyframe = animationClip.keyframes.front();
	const BaseTransform& baseTransform = baseTransforms_[gameObject.id];

	gameObject.translate = {
		baseTransform.translate.x + animatedTranslation.x - referenceKeyframe.translation.x,
		baseTransform.translate.y + animatedTranslation.y - referenceKeyframe.translation.y,
		baseTransform.translate.z + animatedTranslation.z - referenceKeyframe.translation.z};
	gameObject.rotate = {
		baseTransform.rotate.x + animatedRotation.x - referenceKeyframe.rotation.x,
		baseTransform.rotate.y + animatedRotation.y - referenceKeyframe.rotation.y,
		baseTransform.rotate.z + animatedRotation.z - referenceKeyframe.rotation.z};
	gameObject.scale = {
		baseTransform.scale.x * SafeScaleRatio(animatedScale.x, referenceKeyframe.scale.x),
		baseTransform.scale.y * SafeScaleRatio(animatedScale.y, referenceKeyframe.scale.y),
		baseTransform.scale.z * SafeScaleRatio(animatedScale.z, referenceKeyframe.scale.z)};
}
