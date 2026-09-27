#include "EditorSceneOptimizationManager.h"

#include "EditorComponentUtility.h"
#include "EditorObjectPoolManager.h"
#include "EditorPhysicsManager.h"

#include <algorithm>

namespace {
	float DistanceSquared(const Vector3& firstPosition, const Vector3& secondPosition) {
		const float differenceX = firstPosition.x - secondPosition.x;
		const float differenceY = firstPosition.y - secondPosition.y;
		const float differenceZ = firstPosition.z - secondPosition.z;
		return differenceX * differenceX + differenceY * differenceY + differenceZ * differenceZ;
	}
}

void EditorSceneOptimizationManager::Initialize(
	EditorScene* editorScene,
	EditorPhysicsManager* physicsManager,
	EditorObjectPoolManager* objectPoolManager) {
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	objectPoolManager_ = objectPoolManager;
	originalGameObjectActiveStates_.clear();
	runtimeRequestedActiveStates_.clear();
	lastManagedActiveStates_.clear();
	originalComponentActiveStates_.clear();
	runtimeRequestedComponentActiveStates_.clear();
	lastManagedComponentActiveStates_.clear();
	updateCursor_ = 0u;
	isStarted_ = false;
}

void EditorSceneOptimizationManager::Start() {
	originalGameObjectActiveStates_.clear();
	runtimeRequestedActiveStates_.clear();
	lastManagedActiveStates_.clear();
	originalComponentActiveStates_.clear();
	runtimeRequestedComponentActiveStates_.clear();
	lastManagedComponentActiveStates_.clear();
	updateCursor_ = 0u;
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		originalGameObjectActiveStates_[gameObject.id] = gameObject.isActive;
		runtimeRequestedActiveStates_[gameObject.id] = gameObject.isActive;
		lastManagedActiveStates_[gameObject.id] = gameObject.isActive;
		std::vector<bool>& componentStates = originalComponentActiveStates_[gameObject.id];
		std::vector<bool>& runtimeComponentStates = runtimeRequestedComponentActiveStates_[gameObject.id];
		std::vector<bool>& lastManagedComponentStates = lastManagedComponentActiveStates_[gameObject.id];
		componentStates.reserve(gameObject.components.size());
		runtimeComponentStates.reserve(gameObject.components.size());
		lastManagedComponentStates.reserve(gameObject.components.size());

		for (const EditorComponent& component : gameObject.components) {
			componentStates.push_back(component.isActive);
			runtimeComponentStates.push_back(component.isActive);
			lastManagedComponentStates.push_back(component.isActive);
		}

		EditorComponent* distanceActivation = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::DistanceActivation);
		EditorComponent* simulationLod = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SimulationLOD);

		if (distanceActivation != nullptr) {
			distanceActivation->distanceActivationRuntimeActive = gameObject.isActive;
			distanceActivation->distanceActivationRuntimeInitialized = false;
		}

		if (simulationLod != nullptr) {
			simulationLod->simulationLodRuntimeLevel = 0;
		}
	}

	Update();
}

