#include "EditorRailShooterEnemyManager.h"

#include "EditorAnimationManager.h"
#include "EditorComponentUtility.h"
#include "EditorPhysicsManager.h"
#include "EditorRailMovementManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>

void EditorRailShooterEnemyManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorPhysicsManager* physicsManager,
	EditorEffectManager* effectManager,
	EditorAnimationManager* animationManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	physicsManager_ = physicsManager;
	effectManager_ = effectManager;
	animationManager_ = animationManager;
	enemyRuntimes_.clear();
	projectilePoolsByEnemyId_.clear();
	isStarted_ = false;
}

void EditorRailShooterEnemyManager::Start() {
	enemyRuntimes_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Health);

		if (healthComponent != nullptr && healthComponent->isActive) {
			healthComponent->healthCurrent = (std::max)(healthComponent->healthMaximum, 0.0f);
		}

		EditorComponent* enemyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailShooterEnemy);

		if (enemyComponent == nullptr || !enemyComponent->isActive || !gameObject.isActive) {
			continue;
		}

		EnemyRuntime enemyRuntime{};
		enemyRuntime.attackTimer = (std::max)(enemyComponent->enemyAttackInterval, 0.01f);
		enemyRuntimes_[gameObject.id] = enemyRuntime;
		SetGameObjectRuntimeActive(gameObject, false);
	}

	BuildProjectilePools();
}

void EditorRailShooterEnemyManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	UpdateProjectiles(deltaTime);

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* enemyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailShooterEnemy);

		if (enemyComponent == nullptr || !enemyComponent->isActive) {
			continue;
		}

		auto runtimeIterator = enemyRuntimes_.find(gameObject.id);

		if (runtimeIterator == enemyRuntimes_.end()) {
			continue;
		}

		EnemyRuntime& enemyRuntime = runtimeIterator->second;

		if (!enemyRuntime.hasSpawned) {
			const bool usesFollower = enemyComponent->enemySpawnFollowerGameObjectId >= 0;

			if (usesFollower) {
				float followerProgress = 0.0f;
				const bool hasFollowerProgress =
					railMovementManager_ != nullptr &&
					railMovementManager_->GetNormalizedProgress(
						enemyComponent->enemySpawnFollowerGameObjectId,
						followerProgress);

				if (!hasFollowerProgress ||
					followerProgress < (std::clamp)(enemyComponent->enemySpawnNormalized, 0.0f, 1.0f)) {
					continue;
				}
			}

			enemyRuntime.hasSpawned = true;
			SetGameObjectRuntimeActive(gameObject, true);

			if (effectManager_ != nullptr) {
				effectManager_->PlayEffect(gameObject.id);
			}

			if (animationManager_ != nullptr) {
				animationManager_->PlayAnimation(gameObject.id);
			}
		}

		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Health);

		if (healthComponent != nullptr &&
			healthComponent->isActive &&
			healthComponent->healthCurrent <= 0.0f) {
			SetGameObjectRuntimeActive(gameObject, false);
			continue;
		}

		EditorGameObject* targetGameObject = editorScene_->FindGameObject(
			enemyComponent->enemyAttackTargetGameObjectId);

		if (targetGameObject == nullptr || !targetGameObject->isActive) {
			continue;
		}

		const float attackRange = (std::max)(enemyComponent->enemyAttackRange, 0.0f);
		const float targetDistance = Length(Subtract(
			targetGameObject->translate,
			gameObject.translate));

		if (targetDistance > attackRange) {
			continue;
		}

		enemyRuntime.attackTimer -= deltaTime;

		if (enemyRuntime.attackTimer > 0.0f) {
			continue;
		}

		enemyRuntime.attackTimer = (std::max)(enemyComponent->enemyAttackInterval, 0.01f);
		ApplyAttack(gameObject, *enemyComponent);
	}
}

void EditorRailShooterEnemyManager::Draw() {
}

void EditorRailShooterEnemyManager::Stop() {
	enemyRuntimes_.clear();
	projectilePoolsByEnemyId_.clear();
	isStarted_ = false;
}

void EditorRailShooterEnemyManager::SetGameObjectRuntimeActive(
	EditorGameObject& gameObject,
	bool isActive) {
	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObject.id, isActive);
	}

	gameObject.isActive = isActive;
}

