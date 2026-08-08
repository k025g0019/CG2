#include "EditorWeaponManager.h"

#include "Source/Engine/Effect/EditorEffectManager.h"
#include "EditorAudioManager.h"
#include "EditorCameraEffectManager.h"
#include "EditorComponentUtility.h"
#include "EditorDamageManager.h"
#include "EditorInputManager.h"
#include "EditorObjectPoolManager.h"
#include "EditorOceanSystem.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
	constexpr float kWeaponEpsilon = 0.0001f;
	constexpr float kProjectileAimDistance = 1000.0f;
	constexpr float kDegreesToRadians = 0.01745329251994329577f;

	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 MultiplyVector3(float scalar, const Vector3& value) {
		return {scalar * value.x, scalar * value.y, scalar * value.z};
	}

	float GetVectorLength(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 NormalizeVector3(const Vector3& value) {
		const float vectorLength = GetVectorLength(value);

		if (vectorLength <= kWeaponEpsilon) {
			return {0.0f, 0.0f, 1.0f};
		}

		return {
			value.x / vectorLength,
			value.y / vectorLength,
			value.z / vectorLength};
	}

	float DotVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return firstValue.x * secondValue.x + firstValue.y * secondValue.y + firstValue.z * secondValue.z;
	}

	Vector3 CrossVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.y * secondValue.z - firstValue.z * secondValue.y,
			firstValue.z * secondValue.x - firstValue.x * secondValue.z,
			firstValue.x * secondValue.y - firstValue.y * secondValue.x};
	}

	Vector3 ResolveWorldPosition(const EditorScene& editorScene, const EditorGameObject& gameObject) {
		Vector3 worldScale = gameObject.scale;
		Vector3 worldRotation = gameObject.rotate;
		Vector3 worldPosition = gameObject.translate;
		editorScene.GetWorldTransform(gameObject.id, worldScale, worldRotation, worldPosition);
		return worldPosition;
	}

	Vector3 ResolveForwardDirection(const Vector3& worldRotation) {
		const float cosPitch = std::cos(worldRotation.x);
		return NormalizeVector3({
			std::sin(worldRotation.y) * cosPitch,
			-std::sin(worldRotation.x),
			std::cos(worldRotation.y) * cosPitch});
	}
}

void EditorWeaponManager::Initialize(
	EditorScene* editorScene,
	EditorInputManager* inputManager,
	EditorTargetingManager* targetingManager,
	EditorPhysicsManager* physicsManager,
	EditorDamageManager* damageManager,
	EditorObjectPoolManager* objectPoolManager,
	EditorScriptManager* scriptManager,
	EditorEffectManager* effectManager,
	EditorAudioManager* audioManager,
	EditorCameraEffectManager* cameraEffectManager) {
	editorScene_ = editorScene;
	inputManager_ = inputManager;
	targetingManager_ = targetingManager;
	physicsManager_ = physicsManager;
	damageManager_ = damageManager;
	objectPoolManager_ = objectPoolManager;
	scriptManager_ = scriptManager;
	effectManager_ = effectManager;
	audioManager_ = audioManager;
	cameraEffectManager_ = cameraEffectManager;
	hitscanCooldowns_.clear();
	projectileCooldowns_.clear();
	activeProjectiles_.clear();
	pendingShots_.clear();
	pendingWeaponGroupShots_.clear();
	visualRecoilRuntimes_.clear();
	isStarted_ = false;
}

void EditorWeaponManager::Start() {
	hitscanCooldowns_.clear();
	projectileCooldowns_.clear();
	activeProjectiles_.clear();
	pendingShots_.clear();
	pendingWeaponGroupShots_.clear();
	visualRecoilRuntimes_.clear();
	accuracyRandomState_ = 0x434732u;

	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* accuracy = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::WeaponAccuracy);

			if (accuracy != nullptr) {
				accuracy->weaponAccuracyCurrentSpread = 0.0f;
			}

			EditorComponent* weaponGroup = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::WeaponGroup);

			if (weaponGroup != nullptr) {
				weaponGroup->weaponGroupRoundRobinIndex = 0;
				weaponGroup->weaponGroupIsFiring = false;
			}
		}
	}
	isStarted_ = true;
}

void EditorWeaponManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (auto& cooldownPair : hitscanCooldowns_) {
		cooldownPair.second = (std::max)(cooldownPair.second - deltaTime, 0.0f);
	}

	for (auto& cooldownPair : projectileCooldowns_) {
		cooldownPair.second = (std::max)(cooldownPair.second - deltaTime, 0.0f);
	}

	UpdateFireLineChecks();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* hitscanComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::HitscanWeapon);

		const EditorComponent* projectileComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ProjectileEmitter);

		if (hitscanComponent != nullptr && projectileComponent == nullptr && hitscanComponent->isActive &&
			ShouldFire(
				gameObject,
				hitscanComponent->hitscanInputGameObjectId,
				hitscanComponent->hitscanActionMapName,
				hitscanComponent->hitscanFireActionName,
				hitscanComponent->hitscanAutomatic)) {
			FireHitscan(gameObject.id);
		}

		if (projectileComponent != nullptr && projectileComponent->isActive &&
			ShouldFire(
				gameObject,
				projectileComponent->projectileInputGameObjectId,
				projectileComponent->projectileActionMapName,
				projectileComponent->projectileFireActionName,
				projectileComponent->projectileAutomatic)) {
			FireProjectile(gameObject.id);
		}
	}

	RecoverAccuracy(deltaTime);
	UpdateWeaponGroups(deltaTime);
	UpdatePendingShots(deltaTime);
	UpdateProjectiles(deltaTime);
	UpdateVisualRecoil(deltaTime);
}

void EditorWeaponManager::Stop() {
	if (objectPoolManager_ != nullptr) {
		for (const ActiveProjectile& activeProjectile : activeProjectiles_) {
			objectPoolManager_->Release(activeProjectile.gameObjectId);
		}
	}

	if (editorScene_ != nullptr) {
		for (const auto& [weaponGameObjectId, runtime] : visualRecoilRuntimes_) {
			(void)weaponGameObjectId;
			EditorGameObject* visual = editorScene_->FindGameObject(runtime.gameObjectId);

			if (visual != nullptr) {
				visual->translate = SubtractVector3(visual->translate, runtime.positionOffset);
				visual->rotate = SubtractVector3(visual->rotate, runtime.rotationOffset);
			}
		}
	}

	hitscanCooldowns_.clear();
	projectileCooldowns_.clear();
	activeProjectiles_.clear();
	pendingShots_.clear();
	pendingWeaponGroupShots_.clear();
	visualRecoilRuntimes_.clear();
	isStarted_ = false;
}

bool EditorWeaponManager::FireHitscan(int32_t weaponGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* weaponGameObject = editorScene_->FindGameObject(weaponGameObjectId);
	if (weaponGameObject == nullptr || !weaponGameObject->isActive) {
		return false;
	}

	const EditorComponent* hitscanComponent = EditorComponentUtility::FindComponent(
		*weaponGameObject,
		EditorComponentType::HitscanWeapon);

	if (hitscanComponent == nullptr || !hitscanComponent->isActive ||
		hitscanCooldowns_[weaponGameObjectId] > 0.0f ||
		!EvaluateFireLine(weaponGameObjectId, true)) {
		return false;
	}

	return QueueFireRequest(weaponGameObjectId, false);
}

bool EditorWeaponManager::FireProjectile(int32_t emitterGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr || objectPoolManager_ == nullptr) {
		return false;
	}

	EditorGameObject* emitterGameObject = editorScene_->FindGameObject(emitterGameObjectId);
	if (emitterGameObject == nullptr || !emitterGameObject->isActive) {
		return false;
	}

	const EditorComponent* projectileComponent = EditorComponentUtility::FindComponent(
		*emitterGameObject,
		EditorComponentType::ProjectileEmitter);

	if (projectileComponent == nullptr || !projectileComponent->isActive ||
		projectileCooldowns_[emitterGameObjectId] > 0.0f ||
		!EvaluateFireLine(emitterGameObjectId, true)) {
		return false;
	}

	if (emitterGameObject->name == "Weapon 20mm") {
		char debugBuffer[256];
		std::snprintf(
			debugBuffer,
			sizeof(debugBuffer),
			"[Weapon20mm] FireProjectile pool=%d spawn=%d speed=%.1f damage=%.1f\n",
			projectileComponent->projectilePoolGameObjectId,
			projectileComponent->projectileSpawnPointGameObjectId,
			projectileComponent->projectileSpeed,
			projectileComponent->projectileDamage);
		OutputDebugStringA(debugBuffer);
	}

	return QueueFireRequest(emitterGameObjectId, true);
}

bool EditorWeaponManager::FireWeaponGroup(int32_t groupGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* groupGameObject = editorScene_->FindGameObject(groupGameObjectId);
	EditorComponent* group = groupGameObject != nullptr
		? EditorComponentUtility::FindComponent(*groupGameObject, EditorComponentType::WeaponGroup)
		: nullptr;

	if (group == nullptr || !group->isActive || group->weaponGroupEntries.empty()) {
		return false;
	}

	std::vector<int32_t> weapons;

	for (const EditorWeaponGroupEntry& entry : group->weaponGroupEntries) {
		if (entry.isEnabled && entry.weaponGameObjectId >= 0) {
			weapons.push_back(entry.weaponGameObjectId);
		}
	}

	if (weapons.empty()) {
		return false;
	}

	if (group->weaponGroupRequireAllReady) {
		for (const int32_t weaponGameObjectId : weapons) {
			if (!IsWeaponReady(weaponGameObjectId)) {
				return false;
			}
		}
	}

	const int32_t groupMode = (std::clamp)(group->weaponGroupMode, 0, 2);

	if (groupMode == 1) {
		if (group->weaponGroupIsFiring) {
			return false;
		}

		for (size_t weaponIndex = 0u; weaponIndex < weapons.size(); ++weaponIndex) {
			pendingWeaponGroupShots_.push_back({
				groupGameObjectId,
				weapons[weaponIndex],
				static_cast<float>(weaponIndex) * (std::max)(group->weaponGroupInterval, 0.0f),
				weaponIndex + 1u == weapons.size()});
		}

		group->weaponGroupIsFiring = true;
		return true;
	}

	bool firedAnyWeapon = false;

	if (groupMode == 2) {
		const int32_t weaponCount = static_cast<int32_t>(weapons.size());
		const int32_t firstIndex = ((group->weaponGroupRoundRobinIndex % weaponCount) + weaponCount) % weaponCount;

		for (int32_t indexOffset = 0; indexOffset < weaponCount; ++indexOffset) {
			const int32_t weaponIndex = (firstIndex + indexOffset) % weaponCount;

			if (IsWeaponReady(weapons[static_cast<size_t>(weaponIndex)]) &&
				FireWeaponObject(weapons[static_cast<size_t>(weaponIndex)])) {
				group->weaponGroupRoundRobinIndex = (weaponIndex + 1) % weaponCount;
				firedAnyWeapon = true;
				break;
			}
		}
	}
	else {
		for (const int32_t weaponGameObjectId : weapons) {
			if ((group->weaponGroupRequireAllReady || IsWeaponReady(weaponGameObjectId)) &&
				FireWeaponObject(weaponGameObjectId)) {
				firedAnyWeapon = true;
			}
		}
	}

	if (firedAnyWeapon) {
		QueueAction(
			groupGameObjectId,
			group->weaponGroupActionTargetGameObjectId,
			group->weaponGroupCompletedActionName,
			1.0f);
	}

	return firedAnyWeapon;
}

