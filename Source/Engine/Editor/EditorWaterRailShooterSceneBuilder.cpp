#include "EditorWaterRailShooterSceneBuilder.h"

#include "EditorComponentUtility.h"
#include "EditorScene.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {
	constexpr const char* kScenePath = "Assets/Scenes/WaterRailShooter_0817.scene";
	constexpr const char* kSmallBoatPrefabPath = "Assets/Prefabs/SmallBoat_0817.prefab";
	constexpr const char* kMissileBoatPrefabPath = "Assets/Prefabs/MissileBoat_0817.prefab";
	constexpr const char* kScriptDllPath =
		"resources/scripts/WaterRailShooter0817/x64/Release/WaterRailShooter0817.dll";
	constexpr const char* kInputActionsPath = "Assets/WaterRailShooter0817.inputactions";

	//================================================================
	// 実行中に必要になった時だけ生成する物理付きPoolの上限
	//================================================================

	// WAVEの最大同時数と発射間隔×寿命から容量だけを決める。
	// ObjectPoolはPlay開始時に全数生成せず、Acquire時にこの上限まで遅延生成する。
	constexpr int32_t kPlayerRocketPoolSize = 8;
	constexpr int32_t kPlayerMissilePoolSize = 8;
	constexpr int32_t kEnemyBulletPoolSize = 32;
	constexpr int32_t kEnemyMissilePoolSize = 12;
	constexpr int32_t kSmallBoatPoolSize = 10;
	constexpr int32_t kMissileBoatPoolSize = 4;

	EditorComponent* AddComponent(
		EditorScene& editorScene,
		int32_t gameObjectId,
		EditorComponentType componentType) {
		if (!editorScene.AddComponent(gameObjectId, componentType)) {
			return nullptr;
		}

		EditorGameObject* gameObject = editorScene.FindGameObject(gameObjectId);
		return gameObject != nullptr
			? EditorComponentUtility::FindComponent(*gameObject, componentType)
			: nullptr;
	}

	int32_t CreateGameObject(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		int32_t parentGameObjectId = -1) {
		const int32_t gameObjectId = editorScene.CreateGameObject(gameObjectName);
		EditorGameObject* gameObject = editorScene.FindGameObject(gameObjectId);

		// Builderは複数Componentの設定ポインタを同時に保持するため、
		// push_backによる再配置が起きないだけの領域を先に確保する。
		if (gameObject != nullptr) {
			gameObject->components.reserve(32U);
		}

		if (parentGameObjectId >= 0) {
			editorScene.SetParent(gameObjectId, parentGameObjectId);
		}

		return gameObjectId;
	}

	int32_t CreateModel(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		const std::string& assetPath,
		const Vector3& position,
		const Vector3& scale,
		const Vector3& color,
		int32_t parentGameObjectId = -1) {
		const int32_t gameObjectId = CreateGameObject(
			editorScene,
			gameObjectName,
			parentGameObjectId);
		EditorGameObject* gameObject = editorScene.FindGameObject(gameObjectId);

		if (gameObject != nullptr) {
			gameObject->translate = position;
			gameObject->scale = scale;
		}

		EditorComponent* meshFilter = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::MeshFilter);
		EditorComponent* modelRenderer = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::ModelRenderer);

		if (meshFilter != nullptr) {
			meshFilter->assetPath = assetPath;
		}

		if (modelRenderer != nullptr) {
			modelRenderer->assetPath = assetPath;
			modelRenderer->color = color;
			modelRenderer->lightingMode = 3;
			modelRenderer->roughness = 0.42f;
			modelRenderer->metallic = 0.35f;
			modelRenderer->reflectionStrength = 0.55f;
			modelRenderer->useImportedMaterialTextures = true;
		}

		return gameObjectId;
	}

	EditorComponent* AddScript(EditorScene& editorScene, int32_t gameObjectId) {
		EditorComponent* script = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::Script);

		if (script != nullptr) {
			script->assetPath = kScriptDllPath;
		}

		return script;
	}

	void AddTeamAndTarget(
		EditorScene& editorScene,
		int32_t gameObjectId,
		int32_t teamId,
		float targetRadius,
		const Vector3& aimOffset = {0.0f, 0.0f, 0.0f}) {
		EditorComponent* team = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::Team);
		EditorComponent* targetPoint = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::TargetPoint);

		if (team != nullptr) {
			team->teamId = teamId;
			team->teamTargetable = true;
		}

		if (targetPoint != nullptr) {
			targetPoint->targetPointRadius = targetRadius;
			targetPoint->targetPointAimOffset = aimOffset;
		}
	}

	void AddDamageable(
		EditorScene& editorScene,
		int32_t gameObjectId,
		float maximumHealth,
		int32_t actionTargetGameObjectId,
		const std::string& deathActionName,
		bool deactivateOnDeath) {
		EditorComponent* health = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::Health);
		EditorComponent* damageReceiver = AddComponent(
			editorScene,
			gameObjectId,
			EditorComponentType::DamageReceiver);

		if (health != nullptr) {
			health->healthMaximum = maximumHealth;
			health->healthCurrent = maximumHealth;
		}

		if (damageReceiver != nullptr) {
			damageReceiver->damageMultiplier = 1.0f;
			damageReceiver->damageInvulnerabilitySeconds = 0.03f;
			damageReceiver->damageDeactivateOnDeath = deactivateOnDeath;
			damageReceiver->damageActionTargetGameObjectId = actionTargetGameObjectId;
			damageReceiver->deathActionName = deathActionName;
		}
	}

	void ConfigureTargetSelector(
		EditorScene& editorScene,
		int32_t ownerGameObjectId,
		int32_t referenceGameObjectId,
		float maximumDistance) {
		EditorComponent* targetSelector = AddComponent(
			editorScene,
			ownerGameObjectId,
			EditorComponentType::TargetSelector);

		if (targetSelector == nullptr) {
			return;
		}

		targetSelector->targetSelectorSearchLayer = -1;
		targetSelector->targetSelectorMaximumDistance = maximumDistance;
		targetSelector->targetSelectorMaximumAngle = 110.0f;
		targetSelector->targetSelectorReferenceGameObjectId = referenceGameObjectId;
		targetSelector->targetSelectorOcclusionCheck = true;
		targetSelector->targetSelectorOcclusionMode = 3;
		targetSelector->targetSelectorOceanClearance = 0.15f;
		targetSelector->targetSelectorMaximumTargets = 32;
		targetSelector->targetSelectorSelectionMode = 1;
		targetSelector->targetSelectorTeamFilter = 1;
		targetSelector->targetSelectorIncludeNeutral = false;
	}

	void ConfigureAttackFilter(EditorScene& editorScene, int32_t ownerGameObjectId) {
		EditorComponent* attackFilter = AddComponent(
			editorScene,
			ownerGameObjectId,
			EditorComponentType::AttackCollisionFilter);

		if (attackFilter != nullptr) {
			attackFilter->attackFilterInstigatorGameObjectId = -1;
			attackFilter->attackFilterIgnoreInstigator = true;
			attackFilter->attackFilterIgnoreInstigatorHierarchy = true;
			attackFilter->attackFilterTeamRule = 1;
			attackFilter->attackFilterIgnoreNeutral = true;
			attackFilter->attackFilterArmingDistance = 2.0f;
		}
	}

	void ConfigureSimulationLod(
		EditorScene& editorScene,
		int32_t ownerGameObjectId,
		int32_t referenceGameObjectId,
		float mediumDistance,
		float farDistance,
		float culledDistance,
		bool affectsHierarchy) {
		EditorComponent* simulationLod = AddComponent(
			editorScene,
			ownerGameObjectId,
			EditorComponentType::SimulationLOD);

		if (simulationLod == nullptr) {
			return;
		}

		simulationLod->simulationLodReferenceGameObjectId = referenceGameObjectId;
		simulationLod->simulationLodMediumDistance = mediumDistance;
		simulationLod->simulationLodFarDistance = farDistance;
		simulationLod->simulationLodCulledDistance = culledDistance;
		simulationLod->simulationLodMediumScriptInterval = 1.0f / 30.0f;
		simulationLod->simulationLodFarScriptInterval = 0.15f;
		simulationLod->simulationLodDisablePhysicsAtFar = true;
		simulationLod->simulationLodDisableScriptsAtFar = true;
		simulationLod->simulationLodDisableAiAtFar = true;
		simulationLod->simulationLodDisableAnimationAtFar = true;
		simulationLod->simulationLodDisableEffectsAtFar = true;
		simulationLod->simulationLodAffectHierarchy = affectsHierarchy;
	}

	int32_t CreateText(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		const std::string& text,
		const EditorScriptVector2& position,
		const EditorScriptVector2& size,
		const Vector3& color,
		bool isActive = true,
		int32_t parentGameObjectId = -1) {
		const int32_t textGameObjectId = CreateGameObject(
			editorScene,
			gameObjectName,
			parentGameObjectId);
		EditorGameObject* textGameObject = editorScene.FindGameObject(textGameObjectId);
		EditorComponent* textComponent = AddComponent(
			editorScene,
			textGameObjectId,
			EditorComponentType::TextMeshProUGUI);

		if (textGameObject != nullptr) {
			textGameObject->isActive = isActive;
		}

		if (textComponent != nullptr) {
			textComponent->buttonLabel = text;
			textComponent->buttonPosition = position;
			textComponent->buttonSize = size;
			textComponent->color = color;
		}

		return textGameObjectId;
	}

	int32_t CreateButton(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		const std::string& label,
		const std::string& actionName,
		const EditorScriptVector2& position,
		bool isActive,
		int32_t parentGameObjectId = -1) {
		const int32_t buttonGameObjectId = CreateGameObject(
			editorScene,
			gameObjectName,
			parentGameObjectId);
		EditorGameObject* buttonGameObject = editorScene.FindGameObject(buttonGameObjectId);
		EditorComponent* button = AddComponent(
			editorScene,
			buttonGameObjectId,
			EditorComponentType::Button);
		AddScript(editorScene, buttonGameObjectId);

		if (buttonGameObject != nullptr) {
			buttonGameObject->isActive = isActive;
		}

		if (button != nullptr) {
			button->buttonLabel = label;
			button->buttonPosition = position;
			button->buttonSize = {250.0f, 42.0f};
			button->buttonInteractable = true;
			button->buttonOnClickFunction = actionName;
			button->color = {0.06f, 0.20f, 0.32f};
			button->buttonHoverColor = {0.10f, 0.42f, 0.65f};
			button->buttonPressedColor = {0.03f, 0.62f, 0.82f};
		}

		return buttonGameObjectId;
	}

	int32_t CreateProjectileTemplate(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		const Vector3& color,
		float scale,
		bool usesHoming,
		int32_t targetGameObjectId,
		int32_t selectorGameObjectId,
		int32_t parentGameObjectId) {
		const int32_t projectileGameObjectId = CreateModel(
			editorScene,
			gameObjectName,
			"resources/editorDefault/en.fbx",
			{0.0f, -100.0f, 0.0f},
			{scale, scale, scale},
			color,
			parentGameObjectId);
		EditorGameObject* projectileGameObject = editorScene.FindGameObject(projectileGameObjectId);
		EditorComponent* collider = AddComponent(
			editorScene,
			projectileGameObjectId,
			EditorComponentType::SphereCollider);

		if (projectileGameObject != nullptr) {
			projectileGameObject->isActive = false;
		}

		if (collider != nullptr) {
			collider->colliderRadius = scale * 0.7f;
		}

		if (usesHoming) {
			EditorComponent* steering = AddComponent(
				editorScene,
				projectileGameObjectId,
				EditorComponentType::TargetSteering);

			if (steering != nullptr) {
				steering->targetSteeringTargetGameObjectId = targetGameObjectId;
				steering->targetSteeringSelectorGameObjectId = selectorGameObjectId;
				steering->targetSteeringTurnSpeed = 95.0f;
				steering->targetSteeringAcceleration = 45.0f;
				steering->targetSteeringMaximumSpeed = 95.0f;
				steering->targetSteeringStartDelay = 0.15f;
				steering->targetSteeringPredictionSeconds = 0.22f;
				steering->targetSteeringMode = 0;
			}
		}

		return projectileGameObjectId;
	}

	void ConfigureExplosiveProjectile(
		EditorScene& editorScene,
		int32_t projectileGameObjectId,
		float explosionRadius,
		float explosionDamage,
		float explosionImpulse,
		bool detonateOnProximity,
		float proximityRadius) {
		EditorComponent* areaDamage = AddComponent(
			editorScene,
			projectileGameObjectId,
			EditorComponentType::AreaDamage);
		EditorComponent* detonator = AddComponent(
			editorScene,
			projectileGameObjectId,
			EditorComponentType::ProjectileDetonator);

		if (areaDamage != nullptr) {
			areaDamage->areaDamageRadius = explosionRadius;
			areaDamage->areaDamageBaseDamage = explosionDamage;
			areaDamage->areaDamageMinimumMultiplier = 0.1f;
			areaDamage->areaDamageImpulse = explosionImpulse;
			areaDamage->areaDamageFalloffMode = 2;
			areaDamage->areaDamageTag = "Explosion";
			areaDamage->areaDamageIgnoreOwner = true;
			areaDamage->areaDamageOcclusionMode = 2;
			areaDamage->areaDamageBlockedMultiplier = 0.1f;
			areaDamage->areaDamageOcclusionSamplePoints = 3;
			areaDamage->areaDamageTeamRule = 1;
			areaDamage->areaDamageIgnoreNeutral = true;
			areaDamage->areaDamageTeamSourceGameObjectId = -1;
		}

		if (detonator != nullptr) {
			detonator->projectileDetonateOnContact = true;
			detonator->projectileDetonateOnProximity = detonateOnProximity;
			detonator->projectileDetonateOnLifetime = true;
			detonator->projectileDetonatorTargetGameObjectId = -1;
			detonator->projectileDetonatorProximityRadius = proximityRadius;
			detonator->projectileDetonatorAreaDamageGameObjectId = -1;
			detonator->projectileDetonatorActionTargetGameObjectId = -1;
			detonator->projectileDetonatedActionName.clear();
		}
	}

	int32_t CreateObjectPool(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		int32_t templateGameObjectId,
		int32_t initialSize,
		int32_t parentGameObjectId) {
		const int32_t poolGameObjectId = CreateGameObject(
			editorScene,
			gameObjectName,
			parentGameObjectId);
		EditorComponent* objectPool = AddComponent(
			editorScene,
			poolGameObjectId,
			EditorComponentType::ObjectPool);

		if (objectPool != nullptr) {
			objectPool->objectPoolTemplateGameObjectId = templateGameObjectId;
			objectPool->objectPoolInitialSize = initialSize;
			objectPool->objectPoolAllowExpand = false;
		}

		return poolGameObjectId;
	}

	void ConfigureProjectileWeapon(
		EditorScene& editorScene,
		int32_t weaponGameObjectId,
		int32_t aimGameObjectId,
		int32_t poolGameObjectId,
		int32_t spawnPointGameObjectId,
		float speed,
		float damage,
		float interval,
		float radius,
		float lifetime,
		int32_t aimMode) {
		EditorComponent* projectile = AddComponent(
			editorScene,
			weaponGameObjectId,
			EditorComponentType::ProjectileEmitter);

		if (projectile != nullptr) {
			projectile->projectileAimGameObjectId = aimGameObjectId;
			projectile->projectileInputGameObjectId = -1;
			projectile->projectilePoolGameObjectId = poolGameObjectId;
			projectile->projectileSpawnPointGameObjectId = spawnPointGameObjectId;
			projectile->projectileSpeed = speed;
			projectile->projectileDamage = damage;
			projectile->projectileDamageTag = "Projectile";
			projectile->projectileRadius = radius;
			projectile->projectileLifetime = lifetime;
			projectile->projectileInterval = interval;
			projectile->projectileAutomatic = false;
			projectile->projectileOceanCollision = true;
			projectile->projectileAimMode = aimMode;
			projectile->projectileInheritSourceVelocity = true;
			projectile->projectileSourceVelocityGameObjectId = -1;
			projectile->projectileUseParentRigidBody = true;
			projectile->projectileLinearVelocityInheritance = 1.0f;
			projectile->projectileAngularVelocityInheritance = 0.35f;
		}

		ConfigureAttackFilter(editorScene, weaponGameObjectId);
	}

	int32_t CreateEnemyTemplate(
		EditorScene& editorScene,
		const std::string& gameObjectName,
		const Vector3& color,
		float maximumHealth,
		const std::string& deathActionName,
		int32_t stageControllerGameObjectId,
		int32_t playerGameObjectId,
		int32_t oceanGameObjectId,
		int32_t playerRailGameObjectId,
		int32_t projectilePoolGameObjectId,
		float projectileDamage,
		float fireInterval,
		bool usesMissile,
		int32_t parentGameObjectId) {
		const int32_t enemyGameObjectId = CreateModel(
			editorScene,
			gameObjectName,
			"resources/editorDefault/box.fbx",
			{0.0f, -100.0f, 0.0f},
			usesMissile ? Vector3{2.6f, 1.0f, 5.4f} : Vector3{2.2f, 0.85f, 4.2f},
			color,
			parentGameObjectId);
		EditorGameObject* enemyGameObject = editorScene.FindGameObject(enemyGameObjectId);
		EditorComponent* collider = AddComponent(
			editorScene,
			enemyGameObjectId,
			EditorComponentType::BoxCollider);
		EditorComponent* rigidBody = AddComponent(
			editorScene,
			enemyGameObjectId,
			EditorComponentType::RigidBody);
		EditorComponent* buoyancy = AddComponent(
			editorScene,
			enemyGameObjectId,
			EditorComponentType::Buoyancy);
		EditorComponent* railMovement = AddComponent(
			editorScene,
			enemyGameObjectId,
			EditorComponentType::RailMovement);

		if (enemyGameObject != nullptr) {
			enemyGameObject->isActive = false;
		}

		if (collider != nullptr) {
			collider->colliderSize = usesMissile
				? Vector3{5.2f, 2.0f, 10.8f}
				: Vector3{4.4f, 1.7f, 8.4f};
		}

		if (rigidBody != nullptr) {
			rigidBody->mass = usesMissile ? 260.0f : 140.0f;
			rigidBody->drag = 0.08f;
			rigidBody->angularDrag = 0.3f;
			rigidBody->useGravity = true;
			rigidBody->isKinematic = false;
			rigidBody->interpolationMode = 1;
			rigidBody->physicsLayer = 1;
		}

		if (buoyancy != nullptr) {
			buoyancy->buoyancyOceanGameObjectId = oceanGameObjectId;
			buoyancy->buoyancyHullSize = usesMissile
				? Vector3{2.6f, 1.3f, 5.6f}
				: Vector3{2.2f, 1.0f, 4.4f};
			buoyancy->buoyancyWaterDensity = 1025.0f;
			buoyancy->buoyancyTargetSubmersionRatio = 0.46f;
			buoyancy->buoyancyWaterDrag = 1.4f;
			buoyancy->buoyancyAngularDrag = 1.1f;
			buoyancy->buoyancyNormalInfluence = 0.15f;
			buoyancy->buoyancyLateralDrag = 3.6f;
			buoyancy->buoyancyVerticalDrag = 2.4f;
			buoyancy->buoyancySlammingStrength = 1.0f;
		}

		if (railMovement != nullptr) {
			// 敵も同じ航路に乗せ、プレイヤーよりやや遅い速度で並走/追い越される配置にする。
			// レール開始位置は WaveSpawner の waveSpawnRailStartNormalized で上書きされる。
			railMovement->railPathGameObjectId = playerRailGameObjectId;
			railMovement->railSpeed = 26.0f;
			railMovement->railAcceleration = 10.0f;
			railMovement->railDeceleration = 12.0f;
			railMovement->railStartNormalized = 0.0f;
			railMovement->railLookAheadDistance = 14.0f;
			railMovement->railLoop = false;
			railMovement->railOrientToPath = true;
			railMovement->railUseSmoothCurve = true;
			railMovement->railStopAtEnd = false;
			railMovement->railMovementMode = 1;
			railMovement->railPositionInfluence = {1.0f, 0.0f, 1.0f};
			railMovement->railRotationInfluence = {0.0f, 1.0f, 0.0f};
			railMovement->railPositionSpring = 8.0f;
			railMovement->railPositionDamping = 5.0f;
			railMovement->railMaximumAcceleration = 60.0f;
			railMovement->railRotationSpring = 4.5f;
			railMovement->railRotationDamping = 3.0f;
			railMovement->railMaximumAngularAcceleration = 6.0f;
		}

		AddDamageable(
			editorScene,
			enemyGameObjectId,
			maximumHealth,
			stageControllerGameObjectId,
			deathActionName,
			true);
		AddTeamAndTarget(
			editorScene,
			enemyGameObjectId,
			1,
			usesMissile ? 3.5f : 2.5f,
			{0.0f, 1.0f, 0.0f});
		ConfigureTargetSelector(
			editorScene,
			enemyGameObjectId,
			-1,
			420.0f);
		ConfigureSimulationLod(
			editorScene,
			enemyGameObjectId,
			playerGameObjectId,
			260.0f,
			650.0f,
			1200.0f,
			false);
		AddScript(editorScene, enemyGameObjectId);

		EditorComponent* timer = AddComponent(
			editorScene,
			enemyGameObjectId,
			EditorComponentType::Timer);

		if (timer != nullptr) {
			timer->timerDuration = fireInterval;
			timer->timerRepeat = true;
			timer->timerPlayOnStart = true;
			timer->timerActionTargetGameObjectId = -1;
			timer->timerActionName = "OnEnemyFire";
		}

		ConfigureProjectileWeapon(
			editorScene,
			enemyGameObjectId,
			-1,
			projectilePoolGameObjectId,
			-1,
			usesMissile ? 48.0f : 85.0f,
			projectileDamage,
			fireInterval,
			usesMissile ? 0.35f : 0.12f,
			usesMissile ? 9.0f : 4.0f,
			2);

		(void)playerGameObjectId;
		return enemyGameObjectId;
	}

	int32_t CreateWave(
		EditorScene& editorScene,
		const std::string& waveName,
		const Vector3& spawnPosition,
		int32_t poolGameObjectId,
		int32_t spawnCount,
		int32_t formationPattern,
		float spacing,
		float railStartNormalized,
		int32_t stageControllerGameObjectId,
		int32_t parentGameObjectId) {
		const int32_t waveGameObjectId = CreateGameObject(
			editorScene,
			waveName,
			parentGameObjectId);
		EditorGameObject* waveGameObject = editorScene.FindGameObject(waveGameObjectId);
		EditorComponent* waveSpawner = AddComponent(
			editorScene,
			waveGameObjectId,
			EditorComponentType::WaveSpawner);

		if (waveGameObject != nullptr) {
			waveGameObject->translate = spawnPosition;
		}

		if (waveSpawner != nullptr) {
			waveSpawner->waveTriggerMode = 2;
			waveSpawner->waveTriggerSourceGameObjectId = -1;
			waveSpawner->waveSpawnInterval = 0.18f;
			waveSpawner->waveSpawnSourceMode = 0;
			waveSpawner->wavePoolGameObjectId = poolGameObjectId;
			waveSpawner->waveSpawnPointGameObjectId = waveGameObjectId;
			waveSpawner->waveSpawnCount = spawnCount;
			waveSpawner->waveFormationPattern = formationPattern;
			waveSpawner->waveFormationSpacing = spacing;
			waveSpawner->waveFormationColumns = 4;
			waveSpawner->waveCompletionMode = 1;
			waveSpawner->waveSpawnRailStartNormalized = railStartNormalized;
			waveSpawner->waveActionTargetGameObjectId = stageControllerGameObjectId;
		}

		return waveGameObjectId;
	}

	void AddWeaponSlot(
		EditorScene& editorScene,
		int32_t loadoutGameObjectId,
		const std::string& slotName,
		int32_t weaponGameObjectId,
		int32_t visualGameObjectId,
		int32_t currentAmmo,
		int32_t reserveAmmo,
		int32_t maximumAmmo,
		float reloadSeconds) {
		const int32_t slotGameObjectId = CreateGameObject(
			editorScene,
			slotName + " Slot",
			loadoutGameObjectId);
		EditorComponent* slot = AddComponent(
			editorScene,
			slotGameObjectId,
			EditorComponentType::WeaponLoadoutSlot);

		if (slot != nullptr) {
			slot->weaponSlotName = slotName;
			slot->weaponSlotWeaponGameObjectId = weaponGameObjectId;
			slot->weaponSlotVisualGameObjectId = visualGameObjectId;
			slot->weaponSlotCurrentAmmo = currentAmmo;
			slot->weaponSlotReserveAmmo = reserveAmmo;
			slot->weaponSlotMaximumAmmo = maximumAmmo;
			slot->weaponSlotReloadSeconds = reloadSeconds;
			slot->weaponSlotAutoReload = true;
		}
	}

	void ConfigureBossPart(
		EditorScene& editorScene,
		int32_t partGameObjectId,
		float maximumHealth,
		float damageMultiplier,
		int32_t stageControllerGameObjectId,
		const std::string& deathActionName) {
		EditorComponent* collider = AddComponent(
			editorScene,
			partGameObjectId,
			EditorComponentType::BoxCollider);
		EditorComponent* hitZone = AddComponent(
			editorScene,
			partGameObjectId,
			EditorComponentType::HitZone);
		EditorComponent* destructible = AddComponent(
			editorScene,
			partGameObjectId,
			EditorComponentType::DestructiblePart);

		if (collider != nullptr) {
			collider->colliderSize = {2.0f, 2.0f, 2.0f};
		}

		AddDamageable(
			editorScene,
			partGameObjectId,
			maximumHealth,
			stageControllerGameObjectId,
			deathActionName,
			false);
		AddTeamAndTarget(editorScene, partGameObjectId, 1, 3.0f);

		if (hitZone != nullptr) {
			hitZone->hitZoneHealthGameObjectId = partGameObjectId;
			hitZone->hitZoneDamageMultiplier = damageMultiplier;
		}

		if (destructible != nullptr) {
			destructible->destructibleHealthGameObjectId = partGameObjectId;
			destructible->destructibleDisableChildren = true;
			destructible->destructibleActionTargetGameObjectId = stageControllerGameObjectId;
			destructible->destructibleDestroyedActionName.clear();
		}
	}

	bool HasRequiredObjects(const EditorScene& editorScene) {
		const std::array<const char*, 13> requiredNames = {
			"StageController",
			"Ocean",
			"Main Camera",
			"PlayerShip",
			"Player Rail",
			"SmallBoat Template",
			"MissileBoat Template",
			"Battle A Encounter",
			"Battle B Encounter",
			"Checkpoint 1",
			"Checkpoint 2",
			"BossShip",
			"SALVAGE Counter",
		};

		for (const char* requiredName : requiredNames) {
			bool wasFound = false;

			for (const EditorGameObject& gameObject : editorScene.GetGameObjects()) {
				if (gameObject.name == requiredName) {
					wasFound = true;
					break;
				}
			}

			if (!wasFound) {
				return false;
			}
		}

		return true;
	}
}

