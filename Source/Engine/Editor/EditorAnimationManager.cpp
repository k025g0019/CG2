#include "EditorAnimationManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorScriptManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace {
	constexpr float kPi = 3.14159265f;
	constexpr float kTwoPi = kPi * 2.0f;
	constexpr float kDegreesToRadians = kPi / 180.0f;
	constexpr float kRadiansToDegrees = 180.0f / kPi;
	constexpr float kMinimumValue = 0.00001f;

	std::string GetModelAnimationAssetPath(const EditorGameObject& gameObject) {
		const EditorComponent* modelRenderer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ModelRenderer);

		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* meshFilter = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MeshFilter);

		return meshFilter != nullptr ? meshFilter->assetPath : "";
	}

	std::string GetAnimationAssetPath(
		const EditorGameObject& gameObject,
		const EditorComponent& animationComponent) {
		if (!animationComponent.assetPath.empty() &&
			!EditorAssetUtility::HasExtension(animationComponent.assetPath, ".animgraph")) {
			return animationComponent.assetPath;
		}

		return GetModelAnimationAssetPath(gameObject);
	}

	float LerpFloat(float startValue, float endValue, float interpolationRate) {
		return startValue + (endValue - startValue) * interpolationRate;
	}

	float LerpAngle(float startAngle, float endAngle, float interpolationRate) {
		const float angleDifference = std::remainder(endAngle - startAngle, kTwoPi);
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
		return std::abs(referenceScale) <= kMinimumValue ? 1.0f : animatedScale / referenceScale;
	}

	float Length2(const Vector2& value) {
		return std::sqrt(value.x * value.x + value.y * value.y);
	}

	float NormalizePositiveAngle(float angle) {
		float normalizedAngle = std::fmod(angle, kTwoPi);

		if (normalizedAngle < 0.0f) {
			normalizedAngle += kTwoPi;
		}

		return normalizedAngle;
	}

	float PositiveAngleDistance(float startAngle, float endAngle) {
		return NormalizePositiveAngle(endAngle - startAngle);
	}

	bool IsCrossedEventTime(
		float previousTime,
		float currentTime,
		float eventTime,
		float duration,
		bool loop) {
		if (duration <= kMinimumValue) {
			return false;
		}

		if (!loop) {
			return previousTime < eventTime && eventTime <= currentTime;
		}

		if (currentTime - previousTime >= duration) {
			return true;
		}

		const float previousLoopTime = std::fmod((std::max)(previousTime, 0.0f), duration);
		const float currentLoopTime = std::fmod((std::max)(currentTime, 0.0f), duration);
		const bool crossedLoopBoundary =
			static_cast<int32_t>(previousTime / duration) != static_cast<int32_t>(currentTime / duration);

		if (crossedLoopBoundary) {
			return eventTime > previousLoopTime || eventTime <= currentLoopTime;
		}

		return previousLoopTime < eventTime && eventTime <= currentLoopTime;
	}
}

void EditorAnimationManager::Initialize(
	EditorScene* editorScene,
	EditorEffectManager* effectManager,
	EditorScriptManager* scriptManager,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	effectManager_ = effectManager;
	scriptManager_ = scriptManager;
	consoleMessages_ = consoleMessages;
}

void EditorAnimationManager::Start() {
	animationTimes_.clear();
	baseTransforms_.clear();
	animationClips_.clear();
	propertyAnimationRuntimes_.clear();
	animatorRuntimes_.clear();
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
		const EditorComponent* animatorComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Animator);

		if ((animationComponent == nullptr || !animationComponent->isActive) &&
			(animatorComponent == nullptr || !animatorComponent->isActive)) {
			continue;
		}

		baseTransforms_[gameObject.id] = {
			gameObject.translate,
			gameObject.rotate,
			gameObject.scale};

		if (animationComponent != nullptr && animationComponent->isActive) {
			if (animationComponent->animationType == 0) {
				if (EditorAssetUtility::HasExtension(animationComponent->assetPath, ".animclip")) {
					CachePropertyAnimationClip(gameObject, *animationComponent);
				}
				else {
					CacheAnimationClip(gameObject, *animationComponent);
				}
			}

			if (animationComponent->animationPlayOnAwake) {
				animationTimes_[gameObject.id] = 0.0f;
			}
		}

		if (animatorComponent != nullptr && animatorComponent->isActive) {
			StartAnimator(gameObject, *animatorComponent);
		}
	}
}

void EditorAnimationManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive || gameObject.name.rfind("__EffectParticle_", 0u) == 0u) {
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

		EditorComponent* animatorComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Animator);

		if (animatorComponent != nullptr && animatorComponent->isActive) {
			UpdateAnimator(gameObject, *animatorComponent, deltaTime);
		}
	}
}

void EditorAnimationManager::Stop() {
	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const auto propertyRuntimeIterator = propertyAnimationRuntimes_.find(gameObject.id);
			if (propertyRuntimeIterator != propertyAnimationRuntimes_.end()) {
				for (const auto& baseValuePair : propertyRuntimeIterator->second.baseValues) {
					SetAnimatedProperty(
						gameObject,
						static_cast<AnimationPropertyTarget>(baseValuePair.first),
						baseValuePair.second);
				}
			}

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
	propertyAnimationRuntimes_.clear();
	animatorRuntimes_.clear();
	isStarted_ = false;
}

bool EditorAnimationManager::IsAnimationPlaying(int32_t gameObjectId) const {
	return animationTimes_.find(gameObjectId) != animationTimes_.end() ||
		animatorRuntimes_.find(gameObjectId) != animatorRuntimes_.end();
}

float EditorAnimationManager::GetAnimationTime(int32_t gameObjectId) const {
	const auto animatorIterator = animatorRuntimes_.find(gameObjectId);

	if (animatorIterator != animatorRuntimes_.end()) {
		return animatorIterator->second.stateTime;
	}

	const auto timeIterator = animationTimes_.find(gameObjectId);
	return timeIterator != animationTimes_.end() ? timeIterator->second : 0.0f;
}

std::string EditorAnimationManager::GetAnimatorStateName(int32_t gameObjectId) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return "停止中";
	}

	const AnimatorRuntimeInstance& runtime = runtimeIterator->second;
	if (runtime.currentState < 0 || runtime.currentState >= static_cast<int32_t>(runtime.graph.states.size())) {
		return "無効な State";
	}

	return runtime.graph.states[static_cast<size_t>(runtime.currentState)].name;
}

bool EditorAnimationManager::GetAnimatorParameters(
	int32_t gameObjectId,
	std::vector<std::pair<std::string, AnimatorParameterValue>>& parameters) const {
	parameters.clear();
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	parameters.reserve(runtimeIterator->second.parameters.size());
	for (const auto& parameterPair : runtimeIterator->second.parameters) {
		parameters.emplace_back(parameterPair.first, parameterPair.second);
	}

	std::sort(
		parameters.begin(),
		parameters.end(),
		[](const auto& leftParameter, const auto& rightParameter) {
			return leftParameter.first < rightParameter.first;
		});
	return true;
}

bool EditorAnimationManager::GetFloat(
	int32_t gameObjectId,
	const std::string& parameterName,
	float& value) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	const auto parameterIterator = runtimeIterator->second.parameters.find(parameterName);
	if (parameterIterator == runtimeIterator->second.parameters.end() ||
		parameterIterator->second.type != AnimatorParameterType::Float) {
		return false;
	}

	value = parameterIterator->second.floatValue;
	return true;
}