bool EditorWeaponManager::IsWeaponGroupFiring(int32_t groupGameObjectId) const {
	const EditorGameObject* groupGameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(groupGameObjectId)
		: nullptr;
	const EditorComponent* group = groupGameObject != nullptr
		? EditorComponentUtility::FindComponent(*groupGameObject, EditorComponentType::WeaponGroup)
		: nullptr;
	return group != nullptr && group->weaponGroupIsFiring;
}

bool EditorWeaponManager::GetAccuracySpread(int32_t weaponGameObjectId, float& spreadDegrees) const {
	const EditorGameObject* weapon = editorScene_ != nullptr
		? editorScene_->FindGameObject(weaponGameObjectId)
		: nullptr;
	const EditorComponent* accuracy = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::WeaponAccuracy)
		: nullptr;

	if (accuracy == nullptr || !accuracy->isActive) {
		return false;
	}

	spreadDegrees = (std::clamp)(
		accuracy->weaponAccuracyBaseSpread + accuracy->weaponAccuracyCurrentSpread,
		0.0f,
		(std::max)(accuracy->weaponAccuracyMaximumSpread, 0.0f));
	return true;
}

bool EditorWeaponManager::ShouldFire(
	const EditorGameObject& gameObject,
	int32_t inputGameObjectId,
	const std::string& actionMapName,
	const std::string& actionName,
	bool isAutomatic) const {
	if (inputManager_ == nullptr || actionName.empty()) {
		return false;
	}

	const int32_t sourceGameObjectId = inputGameObjectId >= 0 ? inputGameObjectId : gameObject.id;
	return isAutomatic
		? inputManager_->IsActionPressed(sourceGameObjectId, actionMapName, actionName)
		: inputManager_->WasActionJustPressed(sourceGameObjectId, actionMapName, actionName);
}

bool EditorWeaponManager::BuildAimRay(
	int32_t screenAimGameObjectId,
	EditorTargetingManager::AimRay& aimRay) const {
	return targetingManager_ != nullptr && targetingManager_->GetAimRay(screenAimGameObjectId, aimRay);
}

Vector3 EditorWeaponManager::BuildProjectileDirection(
	const Vector3& spawnPosition,
	const EditorTargetingManager::AimRay& aimRay) const {
	const Vector3 aimTarget = AddVector3(
		aimRay.origin,
		MultiplyVector3(kProjectileAimDistance, aimRay.direction));
	return NormalizeVector3(SubtractVector3(aimTarget, spawnPosition));
}

bool EditorWeaponManager::QueueFireRequest(int32_t weaponGameObjectId, bool isProjectile) {
	EditorGameObject* weaponGameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(weaponGameObjectId)
		: nullptr;

	if (weaponGameObject == nullptr || !weaponGameObject->isActive) {
		return false;
	}

	if (isProjectile) {
		EditorComponent* assignment = EditorComponentUtility::FindComponent(
			*weaponGameObject,
			EditorComponentType::TargetAssignment);

		if (assignment != nullptr && assignment->isActive && QueueTargetAssignment(weaponGameObjectId, *assignment)) {
			return true;
		}
	}

	const EditorComponent* pattern = EditorComponentUtility::FindComponent(
		*weaponGameObject,
		EditorComponentType::WeaponFirePattern);
	QueueFirePattern(
		weaponGameObjectId,
		isProjectile,
		pattern != nullptr && pattern->isActive ? pattern : nullptr);
	return true;
}

bool EditorWeaponManager::QueueTargetAssignment(
	int32_t emitterGameObjectId,
	const EditorComponent& assignment) {
	const int32_t lockOwnerGameObjectId = assignment.targetAssignmentMultiTargetLockGameObjectId >= 0
		? assignment.targetAssignmentMultiTargetLockGameObjectId
		: emitterGameObjectId;
	const EditorGameObject* lockOwner = editorScene_->FindGameObject(lockOwnerGameObjectId);
	const EditorComponent* multiTargetLock = lockOwner != nullptr
		? EditorComponentUtility::FindComponent(*lockOwner, EditorComponentType::MultiTargetLock)
		: nullptr;

	if (multiTargetLock == nullptr || !multiTargetLock->isActive) {
		return false;
	}

	const int32_t maximumTargets = (std::clamp)(assignment.targetAssignmentMaximumTargets, 1, 64);
	int32_t queuedCount = 0;

	for (size_t targetIndex = 0u;
		targetIndex < multiTargetLock->multiTargetLockTargetGameObjectIds.size() && queuedCount < maximumTargets;
		++targetIndex) {
		const bool isLocked = targetIndex < multiTargetLock->multiTargetLockCompletedValues.size() &&
			multiTargetLock->multiTargetLockCompletedValues[targetIndex];

		if (assignment.targetAssignmentLockedOnly && !isLocked) {
			continue;
		}

		PendingShot pendingShot{};
		pendingShot.weaponGameObjectId = emitterGameObjectId;
		pendingShot.targetGameObjectId = multiTargetLock->multiTargetLockTargetGameObjectIds[targetIndex];
		pendingShot.remainingDelay = static_cast<float>(queuedCount) *
			(std::max)(assignment.targetAssignmentInterval, 0.0f);
		pendingShot.completionType = 2;
		pendingShot.isProjectile = true;
		pendingShots_.push_back(pendingShot);
		queuedCount++;
	}

	if (queuedCount <= 0) {
		return false;
	}

	pendingShots_.back().isLast = true;
	const EditorGameObject* emitter = editorScene_->FindGameObject(emitterGameObjectId);
	const EditorComponent* projectile = emitter != nullptr
		? EditorComponentUtility::FindComponent(*emitter, EditorComponentType::ProjectileEmitter)
		: nullptr;
	const float sequenceDuration = static_cast<float>(queuedCount - 1) *
		(std::max)(assignment.targetAssignmentInterval, 0.0f);
	projectileCooldowns_[emitterGameObjectId] = sequenceDuration +
		(std::max)(projectile != nullptr ? projectile->projectileInterval : 0.0f, 0.0f);
	return true;
}

void EditorWeaponManager::QueueFirePattern(
	int32_t weaponGameObjectId,
	bool isProjectile,
	const EditorComponent* pattern) {
	const int32_t mode = pattern != nullptr ? (std::clamp)(pattern->weaponFirePatternMode, 0, 5) : 0;
	const int32_t shotCount = pattern != nullptr && mode != 0 && mode != 5
		? (std::clamp)(pattern->weaponFirePatternCount, 1, 64)
		: 1;
	const float interval = pattern != nullptr ? (std::max)(pattern->weaponFirePatternInterval, 0.0f) : 0.0f;
	const float spreadAngle = pattern != nullptr ? (std::max)(pattern->weaponFirePatternSpreadAngle, 0.0f) : 0.0f;
	float finalDelay = 0.0f;

	for (int32_t shotIndex = 0; shotIndex < shotCount; ++shotIndex) {
		PendingShot pendingShot{};
		pendingShot.weaponGameObjectId = weaponGameObjectId;
		pendingShot.isProjectile = isProjectile;
		pendingShot.completionType = pattern != nullptr ? 1 : 0;

		if (mode == 1 || mode == 4) {
			pendingShot.remainingDelay = static_cast<float>(shotIndex) * interval;
		}
		else if (mode == 5) {
			pendingShot.remainingDelay = (std::max)(pattern->weaponFirePatternChargeSeconds, 0.0f);
		}

		if (mode == 3 && shotCount > 1) {
			const float ratio = static_cast<float>(shotIndex) / static_cast<float>(shotCount - 1);
			pendingShot.patternYawDegrees = -spreadAngle * 0.5f + spreadAngle * ratio;
		}

		if (mode == 4 && pattern != nullptr && !pattern->weaponFirePatternSpawnPointGameObjectIds.empty()) {
			const size_t spawnIndex = static_cast<size_t>(shotIndex) %
				pattern->weaponFirePatternSpawnPointGameObjectIds.size();
			pendingShot.spawnPointGameObjectId = pattern->weaponFirePatternSpawnPointGameObjectIds[spawnIndex];
		}

		finalDelay = (std::max)(finalDelay, pendingShot.remainingDelay);
		pendingShots_.push_back(pendingShot);
	}

	if (!pendingShots_.empty()) {
		pendingShots_.back().isLast = true;
	}

	const EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
	const EditorComponent* baseWeapon = weapon != nullptr
		? EditorComponentUtility::FindComponent(
			*weapon,
			isProjectile ? EditorComponentType::ProjectileEmitter : EditorComponentType::HitscanWeapon)
		: nullptr;
	const float baseInterval = baseWeapon != nullptr
		? (isProjectile ? baseWeapon->projectileInterval : baseWeapon->hitscanInterval)
		: 0.0f;

	if (isProjectile) {
		projectileCooldowns_[weaponGameObjectId] = finalDelay + (std::max)(baseInterval, 0.0f);
	}
	else {
		hitscanCooldowns_[weaponGameObjectId] = finalDelay + (std::max)(baseInterval, 0.0f);
	}
}

