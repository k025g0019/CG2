#include "EditorDamageManager.h"

#include "EditorComponentUtility.h"
#include "EditorObjectPoolManager.h"
#include "EditorOceanSystem.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
	Vector3 ResolveWorldPosition(const EditorScene& editorScene, const EditorGameObject& gameObject) {
		Vector3 worldScale = gameObject.scale;
		Vector3 worldRotation = gameObject.rotate;
		Vector3 worldPosition = gameObject.translate;
		editorScene.GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition);
		return worldPosition;
	}

	Vector3 NormalizeDirection(const Vector3& direction) {
		const float length = std::sqrt(
			direction.x * direction.x +
			direction.y * direction.y +
			direction.z * direction.z);

		if (length <= 0.0001f) {
			return {0.0f, 0.0f, 1.0f};
		}

		return {direction.x / length, direction.y / length, direction.z / length};
	}

	int32_t ResolveTeamId(const EditorScene& editorScene, int32_t gameObjectId) {
		const EditorComponent* team = EditorComponentUtility::FindInheritedComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::Team);
		return team != nullptr ? team->teamId : -2;
	}

	bool IsInHierarchy(
		const EditorScene& editorScene,
		int32_t gameObjectId,
		int32_t hierarchyRootGameObjectId) {
		const EditorGameObject* currentGameObject = editorScene.FindGameObject(gameObjectId);

		while (currentGameObject != nullptr) {
			if (currentGameObject->id == hierarchyRootGameObjectId) {
				return true;
			}

			currentGameObject = currentGameObject->parentId >= 0
				? editorScene.FindGameObject(currentGameObject->parentId)
				: nullptr;
		}

		return false;
	}
}

void EditorDamageManager::Initialize(
	EditorScene* editorScene,
	EditorScriptManager* scriptManager,
	EditorPhysicsManager* physicsManager,
	EditorObjectPoolManager* objectPoolManager) {
	editorScene_ = editorScene;
	scriptManager_ = scriptManager;
	physicsManager_ = physicsManager;
	objectPoolManager_ = objectPoolManager;
	invulnerabilityTimers_.clear();
	deadGameObjectIds_.clear();
	pendingDeactivationIds_.clear();
	lastDamageContexts_.clear();
	damageSequences_.clear();
	lastApplyResult_ = "NotApplied";
}

void EditorDamageManager::Start() {
	invulnerabilityTimers_.clear();
	deadGameObjectIds_.clear();
	pendingDeactivationIds_.clear();
	lastDamageContexts_.clear();
	damageSequences_.clear();
	lastApplyResult_ = "NotApplied";

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* damageEventBuffer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::DamageEventBuffer);

		if (damageEventBuffer != nullptr) {
			damageEventBuffer->damageEventEntries.clear();
		}

		EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Health);

		if (healthComponent == nullptr || !healthComponent->isActive) {
			continue;
		}

		healthComponent->healthMaximum = (std::max)(healthComponent->healthMaximum, 0.0f);
		healthComponent->healthCurrent = healthComponent->healthMaximum;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* areaDamage = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::AreaDamage);

		if (gameObject.isActive && areaDamage != nullptr && areaDamage->isActive && areaDamage->areaDamagePlayOnStart) {
			ApplyAreaDamage(gameObject.id, gameObject.id);
		}
	}
}

