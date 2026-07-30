#include "EditorRailShooterDirectorManager.h"

#include "EditorAnimationManager.h"
#include "EditorComponentUtility.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Source/Engine/Effect/EditorEffekseerManager.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kMotionTau = 6.28318530718f;
	constexpr float kMinimumMotionDistance = 0.0001f;

	Vector3 MoveTowards(const Vector3& currentPosition, const Vector3& targetPosition, float maximumDistance) {
		const Vector3 targetOffset = Subtract(targetPosition, currentPosition);
		const float targetDistance = Length(targetOffset);

		if (targetDistance <= maximumDistance || targetDistance <= kMinimumMotionDistance) {
			return targetPosition;
		}

		return Add(currentPosition, Multiply(maximumDistance / targetDistance, targetOffset));
	}

	void LookAt(EditorGameObject& gameObject, const Vector3& targetPosition) {
		Vector3 forwardDirection = Subtract(targetPosition, gameObject.translate);

		if (Length(forwardDirection) <= kMinimumMotionDistance) {
			return;
		}

		forwardDirection = Normalize(forwardDirection);
		gameObject.rotate.x = -std::asin((std::clamp)(forwardDirection.y, -1.0f, 1.0f));
		gameObject.rotate.y = std::atan2(forwardDirection.x, forwardDirection.z);
	}
}

void EditorRailShooterDirectorManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorAnimationManager* animationManager,
	EditorEffectManager* effectManager,
	EditorEffekseerManager* effekseerManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	animationManager_ = animationManager;
	effectManager_ = effectManager;
	effekseerManager_ = effekseerManager;
	scriptManager_ = scriptManager;
	shipRuntimes_.clear();
	enemyMotionRuntimes_.clear();
	stageRuntimes_.clear();
	isStarted_ = false;
}

void EditorRailShooterDirectorManager::Start() {
	shipRuntimes_.clear();
	enemyMotionRuntimes_.clear();
	stageRuntimes_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* shipComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailShooterShip);

		if (gameObject.isActive && shipComponent != nullptr && shipComponent->isActive) {
			const int32_t speedSourceGameObjectId =
				shipComponent->railShipSpeedSourceGameObjectId >= 0
					? shipComponent->railShipSpeedSourceGameObjectId
					: gameObject.id;
			const EditorGameObject* speedSourceGameObject = editorScene_->FindGameObject(speedSourceGameObjectId);
			ShipRuntime shipRuntime{};
			shipRuntime.previousPosition = speedSourceGameObject != nullptr
				? speedSourceGameObject->translate
				: gameObject.translate;
			shipRuntime.wakeBaseRate = GetEffectBaseRate(shipComponent->railShipWakeEffectGameObjectId);
			shipRuntime.windBaseRate = GetEffectBaseRate(shipComponent->railShipWindEffectGameObjectId);
			shipRuntimes_[gameObject.id] = shipRuntime;

			if (animationManager_ != nullptr && shipComponent->railShipSailGameObjectId >= 0) {
				animationManager_->PlayAnimation(shipComponent->railShipSailGameObjectId);
				animationManager_->SetAnimationSpeed(
					shipComponent->railShipSailGameObjectId,
					(std::max)(shipComponent->railShipSailMinimumSpeed, 0.0f));
			}

			StopCombinedEffect(shipComponent->railShipWakeEffectGameObjectId);
			StopCombinedEffect(shipComponent->railShipWindEffectGameObjectId);
		}

		EditorComponent* enemyMotionComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailShooterEnemyMotion);

		if (enemyMotionComponent != nullptr && enemyMotionComponent->isActive) {
			EnemyMotionRuntime enemyMotionRuntime{};
			enemyMotionRuntime.basePosition = gameObject.translate;
			enemyMotionRuntime.wasActive = gameObject.isActive;
			enemyMotionRuntimes_[gameObject.id] = enemyMotionRuntime;
		}

		EditorComponent* stageComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RailShooterStage);

		if (gameObject.isActive && stageComponent != nullptr && stageComponent->isActive) {
			StageRuntime stageRuntime{};
			stageRuntime.remainingTime = (std::max)(stageComponent->stageStartDelay, 0.0f);

			EditorGameObject* followerGameObject = editorScene_->FindGameObject(
				stageComponent->stageFollowerGameObjectId);
			const EditorGameObject* startMarkerGameObject = editorScene_->FindGameObject(
				stageComponent->stageStartMarkerGameObjectId);

			if (followerGameObject != nullptr && startMarkerGameObject != nullptr) {
				followerGameObject->translate = startMarkerGameObject->translate;
				followerGameObject->rotate = startMarkerGameObject->rotate;
			}

			EditorComponent* railMovementComponent = followerGameObject != nullptr
				? EditorComponentUtility::FindComponent(*followerGameObject, EditorComponentType::RailMovement)
				: nullptr;
			stageRuntime.railWasActive = railMovementComponent == nullptr || railMovementComponent->isActive;

			if (stageRuntime.remainingTime > 0.0f) {
				SetRailMovementActive(stageComponent->stageFollowerGameObjectId, false);
				stageRuntime.phase = StagePhase::WaitingStart;
			}
			else {
				BeginStage(*stageComponent, stageRuntime);
			}

			stageRuntimes_[gameObject.id] = stageRuntime;
		}
	}
}

void EditorRailShooterDirectorManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	UpdateShips(deltaTime);
	UpdateEnemyMotions(deltaTime);
	UpdateStages(deltaTime);
}

void EditorRailShooterDirectorManager::Draw() {
}

void EditorRailShooterDirectorManager::Stop() {
	if (editorScene_ != nullptr) {
		for (const auto& shipRuntimePair : shipRuntimes_) {
			const EditorGameObject* shipGameObject = editorScene_->FindGameObject(shipRuntimePair.first);

			if (shipGameObject == nullptr) {
				continue;
			}

			const EditorComponent* shipComponent = EditorComponentUtility::FindComponent(
				*shipGameObject,
				EditorComponentType::RailShooterShip);

			if (shipComponent != nullptr) {
				StopCombinedEffect(shipComponent->railShipWakeEffectGameObjectId);
				StopCombinedEffect(shipComponent->railShipWindEffectGameObjectId);
			}
		}
	}

	shipRuntimes_.clear();
	enemyMotionRuntimes_.clear();
	stageRuntimes_.clear();
	isStarted_ = false;
}

void EditorRailShooterDirectorManager::UpdateShips(float deltaTime) {
	for (auto& shipRuntimePair : shipRuntimes_) {
		EditorGameObject* shipGameObject = editorScene_->FindGameObject(shipRuntimePair.first);

		if (shipGameObject == nullptr || !shipGameObject->isActive) {
			continue;
		}

		EditorComponent* shipComponent = EditorComponentUtility::FindComponent(
			*shipGameObject,
			EditorComponentType::RailShooterShip);

		if (shipComponent == nullptr || !shipComponent->isActive) {
			continue;
		}

		const int32_t speedSourceGameObjectId =
			shipComponent->railShipSpeedSourceGameObjectId >= 0
				? shipComponent->railShipSpeedSourceGameObjectId
				: shipGameObject->id;
		EditorGameObject* speedSourceGameObject = editorScene_->FindGameObject(speedSourceGameObjectId);

		if (speedSourceGameObject == nullptr) {
			continue;
		}

		ShipRuntime& shipRuntime = shipRuntimePair.second;
		float shipSpeed = Length(Subtract(speedSourceGameObject->translate, shipRuntime.previousPosition)) / deltaTime;
		const EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
			*speedSourceGameObject,
			EditorComponentType::RailMovement);

		if (railMovementComponent != nullptr && railMovementComponent->isActive) {
			shipSpeed = Length(railMovementComponent->velocity);
		}

		shipRuntime.previousPosition = speedSourceGameObject->translate;
		const float startSpeed = (std::max)(shipComponent->railShipEffectStartSpeed, 0.0f);
		const float fullSpeed = (std::max)(shipComponent->railShipEffectFullSpeed, startSpeed + 0.01f);
		const float speedRatio = (std::clamp)((shipSpeed - startSpeed) / (fullSpeed - startSpeed), 0.0f, 1.0f);
		const float sailAnimationSpeed =
			shipComponent->railShipSailMinimumSpeed +
			(shipComponent->railShipSailMaximumSpeed - shipComponent->railShipSailMinimumSpeed) * speedRatio;

		if (animationManager_ != nullptr && shipComponent->railShipSailGameObjectId >= 0) {
			animationManager_->SetAnimationSpeed(
				shipComponent->railShipSailGameObjectId,
				(std::max)(sailAnimationSpeed, 0.0f));
		}

		SetEffectRate(
			shipComponent->railShipWakeEffectGameObjectId,
			shipRuntime.wakeBaseRate * (std::max)(speedRatio, 0.05f));
		SetEffectRate(
			shipComponent->railShipWindEffectGameObjectId,
			shipRuntime.windBaseRate * (std::max)(speedRatio, 0.05f));

		if (!shipRuntime.effectsPlaying && shipSpeed >= startSpeed) {
			PlayCombinedEffect(shipComponent->railShipWakeEffectGameObjectId);
			PlayCombinedEffect(shipComponent->railShipWindEffectGameObjectId);
			shipRuntime.effectsPlaying = true;
		}
		else if (shipRuntime.effectsPlaying && shipSpeed < startSpeed * 0.5f) {
			StopCombinedEffect(shipComponent->railShipWakeEffectGameObjectId);
			StopCombinedEffect(shipComponent->railShipWindEffectGameObjectId);
			shipRuntime.effectsPlaying = false;
		}
	}
}

