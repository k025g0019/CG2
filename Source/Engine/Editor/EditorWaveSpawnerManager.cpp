#include "EditorWaveSpawnerManager.h"

#include "EditorComponentUtility.h"
#include "EditorObjectPoolManager.h"
#include "EditorPhysicsManager.h"
#include "EditorRailMovementManager.h"
#include "EditorScriptManager.h"
#include "../Core/Vector&Matrix.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kCircle = 6.28318530717958647692f;

	EditorScriptActionPayload MakeNonePayload() {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeNone;
		return payload;
	}

	EditorScriptActionPayload MakeGameObjectPayload(int32_t gameObjectId) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeGameObject;
		payload.gameObjectId = gameObjectId;
		return payload;
	}

	EditorScriptActionPayload MakeIntPayload(int32_t value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeInt;
		payload.intValue = value;
		return payload;
	}

	Vector3 TransformOffset(const Vector3& localOffset, const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		return Transform(localOffset, rotationMatrix);
	}
}

void EditorWaveSpawnerManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager,
	EditorPhysicsManager* physicsManager,
	EditorObjectPoolManager* objectPoolManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
	physicsManager_ = physicsManager;
	objectPoolManager_ = objectPoolManager;
	scriptManager_ = scriptManager;
	waveRuntimes_.clear();
	encounterRuntimes_.clear();
	spawnPointIndices_.clear();
	spawnPointLastIndices_.clear();
	originalRailSpeeds_.clear();
	originalActiveStates_.clear();
	isStarted_ = false;
}

void EditorWaveSpawnerManager::Start() {
	waveRuntimes_.clear();
	encounterRuntimes_.clear();
	spawnPointIndices_.clear();
	spawnPointLastIndices_.clear();
	originalRailSpeeds_.clear();
	spawnRandomState_ = 0x53504157u;
	originalActiveStates_.clear();
	isStarted_ = true;

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& waveGameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* waveComponent = EditorComponentUtility::FindComponent(
			waveGameObject,
			EditorComponentType::WaveSpawner);

		if (waveComponent == nullptr || !waveComponent->isActive || !waveGameObject.isActive) {
			continue;
		}

		WaveRuntime waveRuntime{};
		waveRuntime.childGameObjectIds = waveGameObject.children;
		waveRuntime.usesObjectPool =
			waveComponent->waveSpawnSourceMode == 0 &&
			waveComponent->wavePoolGameObjectId >= 0 &&
			objectPoolManager_ != nullptr &&
			objectPoolManager_->HasPool(waveComponent->wavePoolGameObjectId);
		waveRuntime.targetSpawnCount = waveRuntime.usesObjectPool
			? (std::clamp)(waveComponent->waveSpawnCount, 1, 1024)
			: static_cast<int32_t>(waveRuntime.childGameObjectIds.size());

		// Pool未設定の旧Sceneは従来の子方式へフォールバックする。
		if (!waveRuntime.usesObjectPool && waveComponent->waveDeactivateChildrenOnStart) {
			for (const int32_t childGameObjectId : waveRuntime.childGameObjectIds) {
				SetRuntimeActiveRecursive(childGameObjectId, false);
			}
		}

		waveRuntimes_[waveGameObject.id] = std::move(waveRuntime);
	}

	for (const EditorGameObject& object : editorScene_->GetGameObjects()) {
		const EditorComponent* encounter = EditorComponentUtility::FindComponent(object, EditorComponentType::EncounterController);
		if (object.isActive && encounter != nullptr && encounter->isActive) encounterRuntimes_[object.id] = {0, 0.0f, encounter->encounterPlayOnStart, false};
	}
}

void EditorWaveSpawnerManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (auto& waveRuntimePair : waveRuntimes_) {
		EditorGameObject* waveGameObject = editorScene_->FindGameObject(waveRuntimePair.first);

		if (waveGameObject == nullptr || !waveGameObject->isActive) {
			continue;
		}

		const EditorComponent* waveComponent = EditorComponentUtility::FindComponent(
			*waveGameObject,
			EditorComponentType::WaveSpawner);

		if (waveComponent == nullptr || !waveComponent->isActive) {
			continue;
		}

		WaveRuntime& waveRuntime = waveRuntimePair.second;
		UpdateWaveMotion(*waveGameObject, waveRuntime, deltaTime);

		if (!waveRuntime.hasTriggered) {
			if (!IsTriggerSatisfied(*waveGameObject, *waveComponent)) {
				continue;
			}

			waveRuntime.hasTriggered = true;
			waveRuntime.spawnTimer = 0.0f;
			QueueAction(
				*waveGameObject,
				*waveComponent,
				waveComponent->waveStartedActionName,
				MakeNonePayload());
		}

		if (waveRuntime.nextSpawnIndex >= waveRuntime.targetSpawnCount) {
			CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
			continue;
		}

		const float spawnInterval = (std::max)(waveComponent->waveSpawnInterval, 0.0f);
		const int32_t maximumSpawnsPerFrame = (std::clamp)(
			waveComponent->waveSpawnMaximumPerFrame,
			1,
			1024);

		if (spawnInterval <= 0.0f) {
			int32_t spawnedCountThisFrame = 0;

			while (waveRuntime.nextSpawnIndex < waveRuntime.targetSpawnCount &&
				spawnedCountThisFrame < maximumSpawnsPerFrame) {
				const int32_t spawnedGameObjectId = SpawnNext(*waveGameObject, *waveComponent, waveRuntime);

				if (spawnedGameObjectId < 0) {
					break;
				}

				// Spawn() のプール拡張でシーンの GameObject 配列が再確保されるため、
				// ポインタを毎回取り直してから使う。
				waveGameObject = editorScene_->FindGameObject(waveRuntimePair.first);
				waveComponent = waveGameObject != nullptr
					? EditorComponentUtility::FindComponent(
						*waveGameObject,
						EditorComponentType::WaveSpawner)
					: nullptr;

				if (waveGameObject == nullptr || waveComponent == nullptr) {
					break;
				}

				QueueAction(
					*waveGameObject,
					*waveComponent,
					waveComponent->waveSpawnedActionName,
					MakeGameObjectPayload(spawnedGameObjectId));
				spawnedCountThisFrame++;
			}

			CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
			continue;
		}

		waveRuntime.spawnTimer -= deltaTime;

		int32_t spawnedCountThisFrame = 0;

		while (waveRuntime.spawnTimer <= 0.0f &&
			spawnedCountThisFrame < maximumSpawnsPerFrame &&
			waveRuntime.nextSpawnIndex < waveRuntime.targetSpawnCount) {
			const int32_t spawnedGameObjectId = SpawnNext(*waveGameObject, *waveComponent, waveRuntime);

			if (spawnedGameObjectId < 0) {
				waveRuntime.spawnTimer = spawnInterval;
				break;
			}

			// Spawn() のプール拡張でシーンの GameObject 配列が再確保されるため、
			// ポインタを毎回取り直してから使う。
			waveGameObject = editorScene_->FindGameObject(waveRuntimePair.first);
			waveComponent = waveGameObject != nullptr
				? EditorComponentUtility::FindComponent(
					*waveGameObject,
					EditorComponentType::WaveSpawner)
				: nullptr;

			if (waveGameObject == nullptr || waveComponent == nullptr) {
				break;
			}

			QueueAction(
				*waveGameObject,
				*waveComponent,
				waveComponent->waveSpawnedActionName,
				MakeGameObjectPayload(spawnedGameObjectId));
			waveRuntime.spawnTimer += spawnInterval;
			spawnedCountThisFrame++;
		}

		CompleteWaveIfNeeded(*waveGameObject, *waveComponent, waveRuntime);
	}
	UpdateEncounters(deltaTime);
}

void EditorWaveSpawnerManager::Stop() {
	waveRuntimes_.clear();
	encounterRuntimes_.clear();
	spawnPointIndices_.clear();
	spawnPointLastIndices_.clear();
	originalRailSpeeds_.clear();
	originalActiveStates_.clear();
	isStarted_ = false;
}