void EditorWeaponManager::UpdateWeaponGroups(float deltaTime) {
	for (size_t shotIndex = 0u; shotIndex < pendingWeaponGroupShots_.size();) {
		PendingWeaponGroupShot& pendingShot = pendingWeaponGroupShots_[shotIndex];
		pendingShot.remainingDelay -= deltaTime;

		if (pendingShot.remainingDelay > 0.0f) {
			shotIndex++;
			continue;
		}

		FireWeaponObject(pendingShot.weaponGameObjectId);

		if (pendingShot.isLast) {
			EditorGameObject* groupGameObject = editorScene_->FindGameObject(pendingShot.groupGameObjectId);
			EditorComponent* group = groupGameObject != nullptr
				? EditorComponentUtility::FindComponent(*groupGameObject, EditorComponentType::WeaponGroup)
				: nullptr;

			if (group != nullptr) {
				group->weaponGroupIsFiring = false;
				QueueAction(
					pendingShot.groupGameObjectId,
					group->weaponGroupActionTargetGameObjectId,
					group->weaponGroupCompletedActionName,
					1.0f);
			}
		}

		pendingWeaponGroupShots_.erase(
			pendingWeaponGroupShots_.begin() + static_cast<std::ptrdiff_t>(shotIndex));
	}
}

bool EditorWeaponManager::IsWeaponReady(int32_t weaponGameObjectId) const {
	const EditorGameObject* weapon = editorScene_ != nullptr
		? editorScene_->FindGameObject(weaponGameObjectId)
		: nullptr;

	if (weapon == nullptr || !weapon->isActive) {
		return false;
	}

	const EditorComponent* hitscan = EditorComponentUtility::FindComponent(
		*weapon,
		EditorComponentType::HitscanWeapon);

	if (hitscan != nullptr && hitscan->isActive) {
		const auto cooldown = hitscanCooldowns_.find(weaponGameObjectId);
		return cooldown == hitscanCooldowns_.end() || cooldown->second <= 0.0f;
	}

	const EditorComponent* projectile = EditorComponentUtility::FindComponent(
		*weapon,
		EditorComponentType::ProjectileEmitter);

	if (projectile != nullptr && projectile->isActive) {
		const auto cooldown = projectileCooldowns_.find(weaponGameObjectId);
		return cooldown == projectileCooldowns_.end() || cooldown->second <= 0.0f;
	}

	return false;
}

bool EditorWeaponManager::FireWeaponObject(int32_t weaponGameObjectId) {
	EditorGameObject* weapon = editorScene_ != nullptr
		? editorScene_->FindGameObject(weaponGameObjectId)
		: nullptr;

	if (weapon == nullptr) {
		return false;
	}

	if (EditorComponentUtility::FindComponent(*weapon, EditorComponentType::ProjectileEmitter) != nullptr) {
		return FireProjectile(weaponGameObjectId);
	}

	if (EditorComponentUtility::FindComponent(*weapon, EditorComponentType::HitscanWeapon) != nullptr) {
		return FireHitscan(weaponGameObjectId);
	}

	return false;
}

void EditorWeaponManager::UpdatePendingShots(float deltaTime) {
	for (size_t shotIndex = 0u; shotIndex < pendingShots_.size();) {
		PendingShot& pendingShot = pendingShots_[shotIndex];
		pendingShot.remainingDelay -= deltaTime;

		if (pendingShot.remainingDelay > 0.0f) {
			shotIndex++;
			continue;
		}

		if (pendingShot.isProjectile) {
			ExecuteProjectileShot(
				pendingShot.weaponGameObjectId,
				pendingShot.targetGameObjectId,
				pendingShot.spawnPointGameObjectId,
				pendingShot.patternYawDegrees);
		}
		else {
			ExecuteHitscanShot(pendingShot.weaponGameObjectId, pendingShot.patternYawDegrees);
		}

		if (pendingShot.isLast) {
			QueueCompletionAction(pendingShot.weaponGameObjectId, pendingShot.completionType);
		}

		pendingShots_.erase(pendingShots_.begin() + static_cast<std::ptrdiff_t>(shotIndex));
	}
}

bool EditorWeaponManager::ExecuteHitscanShot(int32_t weaponGameObjectId, float patternYawDegrees) {
	EditorGameObject* weapon = editorScene_ != nullptr ? editorScene_->FindGameObject(weaponGameObjectId) : nullptr;
	const EditorComponent* hitscan = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::HitscanWeapon)
		: nullptr;

	if (hitscan == nullptr || !hitscan->isActive || physicsManager_ == nullptr) {
		return false;
	}

	EditorTargetingManager::AimRay aimRay{};
	if (!BuildAimRay(hitscan->hitscanAimGameObjectId, aimRay)) {
		return false;
	}

	aimRay.direction = ApplyAccuracy(weaponGameObjectId, aimRay.direction, patternYawDegrees);
	int32_t instigatorGameObjectId = weaponGameObjectId;
	float armingDistance = 0.0f;
	std::vector<int32_t> ignoredGameObjectIds;
	BuildAttackIgnoredGameObjects(
		weaponGameObjectId,
		-1,
		instigatorGameObjectId,
		armingDistance,
		ignoredGameObjectIds);
	(void)armingDistance;
	EditorJoltPhysicsManager::PhysicsHit physicsHit{};
	const bool hasPhysicsHit = CastAttackPhysics(
		aimRay.origin,
		0.0f,
		aimRay.direction,
		(std::max)(hitscan->hitscanRange, 0.01f),
		ignoredGameObjectIds,
		physicsHit);
	EditorOceanSegmentHit oceanHit{};
	const uint64_t oceanQueryKey =
		(static_cast<uint64_t>(static_cast<uint32_t>(weaponGameObjectId)) << 32u) |
		0x48495453u;
	const bool hasOceanHit = hitscan->hitscanOceanCollision && RaycastEditorOceanSurface(
		*editorScene_,
		-1,
		aimRay.origin,
		aimRay.direction,
		(std::max)(hitscan->hitscanRange, 0.01f),
		0.0f,
		oceanQueryKey,
		GetEditorOceanElapsedTime(),
		16,
		5,
		oceanHit);
	const bool usesOceanHit = hasOceanHit &&
		(!hasPhysicsHit || oceanHit.distance < physicsHit.distance);
	const bool hasHit = hasPhysicsHit || hasOceanHit;
	const int32_t hitGameObjectId = usesOceanHit
		? oceanHit.oceanGameObjectId
		: (hasPhysicsHit ? physicsHit.gameObjectId : -1);
	ApplyRecoil(weaponGameObjectId);
	QueueAction(
		weaponGameObjectId,
		hitscan->hitscanActionTargetGameObjectId,
		hitscan->hitscanFiredActionName,
		hasHit ? static_cast<float>(hitGameObjectId) : -1.0f);

	if (!hasHit) {
		QueueAction(weaponGameObjectId, hitscan->hitscanActionTargetGameObjectId, hitscan->hitscanMissActionName, -1.0f);
		return true;
	}

	const int32_t damageTagId = EditorDamageManager::HashDamageTag(hitscan->hitscanDamageTag);

	if (usesOceanHit) {
		ExecuteImpactResponse(
			weaponGameObjectId,
			oceanHit.oceanGameObjectId,
			oceanHit.position,
			oceanHit.normal,
			damageTagId);
		QueueAction(
			weaponGameObjectId,
			hitscan->hitscanActionTargetGameObjectId,
			hitscan->hitscanHitActionName,
			static_cast<float>(oceanHit.oceanGameObjectId));
		return true;
	}

	if (damageManager_ != nullptr) {
		EditorScriptDamageContext damageContext{};
		damageContext.targetGameObjectId = physicsHit.gameObjectId;
		damageContext.sourceGameObjectId = weaponGameObjectId;
		damageContext.instigatorGameObjectId = instigatorGameObjectId;
		damageContext.hitPosition = {physicsHit.point.x, physicsHit.point.y, physicsHit.point.z};
		damageContext.hitNormal = {physicsHit.normal.x, physicsHit.normal.y, physicsHit.normal.z};
		damageContext.baseDamage = (std::max)(hitscan->hitscanDamage, 0.0f);
		damageContext.userTag = damageTagId;
		damageManager_->ApplyDamage(damageContext);
	}

	ExecuteImpactResponse(weaponGameObjectId, physicsHit.gameObjectId, physicsHit.point, physicsHit.normal, damageTagId);
	QueueAction(weaponGameObjectId, hitscan->hitscanActionTargetGameObjectId, hitscan->hitscanHitActionName, static_cast<float>(physicsHit.gameObjectId));
	return true;
}