void EditorSceneOptimizationManager::Update() {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	std::vector<EditorGameObject>& gameObjects = editorScene_->GetGameObjects();

	if (gameObjects.empty()) {
		updateCursor_ = 0u;
		return;
	}

	// 数千ObjectのSceneでも距離LODだけでCPU時間を占有しないよう、判定をFrame分散する。
	// 256件以下のSceneは従来どおり毎Frame全件更新される。
	constexpr std::size_t kMaximumUpdatesPerFrame = 256u;
	const std::size_t updateCount = (std::min)(gameObjects.size(), kMaximumUpdatesPerFrame);

	for (std::size_t updateOffset = 0u; updateOffset < updateCount; updateOffset++) {
		const std::size_t gameObjectIndex = (updateCursor_ + updateOffset) % gameObjects.size();
		EditorGameObject& gameObject = gameObjects[gameObjectIndex];
		EditorComponent* distanceActivation = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::DistanceActivation);
		EditorComponent* simulationLod = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SimulationLOD);
		const bool usesDistanceActivation =
			distanceActivation != nullptr && distanceActivation->isActive;
		const bool usesSimulationLod = simulationLod != nullptr && simulationLod->isActive;

		if (!usesDistanceActivation && !usesSimulationLod) {
			continue;
		}

		// PoolやScriptがActiveを変更した時は、その要求をLODの一時停止状態と分離して保持する。
		// これによりPlay開始時に非ActiveだったTemplateも、Acquire後は距離LODの対象として動作する。
		auto runtimeRequestedIterator = runtimeRequestedActiveStates_.find(gameObject.id);
		auto lastManagedIterator = lastManagedActiveStates_.find(gameObject.id);

		if (runtimeRequestedIterator == runtimeRequestedActiveStates_.end()) {
			runtimeRequestedIterator = runtimeRequestedActiveStates_
				.emplace(gameObject.id, gameObject.isActive)
				.first;
		}

		if (lastManagedIterator == lastManagedActiveStates_.end()) {
			lastManagedIterator = lastManagedActiveStates_
				.emplace(gameObject.id, gameObject.isActive)
				.first;
		}

		const bool isPooledObject =
			objectPoolManager_ != nullptr &&
			objectPoolManager_->IsPooledObject(gameObject.id);

		if (isPooledObject) {
			// 距離LODで非Active中にPoolへ返却されても、貸出集合なら要求状態を確実に判別できる。
			runtimeRequestedIterator->second =
				objectPoolManager_->IsPooledObjectActive(gameObject.id);
		}
		else if (gameObject.isActive != lastManagedIterator->second) {
			runtimeRequestedIterator->second = gameObject.isActive;
		}

		Vector3 gameObjectScale{};
		Vector3 gameObjectRotation{};
		Vector3 gameObjectPosition{};
		editorScene_->GetWorldTransform(
			gameObject.id,
			gameObjectScale,
			gameObjectRotation,
			gameObjectPosition);
		(void)gameObjectScale;
		(void)gameObjectRotation;
		bool distanceAllowsActivation = true;
		bool affectsHierarchy = false;

		if (usesDistanceActivation) {
			Vector3 referencePosition{};
			const bool hasReference = ResolveReferencePosition(
				distanceActivation->distanceActivationReferenceGameObjectId,
				referencePosition);

			if (hasReference) {
				const float distanceSquared = DistanceSquared(gameObjectPosition, referencePosition);
				const float enterDistance = (std::max)(
					distanceActivation->distanceActivationEnterDistance,
					0.0f);
				const float exitDistance = (std::max)(
					distanceActivation->distanceActivationExitDistance,
					enterDistance);

				if (!distanceActivation->distanceActivationRuntimeInitialized) {
					distanceActivation->distanceActivationRuntimeActive =
						distanceSquared <= exitDistance * exitDistance;
					distanceActivation->distanceActivationRuntimeInitialized = true;
				}
				else if (distanceActivation->distanceActivationRuntimeActive &&
					distanceSquared > exitDistance * exitDistance) {
					distanceActivation->distanceActivationRuntimeActive = false;
				}
				else if (!distanceActivation->distanceActivationRuntimeActive &&
					distanceSquared <= enterDistance * enterDistance) {
					distanceActivation->distanceActivationRuntimeActive = true;
				}
			}

			distanceAllowsActivation = distanceActivation->distanceActivationRuntimeActive;
			affectsHierarchy = affectsHierarchy || distanceActivation->distanceActivationAffectHierarchy;
		}

		int32_t lodLevel = 0;

		if (usesSimulationLod) {
			Vector3 referencePosition{};
			const bool hasReference = ResolveReferencePosition(
				simulationLod->simulationLodReferenceGameObjectId,
				referencePosition);

			if (hasReference) {
				const float distanceSquared = DistanceSquared(gameObjectPosition, referencePosition);
				const float mediumDistance = (std::max)(simulationLod->simulationLodMediumDistance, 0.0f);
				const float farDistance = (std::max)(simulationLod->simulationLodFarDistance, mediumDistance);
				const float culledDistance = (std::max)(simulationLod->simulationLodCulledDistance, farDistance);

				if (distanceSquared >= culledDistance * culledDistance) {
					lodLevel = 3;
				}
				else if (distanceSquared >= farDistance * farDistance) {
					lodLevel = 2;
				}
				else if (distanceSquared >= mediumDistance * mediumDistance) {
					lodLevel = 1;
				}
			}

			simulationLod->simulationLodRuntimeLevel = lodLevel;
			affectsHierarchy = affectsHierarchy || simulationLod->simulationLodAffectHierarchy;
		}

		const bool shouldActivate =
			runtimeRequestedIterator->second && distanceAllowsActivation && lodLevel < 3;
		SetHierarchyActive(gameObject.id, shouldActivate, affectsHierarchy);

		if (shouldActivate && usesSimulationLod) {
			ApplySimulationLod(gameObject.id, *simulationLod, lodLevel, affectsHierarchy);
		}
	}

	updateCursor_ = (updateCursor_ + updateCount) % gameObjects.size();
}