void EditorRailShooterDirectorManager::UpdateEnemyMotions(float deltaTime) {
	for (auto& enemyMotionRuntimePair : enemyMotionRuntimes_) {
		EditorGameObject* enemyGameObject = editorScene_->FindGameObject(enemyMotionRuntimePair.first);

		if (enemyGameObject == nullptr) {
			continue;
		}

		EnemyMotionRuntime& enemyMotionRuntime = enemyMotionRuntimePair.second;
		EditorComponent* enemyMotionComponent = EditorComponentUtility::FindComponent(
			*enemyGameObject,
			EditorComponentType::RailShooterEnemyMotion);

		if (enemyMotionComponent == nullptr || !enemyMotionComponent->isActive || !enemyGameObject->isActive) {
			enemyMotionRuntime.wasActive = false;
			continue;
		}

		if (!enemyMotionRuntime.wasActive) {
			enemyMotionRuntime.basePosition = enemyGameObject->translate;
			enemyMotionRuntime.elapsedTime = 0.0f;
			enemyMotionRuntime.wasActive = true;
		}

		enemyMotionRuntime.elapsedTime += deltaTime;
		const float motionPhase =
			enemyMotionRuntime.elapsedTime *
			(std::max)(enemyMotionComponent->enemyMotionFrequency, 0.0f) *
			kMotionTau + enemyMotionComponent->enemyMotionPhase;
		const int32_t motionPattern = (std::clamp)(enemyMotionComponent->enemyMotionPattern, 0, 4);
		const EditorGameObject* targetGameObject = editorScene_->FindGameObject(
			enemyMotionComponent->enemyMotionTargetGameObjectId);

		if (motionPattern <= 2) {
			Vector3 motionOffset{};

			if (motionPattern == 0) {
				motionOffset = {
					enemyMotionComponent->enemyMotionAmplitude.x * std::sin(motionPhase),
					enemyMotionComponent->enemyMotionAmplitude.y * std::sin(motionPhase * 1.7f),
					enemyMotionComponent->enemyMotionAmplitude.z * std::cos(motionPhase)};
			}
			else if (motionPattern == 1) {
				motionOffset = {
					enemyMotionComponent->enemyMotionAmplitude.x * std::cos(motionPhase),
					enemyMotionComponent->enemyMotionAmplitude.y * std::sin(motionPhase * 0.5f),
					enemyMotionComponent->enemyMotionAmplitude.z * std::sin(motionPhase)};
			}
			else {
				motionOffset = {
					enemyMotionComponent->enemyMotionAmplitude.x * std::sin(motionPhase),
					enemyMotionComponent->enemyMotionAmplitude.y * std::sin(motionPhase * 2.0f),
					enemyMotionComponent->enemyMotionAmplitude.z * std::cos(motionPhase)};
			}

			const EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
				*enemyGameObject,
				EditorComponentType::RailMovement);

			if (railMovementComponent != nullptr && railMovementComponent->isActive) {
				enemyGameObject->translate = Add(enemyGameObject->translate, motionOffset);
			}
			else {
				enemyGameObject->translate = Add(enemyMotionRuntime.basePosition, motionOffset);
			}
		}
		else if (targetGameObject != nullptr && targetGameObject->isActive) {
			const float maximumDistance = (std::max)(enemyMotionComponent->enemyMotionSpeed, 0.0f) * deltaTime;

			if (motionPattern == 3) {
				enemyGameObject->translate = MoveTowards(
					enemyGameObject->translate,
					targetGameObject->translate,
					maximumDistance);
			}
			else {
				const float diveRatio = (1.0f - std::cos(motionPhase)) * 0.45f;
				const Vector3 diveTarget = Add(
					enemyMotionRuntime.basePosition,
					Multiply(diveRatio, Subtract(targetGameObject->translate, enemyMotionRuntime.basePosition)));
				enemyGameObject->translate = MoveTowards(
					enemyGameObject->translate,
					diveTarget,
					maximumDistance);
			}
		}

		if (enemyMotionComponent->enemyMotionLookAtTarget && targetGameObject != nullptr) {
			LookAt(*enemyGameObject, targetGameObject->translate);
		}
	}
}