int32_t EditorWeaponManager::ExecuteProjectileShot(
	int32_t emitterGameObjectId,
	int32_t targetGameObjectId,
	int32_t spawnPointGameObjectId,
	float patternYawDegrees) {
	EditorGameObject* emitter = editorScene_ != nullptr ? editorScene_->FindGameObject(emitterGameObjectId) : nullptr;
	const EditorComponent* projectile = emitter != nullptr
		? EditorComponentUtility::FindComponent(*emitter, EditorComponentType::ProjectileEmitter)
		: nullptr;

	if (projectile == nullptr || !projectile->isActive || objectPoolManager_ == nullptr) {
		return -1;
	}

	const int32_t aimMode = (std::clamp)(projectile->projectileAimMode, 0, 3);
	EditorTargetingManager::AimRay aimRay{};

	if (aimMode == 0 && !BuildAimRay(projectile->projectileAimGameObjectId, aimRay)) {
		return -1;
	}

	const int32_t resolvedSpawnPointId = spawnPointGameObjectId >= 0
		? spawnPointGameObjectId
		: projectile->projectileSpawnPointGameObjectId;
	const EditorGameObject* spawnPoint = resolvedSpawnPointId >= 0
		? editorScene_->FindGameObject(resolvedSpawnPointId)
		: emitter;

	if (spawnPoint == nullptr) {
		return -1;
	}

	Vector3 spawnScale = spawnPoint->scale;
	Vector3 spawnRotation = spawnPoint->rotate;
	Vector3 spawnPosition = spawnPoint->translate;
	editorScene_->GetWorldTransform(spawnPoint->id, spawnScale, spawnRotation, spawnPosition);
	(void)spawnScale;
	Vector3 direction = aimMode == 0
		? BuildProjectileDirection(spawnPosition, aimRay)
		: ResolveForwardDirection(spawnRotation);
	Vector3 sourceVelocity{};
	Vector3 projectileGravity{};
	float projectileDrag = 0.0f;
	float muzzleSpeed = (std::max)(projectile->projectileSpeed, 0.0f);
	const EditorComponent* ballisticPrediction = nullptr;

	if (aimMode == 3) {
		const int32_t predictionGameObjectId = projectile->projectileBallisticPredictionGameObjectId >= 0
			? projectile->projectileBallisticPredictionGameObjectId
			: emitterGameObjectId;
		const EditorGameObject* predictionGameObject = editorScene_->FindGameObject(predictionGameObjectId);
		ballisticPrediction = predictionGameObject != nullptr
			? EditorComponentUtility::FindComponent(
				*predictionGameObject,
				EditorComponentType::BallisticPrediction)
			: nullptr;

		if (ballisticPrediction == nullptr || !ballisticPrediction->isActive ||
			!ballisticPrediction->ballisticValid) {
			return -1;
		}

		direction = ballisticPrediction->ballisticLaunchDirection;
		sourceVelocity = ballisticPrediction->ballisticSourceVelocity;
		projectileGravity = ballisticPrediction->ballisticGravity;
		projectileDrag = (std::max)(ballisticPrediction->ballisticDrag, 0.0f);
		muzzleSpeed = (std::max)(ballisticPrediction->ballisticInitialSpeed, 0.0f);
	}
	else if (projectile->projectileInheritSourceVelocity) {
		const int32_t sourceVelocityGameObjectId = projectile->projectileSourceVelocityGameObjectId >= 0
			? projectile->projectileSourceVelocityGameObjectId
			: emitterGameObjectId;
		sourceVelocity = EditorComponentUtility::ResolveInheritedRigidBodyVelocity(
			*editorScene_,
			sourceVelocityGameObjectId,
			projectile->projectileUseParentRigidBody,
			spawnPosition,
			projectile->projectileLinearVelocityInheritance,
			projectile->projectileAngularVelocityInheritance);
	}

	if (aimMode == 2 && targetGameObjectId < 0 && targetingManager_ != nullptr) {
		const int32_t selectorGameObjectId = projectile->projectileAimGameObjectId >= 0
			? projectile->projectileAimGameObjectId
			: emitterGameObjectId;
		targetingManager_->GetCurrentTarget(selectorGameObjectId, targetGameObjectId);
	}

	const EditorGameObject* target = editorScene_->FindGameObject(targetGameObjectId);

	if (aimMode != 3 && target != nullptr && target->isActive) {
		direction = NormalizeVector3(SubtractVector3(ResolveWorldPosition(*editorScene_, *target), spawnPosition));
	}

	direction = ApplyAccuracy(emitterGameObjectId, direction, patternYawDegrees);
	const Vector3 safePosition = AddVector3(
		spawnPosition,
		MultiplyVector3((std::max)(projectile->projectileRadius, 0.0f) + 0.05f, direction));
	const int32_t projectileGameObjectId = objectPoolManager_->Spawn(
		projectile->projectilePoolGameObjectId,
		safePosition,
		spawnRotation);

	if (projectileGameObjectId < 0) {
		if (emitterGameObjectId >= 0) {
			const EditorGameObject* failedEmitter = editorScene_->FindGameObject(emitterGameObjectId);
			if (failedEmitter != nullptr && failedEmitter->name == "Weapon 20mm") {
				OutputDebugStringA("[Weapon20mm] ObjectPool::Spawn failed\n");
			}
		}
		return -1;
	}

	if (emitterGameObjectId >= 0) {
		const EditorGameObject* spawnedEmitter = editorScene_->FindGameObject(emitterGameObjectId);
		if (spawnedEmitter != nullptr && spawnedEmitter->name == "Weapon 20mm") {
			char debugBuffer[384];
			std::snprintf(
				debugBuffer,
				sizeof(debugBuffer),
				"[Weapon20mm] Spawned id=%d position=(%.2f,%.2f,%.2f) safe=(%.2f,%.2f,%.2f) direction=(%.2f,%.2f,%.2f)\n",
				projectileGameObjectId,
				spawnPosition.x, spawnPosition.y, spawnPosition.z,
				safePosition.x, safePosition.y, safePosition.z,
				direction.x, direction.y, direction.z);
			OutputDebugStringA(debugBuffer);
		}
	}

	// Spawn() がプール拡張時に新規 GameObject をシーンへ追加すると配列が再確保され、
	// 発射前に取得した emitter / projectile ポインタは無効になる。発射後に再取得する。
	emitter = editorScene_ != nullptr ? editorScene_->FindGameObject(emitterGameObjectId) : nullptr;
	projectile = emitter != nullptr
		? EditorComponentUtility::FindComponent(*emitter, EditorComponentType::ProjectileEmitter)
		: nullptr;

	if (projectile == nullptr) {
		return -1;
	}

	EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileGameObjectId);
	if (projectileGameObject != nullptr && emitterGameObjectId >= 0) {
		const EditorGameObject* spawnedEmitter = editorScene_->FindGameObject(emitterGameObjectId);
		if (spawnedEmitter != nullptr && spawnedEmitter->name == "Weapon 20mm") {
			const EditorComponent* modelRenderer = EditorComponentUtility::FindComponent(
				*projectileGameObject,
				EditorComponentType::ModelRenderer);
			char debugBuffer[512];
			std::snprintf(
				debugBuffer,
				sizeof(debugBuffer),
				"[Weapon20mm] Visual id=%d active=%d scale=(%.3f,%.3f,%.3f) asset=%s position=(%.2f,%.2f,%.2f)\n",
				projectileGameObject->id,
				projectileGameObject->isActive ? 1 : 0,
				projectileGameObject->scale.x,
				projectileGameObject->scale.y,
				projectileGameObject->scale.z,
				modelRenderer != nullptr ? modelRenderer->assetPath.c_str() : "<no ModelRenderer>",
				projectileGameObject->translate.x,
				projectileGameObject->translate.y,
				projectileGameObject->translate.z);
			OutputDebugStringA(debugBuffer);
		}
	}

	if (projectileGameObject != nullptr && targetGameObjectId >= 0) {
		EditorComponent* steering = EditorComponentUtility::FindComponent(*projectileGameObject, EditorComponentType::TargetSteering);
		EditorComponent* detonator = EditorComponentUtility::FindComponent(*projectileGameObject, EditorComponentType::ProjectileDetonator);

		if (steering != nullptr) {
			steering->targetSteeringTargetGameObjectId = targetGameObjectId;
		}

		if (detonator != nullptr) {
			detonator->projectileDetonatorTargetGameObjectId = targetGameObjectId;
		}
	}

	ActiveProjectile activeProjectile{};
	activeProjectile.gameObjectId = projectileGameObjectId;
	activeProjectile.ownerGameObjectId = emitterGameObjectId;
	BuildAttackIgnoredGameObjects(
		emitterGameObjectId,
		projectileGameObjectId,
		activeProjectile.instigatorGameObjectId,
		activeProjectile.armingDistance,
		activeProjectile.ignoredGameObjectIds);
	activeProjectile.actionTargetGameObjectId = projectile->projectileActionTargetGameObjectId;
	activeProjectile.hitActionName = projectile->projectileHitActionName;
	activeProjectile.velocity = AddVector3(MultiplyVector3(muzzleSpeed, direction), sourceVelocity);
	activeProjectile.speed = GetVectorLength(activeProjectile.velocity);
	activeProjectile.direction = NormalizeVector3(activeProjectile.velocity);
	if (emitterGameObjectId >= 0) {
		const EditorGameObject* velocityEmitter = editorScene_->FindGameObject(emitterGameObjectId);
		if (velocityEmitter != nullptr && velocityEmitter->name == "Weapon 20mm") {
			char debugBuffer[256];
			std::snprintf(
				debugBuffer,
				sizeof(debugBuffer),
				"[Weapon20mm] velocity=(%.2f,%.2f,%.2f) magnitude=%.2f\n",
				activeProjectile.velocity.x,
				activeProjectile.velocity.y,
				activeProjectile.velocity.z,
				activeProjectile.speed);
			OutputDebugStringA(debugBuffer);
		}
	}
	activeProjectile.gravity = projectileGravity;
	activeProjectile.drag = projectileDrag;
	activeProjectile.radius = (std::max)(projectile->projectileRadius, 0.0f);
	activeProjectile.damage = (std::max)(projectile->projectileDamage, 0.0f);
	activeProjectile.damageTagId = EditorDamageManager::HashDamageTag(projectile->projectileDamageTag);
	activeProjectile.remainingLifetime = (std::max)(projectile->projectileLifetime, 0.01f);
	activeProjectile.oceanCollision = projectile->projectileOceanCollision;
	const EditorComponent* impactPhysics = projectileGameObject != nullptr
		? EditorComponentUtility::FindComponent(
			*projectileGameObject,
			EditorComponentType::ProjectileImpactPhysics)
		: nullptr;

	if (impactPhysics == nullptr) {
		impactPhysics = EditorComponentUtility::FindComponent(
			*emitter,
			EditorComponentType::ProjectileImpactPhysics);
	}

	if (impactPhysics != nullptr && impactPhysics->isActive) {
		activeProjectile.penetrationEnergy = (std::max)(impactPhysics->projectileImpactPenetrationEnergy, 0.0f);
		activeProjectile.penetrationLoss = (std::max)(impactPhysics->projectileImpactPenetrationLoss, 0.0f);
		activeProjectile.maximumPenetrations = (std::max)(impactPhysics->projectileImpactMaximumPenetrations, 0);
		activeProjectile.ricochetAngleDegrees = (std::clamp)(impactPhysics->projectileImpactRicochetAngleDegrees, 0.0f, 90.0f);
		activeProjectile.energyRetention = (std::clamp)(impactPhysics->projectileImpactEnergyRetention, 0.0f, 1.0f);
		activeProjectile.damageRetention = (std::clamp)(impactPhysics->projectileImpactDamageRetention, 0.0f, 1.0f);
		activeProjectile.maximumRicochets = (std::max)(impactPhysics->projectileImpactMaximumRicochets, 0);
		activeProjectile.surfaceModifiers = impactPhysics->projectileImpactSurfaceModifiers;
	}
	activeProjectiles_.push_back(activeProjectile);
	ApplyRecoil(emitterGameObjectId);
	QueueAction(emitterGameObjectId, projectile->projectileActionTargetGameObjectId, projectile->projectileFiredActionName, static_cast<float>(projectileGameObjectId));
	return projectileGameObjectId;
}