bool EditorAnimationManager::GetInt(
	int32_t gameObjectId,
	const std::string& parameterName,
	int32_t& value) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	const auto parameterIterator = runtimeIterator->second.parameters.find(parameterName);
	if (parameterIterator == runtimeIterator->second.parameters.end() ||
		parameterIterator->second.type != AnimatorParameterType::Int) {
		return false;
	}

	value = parameterIterator->second.intValue;
	return true;
}

bool EditorAnimationManager::GetBool(
	int32_t gameObjectId,
	const std::string& parameterName,
	bool& value) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	const auto parameterIterator = runtimeIterator->second.parameters.find(parameterName);
	if (parameterIterator == runtimeIterator->second.parameters.end() ||
		(parameterIterator->second.type != AnimatorParameterType::Bool &&
		 parameterIterator->second.type != AnimatorParameterType::Trigger)) {
		return false;
	}

	value = parameterIterator->second.boolValue;
	return true;
}

bool EditorAnimationManager::GetVector2(
	int32_t gameObjectId,
	const std::string& parameterName,
	Vector2& value) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	const auto parameterIterator = runtimeIterator->second.parameters.find(parameterName);
	if (parameterIterator == runtimeIterator->second.parameters.end() ||
		parameterIterator->second.type != AnimatorParameterType::Vector2) {
		return false;
	}

	value = parameterIterator->second.vector2Value;
	return true;
}

bool EditorAnimationManager::GetVector3(
	int32_t gameObjectId,
	const std::string& parameterName,
	Vector3& value) const {
	const auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return false;
	}

	const auto parameterIterator = runtimeIterator->second.parameters.find(parameterName);
	if (parameterIterator == runtimeIterator->second.parameters.end() ||
		parameterIterator->second.type != AnimatorParameterType::Vector3) {
		return false;
	}

	value = parameterIterator->second.vector3Value;
	return true;
}

bool EditorAnimationManager::SetFloat(
	int32_t gameObjectId,
	const std::string& parameterName,
	float value) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Float;
	parameter->floatValue = value;
	return true;
}

bool EditorAnimationManager::SetInt(
	int32_t gameObjectId,
	const std::string& parameterName,
	int32_t value) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Int;
	parameter->intValue = value;
	return true;
}

bool EditorAnimationManager::SetBool(
	int32_t gameObjectId,
	const std::string& parameterName,
	bool value) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Bool;
	parameter->boolValue = value;
	return true;
}

bool EditorAnimationManager::SetTrigger(
	int32_t gameObjectId,
	const std::string& parameterName) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Trigger;
	parameter->boolValue = true;
	return true;
}

bool EditorAnimationManager::ResetTrigger(
	int32_t gameObjectId,
	const std::string& parameterName) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr || parameter->type != AnimatorParameterType::Trigger) {
		return false;
	}

	parameter->boolValue = false;
	return true;
}

bool EditorAnimationManager::SetVector2(
	int32_t gameObjectId,
	const std::string& parameterName,
	const Vector2& value) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Vector2;
	parameter->vector2Value = value;
	return true;
}

bool EditorAnimationManager::SetVector3(
	int32_t gameObjectId,
	const std::string& parameterName,
	const Vector3& value) {
	AnimatorParameterValue* parameter = FindParameter(gameObjectId, parameterName);

	if (parameter == nullptr) {
		return false;
	}

	parameter->type = AnimatorParameterType::Vector3;
	parameter->vector3Value = value;
	return true;
}

bool EditorAnimationManager::PlayAnimation(int32_t gameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr || !gameObject->isActive) {
		return false;
	}

	const EditorComponent* animationComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Animation);
	if (animationComponent == nullptr || !animationComponent->isActive) {
		return false;
	}

	if (baseTransforms_.find(gameObjectId) == baseTransforms_.end()) {
		baseTransforms_[gameObjectId] = {
			gameObject->translate,
			gameObject->rotate,
			gameObject->scale};
	}

	if (animationComponent->animationType == 0) {
		if (EditorAssetUtility::HasExtension(animationComponent->assetPath, ".animclip")) {
			CachePropertyAnimationClip(*gameObject, *animationComponent);
		}
		else {
			CacheAnimationClip(*gameObject, *animationComponent);
		}
	}

	animationTimes_[gameObjectId] = 0.0f;
	return true;
}

void EditorAnimationManager::StopAnimation(int32_t gameObjectId) {
	animationTimes_.erase(gameObjectId);

	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	const auto propertyRuntimeIterator = propertyAnimationRuntimes_.find(gameObjectId);
	if (propertyRuntimeIterator != propertyAnimationRuntimes_.end()) {
		for (const auto& baseValuePair : propertyRuntimeIterator->second.baseValues) {
			SetAnimatedProperty(
				*gameObject,
				static_cast<AnimationPropertyTarget>(baseValuePair.first),
				baseValuePair.second);
		}
	}

	if (animatorRuntimes_.find(gameObjectId) == animatorRuntimes_.end()) {
		const auto baseTransformIterator = baseTransforms_.find(gameObjectId);

		if (baseTransformIterator != baseTransforms_.end()) {
			gameObject->translate = baseTransformIterator->second.translate;
			gameObject->rotate = baseTransformIterator->second.rotate;
			gameObject->scale = baseTransformIterator->second.scale;
		}
	}
}

bool EditorAnimationManager::SetAnimationTime(int32_t gameObjectId, float playbackTime) {
	const float clampedTime = (std::max)(playbackTime, 0.0f);
	bool hasRuntime = false;
	const auto animationTimeIterator = animationTimes_.find(gameObjectId);

	if (animationTimeIterator != animationTimes_.end()) {
		animationTimeIterator->second = clampedTime;
		hasRuntime = true;
	}

	const auto animatorIterator = animatorRuntimes_.find(gameObjectId);
	if (animatorIterator != animatorRuntimes_.end()) {
		animatorIterator->second.stateTime = clampedTime;
		animatorIterator->second.previousEventTime = clampedTime;
		hasRuntime = true;
	}

	return hasRuntime;
}

bool EditorAnimationManager::SetAnimationSpeed(int32_t gameObjectId, float playbackSpeed) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	EditorComponent* animationComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Animation);
	if (animationComponent == nullptr) {
		return false;
	}

	animationComponent->animationSpeed = (std::max)(playbackSpeed, 0.0f);
	return true;
}

bool EditorAnimationManager::PlayAction(
	int32_t gameObjectId,
	int32_t clipIndex,
	float blendIn,
	float blendOut,
	float playbackSpeed,
	int32_t priority,
	bool loop) {
	auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end() ||
		clipIndex < 0 ||
		clipIndex >= static_cast<int32_t>(runtimeIterator->second.clips.size())) {
		return false;
	}

	ActiveAnimationAction& currentAction = runtimeIterator->second.action;
	if (currentAction.isActive && currentAction.priority > priority) {
		return false;
	}

	currentAction.clipIndex = clipIndex;
	currentAction.playbackTime = 0.0f;
	currentAction.blendIn = (std::max)(blendIn, 0.0f);
	currentAction.blendOut = (std::max)(blendOut, 0.0f);
	currentAction.playbackSpeed = playbackSpeed;
	currentAction.priority = priority;
	currentAction.loop = loop;
	currentAction.isActive = true;
	return true;
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

void EditorAnimationManager::CachePropertyAnimationClip(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	PropertyAnimationRuntime runtime{};

	if (!runtime.clip.LoadFromJson(component.assetPath)) {
		PushConsoleMessage("Animation Clip 読み込み失敗: " + component.assetPath);
		return;
	}

	for (const PropertyAnimationTrack& track : runtime.clip.tracks) {
		float baseValue = 0.0f;

		if (GetAnimatedProperty(gameObject, track.target, baseValue)) {
			runtime.baseValues[static_cast<int32_t>(track.target)] = baseValue;
		}
	}

	propertyAnimationRuntimes_[gameObject.id] = std::move(runtime);
}