void EditorRailShooterDirectorManager::UpdateStages(float deltaTime) {
	for (auto& stageRuntimePair : stageRuntimes_) {
		EditorGameObject* stageGameObject = editorScene_->FindGameObject(stageRuntimePair.first);

		if (stageGameObject == nullptr || !stageGameObject->isActive) {
			continue;
		}

		const EditorComponent* stageComponent = EditorComponentUtility::FindComponent(
			*stageGameObject,
			EditorComponentType::RailShooterStage);

		if (stageComponent == nullptr || !stageComponent->isActive) {
			continue;
		}

		StageRuntime& stageRuntime = stageRuntimePair.second;

		if (stageRuntime.phase == StagePhase::WaitingStart) {
			stageRuntime.remainingTime -= deltaTime;

			if (stageRuntime.remainingTime <= 0.0f) {
				BeginStage(*stageComponent, stageRuntime);
			}
		}
		else if (stageRuntime.phase == StagePhase::Running) {
			const EditorGameObject* followerGameObject = editorScene_->FindGameObject(
				stageComponent->stageFollowerGameObjectId);
			const EditorGameObject* goalMarkerGameObject = editorScene_->FindGameObject(
				stageComponent->stageGoalMarkerGameObjectId);
			bool hasReachedGoal = false;

			if (followerGameObject != nullptr && goalMarkerGameObject != nullptr) {
				hasReachedGoal = Length(Subtract(
					followerGameObject->translate,
					goalMarkerGameObject->translate)) <= (std::max)(stageComponent->stageGoalRadius, 0.01f);
			}
			else if (railMovementManager_ != nullptr) {
				float normalizedProgress = 0.0f;
				hasReachedGoal = railMovementManager_->GetNormalizedProgress(
					stageComponent->stageFollowerGameObjectId,
					normalizedProgress) && normalizedProgress >= 0.999f;
			}

			if (hasReachedGoal) {
				ReachStageGoal(*stageComponent, stageRuntime);
			}
		}
		else if (stageRuntime.phase == StagePhase::WaitingTransition) {
			stageRuntime.remainingTime -= deltaTime;

			if (stageRuntime.remainingTime > 0.0f) {
				continue;
			}

			const std::string& destinationScenePath = !stageComponent->stageNextScenePath.empty()
				? stageComponent->stageNextScenePath
				: stageComponent->stageSelectScenePath;

			if (!destinationScenePath.empty() && scriptManager_ != nullptr) {
				scriptManager_->RequestSceneLoad(destinationScenePath);
			}

			stageRuntime.phase = StagePhase::Complete;
		}
	}
}