Vector3 EditorWeaponManager::ApplyAccuracy(
	int32_t weaponGameObjectId,
	const Vector3& direction,
	float patternYawDegrees) {
	EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
	EditorComponent* accuracy = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::WeaponAccuracy)
		: nullptr;
	float spreadDegrees = 0.0f;

	if (accuracy != nullptr && accuracy->isActive) {
		spreadDegrees = (std::clamp)(
			accuracy->weaponAccuracyBaseSpread + accuracy->weaponAccuracyCurrentSpread,
			0.0f,
			(std::max)(accuracy->weaponAccuracyMaximumSpread, 0.0f));
		const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(*weapon, EditorComponentType::RigidBody);

		if (rigidBody != nullptr) {
			spreadDegrees += GetVectorLength(rigidBody->velocity) * (std::max)(accuracy->weaponAccuracyMovementSpread, 0.0f);
		}
	}

	auto nextRandom = [this]() {
		accuracyRandomState_ = accuracyRandomState_ * 1664525u + 1013904223u;
		return static_cast<float>(accuracyRandomState_ & 0x00FFFFFFu) / 16777215.0f;
	};
	const float randomAngle = nextRandom() * 6.28318530717958647692f;
	float randomRadius = nextRandom();

	if (accuracy != nullptr && accuracy->weaponAccuracyDistribution == 1) {
		randomRadius = std::sqrt(randomRadius);
	}
	else if (accuracy != nullptr && accuracy->weaponAccuracyDistribution == 2) {
		randomRadius *= randomRadius;
	}

	const Vector3 forward = NormalizeVector3(direction);
	const Vector3 referenceUp = std::abs(forward.y) < 0.98f
		? Vector3{0.0f, 1.0f, 0.0f}
		: Vector3{1.0f, 0.0f, 0.0f};
	const Vector3 right = NormalizeVector3(CrossVector3(referenceUp, forward));
	const Vector3 up = NormalizeVector3(CrossVector3(forward, right));
	const float yawTangent = std::tan(patternYawDegrees * kDegreesToRadians);
	const float spreadTangent = std::tan(spreadDegrees * randomRadius * kDegreesToRadians);
	Vector3 result = AddVector3(forward, MultiplyVector3(yawTangent, right));
	result = AddVector3(result, MultiplyVector3(std::cos(randomAngle) * spreadTangent, right));
	result = AddVector3(result, MultiplyVector3(std::sin(randomAngle) * spreadTangent, up));

	if (accuracy != nullptr && accuracy->isActive) {
		accuracy->weaponAccuracyCurrentSpread = (std::min)(
			accuracy->weaponAccuracyCurrentSpread + (std::max)(accuracy->weaponAccuracySpreadPerShot, 0.0f),
			(std::max)(accuracy->weaponAccuracyMaximumSpread - accuracy->weaponAccuracyBaseSpread, 0.0f));
	}

	return NormalizeVector3(result);
}

void EditorWeaponManager::RecoverAccuracy(float deltaTime) {
	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* accuracy = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::WeaponAccuracy);

		if (accuracy == nullptr || !accuracy->isActive) {
			continue;
		}

		accuracy->weaponAccuracyCurrentSpread = (std::max)(
			accuracy->weaponAccuracyCurrentSpread - (std::max)(accuracy->weaponAccuracyRecoveryPerSecond, 0.0f) * deltaTime,
			0.0f);
	}
}

void EditorWeaponManager::ApplyRecoil(int32_t weaponGameObjectId) {
	EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
	const EditorComponent* recoil = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::WeaponRecoil)
		: nullptr;

	if (recoil == nullptr || !recoil->isActive) {
		return;
	}

	if (physicsManager_ != nullptr) {
		physicsManager_->AddImpulse(weaponGameObjectId, recoil->weaponRecoilBodyImpulse);
		physicsManager_->AddTorque(weaponGameObjectId, recoil->weaponRecoilBodyTorque);
	}

	if (recoil->weaponRecoilVisualGameObjectId >= 0) {
		EditorGameObject* visual = editorScene_->FindGameObject(recoil->weaponRecoilVisualGameObjectId);

		if (visual != nullptr) {
			visual->translate = AddVector3(visual->translate, recoil->weaponRecoilVisualPosition);
			visual->rotate = AddVector3(visual->rotate, recoil->weaponRecoilVisualRotation);
			VisualRecoilRuntime& runtime = visualRecoilRuntimes_[weaponGameObjectId];
			runtime.gameObjectId = visual->id;
			runtime.positionOffset = AddVector3(runtime.positionOffset, recoil->weaponRecoilVisualPosition);
			runtime.rotationOffset = AddVector3(runtime.rotationOffset, recoil->weaponRecoilVisualRotation);
		}
	}

	if (cameraEffectManager_ != nullptr && recoil->weaponRecoilCameraShakeGameObjectId >= 0) {
		cameraEffectManager_->PlayShake(recoil->weaponRecoilCameraShakeGameObjectId);
	}

	QueueAction(weaponGameObjectId, recoil->weaponRecoilActionTargetGameObjectId, recoil->weaponRecoilActionName, 1.0f);
}

void EditorWeaponManager::UpdateVisualRecoil(float deltaTime) {
	for (auto runtimeIterator = visualRecoilRuntimes_.begin(); runtimeIterator != visualRecoilRuntimes_.end();) {
		const int32_t weaponGameObjectId = runtimeIterator->first;
		VisualRecoilRuntime& runtime = runtimeIterator->second;
		const EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
		const EditorComponent* recoil = weapon != nullptr
			? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::WeaponRecoil)
			: nullptr;
		EditorGameObject* visual = editorScene_->FindGameObject(runtime.gameObjectId);

		if (recoil == nullptr || visual == nullptr) {
			runtimeIterator = visualRecoilRuntimes_.erase(runtimeIterator);
			continue;
		}

		const float recoveryRatio = (std::clamp)(recoil->weaponRecoilRecoveryPerSecond * deltaTime, 0.0f, 1.0f);
		const Vector3 positionRecovery = MultiplyVector3(recoveryRatio, runtime.positionOffset);
		const Vector3 rotationRecovery = MultiplyVector3(recoveryRatio, runtime.rotationOffset);
		visual->translate = SubtractVector3(visual->translate, positionRecovery);
		visual->rotate = SubtractVector3(visual->rotate, rotationRecovery);
		runtime.positionOffset = SubtractVector3(runtime.positionOffset, positionRecovery);
		runtime.rotationOffset = SubtractVector3(runtime.rotationOffset, rotationRecovery);

		if (GetVectorLength(runtime.positionOffset) <= kWeaponEpsilon &&
			GetVectorLength(runtime.rotationOffset) <= kWeaponEpsilon) {
			runtimeIterator = visualRecoilRuntimes_.erase(runtimeIterator);
			continue;
		}

		++runtimeIterator;
	}
}

void EditorWeaponManager::ExecuteImpactResponse(
	int32_t weaponGameObjectId,
	int32_t hitGameObjectId,
	const Vector3& hitPosition,
	const Vector3& hitNormal,
	int32_t damageTagId) {
	(void)hitNormal;
	const EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
	const EditorComponent* responder = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, EditorComponentType::ImpactResponder)
		: nullptr;

	if (responder == nullptr || !responder->isActive) {
		return;
	}

	const std::string surfaceTag = ResolveSurfaceTag(hitGameObjectId);

	for (const EditorImpactResponseEntry& response : responder->impactResponseEntries) {
		const bool matchesDamage = response.damageTag.empty() ||
			EditorDamageManager::HashDamageTag(response.damageTag) == damageTagId;
		const bool matchesSurface = response.surfaceTag.empty() || response.surfaceTag == surfaceTag;

		if (!matchesDamage || !matchesSurface) {
			continue;
		}

		const EditorGameObject* hitObject = editorScene_->FindGameObject(hitGameObjectId);
		const Vector3 localOffset = hitObject != nullptr
			? SubtractVector3(hitPosition, ResolveWorldPosition(*editorScene_, *hitObject))
			: Vector3{};

		if (effectManager_ != nullptr && !response.effectAssetPath.empty()) {
			effectManager_->PlayEffectAt(hitGameObjectId, response.effectAssetPath, localOffset);
		}

		if (audioManager_ != nullptr && response.audioSourceGameObjectId >= 0) {
			EditorGameObject* audioObject = editorScene_->FindGameObject(response.audioSourceGameObjectId);

			if (audioObject != nullptr) {
				audioObject->translate = hitPosition;
				audioManager_->Play(audioObject->id);
			}
		}

		if (response.decalGameObjectId >= 0) {
			EditorGameObject* decalObject = editorScene_->FindGameObject(response.decalGameObjectId);

			if (decalObject != nullptr) {
				decalObject->translate = hitPosition;
				decalObject->isActive = true;
			}
		}

		if (cameraEffectManager_ != nullptr && response.cameraShakeGameObjectId >= 0) {
			cameraEffectManager_->PlayShake(response.cameraShakeGameObjectId);
		}

		if (scriptManager_ != nullptr && !response.actionName.empty()) {
			EditorScriptActionPayload payload{};
			payload.type = EditorScriptActionPayloadTypeGameObject;
			payload.gameObjectId = hitGameObjectId;
			scriptManager_->QueueActionPayload(
				response.actionTargetGameObjectId >= 0 ? response.actionTargetGameObjectId : weaponGameObjectId,
				response.actionName,
				payload);
		}

		break;
	}
}

std::string EditorWeaponManager::ResolveSurfaceTag(int32_t hitGameObjectId) const {
	int32_t currentGameObjectId = hitGameObjectId;

	while (currentGameObjectId >= 0) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(currentGameObjectId);

		if (gameObject == nullptr) {
			break;
		}

		const EditorComponent* surface = EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::SurfaceType);

		if (surface != nullptr && surface->isActive && !surface->surfaceTypeTag.empty()) {
			return surface->surfaceTypeTag;
		}

		const EditorComponent* ocean = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::Ocean);

		if (ocean != nullptr && ocean->isActive) {
			return "Water";
		}

		currentGameObjectId = gameObject->parentId;
	}

	return "Default";
}

int32_t EditorWeaponManager::ResolveTeamId(int32_t gameObjectId) const {
	int32_t currentGameObjectId = gameObjectId;

	while (currentGameObjectId >= 0) {
		const EditorGameObject* gameObject = editorScene_ != nullptr
			? editorScene_->FindGameObject(currentGameObjectId)
			: nullptr;

		if (gameObject == nullptr) {
			break;
		}

		const EditorComponent* team = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::Team);

		if (team != nullptr && team->isActive) {
			return team->teamId;
		}

		currentGameObjectId = gameObject->parentId;
	}

	return -2;
}