void EditorRailShooterEnemyManager::BuildProjectilePools() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const auto& enemyRuntimePair : enemyRuntimes_) {
		const int32_t enemyGameObjectId = enemyRuntimePair.first;
		EditorGameObject* enemyGameObject = editorScene_->FindGameObject(enemyGameObjectId);

		if (enemyGameObject == nullptr) {
			continue;
		}

		const EditorComponent* enemyComponent = EditorComponentUtility::FindComponent(
			*enemyGameObject,
			EditorComponentType::RailShooterEnemy);

		if (enemyComponent == nullptr ||
			enemyComponent->enemyProjectileTemplateGameObjectId < 0 ||
			enemyComponent->enemyProjectileTemplateGameObjectId == enemyGameObjectId) {
			continue;
		}

		const int32_t templateGameObjectId = enemyComponent->enemyProjectileTemplateGameObjectId;
		EditorGameObject* templateGameObject = editorScene_->FindGameObject(templateGameObjectId);

		if (templateGameObject == nullptr) {
			continue;
		}

		const std::string enemyName = enemyGameObject->name;
		const int32_t projectilePoolSize = (std::clamp)(enemyComponent->enemyProjectilePoolSize, 1, 256);
		SetGameObjectRuntimeActive(*templateGameObject, false);

		std::vector<ProjectileRuntime>& projectilePool = projectilePoolsByEnemyId_[enemyGameObjectId];
		projectilePool.reserve(static_cast<size_t>(projectilePoolSize));

		for (int32_t projectileIndex = 0; projectileIndex < projectilePoolSize; projectileIndex++) {
			const int32_t projectileGameObjectId = editorScene_->DuplicateGameObject(templateGameObjectId);
			EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileGameObjectId);

			if (projectileGameObject == nullptr) {
				continue;
			}

			projectileGameObject->name = enemyName + "_Projectile_" + std::to_string(projectileIndex + 1);
			SetGameObjectRuntimeActive(*projectileGameObject, false);

			ProjectileRuntime projectileRuntime{};
			projectileRuntime.gameObjectId = projectileGameObjectId;
			projectilePool.push_back(projectileRuntime);
		}
	}
}

void EditorRailShooterEnemyManager::UpdateProjectiles(float deltaTime) {
	for (auto& projectilePoolPair : projectilePoolsByEnemyId_) {
		for (ProjectileRuntime& projectileRuntime : projectilePoolPair.second) {
			if (!projectileRuntime.isFlying) {
				continue;
			}

			EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileRuntime.gameObjectId);
			EditorGameObject* targetGameObject = editorScene_->FindGameObject(projectileRuntime.targetGameObjectId);

			if (projectileGameObject == nullptr || targetGameObject == nullptr || !targetGameObject->isActive) {
				DeactivateProjectile(projectileRuntime);
				continue;
			}

			const Vector3 previousPosition = projectileGameObject->translate;
			const Vector3 displacement = Multiply(deltaTime, projectileRuntime.velocity);
			const Vector3 nextPosition = Add(previousPosition, displacement);
			const float displacementLengthSquared = Dot(displacement, displacement);
			float closestRatio = 0.0f;

			if (displacementLengthSquared > 0.000001f) {
				closestRatio = (std::clamp)(
					Dot(Subtract(targetGameObject->translate, previousPosition), displacement) /
						displacementLengthSquared,
					0.0f,
					1.0f);
			}

			const Vector3 closestPosition = Add(previousPosition, Multiply(closestRatio, displacement));
			const float targetDistance = Length(Subtract(targetGameObject->translate, closestPosition));
			projectileGameObject->translate = nextPosition;
			projectileRuntime.remainingLifetime -= deltaTime;

			if (targetDistance <= projectileRuntime.hitRadius) {
				ApplyDamageToTarget(projectileRuntime.targetGameObjectId, projectileRuntime.damage);
				DeactivateProjectile(projectileRuntime);
				continue;
			}

			if (projectileRuntime.remainingLifetime <= 0.0f) {
				DeactivateProjectile(projectileRuntime);
			}
		}
	}
}