bool EditorWaveSpawnerManager::IsTriggerSatisfied(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component) const {
	if (component.waveTriggerMode == 0) {
		return true;
	}

	if (component.waveTriggerMode == 1) {
		if (component.waveTriggerSourceGameObjectId < 0 || railMovementManager_ == nullptr) {
			return false;
		}

		float normalizedProgress = 0.0f;
		const bool hasProgress = railMovementManager_->GetNormalizedProgress(
			component.waveTriggerSourceGameObjectId,
			normalizedProgress);
		return hasProgress &&
			normalizedProgress >= (std::clamp)(component.waveTriggerValue, 0.0f, 1.0f);
	}

	if (component.waveTriggerMode != 3 ||
		component.waveTriggerSourceGameObjectId < 0 ||
		editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(
		component.waveTriggerSourceGameObjectId);
	const EditorGameObject* originGameObject = component.waveSpawnPointGameObjectId >= 0
		? editorScene_->FindGameObject(component.waveSpawnPointGameObjectId)
		: &ownerGameObject;

	if (sourceGameObject == nullptr || originGameObject == nullptr || !sourceGameObject->isActive) {
		return false;
	}

	Vector3 sourceScale{};
	Vector3 sourceRotation{};
	Vector3 sourcePosition{};
	Vector3 originScale{};
	Vector3 originRotation{};
	Vector3 originPosition{};
	editorScene_->GetWorldTransform(
		sourceGameObject->id,
		sourceScale,
		sourceRotation,
		sourcePosition);
	editorScene_->GetWorldTransform(
		originGameObject->id,
		originScale,
		originRotation,
		originPosition);
	(void)sourceScale;
	(void)sourceRotation;
	(void)originScale;
	(void)originRotation;
	const float differenceX = sourcePosition.x - originPosition.x;
	const float differenceY = sourcePosition.y - originPosition.y;
	const float differenceZ = sourcePosition.z - originPosition.z;
	const float distanceSquared =
		differenceX * differenceX +
		differenceY * differenceY +
		differenceZ * differenceZ;
	const float triggerDistance = (std::max)(component.waveTriggerValue, 0.0f);
	return distanceSquared <= triggerDistance * triggerDistance;
}

void EditorWaveSpawnerManager::SetRuntimeActiveRecursive(int32_t gameObjectId, bool isActive) {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (gameObject == nullptr) {
		return;
	}

	const std::vector<int32_t> childGameObjectIds = gameObject->children;

	if (!isActive) {
		originalActiveStates_.try_emplace(gameObjectId, gameObject->isActive);
	}

	const auto originalActiveIterator = originalActiveStates_.find(gameObjectId);
	const bool targetActive = isActive
		? (originalActiveIterator != originalActiveStates_.end()
			? originalActiveIterator->second
			: gameObject->isActive)
		: false;

	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, targetActive);
	}

	gameObject->isActive = targetActive;

	for (const int32_t childGameObjectId : childGameObjectIds) {
		SetRuntimeActiveRecursive(childGameObjectId, isActive);
	}
}