void EditorDamageManager::Update(float deltaTime) {
	if (editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (auto timerIterator = invulnerabilityTimers_.begin();
		timerIterator != invulnerabilityTimers_.end();) {
		timerIterator->second -= deltaTime;

		if (timerIterator->second <= 0.0f) {
			timerIterator = invulnerabilityTimers_.erase(timerIterator);
		}
		else {
			++timerIterator;
		}
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* damageEventBuffer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::DamageEventBuffer);

		if (damageEventBuffer == nullptr || !damageEventBuffer->isActive) {
			continue;
		}

		for (auto eventIterator = damageEventBuffer->damageEventEntries.begin();
			eventIterator != damageEventBuffer->damageEventEntries.end();) {
			eventIterator->remainingSeconds -= deltaTime;

			if (eventIterator->remainingSeconds <= 0.0f) {
				eventIterator = damageEventBuffer->damageEventEntries.erase(eventIterator);
			}
			else {
				++eventIterator;
			}
		}
	}

	for (const int32_t gameObjectId : pendingDeactivationIds_) {
		if (objectPoolManager_ != nullptr && objectPoolManager_->IsPooledObject(gameObjectId)) {
			objectPoolManager_->Release(gameObjectId);
			continue;
		}

		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

		if (gameObject != nullptr) {
			gameObject->isActive = false;

			if (physicsManager_ != nullptr) {
				physicsManager_->SetGameObjectSimulationActive(gameObjectId, false);
			}
		}
	}

	pendingDeactivationIds_.clear();
}

void EditorDamageManager::Stop() {
	invulnerabilityTimers_.clear();
	deadGameObjectIds_.clear();
	pendingDeactivationIds_.clear();
	lastDamageContexts_.clear();
	damageSequences_.clear();

	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* damageEventBuffer = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::DamageEventBuffer);

			if (damageEventBuffer != nullptr) {
				damageEventBuffer->damageEventEntries.clear();
			}
		}
	}
}

bool EditorDamageManager::ApplyDamage(
	int32_t targetGameObjectId,
	float damage,
	int32_t sourceGameObjectId) {
	EditorScriptDamageContext damageContext{};
	damageContext.targetGameObjectId = targetGameObjectId;
	damageContext.sourceGameObjectId = sourceGameObjectId;
	damageContext.instigatorGameObjectId = sourceGameObjectId;
	damageContext.baseDamage = damage;
	return ApplyDamage(damageContext);
}