void EditorSceneOptimizationManager::Stop() {
	if (editorScene_ != nullptr) {
		for (const auto& activeStatePair : originalGameObjectActiveStates_) {
			EditorGameObject* gameObject = editorScene_->FindGameObject(activeStatePair.first);

			if (gameObject != nullptr) {
				gameObject->isActive = activeStatePair.second;
			}
		}

		for (const auto& componentStatePair : originalComponentActiveStates_) {
			RestoreComponentStates(componentStatePair.first, false);
		}
	}

	originalGameObjectActiveStates_.clear();
	runtimeRequestedActiveStates_.clear();
	lastManagedActiveStates_.clear();
	originalComponentActiveStates_.clear();
	runtimeRequestedComponentActiveStates_.clear();
	lastManagedComponentActiveStates_.clear();
	updateCursor_ = 0u;
	isStarted_ = false;
}

bool EditorSceneOptimizationManager::ResolveReferencePosition(
	int32_t referenceGameObjectId,
	Vector3& position) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* referenceGameObject = referenceGameObjectId >= 0
		? editorScene_->FindGameObject(referenceGameObjectId)
		: nullptr;

	if (referenceGameObject == nullptr) {
		int32_t highestPriority = INT32_MIN;

		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* camera = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::Camera);

			if (camera == nullptr || !camera->isActive) {
				camera = EditorComponentUtility::FindComponent(
					gameObject,
					EditorComponentType::CinemachineCamera);
			}

			if (camera != nullptr && camera->isActive && camera->cameraPriority > highestPriority) {
				highestPriority = camera->cameraPriority;
				referenceGameObject = &gameObject;
			}
		}
	}

	if (referenceGameObject == nullptr) {
		return false;
	}

	Vector3 scale{};
	Vector3 rotation{};
	editorScene_->GetWorldTransform(referenceGameObject->id, scale, rotation, position);
	return true;
}

void EditorSceneOptimizationManager::SetHierarchyActive(
	int32_t gameObjectId,
	bool isActive,
	bool affectsHierarchy) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return;
	}

	auto runtimeRequestedIterator = runtimeRequestedActiveStates_.find(gameObjectId);
	auto lastManagedIterator = lastManagedActiveStates_.find(gameObjectId);

	if (runtimeRequestedIterator == runtimeRequestedActiveStates_.end()) {
		runtimeRequestedIterator = runtimeRequestedActiveStates_
			.emplace(gameObjectId, gameObject->isActive)
			.first;
	}

	if (lastManagedIterator == lastManagedActiveStates_.end()) {
		lastManagedIterator = lastManagedActiveStates_
			.emplace(gameObjectId, gameObject->isActive)
			.first;
	}

	if (gameObject->isActive != lastManagedIterator->second) {
		runtimeRequestedIterator->second = gameObject->isActive;
	}

	const bool targetActive = isActive &&
		runtimeRequestedIterator->second;
	gameObject->isActive = targetActive;
	lastManagedActiveStates_[gameObjectId] = targetActive;

	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, targetActive);
	}

	if (!affectsHierarchy) {
		return;
	}

	for (const int32_t childGameObjectId : gameObject->children) {
		SetHierarchyActive(childGameObjectId, isActive, true);
	}
}