bool EditorWeaponManager::IsInHierarchy(
	int32_t gameObjectId,
	int32_t hierarchyRootGameObjectId) const {
	int32_t currentGameObjectId = gameObjectId;

	while (currentGameObjectId >= 0) {
		if (currentGameObjectId == hierarchyRootGameObjectId) {
			return true;
		}

		const EditorGameObject* gameObject = editorScene_ != nullptr
			? editorScene_->FindGameObject(currentGameObjectId)
			: nullptr;
		currentGameObjectId = gameObject != nullptr ? gameObject->parentId : -1;
	}

	return false;
}

void EditorWeaponManager::BuildAttackIgnoredGameObjects(
	int32_t weaponGameObjectId,
	int32_t projectileGameObjectId,
	int32_t& instigatorGameObjectId,
	float& armingDistance,
	std::vector<int32_t>& ignoredGameObjectIds) const {
	instigatorGameObjectId = weaponGameObjectId;
	armingDistance = 0.0f;
	ignoredGameObjectIds.clear();
	const EditorComponent* filter = nullptr;
	int32_t filterOwnerGameObjectId = -1;
	int32_t currentGameObjectId = weaponGameObjectId;

	while (currentGameObjectId >= 0 && filter == nullptr) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(currentGameObjectId);

		if (gameObject == nullptr) {
			break;
		}

		filter = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::AttackCollisionFilter);
		filterOwnerGameObjectId = filter != nullptr ? currentGameObjectId : -1;
		currentGameObjectId = gameObject->parentId;
	}

	if (filter == nullptr && projectileGameObjectId >= 0) {
		const EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileGameObjectId);
		filter = projectileGameObject != nullptr
			? EditorComponentUtility::FindComponent(
				*projectileGameObject,
				EditorComponentType::AttackCollisionFilter)
			: nullptr;
		filterOwnerGameObjectId = filter != nullptr ? projectileGameObjectId : -1;
	}

	if (projectileGameObjectId >= 0) {
		ignoredGameObjectIds.push_back(projectileGameObjectId);
	}

	if (filter == nullptr || !filter->isActive) {
		return;
	}

	instigatorGameObjectId = filter->attackFilterInstigatorGameObjectId >= 0
		? filter->attackFilterInstigatorGameObjectId
		: (filterOwnerGameObjectId == projectileGameObjectId
			? weaponGameObjectId
			: filterOwnerGameObjectId);
	armingDistance = (std::max)(filter->attackFilterArmingDistance, 0.0f);
	ignoredGameObjectIds.insert(
		ignoredGameObjectIds.end(),
		filter->attackFilterIgnoredGameObjectIds.begin(),
		filter->attackFilterIgnoredGameObjectIds.end());

	if (filter->attackFilterIgnoreInstigator && instigatorGameObjectId >= 0) {
		ignoredGameObjectIds.push_back(instigatorGameObjectId);
	}

	if (filter->attackFilterIgnoreInstigatorHierarchy && instigatorGameObjectId >= 0) {
		for (const EditorGameObject& candidate : editorScene_->GetGameObjects()) {
			if (IsInHierarchy(candidate.id, instigatorGameObjectId)) {
				ignoredGameObjectIds.push_back(candidate.id);
			}
		}
	}

	const int32_t instigatorTeamId = ResolveTeamId(instigatorGameObjectId);

	for (const EditorGameObject& candidate : editorScene_->GetGameObjects()) {
		const int32_t candidateTeamId = ResolveTeamId(candidate.id);
		const bool hasCandidateTeam = candidateTeamId >= -1;
		bool ignoresCandidate = filter->attackFilterIgnoreNeutral &&
			hasCandidateTeam && candidateTeamId < 0;

		if (instigatorTeamId >= -1 && hasCandidateTeam) {
			if (filter->attackFilterTeamRule == 1 && candidateTeamId == instigatorTeamId) {
				ignoresCandidate = true;
			}
			else if (filter->attackFilterTeamRule == 2 && candidateTeamId != instigatorTeamId) {
				ignoresCandidate = true;
			}
		}

		if (ignoresCandidate) {
			ignoredGameObjectIds.push_back(candidate.id);
		}
	}

	std::sort(ignoredGameObjectIds.begin(), ignoredGameObjectIds.end());
	ignoredGameObjectIds.erase(
		std::unique(ignoredGameObjectIds.begin(), ignoredGameObjectIds.end()),
		ignoredGameObjectIds.end());
}

bool EditorWeaponManager::CastAttackPhysics(
	const Vector3& origin,
	float radius,
	const Vector3& direction,
	float distance,
	const std::vector<int32_t>& ignoredGameObjectIds,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	if (physicsManager_ == nullptr || distance <= kWeaponEpsilon) {
		return false;
	}

	if (radius > 0.0f) {
		return ignoredGameObjectIds.empty()
			? physicsManager_->SphereCast(origin, radius, direction, distance, hit)
			: physicsManager_->SphereCastIgnoringGameObjects(
				origin,
				radius,
				direction,
				distance,
				ignoredGameObjectIds,
				hit);
	}

	return ignoredGameObjectIds.empty()
		? physicsManager_->Raycast(origin, direction, distance, hit)
		: physicsManager_->RaycastIgnoringGameObjects(
			origin,
			direction,
			distance,
			ignoredGameObjectIds,
			hit);
}

EditorComponent* EditorWeaponManager::FindFireLineComponent(int32_t weaponGameObjectId) const {
	if (editorScene_ == nullptr) {
		return nullptr;
	}

	EditorGameObject* currentGameObject = editorScene_->FindGameObject(weaponGameObjectId);

	while (currentGameObject != nullptr) {
		EditorComponent* fireLine = EditorComponentUtility::FindComponent(
			*currentGameObject,
			EditorComponentType::FireLineCheck);

		if (fireLine != nullptr && fireLine->isActive) {
			return fireLine;
		}

		currentGameObject = currentGameObject->parentId >= 0
			? editorScene_->FindGameObject(currentGameObject->parentId)
			: nullptr;
	}

	return nullptr;
}

bool EditorWeaponManager::EvaluateFireLine(
	int32_t weaponGameObjectId,
	bool writesRuntimeState) const {
	EditorComponent* fireLine = FindFireLineComponent(weaponGameObjectId);

	if (fireLine == nullptr || physicsManager_ == nullptr) {
		return true;
	}

	const int32_t muzzleGameObjectId = fireLine->fireLineMuzzleGameObjectId >= 0
		? fireLine->fireLineMuzzleGameObjectId
		: weaponGameObjectId;
	const int32_t directionGameObjectId = fireLine->fireLineDirectionGameObjectId >= 0
		? fireLine->fireLineDirectionGameObjectId
		: muzzleGameObjectId;
	const EditorGameObject* muzzleGameObject = editorScene_->FindGameObject(muzzleGameObjectId);
	const EditorGameObject* directionGameObject = editorScene_->FindGameObject(directionGameObjectId);

	if (muzzleGameObject == nullptr || directionGameObject == nullptr) {
		if (writesRuntimeState) {
			fireLine->fireLineClear = false;
			fireLine->fireLineBlockingGameObjectId = -1;
			fireLine->fireLineBlockingDistance = 0.0f;
		}

		return false;
	}

	Vector3 muzzleScale = muzzleGameObject->scale;
	Vector3 muzzleRotation = muzzleGameObject->rotate;
	Vector3 muzzlePosition = muzzleGameObject->translate;
	editorScene_->GetWorldTransform(
		muzzleGameObjectId,
		muzzleScale,
		muzzleRotation,
		muzzlePosition);
	Vector3 directionScale = directionGameObject->scale;
	Vector3 directionRotation = directionGameObject->rotate;
	Vector3 directionPosition = directionGameObject->translate;
	editorScene_->GetWorldTransform(
		directionGameObjectId,
		directionScale,
		directionRotation,
		directionPosition);
	const Vector3 direction = ResolveForwardDirection(directionRotation);
	const float maximumDistance = (std::max)(fireLine->fireLineDistance, 0.0f);
	std::vector<int32_t> ignoredGameObjectIds = fireLine->fireLineIgnoredGameObjectIds;
	ignoredGameObjectIds.push_back(muzzleGameObjectId);
	EditorJoltPhysicsManager::PhysicsHit hit{};
	bool isClear = true;

	for (int32_t castIndex = 0; castIndex < 64; castIndex++) {
		const bool hasHit = CastAttackPhysics(
			muzzlePosition,
			(std::max)(fireLine->fireLineRadius, 0.0f),
			direction,
			maximumDistance,
			ignoredGameObjectIds,
			hit);

		if (!hasHit) {
			break;
		}

		if (fireLine->fireLineAllowedTargetGameObjectId >= 0 &&
			IsInHierarchy(hit.gameObjectId, fireLine->fireLineAllowedTargetGameObjectId)) {
			break;
		}

		const EditorGameObject* hitGameObject = editorScene_->FindGameObject(hit.gameObjectId);
		const EditorComponent* hitRigidBody = hitGameObject != nullptr
			? EditorComponentUtility::FindComponent(*hitGameObject, EditorComponentType::RigidBody)
			: nullptr;
		const int32_t hitLayer = hitRigidBody != nullptr
			? (std::clamp)(hitRigidBody->physicsLayer, 0, 30)
			: 0;
		const bool isIncludedLayer = fireLine->fireLineLayerMask < 0 ||
			(fireLine->fireLineLayerMask & (1 << hitLayer)) != 0;

		if (isIncludedLayer) {
			isClear = false;
			break;
		}

		ignoredGameObjectIds.push_back(hit.gameObjectId);
	}

	if (writesRuntimeState) {
		fireLine->fireLineClear = isClear;
		fireLine->fireLineBlockingGameObjectId = isClear ? -1 : hit.gameObjectId;
		fireLine->fireLineBlockingDistance = isClear ? 0.0f : hit.distance;
	}

	return isClear;
}

void EditorWeaponManager::UpdateFireLineChecks() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* fireLine = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::FireLineCheck);

		if (gameObject.isActive && fireLine != nullptr && fireLine->isActive) {
			EvaluateFireLine(gameObject.id, true);
		}
	}
}

const EditorProjectileSurfaceModifierEntry* EditorWeaponManager::FindProjectileSurfaceModifier(
	const ActiveProjectile& projectile,
	const std::string& surfaceTag) const {
	const EditorProjectileSurfaceModifierEntry* defaultModifier = nullptr;

	for (const EditorProjectileSurfaceModifierEntry& modifier : projectile.surfaceModifiers) {
		if (modifier.surfaceTag == surfaceTag) {
			return &modifier;
		}

		if (modifier.surfaceTag.empty()) {
			defaultModifier = &modifier;
		}
	}

	return defaultModifier;
}