bool EditorDamageManager::ApplyDamage(EditorScriptDamageContext& damageContext) {
	damageContext.appliedDamage = 0.0f;

	if (editorScene_ == nullptr) {
		lastApplyResult_ = "SceneUnavailable";
		return false;
	}

	if (damageContext.baseDamage <= 0.0f) {
		lastApplyResult_ = "BaseDamageNotPositive";
		return false;
	}

	float hitZoneMultiplier = 1.0f;
	damageContext.targetGameObjectId = ResolveDamageTarget(
		damageContext.targetGameObjectId,
		hitZoneMultiplier);
	EditorGameObject* targetGameObject = editorScene_->FindGameObject(damageContext.targetGameObjectId);
	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		lastApplyResult_ = "DamageTargetMissingOrInactive";
		return false;
	}

	EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
		*targetGameObject,
		EditorComponentType::Health);

	if (healthComponent == nullptr || !healthComponent->isActive) {
		lastApplyResult_ = "HealthMissingOrInactive";
		return false;
	}

	const auto invulnerabilityIterator = invulnerabilityTimers_.find(damageContext.targetGameObjectId);
	if (invulnerabilityIterator != invulnerabilityTimers_.end() && invulnerabilityIterator->second > 0.0f) {
		lastApplyResult_ = "Invulnerable";
		return false;
	}

	const EditorComponent* damageReceiver = EditorComponentUtility::FindComponent(
		*targetGameObject,
		EditorComponentType::DamageReceiver);
	const float damageMultiplier = damageReceiver != nullptr && damageReceiver->isActive
		? (std::max)(damageReceiver->damageMultiplier, 0.0f)
		: 1.0f;
	const float damageTagMultiplier = ResolveDamageTagMultiplier(*targetGameObject, damageContext.userTag);
	const float appliedDamage = damageContext.baseDamage * hitZoneMultiplier * damageMultiplier * damageTagMultiplier;

	if (appliedDamage <= 0.0f) {
		lastApplyResult_ = "DamageMultiplierRejected";
		return false;
	}

	healthComponent->healthCurrent = (std::clamp)(
		healthComponent->healthCurrent - appliedDamage,
		0.0f,
		(std::max)(healthComponent->healthMaximum, 0.0f));
	damageContext.appliedDamage = appliedDamage;
	lastApplyResult_ = "Applied";
	lastDamageContexts_[damageContext.targetGameObjectId] = damageContext;
	damageSequences_[damageContext.targetGameObjectId]++;
	RecordDamageEvent(damageContext);

	if (physicsManager_ != nullptr &&
		(damageContext.impulse.x != 0.0f || damageContext.impulse.y != 0.0f || damageContext.impulse.z != 0.0f)) {
		physicsManager_->AddImpulse(
			damageContext.targetGameObjectId,
			{damageContext.impulse.x, damageContext.impulse.y, damageContext.impulse.z});
	}

	if (damageReceiver != nullptr && damageReceiver->isActive) {
		const float invulnerabilitySeconds = (std::max)(
			damageReceiver->damageInvulnerabilitySeconds,
			0.0f);

		if (invulnerabilitySeconds > 0.0f) {
			invulnerabilityTimers_[damageContext.targetGameObjectId] = invulnerabilitySeconds;
		}

		QueueAction(
			*targetGameObject,
			damageReceiver,
			damageReceiver->damagedActionName,
			appliedDamage);
	}

	if (healthComponent->healthCurrent > 0.0f || deadGameObjectIds_.contains(damageContext.targetGameObjectId)) {
		return true;
	}

	deadGameObjectIds_.insert(damageContext.targetGameObjectId);

	if (damageReceiver != nullptr && damageReceiver->isActive) {
		if (scriptManager_ != nullptr && !damageReceiver->deathActionName.empty()) {
			const int32_t actionTargetGameObjectId = damageReceiver->damageActionTargetGameObjectId >= 0
				? damageReceiver->damageActionTargetGameObjectId
				: targetGameObject->id;
			EditorScriptActionPayload payload{};
			payload.type = EditorScriptActionPayloadTypeGameObject;
			payload.gameObjectId = targetGameObject->id;
			scriptManager_->QueueActionPayload(
				actionTargetGameObjectId,
				damageReceiver->deathActionName,
				payload);
		}

		if (damageReceiver->damageDeactivateOnDeath) {
			pendingDeactivationIds_.insert(damageContext.targetGameObjectId);
		}
	}

	return true;
}

int32_t EditorDamageManager::ApplyAreaDamage(
	int32_t areaDamageGameObjectId,
	int32_t instigatorGameObjectId) {
	if (editorScene_ == nullptr) {
		return 0;
	}

	const EditorGameObject* areaDamageGameObject = editorScene_->FindGameObject(areaDamageGameObjectId);
	if (areaDamageGameObject == nullptr) {
		return 0;
	}

	return ApplyAreaDamage(
		areaDamageGameObjectId,
		ResolveWorldPosition(*editorScene_, *areaDamageGameObject),
		instigatorGameObjectId);
}