int32_t EditorWaveSpawnerManager::SpawnNext(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	WaveRuntime& waveRuntime) {
	if (waveRuntime.nextSpawnIndex < 0 ||
		waveRuntime.nextSpawnIndex >= waveRuntime.targetSpawnCount) {
		return -1;
	}

	const int32_t spawnIndex = waveRuntime.nextSpawnIndex;
	const Vector3 formationOffset = CalculateFormationOffset(
		component,
		spawnIndex,
		waveRuntime.targetSpawnCount);
	const int32_t ownerGameObjectId = ownerGameObject.id;
	int32_t spawnedGameObjectId = -1;
	uint64_t poolSpawnVersion = 0u;

	if (waveRuntime.usesObjectPool) {
		const EditorGameObject* spawnPointGameObject = component.waveSpawnPointGameObjectId >= 0
			? editorScene_->FindGameObject(component.waveSpawnPointGameObjectId)
			: &ownerGameObject;

		if (spawnPointGameObject == nullptr || objectPoolManager_ == nullptr) {
			return -1;
		}

		Vector3 spawnScale = spawnPointGameObject->scale;
		Vector3 spawnRotation = spawnPointGameObject->rotate;
		Vector3 spawnPosition = spawnPointGameObject->translate;
		editorScene_->GetWorldTransform(
			spawnPointGameObject->id,
			spawnScale,
			spawnRotation,
			spawnPosition);
		(void)spawnScale;
		const EditorComponent* spawnPointSet = EditorComponentUtility::FindComponent(ownerGameObject, EditorComponentType::SpawnPointSet);
		if (spawnPointSet != nullptr && spawnPointSet->isActive) ResolveSpawnPoint(ownerGameObject.id, spawnPosition, spawnRotation);
		const Vector3 worldOffset = TransformOffset(formationOffset, spawnRotation);
		spawnPosition = {
			spawnPosition.x + worldOffset.x,
			spawnPosition.y + worldOffset.y,
			spawnPosition.z + worldOffset.z};
		spawnedGameObjectId = objectPoolManager_->Spawn(
			component.wavePoolGameObjectId,
			spawnPosition,
			spawnRotation);

		if (spawnedGameObjectId < 0) {
			return -1;
		}

		poolSpawnVersion = objectPoolManager_->GetSpawnVersion(spawnedGameObjectId);
	}
	else {
		if (spawnIndex >= static_cast<int32_t>(waveRuntime.childGameObjectIds.size())) {
			return -1;
		}

		spawnedGameObjectId = waveRuntime.childGameObjectIds[static_cast<size_t>(spawnIndex)];
		SetRuntimeActiveRecursive(spawnedGameObjectId, true);
	}

	// この時点で objectPoolManager_->Spawn() がプール拡張によりシーン配列を再確保している可能性があるため、
	// ownerGameObject 参照は使わず、IDから取り直したポインタで後続処理を進める。
	ApplySpawnedObjectSetup(ownerGameObjectId, spawnedGameObjectId, spawnIndex);
	ResetRailMovement(
		spawnedGameObjectId,
		formationOffset,
		component.waveSpawnRailStartNormalized);
	waveRuntime.nextSpawnIndex++;
	waveRuntime.spawnRecords.push_back({
		spawnedGameObjectId,
		poolSpawnVersion,
		waveRuntime.usesObjectPool,
		spawnIndex,
		formationOffset,
		0.0f});
	return spawnedGameObjectId;
}