void EditorAnimationManager::StartAnimator(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	AnimatorRuntimeInstance runtime{};
	const std::string modelAssetPath = GetModelAnimationAssetPath(gameObject);
	ModelData modelData{};

	if (!modelAssetPath.empty() && EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData)) {
		runtime.clips = modelData.animationClips;
	}

	const bool hasGraphAsset =
		!component.assetPath.empty() &&
		EditorAssetUtility::HasExtension(component.assetPath, ".animgraph");
	const bool isGraphLoaded = hasGraphAsset && runtime.graph.LoadFromJson(component.assetPath);

	if (!isGraphLoaded) {
		runtime.graph.BuildDefaultDirectionalGraph(
			component.animatorIdleClipIndex,
			component.animatorForwardClipIndex,
			component.animatorBackwardClipIndex,
			component.animatorLeftClipIndex,
			component.animatorRightClipIndex);

		if (hasGraphAsset) {
			PushConsoleMessage("Animator Graph 読み込み失敗: " + component.assetPath + " / 既定方向 Blend を使用します。");
		}
	}

	for (const AnimationGraphParameter& parameter : runtime.graph.parameters) {
		runtime.parameters[parameter.name] = parameter.defaultValue;
	}

	// Inspector から直接調整する標準 Parameter は、Graph に宣言がなくても必ず用意する。
	runtime.parameters.try_emplace("MoveX", AnimatorParameterValue{});
	runtime.parameters.try_emplace("MoveY", AnimatorParameterValue{});
	runtime.parameters.try_emplace("Speed", AnimatorParameterValue{});
	runtime.currentState = (std::clamp)(
		runtime.graph.entryState,
		0,
		(static_cast<int32_t>(runtime.graph.states.size()) - 1));
	runtime.parameters["MoveX"].floatValue = component.animatorMoveX;
	runtime.parameters["MoveY"].floatValue = component.animatorMoveY;
	runtime.parameters["Speed"].floatValue = component.animatorSpeedParameter;
	animatorRuntimes_[gameObject.id] = runtime;
}