int32_t EditorDamageManager::ApplyAreaDamage(
	int32_t areaDamageGameObjectId,
	const Vector3& center,
	int32_t instigatorGameObjectId) {
	if (editorScene_ == nullptr || physicsManager_ == nullptr) {
		return 0;
	}

	const EditorGameObject* areaDamageGameObject = editorScene_->FindGameObject(areaDamageGameObjectId);
	const EditorComponent* areaDamage = areaDamageGameObject != nullptr
		? EditorComponentUtility::FindComponent(*areaDamageGameObject, EditorComponentType::AreaDamage)
		: nullptr;

	if (areaDamage == nullptr || !areaDamage->isActive || areaDamage->areaDamageRadius <= 0.0f) {
		return 0;
	}

	std::vector<int32_t> hitGameObjectIds;
	if (!physicsManager_->OverlapSphere(center, areaDamage->areaDamageRadius, hitGameObjectIds)) {
		return 0;
	}

	std::unordered_set<int32_t> affectedHealthGameObjectIds;
	int32_t affectedCount = 0;
	const int32_t teamSourceGameObjectId = areaDamage->areaDamageTeamSourceGameObjectId >= 0
		? areaDamage->areaDamageTeamSourceGameObjectId
		: (instigatorGameObjectId >= 0 ? instigatorGameObjectId : areaDamageGameObjectId);
	const int32_t sourceTeamId = ResolveTeamId(*editorScene_, teamSourceGameObjectId);

	for (const int32_t hitGameObjectId : hitGameObjectIds) {
		if (areaDamage->areaDamageIgnoreOwner && hitGameObjectId == instigatorGameObjectId) {
			continue;
		}

		const EditorGameObject* hitGameObject = editorScene_->FindGameObject(hitGameObjectId);
		if (hitGameObject == nullptr || !hitGameObject->isActive) {
			continue;
		}

		const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
			*hitGameObject,
			EditorComponentType::RigidBody);
		const int32_t physicsLayer = rigidBody != nullptr ? rigidBody->physicsLayer : 0;

		if (areaDamage->areaDamageLayerMask >= 0 &&
			(areaDamage->areaDamageLayerMask & (1 << (std::clamp)(physicsLayer, 0, 30))) == 0) {
			continue;
		}

		const int32_t targetTeamId = ResolveTeamId(*editorScene_, hitGameObjectId);
		const bool hasSourceTeam = sourceTeamId >= -1;
		const bool hasTargetTeam = targetTeamId >= -1;
		bool ignoresTarget = areaDamage->areaDamageIgnoreNeutral &&
			hasTargetTeam && targetTeamId < 0;

		if (hasSourceTeam && hasTargetTeam) {
			if (areaDamage->areaDamageTeamRule == 1 && targetTeamId == sourceTeamId) {
				ignoresTarget = true;
			}
			else if (areaDamage->areaDamageTeamRule == 2 && targetTeamId != sourceTeamId) {
				ignoresTarget = true;
			}
		}

		if (ignoresTarget) {
			continue;
		}

		float unusedHitZoneMultiplier = 1.0f;
		const int32_t healthGameObjectId = ResolveDamageTarget(hitGameObjectId, unusedHitZoneMultiplier);
		if (healthGameObjectId < 0 || affectedHealthGameObjectIds.contains(healthGameObjectId)) {
			continue;
		}

		const Vector3 hitPosition = ResolveWorldPosition(*editorScene_, *hitGameObject);
		float occlusionMultiplier = 1.0f;

		if (areaDamage->areaDamageOcclusionMode > 0) {
			Vector3 targetScale = hitGameObject->scale;
			Vector3 targetRotation = hitGameObject->rotate;
			Vector3 targetWorldPosition = hitPosition;
			editorScene_->GetWorldTransform(
				hitGameObjectId,
				targetScale,
				targetRotation,
				targetWorldPosition);
			const int32_t samplePointCount = (std::clamp)(
				areaDamage->areaDamageOcclusionSamplePoints,
				1,
				9);
			int32_t blockedSampleCount = 0;
			std::vector<int32_t> ignoredGameObjectIds;
			ignoredGameObjectIds.reserve(editorScene_->GetGameObjects().size());

			// 発射者、爆発源、Damage対象のHierarchyは全遮蔽Sampleで共通なので一度だけ収集する。
			for (const EditorGameObject& candidate : editorScene_->GetGameObjects()) {
				if (IsInHierarchy(*editorScene_, candidate.id, healthGameObjectId) ||
					IsInHierarchy(*editorScene_, candidate.id, areaDamageGameObjectId) ||
					(instigatorGameObjectId >= 0 &&
						IsInHierarchy(*editorScene_, candidate.id, instigatorGameObjectId))) {
					ignoredGameObjectIds.push_back(candidate.id);
				}
			}

			for (int32_t sampleIndex = 0; sampleIndex < samplePointCount; sampleIndex++) {
				const float normalizedSample = samplePointCount <= 1
					? 0.0f
					: static_cast<float>(sampleIndex) /
						static_cast<float>(samplePointCount - 1) - 0.5f;
				const Vector3 samplePosition{
					targetWorldPosition.x,
					targetWorldPosition.y + normalizedSample * (std::max)(std::abs(targetScale.y), 0.1f),
					targetWorldPosition.z};
				const Vector3 sampleOffset{
					samplePosition.x - center.x,
					samplePosition.y - center.y,
					samplePosition.z - center.z};
				const float sampleDistance = std::sqrt(
					sampleOffset.x * sampleOffset.x +
					sampleOffset.y * sampleOffset.y +
					sampleOffset.z * sampleOffset.z);
				bool isBlocked = false;

				if (sampleDistance > 0.0001f) {
					EditorJoltPhysicsManager::PhysicsHit occlusionHit{};
					const Vector3 sampleDirection = NormalizeDirection(sampleOffset);

					for (int32_t castIndex = 0; castIndex < 64; castIndex++) {
						const bool hasOcclusionHit = physicsManager_->RaycastIgnoringGameObjects(
							center,
							sampleDirection,
							sampleDistance,
							ignoredGameObjectIds,
							occlusionHit);

						if (!hasOcclusionHit) {
							break;
						}

						const EditorGameObject* blocker = editorScene_->FindGameObject(occlusionHit.gameObjectId);
						const EditorComponent* blockerRigidBody = blocker != nullptr
							? EditorComponentUtility::FindComponent(*blocker, EditorComponentType::RigidBody)
							: nullptr;
						const int32_t blockerLayer = blockerRigidBody != nullptr
							? (std::clamp)(blockerRigidBody->physicsLayer, 0, 30)
							: 0;
						const bool isOcclusionLayer = areaDamage->areaDamageOcclusionLayerMask < 0 ||
							(areaDamage->areaDamageOcclusionLayerMask & (1 << blockerLayer)) != 0;

						if (isOcclusionLayer) {
							isBlocked = true;
							break;
						}

						ignoredGameObjectIds.push_back(occlusionHit.gameObjectId);
					}

					if (!isBlocked && areaDamage->areaDamageOcclusionMode == 2) {
						EditorOceanSegmentHit oceanHit{};
						const uint64_t queryKey =
							(static_cast<uint64_t>(static_cast<uint32_t>(areaDamageGameObjectId)) << 32u) |
							static_cast<uint32_t>(hitGameObjectId + sampleIndex);
						isBlocked = CastEditorOceanSegment(
							*editorScene_,
							-1,
							center,
							samplePosition,
							0.0f,
							queryKey,
							GetEditorOceanElapsedTime(),
							2,
							4,
							oceanHit);
					}
				}

				if (isBlocked) {
					blockedSampleCount++;
				}
			}

			const float blockedRatio = static_cast<float>(blockedSampleCount) /
				static_cast<float>(samplePointCount);
			const float blockedMultiplier = (std::clamp)(
				areaDamage->areaDamageBlockedMultiplier,
				0.0f,
				1.0f);
			occlusionMultiplier = 1.0f - blockedRatio * (1.0f - blockedMultiplier);
		}
		const Vector3 offset{
			hitPosition.x - center.x,
			hitPosition.y - center.y,
			hitPosition.z - center.z};
		const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
		const float normalizedDistance = (std::clamp)(distance / areaDamage->areaDamageRadius, 0.0f, 1.0f);
		float falloff = 1.0f;

		if (areaDamage->areaDamageFalloffMode == 1) {
			falloff = 1.0f - normalizedDistance;
		}
		else if (areaDamage->areaDamageFalloffMode == 2) {
			const float smoothDistance = normalizedDistance * normalizedDistance * (3.0f - 2.0f * normalizedDistance);
			falloff = 1.0f - smoothDistance;
		}

		falloff = (std::max)(falloff, (std::clamp)(areaDamage->areaDamageMinimumMultiplier, 0.0f, 1.0f));
		falloff *= occlusionMultiplier;
		EditorScriptDamageContext damageContext{};
		damageContext.targetGameObjectId = hitGameObjectId;
		damageContext.sourceGameObjectId = areaDamageGameObjectId;
		damageContext.instigatorGameObjectId = instigatorGameObjectId >= 0 ? instigatorGameObjectId : areaDamageGameObjectId;
		damageContext.hitPosition = {hitPosition.x, hitPosition.y, hitPosition.z};
		damageContext.baseDamage = (std::max)(areaDamage->areaDamageBaseDamage, 0.0f) * falloff;
		damageContext.userTag = HashDamageTag(areaDamage->areaDamageTag);

		if (distance > 0.0001f && areaDamage->areaDamageImpulse != 0.0f) {
			const float impulseScale = areaDamage->areaDamageImpulse * falloff / distance;
			damageContext.impulse = {
				offset.x * impulseScale,
				offset.y * impulseScale,
				offset.z * impulseScale};
		}

		if (ApplyDamage(damageContext)) {
			affectedHealthGameObjectIds.insert(healthGameObjectId);
			affectedCount++;
		}
	}

	if (affectedCount > 0 && scriptManager_ != nullptr && !areaDamage->areaDamageAppliedActionName.empty()) {
		const int32_t actionTargetGameObjectId = areaDamage->areaDamageActionTargetGameObjectId >= 0
			? areaDamage->areaDamageActionTargetGameObjectId
			: areaDamageGameObjectId;
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeInt;
		payload.intValue = affectedCount;
		scriptManager_->QueueActionPayload(
			actionTargetGameObjectId,
			areaDamage->areaDamageAppliedActionName,
			payload);
	}

	return affectedCount;
}