Vector3 EditorWaveSpawnerManager::CalculateFormationOffset(
	const EditorComponent& component,
	int32_t spawnIndex,
	int32_t spawnCount) const {
	const float spacing = (std::max)(component.waveFormationSpacing, 0.0f);
	const float centeredIndex =
		static_cast<float>(spawnIndex) -
		(static_cast<float>(spawnCount - 1) * 0.5f);

	if (component.waveFormationPattern == 1) {
		return {centeredIndex * spacing, 0.0f, 0.0f};
	}

	if (component.waveFormationPattern == 2) {
		const int32_t row = (spawnIndex + 1) / 2;
		const float side = spawnIndex == 0 ? 0.0f : (spawnIndex % 2 == 1 ? -1.0f : 1.0f);
		return {
			side * static_cast<float>(row) * spacing,
			0.0f,
			-static_cast<float>(row) * spacing};
	}

	if (component.waveFormationPattern == 3) {
		const float angle = spawnCount > 0
			? kCircle * static_cast<float>(spawnIndex) / static_cast<float>(spawnCount)
			: 0.0f;
		const float radius = spawnCount > 1
			? spacing * static_cast<float>(spawnCount) / kCircle
			: 0.0f;
		return {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
	}

	if (component.waveFormationPattern == 4) {
		const int32_t columns = (std::clamp)(component.waveFormationColumns, 1, 1024);
		const int32_t rows = (spawnCount + columns - 1) / columns;
		const int32_t row = spawnIndex / columns;
		const int32_t column = spawnIndex % columns;
		return {
			(static_cast<float>(column) - static_cast<float>((std::min)(columns, spawnCount) - 1) * 0.5f) * spacing,
			0.0f,
			(static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing};
	}

	return {0.0f, 0.0f, 0.0f};
}

void EditorWaveSpawnerManager::ResetRailMovement(
	int32_t gameObjectId,
	const Vector3& formationOffset,
	float railStartNormalizedOverride) const {
	if (editorScene_ == nullptr || railMovementManager_ == nullptr) {
		return;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	const EditorComponent* railMovementComponent = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RailMovement)
		: nullptr;

	if (railMovementComponent == nullptr || !railMovementComponent->isActive) {
		return;
	}

	railMovementManager_->SetNormalizedProgress(
		gameObjectId,
		(std::clamp)(
			railStartNormalizedOverride >= 0.0f
				? railStartNormalizedOverride
				: railMovementComponent->railStartNormalized,
			0.0f,
			1.0f));
	railMovementManager_->SetOffset(
		gameObjectId,
		{
			railMovementComponent->railStartOffset.x + formationOffset.x,
			railMovementComponent->railStartOffset.y + formationOffset.y});
	railMovementManager_->SetReverse(gameObjectId, railMovementComponent->railReverse);
	railMovementManager_->SetPaused(gameObjectId, railMovementComponent->railStartPaused);
}

void EditorWaveSpawnerManager::ApplySpawnedObjectSetup(
	int32_t ownerGameObjectId,
	int32_t spawnedGameObjectId,
	int32_t spawnIndex) {
	if (editorScene_ == nullptr) {
		return;
	}

	const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return;
	}

	const EditorComponent* setup = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::SpawnedObjectSetup);
	EditorGameObject* spawnedGameObject = editorScene_->FindGameObject(spawnedGameObjectId);

	if (setup == nullptr || !setup->isActive || spawnedGameObject == nullptr) {
		return;
	}

	if (setup->spawnedSetupResetRuntimeState && objectPoolManager_ != nullptr) {
		objectPoolManager_->ResetObjectRuntimeState(spawnedGameObjectId);
	}

	EditorComponent* railMovement = EditorComponentUtility::FindComponent(
		*spawnedGameObject,
		EditorComponentType::RailMovement);

	if (railMovement != nullptr && railMovement->isActive) {
		if (setup->spawnedSetupRailPathGameObjectId >= 0) {
			railMovement->railPathGameObjectId = setup->spawnedSetupRailPathGameObjectId;
		}

		railMovement->railStartNormalized = (std::clamp)(
			setup->spawnedSetupRailStartNormalized +
				setup->spawnedSetupRailStartStep * static_cast<float>(spawnIndex),
			0.0f,
			1.0f);
		const auto [baseSpeedIterator, inserted] = originalRailSpeeds_.try_emplace(
			spawnedGameObjectId,
			railMovement->railSpeed);
		(void)inserted;
		railMovement->railSpeed = baseSpeedIterator->second *
			(std::max)(setup->spawnedSetupRailSpeedMultiplier, 0.0f);
	}

	if (setup->spawnedSetupOverrideTeam) {
		EditorComponent* team = EditorComponentUtility::FindComponent(
			*spawnedGameObject,
			EditorComponentType::Team);

		if (team != nullptr && team->isActive) {
			team->teamId = setup->spawnedSetupTeamId;
		}
	}

	if (scriptManager_ != nullptr && !setup->spawnedSetupAppliedActionName.empty()) {
		const int32_t actionTargetGameObjectId = setup->spawnedSetupActionTargetGameObjectId >= 0
			? setup->spawnedSetupActionTargetGameObjectId
			: ownerGameObject->id;
		scriptManager_->QueueActionPayload(
			actionTargetGameObjectId,
			setup->spawnedSetupAppliedActionName,
			MakeGameObjectPayload(spawnedGameObjectId));
	}
}