bool EditorRailShooterEnemyManager::LaunchProjectile(
	EditorGameObject& enemyGameObject,
	const EditorComponent& enemyComponent) {
	auto projectilePoolIterator = projectilePoolsByEnemyId_.find(enemyGameObject.id);

	if (projectilePoolIterator == projectilePoolsByEnemyId_.end()) {
		return false;
	}

	EditorGameObject* targetGameObject = editorScene_->FindGameObject(
		enemyComponent.enemyAttackTargetGameObjectId);

	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		return false;
	}

	const Vector3 targetOffset = Subtract(targetGameObject->translate, enemyGameObject.translate);
	const float targetDistance = Length(targetOffset);

	if (targetDistance <= 0.0001f) {
		ApplyDamageToTarget(enemyComponent.enemyAttackTargetGameObjectId, enemyComponent.enemyAttackDamage);
		return true;
	}

	const Vector3 projectileDirection = Multiply(1.0f / targetDistance, targetOffset);

	for (ProjectileRuntime& projectileRuntime : projectilePoolIterator->second) {
		if (projectileRuntime.isFlying) {
			continue;
		}

		EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileRuntime.gameObjectId);

		if (projectileGameObject == nullptr) {
			continue;
		}

		projectileGameObject->translate = enemyGameObject.translate;
		projectileGameObject->rotate.x = -std::asin((std::clamp)(projectileDirection.y, -1.0f, 1.0f));
		projectileGameObject->rotate.y = std::atan2(projectileDirection.x, projectileDirection.z);
		projectileGameObject->rotate.z = 0.0f;
		SetGameObjectRuntimeActive(*projectileGameObject, true);

		projectileRuntime.targetGameObjectId = enemyComponent.enemyAttackTargetGameObjectId;
		projectileRuntime.velocity = Multiply(
			(std::max)(enemyComponent.enemyProjectileSpeed, 0.01f),
			projectileDirection);
		projectileRuntime.remainingLifetime = (std::max)(enemyComponent.enemyProjectileLifetime, 0.01f);
		projectileRuntime.damage = (std::max)(enemyComponent.enemyAttackDamage, 0.0f);
		projectileRuntime.hitRadius = (std::max)(enemyComponent.enemyProjectileHitRadius, 0.01f);
		projectileRuntime.isFlying = true;
		return true;
	}

	return false;
}

void EditorRailShooterEnemyManager::DeactivateProjectile(ProjectileRuntime& projectileRuntime) {
	EditorGameObject* projectileGameObject = editorScene_->FindGameObject(projectileRuntime.gameObjectId);

	if (projectileGameObject != nullptr) {
		SetGameObjectRuntimeActive(*projectileGameObject, false);
	}

	projectileRuntime.targetGameObjectId = -1;
	projectileRuntime.velocity = {0.0f, 0.0f, 0.0f};
	projectileRuntime.remainingLifetime = 0.0f;
	projectileRuntime.damage = 0.0f;
	projectileRuntime.isFlying = false;
}

void EditorRailShooterEnemyManager::ApplyAttack(
	EditorGameObject& enemyGameObject,
	EditorComponent& enemyComponent) {
	if (effectManager_ != nullptr) {
		effectManager_->PlayEffect(enemyGameObject.id);
	}

	if (animationManager_ != nullptr) {
		animationManager_->SetTrigger(enemyGameObject.id, "Attack");
	}

	if (projectilePoolsByEnemyId_.contains(enemyGameObject.id)) {
		LaunchProjectile(enemyGameObject, enemyComponent);
		return;
	}

	ApplyDamageToTarget(enemyComponent.enemyAttackTargetGameObjectId, enemyComponent.enemyAttackDamage);
}

void EditorRailShooterEnemyManager::ApplyDamageToTarget(int32_t targetGameObjectId, float damage) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* targetGameObject = editorScene_->FindGameObject(
		targetGameObjectId);

	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		return;
	}

	EditorComponent* targetHealthComponent = EditorComponentUtility::FindComponent(
		*targetGameObject,
		EditorComponentType::Health);

	if (targetHealthComponent == nullptr || !targetHealthComponent->isActive) {
		return;
	}

	targetHealthComponent->healthCurrent = (std::max)(
		targetHealthComponent->healthCurrent - (std::max)(damage, 0.0f),
		0.0f);

	if (targetHealthComponent->healthCurrent > 0.0f) {
		return;
	}

	if (effectManager_ != nullptr) {
		effectManager_->PlayEffect(targetGameObject->id);
	}

	SetGameObjectRuntimeActive(*targetGameObject, false);
}