int32_t EditorDamageManager::HashDamageTag(std::string_view damageTag) {
	uint32_t hashValue = 2166136261u;

	for (const char character : damageTag) {
		hashValue ^= static_cast<uint8_t>(character);
		hashValue *= 16777619u;
	}

	return static_cast<int32_t>(hashValue);
}

bool EditorDamageManager::GetLastDamageContext(
	int32_t targetGameObjectId,
	EditorScriptDamageContext& damageContext) const {
	const auto damageContextIterator = lastDamageContexts_.find(targetGameObjectId);

	if (damageContextIterator == lastDamageContexts_.end()) {
		return false;
	}

	damageContext = damageContextIterator->second;
	return true;
}

uint64_t EditorDamageManager::GetDamageSequence(int32_t targetGameObjectId) const {
	const auto iterator = damageSequences_.find(targetGameObjectId);
	return iterator != damageSequences_.end() ? iterator->second : 0u;
}

bool EditorDamageManager::GetDamageEventCount(int32_t gameObjectId, int32_t& eventCount) const {
	const EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	const EditorComponent* damageEventBuffer = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::DamageEventBuffer)
		: nullptr;

	if (damageEventBuffer == nullptr || !damageEventBuffer->isActive) {
		return false;
	}

	eventCount = static_cast<int32_t>(damageEventBuffer->damageEventEntries.size());
	return true;
}