void EditorWaveSpawnerManager::UpdateWaveMotion(
	const EditorGameObject& ownerGameObject,
	WaveRuntime& waveRuntime,
	float deltaTime) {
	if (railMovementManager_ == nullptr) {
		return;
	}

	const EditorComponent* motion = EditorComponentUtility::FindComponent(
		ownerGameObject,
		EditorComponentType::WaveMotionProfile);

	if (motion == nullptr || !motion->isActive || motion->waveMotionMode == 0) {
		return;
	}

	for (SpawnRecord& spawnRecord : waveRuntime.spawnRecords) {
		if (IsSpawnRecordDefeated(spawnRecord)) {
			continue;
		}

		spawnRecord.elapsedTime += deltaTime;
		const float phase = spawnRecord.elapsedTime *
			(std::max)(motion->waveMotionFrequency, 0.0f) * kCircle +
			static_cast<float>(spawnRecord.spawnIndex) * motion->waveMotionPhaseStep;
		float horizontalWave = std::sin(phase);
		float verticalWave = std::cos(phase);

		if (motion->waveMotionMode == 2) {
			verticalWave = std::sin(phase * 2.0f);
		}
		else if (motion->waveMotionMode == 3) {
			const float side = spawnRecord.spawnIndex % 2 == 0 ? 1.0f : -1.0f;
			horizontalWave *= side;
			verticalWave = std::fabs(std::sin(phase)) * side;
		}

		float blendRatio = 1.0f;

		if (motion->waveMotionBlendInSeconds > 0.0f) {
			blendRatio = (std::clamp)(
				spawnRecord.elapsedTime / motion->waveMotionBlendInSeconds,
				0.0f,
				1.0f);
			blendRatio = blendRatio * blendRatio * (3.0f - 2.0f * blendRatio);
		}

		railMovementManager_->SetOffset(
			spawnRecord.gameObjectId,
			{
				spawnRecord.formationOffset.x +
					horizontalWave * motion->waveMotionAmplitude.x * blendRatio,
				spawnRecord.formationOffset.y +
					verticalWave * motion->waveMotionAmplitude.y * blendRatio});
	}
}

bool EditorWaveSpawnerManager::IsSpawnRecordDefeated(const SpawnRecord& spawnRecord) const {
	if (spawnRecord.gameObjectId < 0 || editorScene_ == nullptr) {
		return true;
	}

	if (spawnRecord.usesObjectPool &&
		(objectPoolManager_ == nullptr ||
		!objectPoolManager_->IsSpawnLeaseActive(spawnRecord.gameObjectId, spawnRecord.poolSpawnVersion))) {
		return true;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(spawnRecord.gameObjectId);

	if (gameObject == nullptr || !gameObject->isActive) {
		return true;
	}

	const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Health);
	return
		healthComponent != nullptr &&
		healthComponent->isActive &&
		healthComponent->healthCurrent <= 0.0f;
}

void EditorWaveSpawnerManager::QueueAction(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	const std::string& actionName,
	const EditorScriptActionPayload& payload) const {
	if (scriptManager_ == nullptr || actionName.empty()) {
		return;
	}

	const int32_t targetGameObjectId = component.waveActionTargetGameObjectId >= 0
		? component.waveActionTargetGameObjectId
		: ownerGameObject.id;
	scriptManager_->QueueActionPayload(targetGameObjectId, actionName, payload);
}

void EditorWaveSpawnerManager::CompleteWaveIfNeeded(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	WaveRuntime& waveRuntime) {
	if (!waveRuntime.hasSpawnCompleted &&
		waveRuntime.nextSpawnIndex >= waveRuntime.targetSpawnCount) {
		waveRuntime.hasSpawnCompleted = true;

		if (component.waveCompletionMode == 0) {
			QueueAction(
				ownerGameObject,
				component,
				component.waveCompletedActionName,
				MakeIntPayload(static_cast<int32_t>(waveRuntime.spawnRecords.size())));
		}
	}

	if (!waveRuntime.hasSpawnCompleted || waveRuntime.hasAllDefeatedCompleted) {
		return;
	}

	const bool hasAllDefeated = std::all_of(
		waveRuntime.spawnRecords.begin(),
		waveRuntime.spawnRecords.end(),
		[this](const SpawnRecord& spawnRecord) {
			return IsSpawnRecordDefeated(spawnRecord);
		});

	if (!hasAllDefeated) {
		return;
	}

	waveRuntime.hasAllDefeatedCompleted = true;

	if (component.waveCompletionMode == 1) {
		QueueAction(
			ownerGameObject,
			component,
			component.waveCompletedActionName,
			MakeIntPayload(static_cast<int32_t>(waveRuntime.spawnRecords.size())));
	}

	QueueAction(
		ownerGameObject,
		component,
		component.waveAllDefeatedActionName,
		MakeIntPayload(static_cast<int32_t>(waveRuntime.spawnRecords.size())));
}