void EditorSceneOptimizationManager::ApplySimulationLod(
	int32_t gameObjectId,
	const EditorComponent& simulationLod,
	int32_t lodLevel,
	bool affectsHierarchy) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return;
	}

	std::vector<bool>& runtimeRequestedStates = runtimeRequestedComponentActiveStates_[gameObjectId];
	std::vector<bool>& lastManagedStates = lastManagedComponentActiveStates_[gameObjectId];
	std::vector<bool>& originalStates = originalComponentActiveStates_[gameObjectId];
	runtimeRequestedStates.reserve(gameObject->components.size());
	lastManagedStates.reserve(gameObject->components.size());
	originalStates.reserve(gameObject->components.size());

	while (runtimeRequestedStates.size() < gameObject->components.size()) {
		const size_t componentIndex = runtimeRequestedStates.size();
		const bool componentActive = gameObject->components[componentIndex].isActive;
		runtimeRequestedStates.push_back(componentActive);
	}

	while (lastManagedStates.size() < gameObject->components.size()) {
		const size_t componentIndex = lastManagedStates.size();
		lastManagedStates.push_back(gameObject->components[componentIndex].isActive);
	}

	while (originalStates.size() < gameObject->components.size()) {
		const size_t componentIndex = originalStates.size();
		originalStates.push_back(gameObject->components[componentIndex].isActive);
	}

	for (size_t componentIndex = 0u; componentIndex < gameObject->components.size(); componentIndex++) {
		EditorComponent& component = gameObject->components[componentIndex];

		if (component.isActive != lastManagedStates[componentIndex]) {
			runtimeRequestedStates[componentIndex] = component.isActive;
		}

		bool targetActive = runtimeRequestedStates[componentIndex];

		if (lodLevel >= 2) {
			if (simulationLod.simulationLodDisableScriptsAtFar &&
				(component.type == EditorComponentType::Script || component.type == EditorComponentType::MonoBehaviour)) {
				targetActive = false;
			}

			if (simulationLod.simulationLodDisableAiAtFar && IsAiComponent(component.type)) {
				targetActive = false;
			}

			if (simulationLod.simulationLodDisableAnimationAtFar && IsAnimationComponent(component.type)) {
				targetActive = false;
			}

			if (simulationLod.simulationLodDisableEffectsAtFar && IsEffectComponent(component.type)) {
				targetActive = false;
			}
		}

		component.isActive = targetActive;
		lastManagedStates[componentIndex] = targetActive;
	}

	if (physicsManager_ != nullptr) {
		const bool physicsActive = lodLevel < 2 || !simulationLod.simulationLodDisablePhysicsAtFar;
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, physicsActive && gameObject->isActive);
	}

	if (!affectsHierarchy) {
		return;
	}

	for (const int32_t childGameObjectId : gameObject->children) {
		ApplySimulationLod(childGameObjectId, simulationLod, lodLevel, true);
	}
}

void EditorSceneOptimizationManager::RestoreComponentStates(
	int32_t gameObjectId,
	bool affectsHierarchy) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	const auto originalStatesIterator = originalComponentActiveStates_.find(gameObjectId);

	if (gameObject == nullptr || originalStatesIterator == originalComponentActiveStates_.end()) {
		return;
	}

	for (size_t componentIndex = 0u;
		componentIndex < gameObject->components.size() &&
		componentIndex < originalStatesIterator->second.size();
		componentIndex++) {
		gameObject->components[componentIndex].isActive = originalStatesIterator->second[componentIndex];
	}

	if (affectsHierarchy) {
		for (const int32_t childGameObjectId : gameObject->children) {
			RestoreComponentStates(childGameObjectId, true);
		}
	}
}

bool EditorSceneOptimizationManager::IsAiComponent(EditorComponentType componentType) {
	return static_cast<int32_t>(componentType) >= static_cast<int32_t>(EditorComponentType::AIBehaviorTree) &&
		static_cast<int32_t>(componentType) <= static_cast<int32_t>(EditorComponentType::AIVoiceCommand);
}

bool EditorSceneOptimizationManager::IsAnimationComponent(EditorComponentType componentType) {
	return componentType == EditorComponentType::Animator ||
		componentType == EditorComponentType::Animation ||
		componentType == EditorComponentType::PlayableDirector;
}

bool EditorSceneOptimizationManager::IsEffectComponent(EditorComponentType componentType) {
	return componentType == EditorComponentType::ParticleSystem ||
		componentType == EditorComponentType::VisualEffect ||
		componentType == EditorComponentType::LensFlare ||
		componentType == EditorComponentType::TrailRenderer;
}