bool EditorDamageManager::GetDamageEvent(
	int32_t gameObjectId,
	int32_t eventIndex,
	EditorDamageEventRuntimeEntry& damageEvent) const {
	const EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;
	const EditorComponent* damageEventBuffer = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::DamageEventBuffer)
		: nullptr;

	if (damageEventBuffer == nullptr || !damageEventBuffer->isActive ||
		eventIndex < 0 || eventIndex >= static_cast<int32_t>(damageEventBuffer->damageEventEntries.size())) {
		return false;
	}

	damageEvent = damageEventBuffer->damageEventEntries[static_cast<size_t>(eventIndex)];
	return true;
}

bool EditorDamageManager::GetHealth(
	int32_t gameObjectId,
	float& currentHealth,
	float& maximumHealth) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Health);

	if (healthComponent == nullptr || !healthComponent->isActive) {
		return false;
	}

	currentHealth = healthComponent->healthCurrent;
	maximumHealth = healthComponent->healthMaximum;
	return true;
}

bool EditorDamageManager::SetHealth(int32_t gameObjectId, float currentHealth) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Health);

	if (healthComponent == nullptr || !healthComponent->isActive) {
		return false;
	}

	healthComponent->healthCurrent = (std::clamp)(
		currentHealth,
		0.0f,
		(std::max)(healthComponent->healthMaximum, 0.0f));

	if (healthComponent->healthCurrent > 0.0f) {
		deadGameObjectIds_.erase(gameObjectId);
		pendingDeactivationIds_.erase(gameObjectId);
	}

	return true;
}