void EditorRailShooterDirectorManager::BeginStage(
	const EditorComponent& stageComponent,
	StageRuntime& stageRuntime) {
	SetRailMovementActive(stageComponent.stageFollowerGameObjectId, stageRuntime.railWasActive);
	PlayCombinedEffect(stageComponent.stageStartEffectGameObjectId);

	if (animationManager_ != nullptr && stageComponent.stageFollowerGameObjectId >= 0) {
		animationManager_->SetTrigger(stageComponent.stageFollowerGameObjectId, "Start");
	}

	stageRuntime.remainingTime = 0.0f;
	stageRuntime.phase = StagePhase::Running;
}

void EditorRailShooterDirectorManager::ReachStageGoal(
	const EditorComponent& stageComponent,
	StageRuntime& stageRuntime) {
	SetRailMovementActive(stageComponent.stageFollowerGameObjectId, false);
	PlayCombinedEffect(stageComponent.stageGoalEffectGameObjectId);

	if (animationManager_ != nullptr && stageComponent.stageFollowerGameObjectId >= 0) {
		animationManager_->SetTrigger(stageComponent.stageFollowerGameObjectId, "Goal");
	}

	stageRuntime.remainingTime = (std::max)(stageComponent.stageGoalDelay, 0.0f);
	stageRuntime.phase = StagePhase::WaitingTransition;
}

bool EditorRailShooterDirectorManager::SetRailMovementActive(int32_t gameObjectId, bool isActive) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return false;
	}

	EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::RailMovement);

	if (railMovementComponent == nullptr) {
		return false;
	}

	railMovementComponent->isActive = isActive;
	return true;
}

void EditorRailShooterDirectorManager::PlayCombinedEffect(int32_t gameObjectId) {
	if (gameObjectId < 0) {
		return;
	}

	if (effectManager_ != nullptr) {
		effectManager_->PlayEffect(gameObjectId);
	}

	if (effekseerManager_ != nullptr) {
		effekseerManager_->PlayEffect(gameObjectId);
	}
}

void EditorRailShooterDirectorManager::StopCombinedEffect(int32_t gameObjectId) {
	if (gameObjectId < 0) {
		return;
	}

	if (effectManager_ != nullptr) {
		effectManager_->StopEffect(gameObjectId);
	}

	if (effekseerManager_ != nullptr) {
		effekseerManager_->StopEffect(gameObjectId);
	}
}

float EditorRailShooterDirectorManager::GetEffectBaseRate(int32_t gameObjectId) const {
	const EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return 0.0f;
	}

	for (const EditorComponent& component : gameObject->components) {
		if (component.isActive &&
			(component.type == EditorComponentType::ParticleSystem ||
			 component.type == EditorComponentType::VisualEffect)) {
			return component.particleRate;
		}
	}

	return 0.0f;
}

void EditorRailShooterDirectorManager::SetEffectRate(int32_t gameObjectId, float emissionRate) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return;
	}

	for (EditorComponent& component : gameObject->components) {
		if (component.isActive &&
			(component.type == EditorComponentType::ParticleSystem ||
			 component.type == EditorComponentType::VisualEffect)) {
			component.particleRate = (std::max)(emissionRate, 0.0f);
		}
	}
}