void EditorWeaponManager::QueueCompletionAction(int32_t weaponGameObjectId, int32_t completionType) {
	const EditorGameObject* weapon = editorScene_->FindGameObject(weaponGameObjectId);
	const EditorComponentType componentType = completionType == 2
		? EditorComponentType::TargetAssignment
		: EditorComponentType::WeaponFirePattern;
	const EditorComponent* component = weapon != nullptr
		? EditorComponentUtility::FindComponent(*weapon, componentType)
		: nullptr;

	if (component == nullptr || completionType == 0) {
		return;
	}

	QueueAction(
		weaponGameObjectId,
		completionType == 2
			? component->targetAssignmentActionTargetGameObjectId
			: component->weaponFirePatternActionTargetGameObjectId,
		completionType == 2
			? component->targetAssignmentCompletedActionName
			: component->weaponFirePatternCompletedActionName,
		1.0f);
}

void EditorWeaponManager::UpdateProjectiles(float deltaTime) {
	if (editorScene_ == nullptr || physicsManager_ == nullptr || objectPoolManager_ == nullptr) {
		return;
	}

	for (size_t projectileIndex = 0u; projectileIndex < activeProjectiles_.size();) {
		ActiveProjectile& activeProjectile = activeProjectiles_[projectileIndex];
		EditorGameObject* projectileGameObject = editorScene_->FindGameObject(activeProjectile.gameObjectId);
		bool shouldRelease = projectileGameObject == nullptr || !projectileGameObject->isActive;
		EditorJoltPhysicsManager::PhysicsHit physicsHit{};

		if (!shouldRelease) {
			const EditorComponent* detonator = EditorComponentUtility::FindComponent(
				*projectileGameObject,
				EditorComponentType::ProjectileDetonator);

			if (detonator != nullptr && detonator->isActive && detonator->projectileDetonateOnProximity) {
				int32_t proximityTargetGameObjectId = detonator->projectileDetonatorTargetGameObjectId;

				if (proximityTargetGameObjectId < 0) {
					const EditorComponent* steering = EditorComponentUtility::FindComponent(
						*projectileGameObject,
						EditorComponentType::TargetSteering);

					if (steering != nullptr && steering->isActive) {
						proximityTargetGameObjectId = steering->targetSteeringTargetGameObjectId;

						if (proximityTargetGameObjectId < 0) {
							const int32_t selectorGameObjectId = steering->targetSteeringSelectorGameObjectId >= 0
								? steering->targetSteeringSelectorGameObjectId
								: projectileGameObject->id;
							const EditorGameObject* selectorGameObject = editorScene_->FindGameObject(selectorGameObjectId);
							const EditorComponent* selector = selectorGameObject != nullptr
								? EditorComponentUtility::FindComponent(*selectorGameObject, EditorComponentType::TargetSelector)
								: nullptr;
							proximityTargetGameObjectId = selector != nullptr
								? selector->targetSelectorCurrentTargetGameObjectId
								: -1;
						}
					}

					if (proximityTargetGameObjectId < 0) {
						const EditorComponent* selector = EditorComponentUtility::FindComponent(
							*projectileGameObject,
							EditorComponentType::TargetSelector);

						if (selector != nullptr && selector->isActive) {
							proximityTargetGameObjectId = selector->targetSelectorCurrentTargetGameObjectId;
						}
					}
				}

				const EditorGameObject* proximityTarget = editorScene_->FindGameObject(proximityTargetGameObjectId);

				if (proximityTarget != nullptr && proximityTarget->isActive) {
					const Vector3 targetPosition = ResolveWorldPosition(*editorScene_, *proximityTarget);
					const float targetDistance = GetVectorLength(SubtractVector3(targetPosition, projectileGameObject->translate));

					if (targetDistance <= (std::max)(detonator->projectileDetonatorProximityRadius, 0.0f)) {
						ExecuteDetonation(activeProjectile, projectileGameObject->translate, proximityTargetGameObjectId);
						shouldRelease = true;
					}
				}
			}

			if (shouldRelease) {
				objectPoolManager_->Release(activeProjectile.gameObjectId);
				activeProjectiles_.erase(activeProjectiles_.begin() + static_cast<std::ptrdiff_t>(projectileIndex));
				continue;
			}

			const Vector3 acceleration{
				activeProjectile.gravity.x - activeProjectile.drag * activeProjectile.velocity.x,
				activeProjectile.gravity.y - activeProjectile.drag * activeProjectile.velocity.y,
				activeProjectile.gravity.z - activeProjectile.drag * activeProjectile.velocity.z};
			const Vector3 frameDisplacement{
				activeProjectile.velocity.x * deltaTime + 0.5f * acceleration.x * deltaTime * deltaTime,
				activeProjectile.velocity.y * deltaTime + 0.5f * acceleration.y * deltaTime * deltaTime,
				activeProjectile.velocity.z * deltaTime + 0.5f * acceleration.z * deltaTime * deltaTime};
			activeProjectile.velocity = {
				activeProjectile.velocity.x + acceleration.x * deltaTime,
				activeProjectile.velocity.y + acceleration.y * deltaTime,
				activeProjectile.velocity.z + acceleration.z * deltaTime};
			activeProjectile.speed = GetVectorLength(activeProjectile.velocity);
			const float travelDistance = GetVectorLength(frameDisplacement);

			if (travelDistance > kWeaponEpsilon) {
				activeProjectile.direction = MultiplyVector3(1.0f / travelDistance, frameDisplacement);
			}

			const Vector3 projectileStart = projectileGameObject->translate;
			const Vector3 projectileEnd = AddVector3(projectileStart, frameDisplacement);
			const float collisionStartOffset = (std::clamp)(
				activeProjectile.armingDistance - activeProjectile.traveledDistance,
				0.0f,
				travelDistance);
			const float collisionDistance = travelDistance - collisionStartOffset;
			const Vector3 collisionStart = AddVector3(
				projectileStart,
				MultiplyVector3(collisionStartOffset, activeProjectile.direction));
			const bool hasHit = CastAttackPhysics(
				collisionStart,
				activeProjectile.radius,
				activeProjectile.direction,
				collisionDistance,
				activeProjectile.ignoredGameObjectIds,
				physicsHit);
			EditorOceanSegmentHit oceanHit{};
			const uint64_t oceanQueryKey =
				(static_cast<uint64_t>(static_cast<uint32_t>(activeProjectile.gameObjectId)) << 32u) |
				0x50524f4au;
			const bool hasOceanHit = activeProjectile.oceanCollision &&
				collisionDistance > kWeaponEpsilon && CastEditorOceanSegment(
				*editorScene_,
				-1,
				collisionStart,
				projectileEnd,
				activeProjectile.radius,
				oceanQueryKey,
				GetEditorOceanElapsedTime(),
				2,
				4,
				oceanHit);
			const bool usesOceanHit = hasOceanHit &&
				(!hasHit || oceanHit.distance < physicsHit.distance);

			if (usesOceanHit) {
				ExecuteImpactResponse(
					activeProjectile.ownerGameObjectId,
					oceanHit.oceanGameObjectId,
					oceanHit.position,
					oceanHit.normal,
					activeProjectile.damageTagId);

				if (detonator != nullptr && detonator->isActive && detonator->projectileDetonateOnContact) {
					ExecuteDetonation(activeProjectile, oceanHit.position, oceanHit.oceanGameObjectId);
				}

				QueueAction(
					activeProjectile.ownerGameObjectId,
					activeProjectile.actionTargetGameObjectId,
					activeProjectile.hitActionName,
					static_cast<float>(oceanHit.oceanGameObjectId));
				shouldRelease = true;
			}
			else if (hasHit) {
				if (activeProjectile.ownerGameObjectId == 58 && physicsHit.gameObjectId == 46) {
					OutputDebugStringA("[Weapon20mm] projectile hit PlayerShip\n");
				}
				if (damageManager_ != nullptr) {
					EditorScriptDamageContext damageContext{};
					damageContext.targetGameObjectId = physicsHit.gameObjectId;
					damageContext.sourceGameObjectId = activeProjectile.gameObjectId;
					damageContext.instigatorGameObjectId = activeProjectile.instigatorGameObjectId;
					damageContext.hitPosition = {physicsHit.point.x, physicsHit.point.y, physicsHit.point.z};
					damageContext.hitNormal = {physicsHit.normal.x, physicsHit.normal.y, physicsHit.normal.z};
					damageContext.baseDamage = activeProjectile.damage;
					damageContext.userTag = activeProjectile.damageTagId;
					damageManager_->ApplyDamage(damageContext);
				}

				ExecuteImpactResponse(
					activeProjectile.ownerGameObjectId,
					physicsHit.gameObjectId,
					physicsHit.point,
					physicsHit.normal,
					activeProjectile.damageTagId);

				QueueAction(
					activeProjectile.ownerGameObjectId,
					activeProjectile.actionTargetGameObjectId,
					activeProjectile.hitActionName,
					static_cast<float>(physicsHit.gameObjectId));
				const std::string surfaceTag = ResolveSurfaceTag(physicsHit.gameObjectId);
				const EditorProjectileSurfaceModifierEntry* surfaceModifier =
					FindProjectileSurfaceModifier(activeProjectile, surfaceTag);
				const float penetrationLossMultiplier = surfaceModifier != nullptr
					? (std::max)(surfaceModifier->penetrationLossMultiplier, 0.0f)
					: 1.0f;
				const float retentionMultiplier = surfaceModifier != nullptr
					? (std::max)(surfaceModifier->energyRetentionMultiplier, 0.0f)
					: 1.0f;
				const float ricochetAngleOffset = surfaceModifier != nullptr
					? surfaceModifier->ricochetAngleOffset
					: 0.0f;
				const Vector3 normalizedNormal = NormalizeVector3(physicsHit.normal);
				const float incidenceDot = (std::clamp)(
					-DotVector3(activeProjectile.direction, normalizedNormal),
					0.0f,
					1.0f);
				const float incidenceAngleDegrees = std::acos(incidenceDot) / kDegreesToRadians;
				const float ricochetThreshold = (std::clamp)(
					activeProjectile.ricochetAngleDegrees + ricochetAngleOffset,
					0.0f,
					90.0f);
				const float effectiveRetention = (std::clamp)(
					activeProjectile.energyRetention * retentionMultiplier,
					0.0f,
					1.0f);
				const float effectivePenetrationLoss =
					activeProjectile.penetrationLoss * penetrationLossMultiplier;
				const bool canRicochet =
					activeProjectile.ricochetCount < activeProjectile.maximumRicochets &&
					incidenceAngleDegrees >= ricochetThreshold;
				const bool canPenetrate = !canRicochet &&
					activeProjectile.penetrationCount < activeProjectile.maximumPenetrations &&
					activeProjectile.penetrationEnergy > effectivePenetrationLoss;

				if (canRicochet) {
					activeProjectile.direction = NormalizeVector3(SubtractVector3(
						activeProjectile.direction,
						MultiplyVector3(
							2.0f * DotVector3(activeProjectile.direction, normalizedNormal),
							normalizedNormal)));
					activeProjectile.ricochetCount++;
				}
				else if (canPenetrate) {
					activeProjectile.penetrationEnergy -= effectivePenetrationLoss;
					activeProjectile.penetrationCount++;
				}

				if (canRicochet || canPenetrate) {
					activeProjectile.speed *= effectiveRetention;
					activeProjectile.velocity = MultiplyVector3(
						activeProjectile.speed,
						activeProjectile.direction);
					activeProjectile.damage *= (std::clamp)(
						activeProjectile.damageRetention * retentionMultiplier,
						0.0f,
						1.0f);
					activeProjectile.ignoredGameObjectIds.push_back(physicsHit.gameObjectId);
					std::sort(
						activeProjectile.ignoredGameObjectIds.begin(),
						activeProjectile.ignoredGameObjectIds.end());
					activeProjectile.ignoredGameObjectIds.erase(
						std::unique(
							activeProjectile.ignoredGameObjectIds.begin(),
							activeProjectile.ignoredGameObjectIds.end()),
						activeProjectile.ignoredGameObjectIds.end());
					projectileGameObject->translate = AddVector3(
						physicsHit.point,
						MultiplyVector3(
							activeProjectile.radius + 0.02f,
							activeProjectile.direction));
					physicsManager_->SetGameObjectTransform(
						projectileGameObject->id,
						projectileGameObject->translate,
						projectileGameObject->rotate);
					activeProjectile.traveledDistance += collisionStartOffset + physicsHit.distance;
					activeProjectile.remainingLifetime -= deltaTime;
					shouldRelease = activeProjectile.speed <= kWeaponEpsilon ||
						activeProjectile.remainingLifetime <= 0.0f;

					if (shouldRelease && detonator != nullptr && detonator->isActive &&
						detonator->projectileDetonateOnLifetime) {
						ExecuteDetonation(activeProjectile, projectileGameObject->translate, -1);
					}
				}
				else {
					if (detonator != nullptr && detonator->isActive && detonator->projectileDetonateOnContact) {
						ExecuteDetonation(activeProjectile, physicsHit.point, physicsHit.gameObjectId);
					}

					shouldRelease = true;
				}
			}
			else {
				projectileGameObject->translate = projectileEnd;
				physicsManager_->SetGameObjectTransform(
					projectileGameObject->id,
					projectileGameObject->translate,
					projectileGameObject->rotate);
				activeProjectile.remainingLifetime -= deltaTime;
				activeProjectile.traveledDistance += travelDistance;
				shouldRelease = activeProjectile.remainingLifetime <= 0.0f;

				if (shouldRelease && detonator != nullptr && detonator->isActive && detonator->projectileDetonateOnLifetime) {
					ExecuteDetonation(activeProjectile, projectileGameObject->translate, -1);
				}
			}
		}

		if (shouldRelease) {
			objectPoolManager_->Release(activeProjectile.gameObjectId);
			activeProjectiles_.erase(activeProjectiles_.begin() + static_cast<std::ptrdiff_t>(projectileIndex));
			continue;
		}

		projectileIndex++;
	}
}