void EditorDamageManager::ResetRuntimeState(int32_t gameObjectId) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return;
	}

	EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Health);

	if (healthComponent != nullptr && healthComponent->isActive) {
		healthComponent->healthMaximum = (std::max)(healthComponent->healthMaximum, 0.0f);
		healthComponent->healthCurrent = healthComponent->healthMaximum;
	}

	EditorComponent* damageEventBuffer = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::DamageEventBuffer);

	if (damageEventBuffer != nullptr) {
		damageEventBuffer->damageEventEntries.clear();
	}

	invulnerabilityTimers_.erase(gameObjectId);
	deadGameObjectIds_.erase(gameObjectId);
	pendingDeactivationIds_.erase(gameObjectId);
	lastDamageContexts_.erase(gameObjectId);
	damageSequences_.erase(gameObjectId);
}

void EditorDamageManager::RecordDamageEvent(const EditorScriptDamageContext& damageContext) {
	if (editorScene_ == nullptr || damageContext.appliedDamage <= 0.0f) {
		return;
	}

	EditorGameObject* targetGameObject = editorScene_->FindGameObject(damageContext.targetGameObjectId);
	EditorComponent* damageEventBuffer = targetGameObject != nullptr
		? EditorComponentUtility::FindComponent(*targetGameObject, EditorComponentType::DamageEventBuffer)
		: nullptr;

	if (targetGameObject == nullptr || damageEventBuffer == nullptr || !damageEventBuffer->isActive ||
		damageContext.appliedDamage < (std::max)(damageEventBuffer->damageEventMinimumDamage, 0.0f)) {
		return;
	}

	Vector3 direction{
		-damageContext.hitNormal.x,
		-damageContext.hitNormal.y,
		-damageContext.hitNormal.z};
	const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(damageContext.sourceGameObjectId);

	if (sourceGameObject != nullptr) {
		const Vector3 sourcePosition = ResolveWorldPosition(*editorScene_, *sourceGameObject);
		const Vector3 targetPosition = ResolveWorldPosition(*editorScene_, *targetGameObject);
		direction = {
			sourcePosition.x - targetPosition.x,
			sourcePosition.y - targetPosition.y,
			sourcePosition.z - targetPosition.z};
	}

	const float lifetime = (std::max)(damageEventBuffer->damageEventLifetime, 0.01f);

	if (damageEventBuffer->damageEventMergeSameSource && damageContext.sourceGameObjectId >= 0) {
		for (EditorDamageEventRuntimeEntry& damageEvent : damageEventBuffer->damageEventEntries) {
			if (damageEvent.sourceGameObjectId != damageContext.sourceGameObjectId) {
				continue;
			}

			damageEvent.worldDirection = NormalizeDirection(direction);
			damageEvent.damage += damageContext.appliedDamage;
			damageEvent.damageTagId = damageContext.userTag;
			damageEvent.remainingSeconds = lifetime;
			return;
		}
	}

	EditorDamageEventRuntimeEntry damageEvent{};
	damageEvent.sourceGameObjectId = damageContext.sourceGameObjectId;
	damageEvent.worldDirection = NormalizeDirection(direction);
	damageEvent.damage = damageContext.appliedDamage;
	damageEvent.damageTagId = damageContext.userTag;
	damageEvent.remainingSeconds = lifetime;
	damageEventBuffer->damageEventEntries.push_back(damageEvent);

	const int32_t maximumEntries = (std::clamp)(damageEventBuffer->damageEventMaximumEntries, 1, 64);

	while (static_cast<int32_t>(damageEventBuffer->damageEventEntries.size()) > maximumEntries) {
		damageEventBuffer->damageEventEntries.erase(damageEventBuffer->damageEventEntries.begin());
	}
}