void EditorAnimationManager::UpdateAnimation(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float& playbackTime) {
	if (baseTransforms_.find(gameObject.id) == baseTransforms_.end()) {
		baseTransforms_[gameObject.id] = {gameObject.translate, gameObject.rotate, gameObject.scale};
	}

	if (component.animationType == 0) {
		if (propertyAnimationRuntimes_.find(gameObject.id) != propertyAnimationRuntimes_.end()) {
			UpdatePropertyAnimation(gameObject, component, playbackTime);
			return;
		}

		UpdateFbxAnimation(gameObject, component, playbackTime);
		return;
	}

	const BaseTransform& baseTransform = baseTransforms_[gameObject.id];
	gameObject.translate = baseTransform.translate;
	gameObject.rotate = baseTransform.rotate;
	gameObject.scale = baseTransform.scale;

	switch (component.animationType) {
	case 1:
		gameObject.translate.y = baseTransform.translate.y +
			std::sin(playbackTime * kTwoPi) * component.animationAmplitude;
		break;

	case 2:
		gameObject.rotate.y = baseTransform.rotate.y + playbackTime * 2.0f;
		break;

	case 3: {
		const float scaleRate = 1.0f +
			std::sin(playbackTime * kTwoPi) * component.animationAmplitude * 0.5f;
		gameObject.scale = {
			baseTransform.scale.x * scaleRate,
			baseTransform.scale.y * scaleRate,
			baseTransform.scale.z * scaleRate};
		break;
	}

	case 4:
		gameObject.translate = {
			baseTransform.translate.x + std::sin(playbackTime * kPi * 1.3f) * component.animationAmplitude,
			baseTransform.translate.y + std::sin(playbackTime * kTwoPi) * component.animationAmplitude * 0.5f,
			baseTransform.translate.z + std::sin(playbackTime * kPi * 1.7f) * component.animationAmplitude};
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

	AnimatorRuntimeInstance temporaryRuntime{};
	temporaryRuntime.clips.push_back(animationClipIterator->second);
	const SampledTransform pose = SampleClip(temporaryRuntime, 0, playbackTime, component.animationLoop);
	const SampledTransform referencePose = SampleClip(temporaryRuntime, 0, 0.0f, false);

	if (!pose.isValid || !referencePose.isValid) {
		return;
	}

	if (!component.animationLoop) {
		playbackTime = (std::min)(playbackTime, animationClipIterator->second.durationSeconds);
	}

	const BaseTransform& baseTransform = baseTransforms_[gameObject.id];
	gameObject.translate = {
		baseTransform.translate.x + pose.translation.x - referencePose.translation.x,
		baseTransform.translate.y + pose.translation.y - referencePose.translation.y,
		baseTransform.translate.z + pose.translation.z - referencePose.translation.z};
	gameObject.rotate = {
		baseTransform.rotate.x + pose.rotation.x - referencePose.rotation.x,
		baseTransform.rotate.y + pose.rotation.y - referencePose.rotation.y,
		baseTransform.rotate.z + pose.rotation.z - referencePose.rotation.z};
	gameObject.scale = {
		baseTransform.scale.x * SafeScaleRatio(pose.scale.x, referencePose.scale.x),
		baseTransform.scale.y * SafeScaleRatio(pose.scale.y, referencePose.scale.y),
		baseTransform.scale.z * SafeScaleRatio(pose.scale.z, referencePose.scale.z)};
}

void EditorAnimationManager::UpdatePropertyAnimation(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float playbackTime) {
	auto runtimeIterator = propertyAnimationRuntimes_.find(gameObject.id);

	if (runtimeIterator == propertyAnimationRuntimes_.end()) {
		return;
	}

	PropertyAnimationRuntime& runtime = runtimeIterator->second;
	const bool isLooping = component.animationLoop && runtime.clip.loop;
	const float clipDuration = (std::max)(runtime.clip.durationSeconds, kMinimumValue);
	float sampleTime = playbackTime;

	if (isLooping) {
		sampleTime = std::fmod((std::max)(playbackTime, 0.0f), clipDuration);
	}
	else {
		sampleTime = (std::clamp)(playbackTime, 0.0f, clipDuration);
	}

	for (const PropertyAnimationTrack& track : runtime.clip.tracks) {
		const auto baseValueIterator = runtime.baseValues.find(static_cast<int32_t>(track.target));

		if (baseValueIterator == runtime.baseValues.end()) {
			continue;
		}

		const float sampledValue = runtime.clip.SampleTrack(track, sampleTime);
		float writtenValue = sampledValue;

		if (track.writeMode == AnimationPropertyWriteMode::Additive) {
			writtenValue = baseValueIterator->second + sampledValue;
		}
		else if (track.writeMode == AnimationPropertyWriteMode::Multiply) {
			writtenValue = baseValueIterator->second * sampledValue;
		}

		SetAnimatedProperty(gameObject, track.target, writtenValue);
	}

	for (const PropertyAnimationEvent& animationEvent : runtime.clip.events) {
		if (!IsCrossedEventTime(
			runtime.previousEventTime,
			playbackTime,
			animationEvent.time,
			clipDuration,
			isLooping)) {
			continue;
		}

		if (!animationEvent.effectAssetPath.empty() && effectManager_ != nullptr) {
			effectManager_->PlayEffectAt(
				gameObject.id,
				animationEvent.effectAssetPath,
				animationEvent.localOffset);
		}

		if (scriptManager_ != nullptr) {
			scriptManager_->DispatchAnimationEvent(
				gameObject.id,
				animationEvent.name,
				animationEvent.time,
				animationEvent.effectAssetPath,
				animationEvent.localOffset);
		}

		PushConsoleMessage(
			"Animation Event: " + gameObject.name + " / " + animationEvent.name +
			" / " + std::to_string(animationEvent.time) + "秒");
	}

	runtime.previousEventTime = playbackTime;
}

void EditorAnimationManager::UpdateAnimator(
	EditorGameObject& gameObject,
	EditorComponent& component,
	float deltaTime) {
	auto runtimeIterator = animatorRuntimes_.find(gameObject.id);

	if (runtimeIterator == animatorRuntimes_.end()) {
		StartAnimator(gameObject, component);
		runtimeIterator = animatorRuntimes_.find(gameObject.id);
	}

	if (runtimeIterator == animatorRuntimes_.end() || runtimeIterator->second.graph.states.empty()) {
		return;
	}

	AnimatorRuntimeInstance& runtime = runtimeIterator->second;
	UpdateAutomaticParameters(gameObject, component, runtime);
	EvaluateStateMachine(runtime, component);
	const int32_t currentStateIndex = (std::clamp)(
		runtime.currentState,
		0,
		static_cast<int32_t>(runtime.graph.states.size()) - 1);
	const AnimationGraphState& currentState = runtime.graph.states[static_cast<size_t>(currentStateIndex)];
	const float previousStateTime = runtime.stateTime;
	runtime.stateTime += deltaTime * component.animationSpeed;
	SampledTransform pose = SampleState(runtime, currentStateIndex, runtime.stateTime);

	if (runtime.previousState >= 0 && runtime.transitionDuration > kMinimumValue) {
		runtime.previousStateTime += deltaTime * component.animationSpeed;
		runtime.transitionTime += deltaTime;
		const SampledTransform previousPose = SampleState(
			runtime,
			runtime.previousState,
			runtime.previousStateTime);
		const float transitionRate = (std::clamp)(
			runtime.transitionTime / runtime.transitionDuration,
			0.0f,
			1.0f);

		if (previousPose.isValid && pose.isValid) {
			pose.translation = LerpVector3(previousPose.translation, pose.translation, transitionRate);
			pose.rotation = LerpRotation(previousPose.rotation, pose.rotation, transitionRate);
			pose.scale = LerpVector3(previousPose.scale, pose.scale, transitionRate);
		}

		if (transitionRate >= 1.0f) {
			runtime.previousState = -1;
			runtime.transitionTime = 0.0f;
			runtime.transitionDuration = 0.0f;
		}
	}

	const bool wasActionActive = runtime.action.isActive;
	const int32_t actionClipIndex = runtime.action.clipIndex;
	const float previousActionTime = runtime.action.playbackTime;
	const bool isActionLooping = runtime.action.loop;
	UpdateAction(runtime, deltaTime, pose);
	ApplyAnimatorPose(gameObject, component, runtime, pose);

	int32_t eventClipIndex = currentState.clipIndex;
	if (!currentState.blendSamples.empty()) {
		if (currentState.blendTreeType == AnimationBlendTreeType::Direct) {
			float bestWeight = -(std::numeric_limits<float>::max)();

			for (const AnimationBlendSample& sample : currentState.blendSamples) {
				const float sampleWeight = runtime.parameters[sample.weightParameter].floatValue;

				if (sampleWeight > bestWeight) {
					bestWeight = sampleWeight;
					eventClipIndex = sample.clipIndex;
				}
			}
		}
		else if (currentState.blendTreeType == AnimationBlendTreeType::Blend1D) {
			float bestDistance = (std::numeric_limits<float>::max)();
			const float parameterValue = runtime.parameters[currentState.blendParameter].floatValue;

			for (const AnimationBlendSample& sample : currentState.blendSamples) {
				const float distance = std::abs(sample.position.x - parameterValue);

				if (distance < bestDistance) {
					bestDistance = distance;
					eventClipIndex = sample.clipIndex;
				}
			}
		}
		else {
			float bestDistance = (std::numeric_limits<float>::max)();
			const Vector2 parameterPoint{
				runtime.parameters[currentState.blendParameterX].floatValue,
				runtime.parameters[currentState.blendParameterY].floatValue};

			for (const AnimationBlendSample& sample : currentState.blendSamples) {
				const float differenceX = sample.position.x - parameterPoint.x;
				const float differenceY = sample.position.y - parameterPoint.y;
				const float distance = differenceX * differenceX + differenceY * differenceY;

				if (distance < bestDistance) {
					bestDistance = distance;
					eventClipIndex = sample.clipIndex;
				}
			}
		}
	}

	DispatchAnimationEvents(
		gameObject,
		runtime,
		eventClipIndex,
		previousStateTime * currentState.playbackSpeed,
		runtime.stateTime * currentState.playbackSpeed,
		currentState.loop);

	if (wasActionActive) {
		DispatchAnimationEvents(
			gameObject,
			runtime,
			actionClipIndex,
			previousActionTime,
			runtime.action.playbackTime,
			isActionLooping);
	}

	runtime.previousEventTime = runtime.stateTime;
	component.animatorState = runtime.currentState;
	component.animatorMoveX = runtime.parameters["MoveX"].floatValue;
	component.animatorMoveY = runtime.parameters["MoveY"].floatValue;
	component.animatorSpeedParameter = runtime.parameters["Speed"].floatValue;
}

void EditorAnimationManager::UpdateAutomaticParameters(
	EditorGameObject& gameObject,
	EditorComponent& component,
	AnimatorRuntimeInstance& runtime) {
	if (!component.animatorAutoVelocity) {
		runtime.parameters["MoveX"].floatValue = component.animatorMoveX;
		runtime.parameters["MoveY"].floatValue = component.animatorMoveY;
		runtime.parameters["Speed"].floatValue = component.animatorSpeedParameter;
		return;
	}

	Vector3 worldVelocity{0.0f, 0.0f, 0.0f};
	const EditorComponentType velocityComponentTypes[] = {
		EditorComponentType::RigidBody,
		EditorComponentType::CharacterController,
		EditorComponentType::LocalMove,
		EditorComponentType::RollingMove};

	for (EditorComponentType componentType : velocityComponentTypes) {
		const EditorComponent* movementComponent = EditorComponentUtility::FindComponent(gameObject, componentType);

		if (movementComponent != nullptr && movementComponent->isActive) {
			worldVelocity = movementComponent->velocity;
			break;
		}
	}

	// Yaw の逆回転を掛け、ワールド速度をキャラクター基準の左右・前後へ変換する。
	const float cosineYaw = std::cos(gameObject.rotate.y);
	const float sineYaw = std::sin(gameObject.rotate.y);
	const float localVelocityX = cosineYaw * worldVelocity.x - sineYaw * worldVelocity.z;
	const float localVelocityZ = sineYaw * worldVelocity.x + cosineYaw * worldVelocity.z;
	const float planarSpeed = std::sqrt(localVelocityX * localVelocityX + localVelocityZ * localVelocityZ);
	const float normalizeScale = planarSpeed > 1.0f ? 1.0f / planarSpeed : 1.0f;
	runtime.parameters["MoveX"].floatValue = localVelocityX * normalizeScale;
	runtime.parameters["MoveY"].floatValue = localVelocityZ * normalizeScale;
	runtime.parameters["Speed"].floatValue = planarSpeed;
}

void EditorAnimationManager::EvaluateStateMachine(
	AnimatorRuntimeInstance& runtime,
	const EditorComponent& component) {
	if (runtime.currentState < 0 || runtime.currentState >= static_cast<int32_t>(runtime.graph.states.size())) {
		return;
	}

	const AnimationGraphState& currentState = runtime.graph.states[static_cast<size_t>(runtime.currentState)];
	float normalizedTime = 0.0f;

	if (!runtime.clips.empty()) {
		const int32_t clipIndex = (std::clamp)(
			currentState.clipIndex,
			0,
			(static_cast<int32_t>(runtime.clips.size()) - 1));
		const float duration = runtime.clips[static_cast<size_t>(clipIndex)].durationSeconds;
		normalizedTime = duration > kMinimumValue
			? std::fmod(runtime.stateTime * currentState.playbackSpeed, duration) / duration
			: 0.0f;
	}

	for (const AnimationGraphTransition& transition : runtime.graph.transitions) {
		const bool isSourceMatched = transition.sourceState == runtime.currentState || transition.sourceState < 0;

		if (!isSourceMatched ||
			transition.destinationState < 0 ||
			transition.destinationState >= static_cast<int32_t>(runtime.graph.states.size())) {
			continue;
		}

		if (runtime.previousState >= 0 && !transition.canInterrupt) {
			continue;
		}

		if (transition.hasExitTime && normalizedTime < transition.exitTime) {
			continue;
		}

		const bool areConditionsMatched = std::all_of(
			transition.conditions.begin(),
			transition.conditions.end(),
			[&runtime](const AnimationTransitionCondition& condition) {
				return EvaluateAnimationTransitionCondition(condition, runtime.parameters);
			});

		if (!areConditionsMatched) {
			continue;
		}

		// Trigger は遷移が成立した時だけ消費し、他条件待ちの間は次フレームへ保持する。
		for (const AnimationTransitionCondition& condition : transition.conditions) {
			auto parameterIterator = runtime.parameters.find(condition.parameterName);

			if (parameterIterator != runtime.parameters.end() &&
				parameterIterator->second.type == AnimatorParameterType::Trigger) {
				parameterIterator->second.boolValue = false;
			}
		}

		runtime.previousState = runtime.currentState;
		runtime.previousStateTime = runtime.stateTime;
		runtime.currentState = transition.destinationState;
		runtime.stateTime = 0.0f;
		runtime.previousEventTime = 0.0f;
		runtime.transitionTime = 0.0f;
		runtime.transitionDuration = transition.blendDuration > 0.0f
			? transition.blendDuration
			: component.animatorTransitionDuration;
		runtime.hasPreviousRootPose = false;
		break;
	}
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleState(
	const AnimatorRuntimeInstance& runtime,
	int32_t stateIndex,
	float playbackTime) const {
	if (stateIndex < 0 || stateIndex >= static_cast<int32_t>(runtime.graph.states.size())) {
		return {};
	}

	const AnimationGraphState& state = runtime.graph.states[static_cast<size_t>(stateIndex)];
	switch (state.blendTreeType) {
	case AnimationBlendTreeType::Blend1D:
		return SampleBlend1D(runtime, state, playbackTime);
	case AnimationBlendTreeType::Blend2DDirectional:
		return SampleDirectionalBlend(runtime, state, playbackTime);
	case AnimationBlendTreeType::Blend2DCartesian:
		return SampleCartesianBlend(runtime, state, playbackTime);
	case AnimationBlendTreeType::Direct:
		return SampleDirectBlend(runtime, state, playbackTime);
	case AnimationBlendTreeType::Clip:
	default:
		return SampleClip(runtime, state.clipIndex, playbackTime * state.playbackSpeed, state.loop);
	}
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleClip(
	const AnimatorRuntimeInstance& runtime,
	int32_t clipIndex,
	float playbackTime,
	bool loop) const {
	if (clipIndex < 0 || clipIndex >= static_cast<int32_t>(runtime.clips.size())) {
		return {};
	}

	const ModelAnimationClipData& animationClip = runtime.clips[static_cast<size_t>(clipIndex)];
	if (animationClip.keyframes.empty() || animationClip.durationSeconds <= kMinimumValue) {
		return {};
	}

	float sampleTime = playbackTime;
	if (loop) {
		sampleTime = std::fmod(sampleTime, animationClip.durationSeconds);

		if (sampleTime < 0.0f) {
			sampleTime += animationClip.durationSeconds;
		}
	}
	else {
		sampleTime = (std::clamp)(sampleTime, 0.0f, animationClip.durationSeconds);
	}

	const auto nextKeyframeIterator = std::lower_bound(
		animationClip.keyframes.begin(),
		animationClip.keyframes.end(),
		sampleTime,
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
	const float interpolationRate = keyframeDuration > kMinimumValue
		? (std::clamp)((sampleTime - previousKeyframe->timeSeconds) / keyframeDuration, 0.0f, 1.0f)
		: 0.0f;
	SampledTransform pose{};
	pose.translation = LerpVector3(previousKeyframe->translation, nextKeyframe->translation, interpolationRate);
	pose.rotation = LerpRotation(previousKeyframe->rotation, nextKeyframe->rotation, interpolationRate);
	pose.scale = LerpVector3(previousKeyframe->scale, nextKeyframe->scale, interpolationRate);
	pose.isValid = true;
	return pose;
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleBlend1D(
	const AnimatorRuntimeInstance& runtime,
	const AnimationGraphState& state,
	float playbackTime) const {
	if (state.blendSamples.empty()) {
		return SampleClip(runtime, state.clipIndex, playbackTime * state.playbackSpeed, state.loop);
	}

	const auto parameterIterator = runtime.parameters.find(state.blendParameter);
	const float parameterValue = parameterIterator != runtime.parameters.end()
		? parameterIterator->second.floatValue
		: 0.0f;
	std::vector<const AnimationBlendSample*> orderedSamples;

	for (const AnimationBlendSample& sample : state.blendSamples) {
		orderedSamples.push_back(&sample);
	}

	std::sort(
		orderedSamples.begin(),
		orderedSamples.end(),
		[](const AnimationBlendSample* leftSample, const AnimationBlendSample* rightSample) {
			return leftSample->position.x < rightSample->position.x;
		});

	if (parameterValue <= orderedSamples.front()->position.x) {
		return SampleClip(runtime, orderedSamples.front()->clipIndex, playbackTime * orderedSamples.front()->playbackSpeed, state.loop);
	}
	if (parameterValue >= orderedSamples.back()->position.x) {
		return SampleClip(runtime, orderedSamples.back()->clipIndex, playbackTime * orderedSamples.back()->playbackSpeed, state.loop);
	}

	for (size_t sampleIndex = 1u; sampleIndex < orderedSamples.size(); sampleIndex++) {
		const AnimationBlendSample& previousSample = *orderedSamples[sampleIndex - 1u];
		const AnimationBlendSample& nextSample = *orderedSamples[sampleIndex];

		if (parameterValue > nextSample.position.x) {
			continue;
		}

		const float interval = nextSample.position.x - previousSample.position.x;
		const float rate = interval > kMinimumValue
			? (std::clamp)((parameterValue - previousSample.position.x) / interval, 0.0f, 1.0f)
			: 0.0f;
		SampledTransform previousPose = SampleClip(runtime, previousSample.clipIndex, playbackTime * previousSample.playbackSpeed, state.loop);
		const SampledTransform nextPose = SampleClip(runtime, nextSample.clipIndex, playbackTime * nextSample.playbackSpeed, state.loop);

		if (!previousPose.isValid) {
			return nextPose;
		}
		if (!nextPose.isValid) {
			return previousPose;
		}

		previousPose.translation = LerpVector3(previousPose.translation, nextPose.translation, rate);
		previousPose.rotation = LerpRotation(previousPose.rotation, nextPose.rotation, rate);
		previousPose.scale = LerpVector3(previousPose.scale, nextPose.scale, rate);
		return previousPose;
	}

	return {};
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleDirectionalBlend(
	const AnimatorRuntimeInstance& runtime,
	const AnimationGraphState& state,
	float playbackTime) const {
	if (state.blendSamples.empty()) {
		return SampleClip(runtime, state.clipIndex, playbackTime * state.playbackSpeed, state.loop);
	}

	const float moveX = runtime.parameters.find(state.blendParameterX) != runtime.parameters.end()
		? runtime.parameters.at(state.blendParameterX).floatValue
		: 0.0f;
	const float moveY = runtime.parameters.find(state.blendParameterY) != runtime.parameters.end()
		? runtime.parameters.at(state.blendParameterY).floatValue
		: 0.0f;
	const Vector2 direction{moveX, moveY};
	const float inputMagnitude = (std::clamp)(Length2(direction), 0.0f, 1.0f);
	const AnimationBlendSample* centerSample = nullptr;
	std::vector<const AnimationBlendSample*> directionSamples;

	for (const AnimationBlendSample& sample : state.blendSamples) {
		if (Length2(sample.position) <= kMinimumValue) {
			centerSample = &sample;
		}
		else {
			directionSamples.push_back(&sample);
		}
	}

	if (inputMagnitude <= kMinimumValue || directionSamples.empty()) {
		const AnimationBlendSample& sample = centerSample != nullptr ? *centerSample : state.blendSamples.front();
		return SampleClip(runtime, sample.clipIndex, playbackTime * sample.playbackSpeed, state.loop);
	}

	const float inputAngle = NormalizePositiveAngle(std::atan2(moveY, moveX));
	const AnimationBlendSample* previousSample = directionSamples.front();
	const AnimationBlendSample* nextSample = directionSamples.front();
	float previousDistance = (std::numeric_limits<float>::max)();
	float nextDistance = (std::numeric_limits<float>::max)();

	for (const AnimationBlendSample* sample : directionSamples) {
		const float sampleAngle = NormalizePositiveAngle(std::atan2(sample->position.y, sample->position.x));
		const float distanceFromSample = PositiveAngleDistance(sampleAngle, inputAngle);
		const float distanceToSample = PositiveAngleDistance(inputAngle, sampleAngle);

		if (distanceFromSample < previousDistance) {
			previousDistance = distanceFromSample;
			previousSample = sample;
		}
		if (distanceToSample < nextDistance) {
			nextDistance = distanceToSample;
			nextSample = sample;
		}
	}

	const float angleInterval = previousDistance + nextDistance;
	const float directionBlendRate = angleInterval > kMinimumValue ? previousDistance / angleInterval : 0.0f;
	SampledTransform directionPose = SampleClip(
		runtime,
		previousSample->clipIndex,
		playbackTime * previousSample->playbackSpeed,
		state.loop);
	const SampledTransform nextPose = SampleClip(
		runtime,
		nextSample->clipIndex,
		playbackTime * nextSample->playbackSpeed,
		state.loop);

	if (!directionPose.isValid) {
		directionPose = nextPose;
	}
	else if (nextPose.isValid) {
		directionPose.translation = LerpVector3(directionPose.translation, nextPose.translation, directionBlendRate);
		directionPose.rotation = LerpRotation(directionPose.rotation, nextPose.rotation, directionBlendRate);
		directionPose.scale = LerpVector3(directionPose.scale, nextPose.scale, directionBlendRate);
	}

	if (centerSample == nullptr || !directionPose.isValid || inputMagnitude >= 1.0f) {
		return directionPose;
	}

	SampledTransform centerPose = SampleClip(
		runtime,
		centerSample->clipIndex,
		playbackTime * centerSample->playbackSpeed,
		state.loop);

	if (!centerPose.isValid) {
		return directionPose;
	}

	centerPose.translation = LerpVector3(centerPose.translation, directionPose.translation, inputMagnitude);
	centerPose.rotation = LerpRotation(centerPose.rotation, directionPose.rotation, inputMagnitude);
	centerPose.scale = LerpVector3(centerPose.scale, directionPose.scale, inputMagnitude);
	return centerPose;
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleCartesianBlend(
	const AnimatorRuntimeInstance& runtime,
	const AnimationGraphState& state,
	float playbackTime) const {
	if (state.blendSamples.empty()) {
		return SampleClip(runtime, state.clipIndex, playbackTime * state.playbackSpeed, state.loop);
	}

	const Vector2 point{
		runtime.parameters.find(state.blendParameterX) != runtime.parameters.end()
			? runtime.parameters.at(state.blendParameterX).floatValue
			: 0.0f,
		runtime.parameters.find(state.blendParameterY) != runtime.parameters.end()
			? runtime.parameters.at(state.blendParameterY).floatValue
			: 0.0f};
	struct WeightedSample {
		const AnimationBlendSample* sample = nullptr;
		float weight = 0.0f;
		float distanceSquared = 0.0f;
	};
	auto blendSamples = [&](const std::vector<WeightedSample>& weightedSamples) {
		SampledTransform result{};
		float accumulatedWeight = 0.0f;

		for (const WeightedSample& weightedSample : weightedSamples) {
			if (weightedSample.sample == nullptr || weightedSample.weight <= 0.0f) {
				continue;
			}

			const SampledTransform samplePose = SampleClip(
				runtime,
				weightedSample.sample->clipIndex,
				playbackTime * weightedSample.sample->playbackSpeed,
				state.loop);

			if (!samplePose.isValid) {
				continue;
			}

			if (!result.isValid) {
				result = samplePose;
				accumulatedWeight = weightedSample.weight;
				continue;
			}

			const float blendRate = weightedSample.weight / (accumulatedWeight + weightedSample.weight);
			result.translation = LerpVector3(result.translation, samplePose.translation, blendRate);
			result.rotation = LerpRotation(result.rotation, samplePose.rotation, blendRate);
			result.scale = LerpVector3(result.scale, samplePose.scale, blendRate);
			accumulatedWeight += weightedSample.weight;
		}

		return result;
	};

	// Sample 3 点で作られる三角形の内側なら、距離ではなく重心座標で正確に補間する。
	std::vector<WeightedSample> triangleSamples;
	float bestMinimumWeight = -(std::numeric_limits<float>::max)();
	for (size_t firstIndex = 0u; firstIndex < state.blendSamples.size(); firstIndex++) {
		for (size_t secondIndex = firstIndex + 1u; secondIndex < state.blendSamples.size(); secondIndex++) {
			for (size_t thirdIndex = secondIndex + 1u; thirdIndex < state.blendSamples.size(); thirdIndex++) {
				const AnimationBlendSample& firstSample = state.blendSamples[firstIndex];
				const AnimationBlendSample& secondSample = state.blendSamples[secondIndex];
				const AnimationBlendSample& thirdSample = state.blendSamples[thirdIndex];
				const float denominator =
					(secondSample.position.y - thirdSample.position.y) *
						(firstSample.position.x - thirdSample.position.x) +
					(thirdSample.position.x - secondSample.position.x) *
						(firstSample.position.y - thirdSample.position.y);

				if (std::fabs(denominator) <= kMinimumValue) {
					continue;
				}

				const float firstWeight =
					((secondSample.position.y - thirdSample.position.y) * (point.x - thirdSample.position.x) +
					 (thirdSample.position.x - secondSample.position.x) * (point.y - thirdSample.position.y)) /
					denominator;
				const float secondWeight =
					((thirdSample.position.y - firstSample.position.y) * (point.x - thirdSample.position.x) +
					 (firstSample.position.x - thirdSample.position.x) * (point.y - thirdSample.position.y)) /
					denominator;
				const float thirdWeight = 1.0f - firstWeight - secondWeight;
				const float minimumWeight = (std::min)(firstWeight, (std::min)(secondWeight, thirdWeight));

				if (minimumWeight < -0.0001f || minimumWeight <= bestMinimumWeight) {
					continue;
				}

				bestMinimumWeight = minimumWeight;
				triangleSamples = {
					{&firstSample, (std::max)(firstWeight, 0.0f), 0.0f},
					{&secondSample, (std::max)(secondWeight, 0.0f), 0.0f},
					{&thirdSample, (std::max)(thirdWeight, 0.0f), 0.0f}};
			}
		}
	}

	if (!triangleSamples.empty()) {
		return blendSamples(triangleSamples);
	}

	// Blend Space 外側では最寄り 3 点の逆距離を使い、境界から外れても急に Pose が切り替わらないようにする。
	std::vector<WeightedSample> nearestSamples;

	for (const AnimationBlendSample& sample : state.blendSamples) {
		const float differenceX = sample.position.x - point.x;
		const float differenceY = sample.position.y - point.y;
		nearestSamples.push_back({&sample, 0.0f, differenceX * differenceX + differenceY * differenceY});
	}

	std::sort(
		nearestSamples.begin(),
		nearestSamples.end(),
		[](const WeightedSample& leftSample, const WeightedSample& rightSample) {
			return leftSample.distanceSquared < rightSample.distanceSquared;
		});
	nearestSamples.resize((std::min)(nearestSamples.size(), static_cast<size_t>(3u)));

	if (nearestSamples.front().distanceSquared <= kMinimumValue) {
		const AnimationBlendSample& sample = *nearestSamples.front().sample;
		return SampleClip(runtime, sample.clipIndex, playbackTime * sample.playbackSpeed, state.loop);
	}

	float totalWeight = 0.0f;
	for (WeightedSample& sample : nearestSamples) {
		sample.weight = 1.0f / (std::sqrt(sample.distanceSquared) + kMinimumValue);
		totalWeight += sample.weight;
	}

	for (WeightedSample& weightedSample : nearestSamples) {
		weightedSample.weight /= totalWeight;
	}

	return blendSamples(nearestSamples);
}

EditorAnimationManager::SampledTransform EditorAnimationManager::SampleDirectBlend(
	const AnimatorRuntimeInstance& runtime,
	const AnimationGraphState& state,
	float playbackTime) const {
	SampledTransform result{};
	float accumulatedWeight = 0.0f;

	for (const AnimationBlendSample& sample : state.blendSamples) {
		const auto parameterIterator = runtime.parameters.find(sample.weightParameter);
		const float sampleWeight = parameterIterator != runtime.parameters.end()
			? (std::max)(parameterIterator->second.floatValue, 0.0f)
			: 0.0f;

		if (sampleWeight <= 0.0f) {
			continue;
		}

		const SampledTransform samplePose = SampleClip(
			runtime,
			sample.clipIndex,
			playbackTime * sample.playbackSpeed,
			state.loop);

		if (!samplePose.isValid) {
			continue;
		}

		if (!result.isValid) {
			result = samplePose;
			accumulatedWeight = sampleWeight;
			continue;
		}

		const float blendRate = sampleWeight / (accumulatedWeight + sampleWeight);
		result.translation = LerpVector3(result.translation, samplePose.translation, blendRate);
		result.rotation = LerpRotation(result.rotation, samplePose.rotation, blendRate);
		result.scale = LerpVector3(result.scale, samplePose.scale, blendRate);
		accumulatedWeight += sampleWeight;
	}

	return result.isValid
		? result
		: SampleClip(runtime, state.clipIndex, playbackTime * state.playbackSpeed, state.loop);
}

void EditorAnimationManager::ApplyAnimatorPose(
	EditorGameObject& gameObject,
	EditorComponent& component,
	AnimatorRuntimeInstance& runtime,
	const SampledTransform& pose) {
	if (!pose.isValid) {
		return;
	}

	const SampledTransform referencePose = SampleState(runtime, runtime.currentState, 0.0f);
	if (!referencePose.isValid) {
		return;
	}

	const BaseTransform& baseTransform = baseTransforms_[gameObject.id];
	if (component.animatorApplyRootMotion) {
		if (runtime.hasPreviousRootPose) {
			gameObject.translate.x += pose.translation.x - runtime.previousRootPose.translation.x;
			gameObject.translate.y += pose.translation.y - runtime.previousRootPose.translation.y;
			gameObject.translate.z += pose.translation.z - runtime.previousRootPose.translation.z;
			gameObject.rotate.x += std::remainder(pose.rotation.x - runtime.previousRootPose.rotation.x, kTwoPi);
			gameObject.rotate.y += std::remainder(pose.rotation.y - runtime.previousRootPose.rotation.y, kTwoPi);
			gameObject.rotate.z += std::remainder(pose.rotation.z - runtime.previousRootPose.rotation.z, kTwoPi);
		}

		runtime.previousRootPose = pose;
		runtime.hasPreviousRootPose = true;
	}
	else {
		// In-Place では Physics / CharacterController が確定した位置を保持し、姿勢だけ Animation が所有する。
		gameObject.rotate = {
			baseTransform.rotate.x + pose.rotation.x - referencePose.rotation.x,
			baseTransform.rotate.y + pose.rotation.y - referencePose.rotation.y,
			baseTransform.rotate.z + pose.rotation.z - referencePose.rotation.z};
	}

	gameObject.scale = {
		baseTransform.scale.x * SafeScaleRatio(pose.scale.x, referencePose.scale.x),
		baseTransform.scale.y * SafeScaleRatio(pose.scale.y, referencePose.scale.y),
		baseTransform.scale.z * SafeScaleRatio(pose.scale.z, referencePose.scale.z)};
}

void EditorAnimationManager::UpdateAction(
	AnimatorRuntimeInstance& runtime,
	float deltaTime,
	SampledTransform& basePose) const {
	ActiveAnimationAction& action = runtime.action;

	if (!action.isActive || action.clipIndex < 0 || action.clipIndex >= static_cast<int32_t>(runtime.clips.size())) {
		return;
	}

	const ModelAnimationClipData& actionClip = runtime.clips[static_cast<size_t>(action.clipIndex)];
	const float duration = (std::max)(actionClip.durationSeconds, kMinimumValue);
	action.playbackTime += deltaTime * action.playbackSpeed;

	const SampledTransform actionPose = SampleClip(runtime, action.clipIndex, action.playbackTime, action.loop);
	if (!actionPose.isValid) {
		action.isActive = false;
		return;
	}

	float blendWeight = action.blendIn > kMinimumValue
		? (std::clamp)(action.playbackTime / action.blendIn, 0.0f, 1.0f)
		: 1.0f;

	if (!action.loop) {
		const float remainingTime = duration - action.playbackTime;

		if (action.blendOut > kMinimumValue && remainingTime < action.blendOut) {
			blendWeight *= (std::clamp)(remainingTime / action.blendOut, 0.0f, 1.0f);
		}
	}

	if (!basePose.isValid) {
		basePose = actionPose;
	}
	else {
		basePose.translation = LerpVector3(basePose.translation, actionPose.translation, blendWeight);
		basePose.rotation = LerpRotation(basePose.rotation, actionPose.rotation, blendWeight);
		basePose.scale = LerpVector3(basePose.scale, actionPose.scale, blendWeight);
	}

	if (!action.loop && action.playbackTime >= duration) {
		action.isActive = false;
	}
}

void EditorAnimationManager::DispatchAnimationEvents(
	EditorGameObject& gameObject,
	AnimatorRuntimeInstance& runtime,
	int32_t clipIndex,
	float previousTime,
	float currentTime,
	bool loop) {
	if (clipIndex < 0 || clipIndex >= static_cast<int32_t>(runtime.clips.size())) {
		return;
	}

	const float duration = runtime.clips[static_cast<size_t>(clipIndex)].durationSeconds;
	for (const AnimationGraphEvent& animationEvent : runtime.graph.events) {
		if (animationEvent.clipIndex != clipIndex ||
			!IsCrossedEventTime(previousTime, currentTime, animationEvent.time, duration, loop)) {
			continue;
		}

		if (!animationEvent.effectAssetPath.empty() && effectManager_ != nullptr) {
			effectManager_->PlayEffectAt(gameObject.id, animationEvent.effectAssetPath, animationEvent.localOffset);
		}

		if (scriptManager_ != nullptr) {
			scriptManager_->DispatchAnimationEvent(
				gameObject.id,
				animationEvent.name,
				animationEvent.time,
				animationEvent.effectAssetPath,
				animationEvent.localOffset);
		}

		PushConsoleMessage(
			"Animation Event: " + gameObject.name + " / " + animationEvent.name +
			" / " + std::to_string(animationEvent.time) + "秒");
	}
}

bool EditorAnimationManager::GetAnimatedProperty(
	const EditorGameObject& gameObject,
	AnimationPropertyTarget target,
	float& value) const {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX:
		value = gameObject.translate.x;
		return true;
	case AnimationPropertyTarget::TransformPositionY:
		value = gameObject.translate.y;
		return true;
	case AnimationPropertyTarget::TransformPositionZ:
		value = gameObject.translate.z;
		return true;
	case AnimationPropertyTarget::TransformRotationXDegrees:
		value = gameObject.rotate.x * kRadiansToDegrees;
		return true;
	case AnimationPropertyTarget::TransformRotationYDegrees:
		value = gameObject.rotate.y * kRadiansToDegrees;
		return true;
	case AnimationPropertyTarget::TransformRotationZDegrees:
		value = gameObject.rotate.z * kRadiansToDegrees;
		return true;
	case AnimationPropertyTarget::TransformScaleX:
		value = gameObject.scale.x;
		return true;
	case AnimationPropertyTarget::TransformScaleY:
		value = gameObject.scale.y;
		return true;
	case AnimationPropertyTarget::TransformScaleZ:
		value = gameObject.scale.z;
		return true;
	default:
		break;
	}

	if (target == AnimationPropertyTarget::LightIntensity ||
		target == AnimationPropertyTarget::LightRange) {
		const EditorComponent* lightComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Light);

		if (lightComponent == nullptr) {
			return false;
		}

		value = target == AnimationPropertyTarget::LightIntensity
			? lightComponent->intensity
			: lightComponent->colliderRadius;
		return true;
	}

	const EditorComponent* rendererComponent = EditorComponentUtility::FindComponent(
		gameObject,
		EditorComponentType::ModelRenderer);

	if (rendererComponent == nullptr) {
		return false;
	}

	switch (target) {
	case AnimationPropertyTarget::MaterialBaseColorR: value = rendererComponent->color.x; return true;
	case AnimationPropertyTarget::MaterialBaseColorG: value = rendererComponent->color.y; return true;
	case AnimationPropertyTarget::MaterialBaseColorB: value = rendererComponent->color.z; return true;
	case AnimationPropertyTarget::MaterialMetallic: value = rendererComponent->metallic; return true;
	case AnimationPropertyTarget::MaterialRoughness: value = rendererComponent->roughness; return true;
	case AnimationPropertyTarget::MaterialAlpha: value = rendererComponent->alpha; return true;
	case AnimationPropertyTarget::MaterialEmissionStrength: value = rendererComponent->emissionStrength; return true;
	case AnimationPropertyTarget::MaterialEmissionColorR: value = rendererComponent->emissionColor.x; return true;
	case AnimationPropertyTarget::MaterialEmissionColorG: value = rendererComponent->emissionColor.y; return true;
	case AnimationPropertyTarget::MaterialEmissionColorB: value = rendererComponent->emissionColor.z; return true;
	default: return false;
	}
}

bool EditorAnimationManager::SetAnimatedProperty(
	EditorGameObject& gameObject,
	AnimationPropertyTarget target,
	float value) const {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX:
		gameObject.translate.x = value;
		return true;
	case AnimationPropertyTarget::TransformPositionY:
		gameObject.translate.y = value;
		return true;
	case AnimationPropertyTarget::TransformPositionZ:
		gameObject.translate.z = value;
		return true;
	case AnimationPropertyTarget::TransformRotationXDegrees:
		gameObject.rotate.x = value * kDegreesToRadians;
		return true;
	case AnimationPropertyTarget::TransformRotationYDegrees:
		gameObject.rotate.y = value * kDegreesToRadians;
		return true;
	case AnimationPropertyTarget::TransformRotationZDegrees:
		gameObject.rotate.z = value * kDegreesToRadians;
		return true;
	case AnimationPropertyTarget::TransformScaleX:
		gameObject.scale.x = value;
		return true;
	case AnimationPropertyTarget::TransformScaleY:
		gameObject.scale.y = value;
		return true;
	case AnimationPropertyTarget::TransformScaleZ:
		gameObject.scale.z = value;
		return true;
	default:
		break;
	}

	if (target == AnimationPropertyTarget::LightIntensity ||
		target == AnimationPropertyTarget::LightRange) {
		EditorComponent* lightComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Light);

		if (lightComponent == nullptr) {
			return false;
		}

		if (target == AnimationPropertyTarget::LightIntensity) {
			lightComponent->intensity = (std::max)(value, 0.0f);
		}
		else {
			lightComponent->colliderRadius = (std::max)(value, 0.01f);
		}

		return true;
	}

	EditorComponent* rendererComponent = EditorComponentUtility::FindComponent(
		gameObject,
		EditorComponentType::ModelRenderer);

	if (rendererComponent == nullptr) {
		return false;
	}

	switch (target) {
	case AnimationPropertyTarget::MaterialBaseColorR:
		rendererComponent->color.x = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialBaseColorG:
		rendererComponent->color.y = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialBaseColorB:
		rendererComponent->color.z = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialMetallic:
		rendererComponent->metallic = (std::clamp)(value, 0.0f, 1.0f);
		return true;
	case AnimationPropertyTarget::MaterialRoughness:
		rendererComponent->roughness = (std::clamp)(value, 0.0f, 1.0f);
		return true;
	case AnimationPropertyTarget::MaterialAlpha:
		rendererComponent->alpha = (std::clamp)(value, 0.0f, 1.0f);
		return true;
	case AnimationPropertyTarget::MaterialEmissionStrength:
		rendererComponent->emissionStrength = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialEmissionColorR:
		rendererComponent->emissionColor.x = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialEmissionColorG:
		rendererComponent->emissionColor.y = (std::max)(value, 0.0f);
		return true;
	case AnimationPropertyTarget::MaterialEmissionColorB:
		rendererComponent->emissionColor.z = (std::max)(value, 0.0f);
		return true;
	default:
		return false;
	}
}

AnimatorParameterValue* EditorAnimationManager::FindParameter(
	int32_t gameObjectId,
	const std::string& parameterName) {
	auto runtimeIterator = animatorRuntimes_.find(gameObjectId);

	if (runtimeIterator == animatorRuntimes_.end()) {
		return nullptr;
	}

	return &runtimeIterator->second.parameters[parameterName];
}

void EditorAnimationManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}