bool EditorWaterRailShooterSceneBuilder::Generate(std::string& resultMessage) {
	std::error_code directoryError;
	std::filesystem::create_directories("Assets/Scenes", directoryError);
	std::filesystem::create_directories("Assets/Prefabs", directoryError);

	if (directoryError) {
		resultMessage = "WaterRailShooter0817: Assets folder creation failed";
		return false;
	}

	EditorScene editorScene;
	EditorPhysicsSettings& physicsSettings = editorScene.GetPhysicsSettings();
	physicsSettings.gravity = {0.0f, -9.80665f, 0.0f};
	physicsSettings.fixedTimeStep = 1.0f / 60.0f;
	physicsSettings.collisionStepCount = 2;

	//================================================================
	// Scene root / Stage state
	//================================================================

	const int32_t systemsRootGameObjectId = CreateGameObject(editorScene, "SYSTEMS");
	const int32_t environmentRootGameObjectId = CreateGameObject(editorScene, "ENVIRONMENT");
	const int32_t gameplayRootGameObjectId = CreateGameObject(editorScene, "GAMEPLAY");
	const int32_t enemiesRootGameObjectId = CreateGameObject(editorScene, "ENEMIES");
	const int32_t uiRootGameObjectId = CreateGameObject(editorScene, "UI");
	const int32_t stageControllerGameObjectId = CreateGameObject(
		editorScene,
		"StageController",
		systemsRootGameObjectId);
	AddScript(editorScene, stageControllerGameObjectId);
	EditorComponent* stateMachine = AddComponent(
		editorScene,
		stageControllerGameObjectId,
		EditorComponentType::GenericStateMachine);
	EditorComponent* objectives = AddComponent(
		editorScene,
		stageControllerGameObjectId,
		EditorComponentType::ObjectiveTracker);

	if (stateMachine != nullptr) {
		stateMachine->stateMachineInitialState = "BattleA";
		stateMachine->stateMachineCurrentState = "BattleA";
	}

	if (objectives != nullptr) {
		objectives->objectiveEntries = {
			{"BattleA", "小型艇部隊を突破", 1, 0.0f, 15.0f},
			{"BattleB", "ミサイル艇混成部隊を突破", 0, 0.0f, 10.0f},
			{"Boss", "大型艦を撃沈", 0, 0.0f, 1.0f},
		};
		objectives->objectiveActionTargetGameObjectId = stageControllerGameObjectId;
		objectives->objectiveChangedActionName = "OnObjectiveChanged";
	}

	const int32_t salvageCounterGameObjectId = CreateGameObject(
		editorScene,
		"SALVAGE Counter",
		systemsRootGameObjectId);
	EditorComponent* salvageCounter = AddComponent(
		editorScene,
		salvageCounterGameObjectId,
		EditorComponentType::GenericCounter);
	AddComponent(editorScene, salvageCounterGameObjectId, EditorComponentType::Saveable);

	if (salvageCounter != nullptr) {
		salvageCounter->counterName = "SALVAGE";
		salvageCounter->counterInitialValue = 0.0f;
		salvageCounter->counterCurrentValue = 0.0f;
		salvageCounter->counterMinimumValue = 0.0f;
		salvageCounter->counterMaximumValue = 99999.0f;
	}

	//================================================================
	// Environment / Ocean / Post Process / Sun
	//================================================================

	const int32_t environmentGameObjectId = CreateGameObject(
		editorScene,
		"Environment Light",
		environmentRootGameObjectId);
	EditorComponent* environment = AddComponent(
		editorScene,
		environmentGameObjectId,
		EditorComponentType::Environment);

	if (environment != nullptr) {
		environment->color = {0.18f, 0.48f, 0.78f};
		environment->skyLowerColor = {0.06f, 0.20f, 0.30f};
		environment->intensity = 1.1f;
		environment->metallic = 0.32f;
		environment->reflectionStrength = 1.0f;
	}

	const int32_t oceanGameObjectId = CreateGameObject(
		editorScene,
		"Ocean",
		environmentRootGameObjectId);
	EditorComponent* ocean = AddComponent(
		editorScene,
		oceanGameObjectId,
		EditorComponentType::Ocean);

	if (ocean != nullptr) {
		// FFTの解像度・計算領域・波高を別の意味として調整する。
		// 解像度や波高を上限へ寄せても海らしさは増えず、周期の粗い平面に見えるため使用しない。
		ocean->oceanGridResolution = 512;
		ocean->oceanSize = 360.0f;
		ocean->oceanWaveHeight = 1.55f;
		ocean->oceanMaxWaveHeight = 5.2f;
		ocean->oceanWaveLength = 30.0f;
		ocean->oceanWaveSpeed = 1.2f;
		ocean->oceanTimeScale = 1.0f;
		ocean->oceanChoppiness = 0.64f;
		ocean->oceanWindSpeed = 16.0f;
		ocean->oceanWaterDepth = 80.0f;
		ocean->oceanDirectionSpread = 0.48f;
		ocean->oceanSwellStrength = 0.72f;
		ocean->oceanCrestSharpness = 0.34f;
		ocean->oceanRoughness = 0.12f;
		ocean->oceanReflectionStrength = 0.82f;
		ocean->oceanDetailNormalStrength = 0.3f;
		ocean->oceanAbsorptionDistance = 30.0f;
		ocean->oceanRefractionDistortion = 0.032f;
		// 深い色を明るくし、波の起伏が暗転で潰れないようにする。
		ocean->oceanShallowColor = {0.05f, 0.42f, 0.50f};
		ocean->oceanDeepColor = {0.012f, 0.14f, 0.26f};
		ocean->oceanFoamStrength = 0.48f;
		ocean->oceanFoamThreshold = 0.55f;
	}

	const int32_t sunGameObjectId = CreateGameObject(
		editorScene,
		"Sun",
		environmentRootGameObjectId);
	EditorGameObject* sunGameObject = editorScene.FindGameObject(sunGameObjectId);
	EditorComponent* sun = AddComponent(
		editorScene,
		sunGameObjectId,
		EditorComponentType::Light);

	if (sunGameObject != nullptr) {
		sunGameObject->translate = {0.0f, 80.0f, -120.0f};
		sunGameObject->rotate = {0.72f, -0.48f, 0.0f};
	}

	if (sun != nullptr) {
		// Light種別はRendererが認識する"Sun"を指定する。未対応文字列はPoint Light扱いになる。
		sun->assetPath = "Sun";
		sun->color = {1.0f, 0.91f, 0.76f};
		sun->intensity = 3.2f;
	}

	const int32_t postProcessGameObjectId = CreateGameObject(
		editorScene,
		"Post Process",
		environmentRootGameObjectId);
	EditorComponent* postProcess = AddComponent(
		editorScene,
		postProcessGameObjectId,
		EditorComponentType::PostProcess);

	if (postProcess != nullptr) {
		postProcess->aaMode = 2;
		postProcess->smaaEnabled = true;
		postProcess->taaEnabled = false;
		postProcess->smaaThreshold = 0.08f;
		postProcess->smaaCornerRounding = 25.0f;
		postProcess->ssrEnabled = true;
		postProcess->bloomIntensity = 0.42f;
		postProcess->bloomThreshold = 1.05f;
		postProcess->bloomSoftKnee = 0.45f;
		postProcess->bloomScatter = 0.62f;
		postProcess->glareIntensity = 0.45f;
		postProcess->compositeToneMappingMode = 4;
		postProcess->compositeExposure = 1.12f;
		postProcess->compositeBloomIntensity = 0.18f;
		postProcess->compositeSaturation = 1.03f;
		postProcess->compositeContrast = 1.04f;
		postProcess->compositeVignetteStrength = 0.0f;
		postProcess->compositeVignetteRadius = 0.98f;
		postProcess->compositeFilmGrain = 0.01f;
		postProcess->compositeChromaticAberration = 0.005f;
		postProcess->compositeAutoExposureEnabled = true;
		postProcess->compositeMinimumExposure = 0.95f;
		postProcess->compositeMaximumExposure = 1.35f;
		postProcess->compositeExposureAdaptationSpeed = 2.0f;
		postProcess->compositeTargetLuminance = 0.20f;
		postProcess->compositeLocalContrast = 0.16f;
		postProcess->compositeOutputDither = 0.70f;
		postProcess->compositeSsgiEnabled = true;
		postProcess->compositeSsgiIntensity = 0.15f;
		postProcess->compositeSsgiRadiusPixels = 14.0f;
	}

	//================================================================
	// Rail / Player / Camera
	//================================================================

	const int32_t playerRailGameObjectId = CreateGameObject(
		editorScene,
		"Player Rail",
		gameplayRootGameObjectId);
	// 約4kmの航路を用意し、Camera Far Clipより十分長いゲーム空間にする。
	// 緩い蛇行を入れて水平線と近景の相対移動を継続的に見せる。
	const std::array<Vector3, 12> railPoints = {
		Vector3{0.0f, 1.4f, 0.0f},
		Vector3{-38.0f, 1.4f, 360.0f},
		Vector3{72.0f, 1.4f, 720.0f},
		Vector3{-92.0f, 1.4f, 1080.0f},
		Vector3{118.0f, 1.4f, 1440.0f},
		Vector3{-148.0f, 1.4f, 1800.0f},
		Vector3{172.0f, 1.4f, 2160.0f},
		Vector3{-126.0f, 1.4f, 2520.0f},
		Vector3{152.0f, 1.4f, 2880.0f},
		Vector3{-82.0f, 1.4f, 3240.0f},
		Vector3{88.0f, 1.4f, 3600.0f},
		Vector3{0.0f, 1.4f, 4040.0f},
	};

	for (int32_t pointIndex = 0; pointIndex < static_cast<int32_t>(railPoints.size()); pointIndex++) {
		const int32_t pointGameObjectId = CreateGameObject(
			editorScene,
			"Rail Point " + std::to_string(pointIndex),
			playerRailGameObjectId);
		EditorGameObject* pointGameObject = editorScene.FindGameObject(pointGameObjectId);

		if (pointGameObject != nullptr) {
			pointGameObject->translate = railPoints[static_cast<size_t>(pointIndex)];
		}
	}

	// 海だけでは速度を比較する基準がないため、航路沿いへ軽量な近景目標を置く。
	// ColliderやScriptは付けず、描画距離・視差・速度感だけを担当させる。
	const int32_t routeMarkersGameObjectId = CreateGameObject(
		editorScene,
		"Route Markers",
		environmentRootGameObjectId);

	for (int32_t markerIndex = 0; markerIndex < 20; markerIndex++) {
		const float markerDistance = 180.0f + static_cast<float>(markerIndex) * 190.0f;
		const bool isLeftMarker = markerIndex % 2 == 0;
		const float markerSide = isLeftMarker ? -42.0f : 42.0f;
		const Vector3 markerColor = isLeftMarker
			? Vector3{0.9f, 0.18f, 0.08f}
			: Vector3{0.95f, 0.72f, 0.08f};
		CreateModel(
			editorScene,
			"Route Marker " + std::to_string(markerIndex),
			"resources/editorDefault/cone.fbx",
			{markerSide, 0.9f, markerDistance},
			{1.2f, 3.8f, 1.2f},
			markerColor,
			routeMarkersGameObjectId);
	}

	const int32_t playerGameObjectId = CreateModel(
		editorScene,
		"PlayerShip",
		"resources/model/Ship/Player.fbx",
		{0.0f, 1.4f, 0.0f},
		{1.0f, 1.0f, 1.0f},
		{0.58f, 0.66f, 0.72f},
		gameplayRootGameObjectId);
	EditorComponent* playerRigidBody = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::RigidBody);
	EditorComponent* playerCollider = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::MeshCollider);
	EditorComponent* buoyancy = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::Buoyancy);
	EditorComponent* railMovement = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::RailMovement);
	EditorComponent* playerInput = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::PlayerInput);
	EditorComponent* screenAim = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::ScreenAim);
	EditorComponent* loadout = AddComponent(
		editorScene,
		playerGameObjectId,
		EditorComponentType::WeaponLoadout);
	AddScript(editorScene, playerGameObjectId);
	AddComponent(editorScene, playerGameObjectId, EditorComponentType::Saveable);
	AddDamageable(
		editorScene,
		playerGameObjectId,
		300.0f,
		stageControllerGameObjectId,
		"OnPlayerDestroyed",
		false);
	AddTeamAndTarget(editorScene, playerGameObjectId, 0, 3.5f, {0.0f, 1.4f, 0.0f});

	if (playerRigidBody != nullptr) {
		playerRigidBody->automaticMassFromCollider = true;
		playerRigidBody->bodyDensity = 165.0f;
		playerRigidBody->mass = 950.0f;
		playerRigidBody->drag = 0.04f;
		playerRigidBody->angularDrag = 0.24f;
		playerRigidBody->inertiaMultiplier = 1.35f;
		playerRigidBody->useGravity = true;
		playerRigidBody->isKinematic = false;
		playerRigidBody->interpolationMode = 1;
		playerRigidBody->collisionDetectionMode = 1;
		playerRigidBody->physicsLayer = 1;
	}

	if (playerCollider != nullptr) {
		playerCollider->assetPath = "resources/model/Ship/Player.fbx";
		playerCollider->autoConvexMaximumHulls = 16;
	}

	if (buoyancy != nullptr) {
		buoyancy->buoyancyOceanGameObjectId = oceanGameObjectId;
		buoyancy->buoyancyHullSize = {4.2f, 2.0f, 10.0f};
		buoyancy->buoyancyAutomaticPhysicalProperties = true;
		buoyancy->buoyancyWaterDensity = 1025.0f;
		buoyancy->buoyancyTargetSubmersionRatio = 0.48f;
		buoyancy->buoyancyWaterDrag = 0.82f;
		buoyancy->buoyancyAngularDrag = 0.7f;
		buoyancy->buoyancyNormalInfluence = 0.18f;
		buoyancy->buoyancyLateralDrag = 3.4f;
		buoyancy->buoyancyVerticalDrag = 2.2f;
		buoyancy->buoyancySlammingStrength = 1.15f;
	}

	if (railMovement != nullptr) {
		railMovement->railPathGameObjectId = playerRailGameObjectId;
		railMovement->railSpeed = 30.0f;
		railMovement->railAcceleration = 8.0f;
		railMovement->railDeceleration = 10.0f;
		railMovement->railLookAheadDistance = 22.0f;
		railMovement->railLoop = false;
		railMovement->railOrientToPath = true;
		railMovement->railUseSmoothCurve = true;
		railMovement->railStopAtEnd = true;
		railMovement->railMovementMode = 1;
		railMovement->railPositionInfluence = {1.0f, 0.0f, 1.0f};
		railMovement->railRotationInfluence = {0.0f, 1.0f, 0.0f};
		railMovement->railPositionSpring = 5.5f;
		railMovement->railPositionDamping = 4.2f;
		railMovement->railMaximumAcceleration = 42.0f;
		railMovement->railRotationSpring = 3.5f;
		railMovement->railRotationDamping = 2.8f;
		railMovement->railMaximumAngularAcceleration = 5.5f;
		railMovement->railLocalForwardAxis = 0;
		railMovement->railShipHorizontalThrust = true;
		railMovement->railShipLateralAssist = 0.22f;
		railMovement->railMovementRange = {8.0f, 2.0f};
		railMovement->railOffsetMoveSpeed = 9.0f;
		railMovement->railUsePlayerInput = true;
		railMovement->railInputActionMapName = "Player";
		railMovement->railInputActionName = "Move";
	}

	if (playerInput != nullptr) {
		playerInput->assetPath = kInputActionsPath;
		playerInput->inputActionMapName = "Player";
		playerInput->inputEventBindings = {
			{"Player", "Fire", "OnPlayerFire", 0},
			{"Player", "Reload", "OnPlayerReload", 0},
			{"Player", "NextWeapon", "OnNextWeapon", 0},
		};
	}

	if (screenAim != nullptr) {
		screenAim->screenAimInputGameObjectId = playerGameObjectId;
		screenAim->screenAimInputMode = 0;
		screenAim->screenAimNormalizedPosition = {0.5f, 0.5f};
		screenAim->screenAimClamp = true;
	}

	ConfigureTargetSelector(
		editorScene,
		playerGameObjectId,
		playerGameObjectId,
		900.0f);

	if (loadout != nullptr) {
		loadout->weaponLoadoutSelectedSlotIndex = 0;
		loadout->weaponLoadoutActionTargetGameObjectId = stageControllerGameObjectId;
		loadout->weaponLoadoutChangedActionName = "OnLoadoutChanged";
	}

	const int32_t cameraGameObjectId = CreateGameObject(
		editorScene,
		"Main Camera",
		gameplayRootGameObjectId);
	EditorGameObject* cameraGameObject = editorScene.FindGameObject(cameraGameObjectId);
	EditorComponent* camera = AddComponent(
		editorScene,
		cameraGameObjectId,
		EditorComponentType::Camera);
	EditorComponent* horizonStabilizer = AddComponent(
		editorScene,
		cameraGameObjectId,
		EditorComponentType::CameraHorizonStabilizer);

	if (cameraGameObject != nullptr) {
		cameraGameObject->translate = {0.0f, 6.2f, -14.0f};
		cameraGameObject->rotate = {0.1f, 0.0f, 0.0f};
	}

	if (camera != nullptr) {
		camera->connectedGameObjectId = playerGameObjectId;
		camera->cameraFieldOfView = 72.0f;
		camera->cameraNearClip = 0.15f;
		camera->cameraFarClip = 1400.0f;
		camera->cameraPriority = 20;
		camera->cameraFollowPositionSpace = 1;
		camera->cameraFollowRotationMode = 1;
		camera->cameraMotionBlurEnabled = true;
		camera->cameraMotionBlurIntensity = 0.14f;
	}

	if (horizonStabilizer != nullptr) {
		horizonStabilizer->connectedGameObjectId = playerGameObjectId;
	}

	//================================================================
	// Projectile pools / Player weapons / Visual loadout
	//================================================================

	const int32_t poolsRootGameObjectId = CreateGameObject(
		editorScene,
		"Projectile Pools",
		systemsRootGameObjectId);
	const int32_t playerRocketTemplateGameObjectId = CreateProjectileTemplate(
		editorScene,
		"Player Rocket Template",
		{1.0f, 0.52f, 0.08f},
		0.22f,
		false,
		-1,
		-1,
		poolsRootGameObjectId);
	const int32_t playerRocketPoolGameObjectId = CreateObjectPool(
		editorScene,
		"Player Rocket Pool",
		playerRocketTemplateGameObjectId,
		kPlayerRocketPoolSize,
		poolsRootGameObjectId);
	ConfigureExplosiveProjectile(
		editorScene,
		playerRocketTemplateGameObjectId,
		5.0f,
		42.0f,
		9.0f,
		false,
		0.0f);
	const int32_t playerMissileTemplateGameObjectId = CreateProjectileTemplate(
		editorScene,
		"Player Missile Template",
		{0.95f, 0.18f, 0.08f},
		0.28f,
		true,
		-1,
		playerGameObjectId,
		poolsRootGameObjectId);
	const int32_t playerMissilePoolGameObjectId = CreateObjectPool(
		editorScene,
		"Player Missile Pool",
		playerMissileTemplateGameObjectId,
		kPlayerMissilePoolSize,
		poolsRootGameObjectId);
	ConfigureExplosiveProjectile(
		editorScene,
		playerMissileTemplateGameObjectId,
		8.0f,
		95.0f,
		16.0f,
		true,
		2.5f);
	const int32_t enemyBulletTemplateGameObjectId = CreateProjectileTemplate(
		editorScene,
		"Enemy Bullet Template",
		{1.0f, 0.22f, 0.05f},
		0.12f,
		false,
		playerGameObjectId,
		-1,
		poolsRootGameObjectId);
	const int32_t enemyBulletPoolGameObjectId = CreateObjectPool(
		editorScene,
		"Enemy Bullet Pool",
		enemyBulletTemplateGameObjectId,
		kEnemyBulletPoolSize,
		poolsRootGameObjectId);
	const int32_t enemyMissileTemplateGameObjectId = CreateProjectileTemplate(
		editorScene,
		"Enemy Missile Template",
		{1.0f, 0.05f, 0.04f},
		0.3f,
		true,
		playerGameObjectId,
		-1,
		poolsRootGameObjectId);
	const int32_t enemyMissilePoolGameObjectId = CreateObjectPool(
		editorScene,
		"Enemy Missile Pool",
		enemyMissileTemplateGameObjectId,
		kEnemyMissilePoolSize,
		poolsRootGameObjectId);
	ConfigureExplosiveProjectile(
		editorScene,
		enemyMissileTemplateGameObjectId,
		4.5f,
		20.0f,
		8.0f,
		true,
		1.6f);
	AddDamageable(
		editorScene,
		enemyMissileTemplateGameObjectId,
		18.0f,
		-1,
		"",
		true);
	AddTeamAndTarget(
		editorScene,
		enemyMissileTemplateGameObjectId,
		1,
		0.9f);

	const int32_t weaponsRootGameObjectId = CreateGameObject(
		editorScene,
		"Player Weapons",
		playerGameObjectId);
	const int32_t weapon20GameObjectId = CreateGameObject(
		editorScene,
		"Weapon 20mm",
		weaponsRootGameObjectId);
	EditorComponent* weapon20 = AddComponent(
		editorScene,
		weapon20GameObjectId,
		EditorComponentType::HitscanWeapon);
	ConfigureAttackFilter(editorScene, weapon20GameObjectId);

	if (weapon20 != nullptr) {
		weapon20->hitscanAimGameObjectId = playerGameObjectId;
		weapon20->hitscanRange = 650.0f;
		weapon20->hitscanDamage = 12.0f;
		weapon20->hitscanDamageTag = "20mm";
		weapon20->hitscanInterval = 0.09f;
		weapon20->hitscanAutomatic = true;
		weapon20->hitscanOceanCollision = true;
	}

	const int32_t weapon40GameObjectId = CreateGameObject(
		editorScene,
		"Weapon 40mm",
		weaponsRootGameObjectId);
	EditorComponent* weapon40 = AddComponent(
		editorScene,
		weapon40GameObjectId,
		EditorComponentType::HitscanWeapon);
	ConfigureAttackFilter(editorScene, weapon40GameObjectId);

	if (weapon40 != nullptr) {
		weapon40->hitscanAimGameObjectId = playerGameObjectId;
		weapon40->hitscanRange = 800.0f;
		weapon40->hitscanDamage = 28.0f;
		weapon40->hitscanDamageTag = "40mm";
		weapon40->hitscanInterval = 0.18f;
		weapon40->hitscanAutomatic = true;
		weapon40->hitscanOceanCollision = true;
	}

	const int32_t rocketWeaponGameObjectId = CreateGameObject(
		editorScene,
		"Weapon Rocket Pod",
		weaponsRootGameObjectId);
	ConfigureProjectileWeapon(
		editorScene,
		rocketWeaponGameObjectId,
		playerGameObjectId,
		playerRocketPoolGameObjectId,
		playerGameObjectId,
		92.0f,
		55.0f,
		0.42f,
		0.18f,
		6.0f,
		0);
	const int32_t missileWeaponGameObjectId = CreateGameObject(
		editorScene,
		"Weapon Anti-Ship Missile",
		weaponsRootGameObjectId);
	ConfigureTargetSelector(
		editorScene,
		missileWeaponGameObjectId,
		playerGameObjectId,
		900.0f);
	ConfigureProjectileWeapon(
		editorScene,
		missileWeaponGameObjectId,
		playerGameObjectId,
		playerMissilePoolGameObjectId,
		playerGameObjectId,
		72.0f,
		120.0f,
		1.1f,
		0.3f,
		10.0f,
		2);

	const int32_t visual20GameObjectId = CreateModel(
		editorScene,
		"Visual 20mm",
		"resources/editorDefault/cone.fbx",
		{0.0f, 1.9f, -1.4f},
		{0.25f, 0.25f, 0.75f},
		{0.32f, 0.38f, 0.42f},
		playerGameObjectId);
	const int32_t visual40GameObjectId = CreateModel(
		editorScene,
		"Visual 40mm",
		"resources/editorDefault/cone.fbx",
		{0.0f, 2.05f, -1.2f},
		{0.42f, 0.42f, 1.05f},
		{0.22f, 0.27f, 0.30f},
		playerGameObjectId);
	const int32_t visualRocketGameObjectId = CreateModel(
		editorScene,
		"Visual Rocket Pod",
		"resources/editorDefault/box.fbx",
		{-1.5f, 1.45f, 0.5f},
		{0.55f, 0.35f, 1.1f},
		{0.20f, 0.28f, 0.22f},
		playerGameObjectId);
	const int32_t visualMissileGameObjectId = CreateModel(
		editorScene,
		"Visual Anti-Ship Missile",
		"resources/editorDefault/en.fbx",
		{1.5f, 1.55f, 0.6f},
		{0.48f, 0.48f, 0.48f},
		{0.62f, 0.64f, 0.58f},
		playerGameObjectId);

	if (EditorGameObject* visual = editorScene.FindGameObject(visual40GameObjectId)) {
		visual->isActive = false;
	}

	if (EditorGameObject* visual = editorScene.FindGameObject(visualRocketGameObjectId)) {
		visual->isActive = false;
	}

	if (EditorGameObject* visual = editorScene.FindGameObject(visualMissileGameObjectId)) {
		visual->isActive = false;
	}

	AddWeaponSlot(editorScene, playerGameObjectId, "MAIN 20mm", weapon20GameObjectId, visual20GameObjectId, 90, 540, 90, 1.5f);
	AddWeaponSlot(editorScene, playerGameObjectId, "MAIN 40mm", weapon40GameObjectId, visual40GameObjectId, 30, 180, 30, 2.1f);
	AddWeaponSlot(editorScene, playerGameObjectId, "SECONDARY Rocket", rocketWeaponGameObjectId, visualRocketGameObjectId, 8, 32, 8, 2.8f);
	AddWeaponSlot(editorScene, playerGameObjectId, "MISSILE Anti-Ship", missileWeaponGameObjectId, visualMissileGameObjectId, 4, 12, 4, 4.0f);

	//================================================================
	// Enemy templates / Pools / Encounters
	//================================================================

	const int32_t enemyTemplatesRootGameObjectId = CreateGameObject(
		editorScene,
		"Enemy Templates",
		enemiesRootGameObjectId);
	const int32_t smallBoatTemplateGameObjectId = CreateEnemyTemplate(
		editorScene,
		"SmallBoat Template",
		{0.76f, 0.18f, 0.08f},
		60.0f,
		"OnSmallBoatDestroyed",
		stageControllerGameObjectId,
		playerGameObjectId,
		oceanGameObjectId,
		playerRailGameObjectId,
		enemyBulletPoolGameObjectId,
		7.0f,
		1.25f,
		false,
		enemyTemplatesRootGameObjectId);
	const int32_t missileBoatTemplateGameObjectId = CreateEnemyTemplate(
		editorScene,
		"MissileBoat Template",
		{0.52f, 0.08f, 0.12f},
		110.0f,
		"OnMissileBoatDestroyed",
		stageControllerGameObjectId,
		playerGameObjectId,
		oceanGameObjectId,
		playerRailGameObjectId,
		enemyMissilePoolGameObjectId,
		24.0f,
		3.4f,
		true,
		enemyTemplatesRootGameObjectId);
	editorScene.SavePrefab(smallBoatTemplateGameObjectId, kSmallBoatPrefabPath);
	editorScene.SavePrefab(missileBoatTemplateGameObjectId, kMissileBoatPrefabPath);

	const int32_t smallBoatPoolGameObjectId = CreateObjectPool(
		editorScene,
		"SmallBoat Pool",
		smallBoatTemplateGameObjectId,
		kSmallBoatPoolSize,
		enemiesRootGameObjectId);
	const int32_t missileBoatPoolGameObjectId = CreateObjectPool(
		editorScene,
		"MissileBoat Pool",
		missileBoatTemplateGameObjectId,
		kMissileBoatPoolSize,
		enemiesRootGameObjectId);
	const int32_t wavesRootGameObjectId = CreateGameObject(
		editorScene,
		"Waves",
		enemiesRootGameObjectId);
	const int32_t battleAWave1GameObjectId = CreateWave(
		editorScene,
		"Battle A - SmallBoat 10",
		{-17.0f, 1.2f, 430.0f},
		smallBoatPoolGameObjectId,
		10,
		2,
		7.0f,
		430.0f / 4762.0f,
		stageControllerGameObjectId,
		wavesRootGameObjectId);

	const int32_t battleAWave2GameObjectId = CreateWave(
		editorScene,
		"Battle A - SmallBoat 5",
		{-24.0f, 1.2f, 930.0f},
		smallBoatPoolGameObjectId,
		5,
		1,
		8.0f,
		930.0f / 4762.0f,
		stageControllerGameObjectId,
		wavesRootGameObjectId);

	const int32_t battleBWave1GameObjectId = CreateWave(
		editorScene,
		"Battle B - SmallBoat 6",
		{-45.0f, 1.2f, 1660.0f},
		smallBoatPoolGameObjectId,
		6,
		4,
		7.0f,
		1660.0f / 4762.0f,
		stageControllerGameObjectId,
		wavesRootGameObjectId);

	const int32_t battleBWave2GameObjectId = CreateWave(
		editorScene,
		"Battle B - MissileBoat 4",
		{92.0f, 1.2f, 2070.0f},
		missileBoatPoolGameObjectId,
		4,
		2,
		10.0f,
		2070.0f / 4762.0f,
		stageControllerGameObjectId,
		wavesRootGameObjectId);

	const int32_t battleAEncounterGameObjectId = CreateGameObject(
		editorScene,
		"Battle A Encounter",
		enemiesRootGameObjectId);
	EditorComponent* battleAEncounter = AddComponent(
		editorScene,
		battleAEncounterGameObjectId,
		EditorComponentType::EncounterController);

	if (battleAEncounter != nullptr) {
		battleAEncounter->encounterWaveEntries = {
			{battleAWave1GameObjectId, 1.0f, true},
			{battleAWave2GameObjectId, 1.5f, true},
		};
		battleAEncounter->encounterPlayOnStart = false;
		battleAEncounter->encounterActionTargetGameObjectId = stageControllerGameObjectId;
		battleAEncounter->encounterCompletedActionName = "OnBattleACompleted";
	}

	const int32_t battleBEncounterGameObjectId = CreateGameObject(
		editorScene,
		"Battle B Encounter",
		enemiesRootGameObjectId);
	EditorComponent* battleBEncounter = AddComponent(
		editorScene,
		battleBEncounterGameObjectId,
		EditorComponentType::EncounterController);

	if (battleBEncounter != nullptr) {
		battleBEncounter->encounterWaveEntries = {
			{battleBWave1GameObjectId, 0.4f, true},
			{battleBWave2GameObjectId, 1.0f, true},
		};
		battleBEncounter->encounterPlayOnStart = false;
		battleBEncounter->encounterActionTargetGameObjectId = stageControllerGameObjectId;
		battleBEncounter->encounterCompletedActionName = "OnBattleBCompleted";
	}

	//================================================================
	// Checkpoints / Boss and destructible parts
	//================================================================

	const int32_t checkpoint1GameObjectId = CreateGameObject(
		editorScene,
		"Checkpoint 1",
		gameplayRootGameObjectId);
	EditorComponent* checkpoint1 = AddComponent(
		editorScene,
		checkpoint1GameObjectId,
		EditorComponentType::Checkpoint);

	if (checkpoint1 != nullptr) {
		checkpoint1->checkpointSlotName = "water_rail_shooter_0817_cp1";
		checkpoint1->checkpointSaveOnStart = false;
		checkpoint1->checkpointLoadOnStart = false;
	}

	const int32_t checkpoint2GameObjectId = CreateGameObject(
		editorScene,
		"Checkpoint 2",
		gameplayRootGameObjectId);
	EditorComponent* checkpoint2 = AddComponent(
		editorScene,
		checkpoint2GameObjectId,
		EditorComponentType::Checkpoint);

	if (checkpoint2 != nullptr) {
		checkpoint2->checkpointSlotName = "water_rail_shooter_0817_cp2";
		checkpoint2->checkpointSaveOnStart = false;
		checkpoint2->checkpointLoadOnStart = false;
	}

	const int32_t bossGameObjectId = CreateModel(
		editorScene,
		"BossShip",
		"resources/editorDefault/box.fbx",
		{36.0f, 2.0f, 3190.0f},
		{8.0f, 2.0f, 18.0f},
		{0.18f, 0.22f, 0.25f},
		enemiesRootGameObjectId);
	EditorGameObject* bossGameObject = editorScene.FindGameObject(bossGameObjectId);
	EditorComponent* bossCollider = AddComponent(
		editorScene,
		bossGameObjectId,
		EditorComponentType::BoxCollider);
	EditorComponent* bossThreshold = AddComponent(
		editorScene,
		bossGameObjectId,
		EditorComponentType::ThresholdState);

	if (bossGameObject != nullptr) {
		bossGameObject->isActive = false;
	}

	if (bossCollider != nullptr) {
		bossCollider->colliderSize = {16.0f, 4.0f, 36.0f};
	}

	AddDamageable(
		editorScene,
		bossGameObjectId,
		1200.0f,
		stageControllerGameObjectId,
		"OnBossDestroyed",
		false);
	AddTeamAndTarget(editorScene, bossGameObjectId, 1, 9.0f, {0.0f, 2.0f, 0.0f});
	ConfigureSimulationLod(
		editorScene,
		bossGameObjectId,
		playerGameObjectId,
		600.0f,
		1200.0f,
		1900.0f,
		true);

	if (bossThreshold != nullptr) {
		bossThreshold->thresholdSourceMode = 0;
		bossThreshold->thresholdSourceGameObjectId = bossGameObjectId;
		bossThreshold->thresholdTargetGameObjectId = stageControllerGameObjectId;
		bossThreshold->thresholdSecondValue = 0.66f;
		bossThreshold->thresholdThirdValue = 0.33f;
		bossThreshold->thresholdFirstActionName = "OnBossPhase1";
		bossThreshold->thresholdSecondActionName = "OnBossPhase2";
		bossThreshold->thresholdThirdActionName = "OnBossPhase3";
	}

	const int32_t bossMainGunGameObjectId = CreateModel(
		editorScene,
		"Boss Main Gun",
		"resources/editorDefault/cone.fbx",
		{0.0f, 3.0f, -5.0f},
		{2.4f, 1.4f, 3.0f},
		{0.36f, 0.38f, 0.40f},
		bossGameObjectId);
	ConfigureBossPart(
		editorScene,
		bossMainGunGameObjectId,
		260.0f,
		1.25f,
		stageControllerGameObjectId,
		"OnBossMainGunDestroyed");
	AddScript(editorScene, bossMainGunGameObjectId);
	ConfigureTargetSelector(editorScene, bossMainGunGameObjectId, bossMainGunGameObjectId, 700.0f);
	ConfigureProjectileWeapon(
		editorScene,
		bossMainGunGameObjectId,
		bossMainGunGameObjectId,
		enemyBulletPoolGameObjectId,
		bossMainGunGameObjectId,
		105.0f,
		35.0f,
		1.6f,
		0.18f,
		5.0f,
		2);
	EditorComponent* bossGunTimer = AddComponent(
		editorScene,
		bossMainGunGameObjectId,
		EditorComponentType::Timer);

	if (bossGunTimer != nullptr) {
		bossGunTimer->timerDuration = 1.6f;
		bossGunTimer->timerRepeat = true;
		bossGunTimer->timerPlayOnStart = true;
		bossGunTimer->timerActionTargetGameObjectId = -1;
		bossGunTimer->timerActionName = "OnEnemyFire";
	}

	const int32_t bossMissileGameObjectId = CreateModel(
		editorScene,
		"Boss Missile Launcher",
		"resources/editorDefault/box.fbx",
		{-3.8f, 2.5f, 2.0f},
		{1.6f, 1.0f, 2.4f},
		{0.28f, 0.20f, 0.18f},
		bossGameObjectId);
	ConfigureBossPart(
		editorScene,
		bossMissileGameObjectId,
		220.0f,
		1.15f,
		stageControllerGameObjectId,
		"OnBossMissileDestroyed");
	AddScript(editorScene, bossMissileGameObjectId);
	ConfigureTargetSelector(editorScene, bossMissileGameObjectId, bossMissileGameObjectId, 850.0f);
	ConfigureProjectileWeapon(
		editorScene,
		bossMissileGameObjectId,
		bossMissileGameObjectId,
		enemyMissilePoolGameObjectId,
		bossMissileGameObjectId,
		54.0f,
		48.0f,
		3.2f,
		0.34f,
		11.0f,
		2);
	EditorComponent* bossMissileTimer = AddComponent(
		editorScene,
		bossMissileGameObjectId,
		EditorComponentType::Timer);

	if (bossMissileTimer != nullptr) {
		bossMissileTimer->timerDuration = 3.2f;
		bossMissileTimer->timerRepeat = true;
		bossMissileTimer->timerPlayOnStart = true;
		bossMissileTimer->timerActionTargetGameObjectId = -1;
		bossMissileTimer->timerActionName = "OnEnemyFire";
	}

	const int32_t bossEngineGameObjectId = CreateModel(
		editorScene,
		"Boss Engine",
		"resources/editorDefault/ICOCube.fbx",
		{0.0f, 1.2f, 8.0f},
		{2.4f, 1.5f, 2.7f},
		{0.62f, 0.18f, 0.05f},
		bossGameObjectId);
	ConfigureBossPart(
		editorScene,
		bossEngineGameObjectId,
		300.0f,
		1.5f,
		stageControllerGameObjectId,
		"OnBossEngineDestroyed");

	for (const int32_t bossPartGameObjectId : {
		bossMainGunGameObjectId,
		bossMissileGameObjectId,
		bossEngineGameObjectId}) {
		if (EditorGameObject* bossPart = editorScene.FindGameObject(bossPartGameObjectId)) {
			bossPart->isActive = false;
		}
	}

	//================================================================
	// HUD / Shop / Clear UI
	//================================================================

	const int32_t canvasGameObjectId = CreateGameObject(editorScene, "Canvas", uiRootGameObjectId);
	AddComponent(editorScene, canvasGameObjectId, EditorComponentType::Canvas);
	EditorComponent* canvasScaler = AddComponent(
		editorScene,
		canvasGameObjectId,
		EditorComponentType::CanvasScaler);

	if (canvasScaler != nullptr) {
		// UIは1280x720基準で配置しているため、その基準解像度で等倍表示させる。
		canvasScaler->buttonSize = {1280.0f, 720.0f};
		canvasScaler->sliderValue = 0.5f;
	}

	AddComponent(editorScene, canvasGameObjectId, EditorComponentType::GraphicRaycaster);
	const int32_t healthTextGameObjectId = CreateText(
		editorScene,
		"HUD Player HP",
		"HULL ",
		{24.0f, 24.0f},
		{260.0f, 34.0f},
		{0.86f, 0.96f, 1.0f},
		true,
		canvasGameObjectId);
	EditorComponent* healthBinding = AddComponent(
		editorScene,
		healthTextGameObjectId,
		EditorComponentType::UIValueBinding);

	if (healthBinding != nullptr) {
		healthBinding->uiBindingSourceGameObjectId = playerGameObjectId;
		healthBinding->uiBindingValueType = 0;
		healthBinding->uiBindingPrefix = "HULL ";
		healthBinding->uiBindingPrecision = 0;
	}

	const int32_t salvageTextGameObjectId = CreateText(
		editorScene,
		"HUD SALVAGE",
		"SALVAGE ",
		{24.0f, 60.0f},
		{300.0f, 34.0f},
		{0.18f, 0.92f, 0.84f},
		true,
		canvasGameObjectId);
	EditorComponent* salvageBinding = AddComponent(
		editorScene,
		salvageTextGameObjectId,
		EditorComponentType::UIValueBinding);

	if (salvageBinding != nullptr) {
		salvageBinding->uiBindingSourceGameObjectId = salvageCounterGameObjectId;
		salvageBinding->uiBindingValueType = 4;
		salvageBinding->uiBindingPrefix = "SALVAGE ";
		salvageBinding->uiBindingPrecision = 0;
	}

	CreateText(
		editorScene,
		"HUD Controls",
		"LMB: FIRE  R: RELOAD  E: NEXT WEAPON  WASD: RAIL OFFSET",
		{24.0f, 96.0f},
		{680.0f, 30.0f},
		{0.72f, 0.80f, 0.88f},
		true,
		canvasGameObjectId);
	CreateText(
		editorScene,
		"HUD Reticle",
		"+",
		{640.0f, 360.0f},
		{32.0f, 32.0f},
		{1.0f, 0.28f, 0.12f},
		true,
		canvasGameObjectId);
	const int32_t bossHealthTextGameObjectId = CreateText(
		editorScene,
		"HUD Boss HP",
		"BOSS ",
		{480.0f, 24.0f},
		{320.0f, 34.0f},
		{1.0f, 0.28f, 0.16f},
		false,
		canvasGameObjectId);
	EditorComponent* bossHealthBinding = AddComponent(
		editorScene,
		bossHealthTextGameObjectId,
		EditorComponentType::UIValueBinding);

	if (bossHealthBinding != nullptr) {
		bossHealthBinding->uiBindingSourceGameObjectId = bossGameObjectId;
		bossHealthBinding->uiBindingValueType = 0;
		bossHealthBinding->uiBindingPrefix = "BOSS ";
		bossHealthBinding->uiBindingPrecision = 0;
	}

	const int32_t shopRootGameObjectId = CreateGameObject(editorScene, "SHOP UI", canvasGameObjectId);
	if (EditorGameObject* shopRoot = editorScene.FindGameObject(shopRootGameObjectId)) {
		shopRoot->isActive = false;
	}

	CreateText(editorScene, "SHOP Title", "SUPPLY POINT / SHOP / LOADOUT", {870.0f, 90.0f}, {360.0f, 36.0f}, {0.15f, 0.92f, 0.85f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "SHOP Buy 40mm", "BUY 40mm AUTOCANNON  300", "OnBuy40mm", {900.0f, 145.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "SHOP Buy Rocket", "BUY ROCKET POD  250", "OnBuyRocket", {900.0f, 193.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "SHOP Buy Missile", "BUY ANTI-SHIP MISSILE  400", "OnBuyMissile", {900.0f, 241.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "LOADOUT Equip 20mm", "EQUIP MAIN: 20mm", "OnEquip20mm", {900.0f, 315.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "LOADOUT Equip 40mm", "EQUIP MAIN: 40mm", "OnEquip40mm", {900.0f, 363.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "LOADOUT Equip Rocket", "EQUIP SECONDARY: ROCKET", "OnEquipRocket", {900.0f, 411.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "LOADOUT Equip Missile", "EQUIP MISSILE", "OnEquipMissile", {900.0f, 459.0f}, false, shopRootGameObjectId);
	CreateButton(editorScene, "SHOP Continue", "CONTINUE", "OnContinue", {900.0f, 535.0f}, false, shopRootGameObjectId);
	CreateText(editorScene, "OWNED 40mm", "OWNED: 40mm", {900.0f, 585.0f}, {250.0f, 28.0f}, {0.18f, 0.95f, 0.58f}, false, shopRootGameObjectId);
	CreateText(editorScene, "OWNED Rocket", "OWNED: ROCKET", {900.0f, 615.0f}, {250.0f, 28.0f}, {0.18f, 0.95f, 0.58f}, false, shopRootGameObjectId);
	CreateText(editorScene, "OWNED Missile", "OWNED: MISSILE", {900.0f, 645.0f}, {250.0f, 28.0f}, {0.18f, 0.95f, 0.58f}, false, shopRootGameObjectId);

	CreateText(editorScene, "MISSION CLEAR Text", "MISSION CLEAR", {500.0f, 230.0f}, {360.0f, 70.0f}, {0.18f, 1.0f, 0.72f}, false, canvasGameObjectId);
	CreateButton(editorScene, "RESULT Button", "RESULT", "OnResult", {555.0f, 330.0f}, false, canvasGameObjectId);
	CreateText(editorScene, "MISSION FAILED Text", "SHIP LOST", {520.0f, 230.0f}, {320.0f, 70.0f}, {1.0f, 0.12f, 0.08f}, false, canvasGameObjectId);
	CreateButton(editorScene, "RESTART Button", "RESTART", "OnRestart", {555.0f, 330.0f}, false, canvasGameObjectId);

	//============================================================
	// Save and round-trip validation
	//================================================================

	if (!editorScene.SaveScene(kScenePath)) {
		resultMessage = "WaterRailShooter0817: scene save failed";
		return false;
	}

	EditorScene loadedScene;

	if (!loadedScene.LoadScene(kScenePath) || !HasRequiredObjects(loadedScene)) {
		resultMessage = "WaterRailShooter0817: scene round-trip validation failed";
		return false;
	}

	resultMessage =
		"WaterRailShooter0817: generated " +
		std::to_string(loadedScene.GetGameObjects().size()) +
		" GameObjects";
	return true;
}