int32_t EditorDamageManager::ResolveDamageTarget(
	int32_t hitGameObjectId,
	float& hitZoneMultiplier) const {
	hitZoneMultiplier = 1.0f;

	if (editorScene_ == nullptr) {
		return -1;
	}

	const EditorGameObject* hitGameObject = editorScene_->FindGameObject(hitGameObjectId);
	if (hitGameObject == nullptr) {
		return -1;
	}

	const EditorComponent* hitZone = EditorComponentUtility::FindComponent(
		*hitGameObject,
		EditorComponentType::HitZone);

	if (hitZone != nullptr && hitZone->isActive) {
		hitZoneMultiplier = (std::max)(hitZone->hitZoneDamageMultiplier, 0.0f);

		if (hitZone->hitZoneHealthGameObjectId >= 0) {
			return hitZone->hitZoneHealthGameObjectId;
		}
	}

	// Imported ModelのColliderが子GameObjectへ分かれていても、親のHealthへDamageを届ける。
	const EditorGameObject* damageTargetGameObject = hitGameObject;

	while (damageTargetGameObject != nullptr) {
		const EditorComponent* health = EditorComponentUtility::FindComponent(
			*damageTargetGameObject,
			EditorComponentType::Health);

		if (health != nullptr && health->isActive) {
			return damageTargetGameObject->id;
		}

		if (damageTargetGameObject->parentId < 0) {
			break;
		}

		damageTargetGameObject = editorScene_->FindGameObject(damageTargetGameObject->parentId);
	}

	return hitGameObjectId;
}

float EditorDamageManager::ResolveDamageTagMultiplier(
	const EditorGameObject& targetGameObject,
	int32_t damageTagId) const {
	const EditorComponent* modifier = EditorComponentUtility::FindComponent(
		targetGameObject,
		EditorComponentType::DamageTagModifier);

	if (modifier == nullptr || !modifier->isActive) {
		return 1.0f;
	}

	for (const EditorDamageTagModifierEntry& entry : modifier->damageTagModifierEntries) {
		if (HashDamageTag(entry.tagName) == damageTagId) {
			return (std::max)(entry.multiplier, 0.0f);
		}
	}

	return (std::max)(modifier->damageTagDefaultMultiplier, 0.0f);
}

void EditorDamageManager::QueueAction(
	const EditorGameObject& ownerGameObject,
	const EditorComponent* damageReceiver,
	const std::string& actionName,
	float value) const {
	if (scriptManager_ == nullptr || damageReceiver == nullptr || actionName.empty()) {
		return;
	}

	const int32_t targetGameObjectId = damageReceiver->damageActionTargetGameObjectId >= 0
		? damageReceiver->damageActionTargetGameObjectId
		: ownerGameObject.id;
	scriptManager_->QueueActionEvent(
		targetGameObjectId,
		actionName,
		EditorScriptInputValueTypeButton,
		value,
		EditorScriptVector2{});
}