bool EditorWeaponManager::DetonateProjectile(int32_t projectileGameObjectId) {
	if (!isStarted_ || editorScene_ == nullptr || objectPoolManager_ == nullptr) {
		return false;
	}

	for (size_t projectileIndex = 0u; projectileIndex < activeProjectiles_.size(); ++projectileIndex) {
		ActiveProjectile& activeProjectile = activeProjectiles_[projectileIndex];
		if (activeProjectile.gameObjectId != projectileGameObjectId) {
			continue;
		}

		const EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileGameObjectId);
		if (projectileGameObject == nullptr) {
			return false;
		}

		ExecuteDetonation(activeProjectile, projectileGameObject->translate, -1);
		objectPoolManager_->Release(projectileGameObjectId);
		activeProjectiles_.erase(activeProjectiles_.begin() + static_cast<std::ptrdiff_t>(projectileIndex));
		return true;
	}

	return false;
}

void EditorWeaponManager::GetIncomingThreats(
	int32_t targetGameObjectId,
	float maximumDistance,
	float minimumClosingSpeed,
	float maximumMissDistance,
	int32_t maximumCount,
	std::vector<ThreatInfo>& threats) const {
	threats.clear();

	if (!isStarted_ || editorScene_ == nullptr || maximumCount <= 0) {
		return;
	}

	const EditorGameObject* targetGameObject = editorScene_->FindGameObject(targetGameObjectId);
	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		return;
	}

	const Vector3 targetPosition = ResolveWorldPosition(*editorScene_, *targetGameObject);

	for (const ActiveProjectile& activeProjectile : activeProjectiles_) {
		const EditorGameObject* projectileGameObject = editorScene_->FindGameObject(activeProjectile.gameObjectId);
		if (projectileGameObject == nullptr || !projectileGameObject->isActive || activeProjectile.speed <= kWeaponEpsilon) {
			continue;
		}

		const Vector3 relativePosition = SubtractVector3(projectileGameObject->translate, targetPosition);
		const float distance = GetVectorLength(relativePosition);
		if (distance > maximumDistance || distance <= kWeaponEpsilon) {
			continue;
		}

		const Vector3 velocity = activeProjectile.velocity;
		const float closingSpeed = -DotVector3(MultiplyVector3(1.0f / distance, relativePosition), velocity);
		if (closingSpeed < minimumClosingSpeed) {
			continue;
		}

		const float velocitySquared = DotVector3(velocity, velocity);

		if (velocitySquared <= kWeaponEpsilon) {
			continue;
		}
		const float closestTime = (std::max)(-DotVector3(relativePosition, velocity) / velocitySquared, 0.0f);
		const Vector3 closestOffset = AddVector3(relativePosition, MultiplyVector3(closestTime, velocity));
		if (GetVectorLength(closestOffset) > maximumMissDistance) {
			continue;
		}

		threats.push_back(ThreatInfo{
			activeProjectile.gameObjectId,
			activeProjectile.ownerGameObjectId,
			distance,
			closingSpeed,
			closestTime});
	}

	std::ranges::sort(threats, [](const ThreatInfo& firstThreat, const ThreatInfo& secondThreat) {
		return firstThreat.estimatedArrivalSeconds < secondThreat.estimatedArrivalSeconds;
	});

	if (threats.size() > static_cast<size_t>(maximumCount)) {
		threats.resize(static_cast<size_t>(maximumCount));
	}
}

void EditorWeaponManager::ResetRuntimeState(int32_t gameObjectId) {
	hitscanCooldowns_.erase(gameObjectId);
	projectileCooldowns_.erase(gameObjectId);
	pendingShots_.erase(
		std::remove_if(
			pendingShots_.begin(),
			pendingShots_.end(),
			[gameObjectId](const PendingShot& pendingShot) {
				return pendingShot.weaponGameObjectId == gameObjectId;
			}),
		pendingShots_.end());
	const auto recoilRuntimeIterator = visualRecoilRuntimes_.find(gameObjectId);

	if (recoilRuntimeIterator != visualRecoilRuntimes_.end() && editorScene_ != nullptr) {
		EditorGameObject* visual = editorScene_->FindGameObject(recoilRuntimeIterator->second.gameObjectId);

		if (visual != nullptr) {
			visual->translate = SubtractVector3(visual->translate, recoilRuntimeIterator->second.positionOffset);
			visual->rotate = SubtractVector3(visual->rotate, recoilRuntimeIterator->second.rotationOffset);
		}

		visualRecoilRuntimes_.erase(recoilRuntimeIterator);
	}

	EditorGameObject* gameObject = editorScene_ != nullptr ? editorScene_->FindGameObject(gameObjectId) : nullptr;
	EditorComponent* accuracy = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::WeaponAccuracy)
		: nullptr;

	if (accuracy != nullptr) {
		accuracy->weaponAccuracyCurrentSpread = 0.0f;
	}
}

void EditorWeaponManager::ExecuteDetonation(
	ActiveProjectile& activeProjectile,
	const Vector3& position,
	int32_t hitGameObjectId) {
	if (editorScene_ == nullptr) {
		return;
	}

	const EditorGameObject* projectileGameObject = editorScene_->FindGameObject(activeProjectile.gameObjectId);
	const EditorComponent* detonator = projectileGameObject != nullptr
		? EditorComponentUtility::FindComponent(*projectileGameObject, EditorComponentType::ProjectileDetonator)
		: nullptr;

	if (detonator == nullptr || !detonator->isActive) {
		return;
	}

	const int32_t areaDamageGameObjectId = detonator->projectileDetonatorAreaDamageGameObjectId >= 0
		? detonator->projectileDetonatorAreaDamageGameObjectId
		: activeProjectile.gameObjectId;

	if (damageManager_ != nullptr) {
		damageManager_->ApplyAreaDamage(areaDamageGameObjectId, position, activeProjectile.ownerGameObjectId);
	}

	if (scriptManager_ != nullptr && !detonator->projectileDetonatedActionName.empty()) {
		const int32_t actionTargetGameObjectId = detonator->projectileDetonatorActionTargetGameObjectId >= 0
			? detonator->projectileDetonatorActionTargetGameObjectId
			: activeProjectile.gameObjectId;
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeGameObject;
		payload.gameObjectId = hitGameObjectId;
		scriptManager_->QueueActionPayload(
			actionTargetGameObjectId,
			detonator->projectileDetonatedActionName,
			payload);
	}
}

void EditorWeaponManager::QueueAction(
	int32_t ownerGameObjectId,
	int32_t actionTargetGameObjectId,
	const std::string& actionName,
	float value) const {
	if (scriptManager_ == nullptr || actionName.empty()) {
		return;
	}

	const int32_t targetGameObjectId = actionTargetGameObjectId >= 0
		? actionTargetGameObjectId
		: ownerGameObjectId;
	scriptManager_->QueueActionEvent(
		targetGameObjectId,
		actionName,
		EditorScriptInputValueTypeButton,
		value,
		EditorScriptVector2{});
}