bool EditorWaveSpawnerManager::StartWave(int32_t waveSpawnerGameObjectId) {
	auto iterator = waveRuntimes_.find(waveSpawnerGameObjectId);

	if (iterator == waveRuntimes_.end()) {
		return false;
	}

	WaveRuntime& waveRuntime = iterator->second;

	if (waveRuntime.hasTriggered && !waveRuntime.hasAllDefeatedCompleted &&
		(waveRuntime.nextSpawnIndex > 0 || !waveRuntime.spawnRecords.empty())) {
		return false;
	}

	waveRuntime.nextSpawnIndex = 0;
	waveRuntime.spawnRecords.clear();
	waveRuntime.spawnTimer = 0.0f;
	waveRuntime.hasTriggered = true;
	waveRuntime.hasSpawnCompleted = false;
	waveRuntime.hasAllDefeatedCompleted = false;
	return true;
}

bool EditorWaveSpawnerManager::IsWaveComplete(int32_t waveSpawnerGameObjectId, bool waitsForAllDefeated) const {
	const auto iterator = waveRuntimes_.find(waveSpawnerGameObjectId);
	if (iterator == waveRuntimes_.end()) return false;
	return waitsForAllDefeated ? iterator->second.hasAllDefeatedCompleted : iterator->second.hasSpawnCompleted;
}

bool EditorWaveSpawnerManager::StartEncounter(int32_t encounterGameObjectId) {
	auto iterator = encounterRuntimes_.find(encounterGameObjectId);
	if (iterator == encounterRuntimes_.end()) return false;
	iterator->second = {0, 0.0f, true, false}; return true;
}

bool EditorWaveSpawnerManager::ResolveSpawnPoint(int32_t spawnPointSetGameObjectId, Vector3& position, Vector3& rotation) {
	const EditorGameObject* owner = editorScene_ != nullptr ? editorScene_->FindGameObject(spawnPointSetGameObjectId) : nullptr;
	const EditorComponent* set = owner != nullptr ? EditorComponentUtility::FindComponent(*owner, EditorComponentType::SpawnPointSet) : nullptr;
	if (set == nullptr || !set->isActive) return false;
	if (set->spawnPointSetMode == 3) {
		spawnRandomState_ = spawnRandomState_ * 1664525u + 1013904223u; const float x = static_cast<float>(spawnRandomState_ & 0xFFFFu) / 65535.0f - 0.5f;
		spawnRandomState_ = spawnRandomState_ * 1664525u + 1013904223u; const float y = static_cast<float>(spawnRandomState_ & 0xFFFFu) / 65535.0f - 0.5f;
		spawnRandomState_ = spawnRandomState_ * 1664525u + 1013904223u; const float z = static_cast<float>(spawnRandomState_ & 0xFFFFu) / 65535.0f - 0.5f;
		position.x += x * set->spawnPointVolumeSize.x; position.y += y * set->spawnPointVolumeSize.y; position.z += z * set->spawnPointVolumeSize.z; return true;
	}
	std::vector<EditorSpawnPointEntry> valid;
	for (const EditorSpawnPointEntry& entry : set->spawnPointSetEntries) if (editorScene_->FindGameObject(entry.gameObjectId) != nullptr) valid.push_back(entry);
	if (valid.empty()) return false;
	int32_t& current = spawnPointIndices_[spawnPointSetGameObjectId];
	int32_t selectedIndex = current % static_cast<int32_t>(valid.size());
	if (set->spawnPointSetMode == 1 || set->spawnPointSetMode == 2) {
		auto selectRandomIndex = [&]() {
			spawnRandomState_ = spawnRandomState_ * 1664525u + 1013904223u;
			const float random = static_cast<float>(spawnRandomState_ & 0xFFFFFFu) / 16777215.0f;

			if (set->spawnPointSetMode == 1) {
				return static_cast<int32_t>(random * static_cast<float>(valid.size())) %
					static_cast<int32_t>(valid.size());
			}

			float totalWeight = 0.0f;
			for (const EditorSpawnPointEntry& entry : valid) {
				totalWeight += (std::max)(entry.weight, 0.0f);
			}

			if (totalWeight <= 0.0f) {
				return static_cast<int32_t>(random * static_cast<float>(valid.size())) %
					static_cast<int32_t>(valid.size());
			}

			float weightCursor = random * totalWeight;
			for (int32_t index = 0; index < static_cast<int32_t>(valid.size()); index++) {
				weightCursor -= (std::max)(valid[static_cast<size_t>(index)].weight, 0.0f);

				if (weightCursor <= 0.0f) {
					return index;
				}
			}

			return static_cast<int32_t>(valid.size()) - 1;
		};

		selectedIndex = selectRandomIndex();
		const auto lastIterator = spawnPointLastIndices_.find(spawnPointSetGameObjectId);

		if (set->spawnPointAvoidImmediateRepeat && valid.size() > 1u &&
			lastIterator != spawnPointLastIndices_.end() && selectedIndex == lastIterator->second) {
			selectedIndex = selectRandomIndex();

			if (selectedIndex == lastIterator->second) {
				selectedIndex = (selectedIndex + 1) % static_cast<int32_t>(valid.size());
			}
		}
	}

	const EditorGameObject* point = editorScene_->FindGameObject(
		valid[static_cast<size_t>(selectedIndex)].gameObjectId);

	if (point == nullptr) {
		return false;
	}

	Vector3 scale{};
	editorScene_->GetWorldTransform(point->id, scale, rotation, position);
	spawnPointLastIndices_[spawnPointSetGameObjectId] = selectedIndex;
	current = (selectedIndex + 1) % static_cast<int32_t>(valid.size());
	return true;
}

void EditorWaveSpawnerManager::UpdateEncounters(float deltaTime) {
	for (auto& [ownerId, runtime] : encounterRuntimes_) {
		if (!runtime.isPlaying) continue;
		const EditorGameObject* owner = editorScene_->FindGameObject(ownerId); const EditorComponent* encounter = owner != nullptr ? EditorComponentUtility::FindComponent(*owner, EditorComponentType::EncounterController) : nullptr;
		if (encounter == nullptr || runtime.currentIndex >= static_cast<int32_t>(encounter->encounterWaveEntries.size())) {
			runtime.isPlaying = false;
			if (encounter != nullptr && scriptManager_ != nullptr && !encounter->encounterCompletedActionName.empty()) scriptManager_->QueueActionEvent(encounter->encounterActionTargetGameObjectId >= 0 ? encounter->encounterActionTargetGameObjectId : ownerId, encounter->encounterCompletedActionName);
			continue;
		}
		const EditorEncounterWaveEntry& entry = encounter->encounterWaveEntries[static_cast<size_t>(runtime.currentIndex)];
		if (!runtime.hasStartedCurrent) { runtime.delayRemaining += deltaTime; if (runtime.delayRemaining < entry.startDelay) continue; if (!StartWave(entry.waveSpawnerGameObjectId)) { runtime.currentIndex++; runtime.delayRemaining = 0.0f; continue; } runtime.hasStartedCurrent = true; }
		if (IsWaveComplete(entry.waveSpawnerGameObjectId, entry.waitsForAllDefeated)) { runtime.currentIndex++; runtime.delayRemaining = 0.0f; runtime.hasStartedCurrent = false; }
	}
}
