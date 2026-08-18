#include "EditorRuntimeManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>

void EditorRuntimeManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play / Stop のたびに操作する Scene
	consoleMessages_ = consoleMessages;  // Runtime 内のイベントログ出力先
	effectManager_.Initialize(editorScene_, consoleMessages_);
	effekseerManager_.InitializeScene(editorScene_, consoleMessages_);
	aiManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
	scriptManager_.Initialize(editorScene_, &inputManager_, &animationManager_, &effectManager_, &aiManager_, &physicsManager_, consoleMessages_);
	inputManager_.Initialize(editorScene_, consoleMessages_);
	animationManager_.Initialize(editorScene_, &effectManager_, &scriptManager_, consoleMessages_);
	audioManager_.Initialize(editorScene_, &physicsManager_);
	freeTransformManager_.Initialize(editorScene_);
	constraintManager_.Initialize(editorScene_);
	physicsManager_.Initialize(editorScene_, consoleMessages_);
	sceneOptimizationManager_.Initialize(
		editorScene_,
		&physicsManager_,
		&objectPoolManager_);
	targetingManager_.Initialize(editorScene_, &inputManager_, &physicsManager_, &scriptManager_);
	damageManager_.Initialize(editorScene_, &scriptManager_, &physicsManager_, &objectPoolManager_);
	objectPoolManager_.Initialize(editorScene_, &physicsManager_, &damageManager_, &scriptManager_);
	weaponManager_.Initialize(
		editorScene_,
		&inputManager_,
		&targetingManager_,
		&physicsManager_,
		&damageManager_,
		&objectPoolManager_,
		&scriptManager_,
		&effectManager_,
		&audioManager_,
		&cameraEffectManager_);
	weaponLoadoutManager_.Initialize(editorScene_, &weaponManager_, &scriptManager_);
	runtimePropertyManager_.Initialize(
		editorScene_,
		&scriptManager_,
		&targetingManager_,
		&weaponManager_,
		&damageManager_,
		&inputManager_,
		&audioManager_,
		&effectManager_);
	logMonitorManager_.Initialize(editorScene_, &profilerManager_, &weaponManager_, &runtimePropertyManager_);
	objectPoolManager_.SetRuntimeResetCallback([this](int32_t gameObjectId) {
		if (editorScene_ == nullptr) {
			return;
		}

		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr) {
			return;
		}

		const EditorComponent* resetConfiguration = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::RuntimeStateReset);

		if (resetConfiguration != nullptr && !resetConfiguration->isActive) {
			resetConfiguration = nullptr;
		}

		if (resetConfiguration == nullptr || resetConfiguration->runtimeResetHealth) {
			damageManager_.ResetRuntimeState(gameObjectId);
		}

		runtimePropertyManager_.ResetRuntimeState(gameObjectId, resetConfiguration);

		if (resetConfiguration == nullptr || resetConfiguration->runtimeResetCooldowns) {
			weaponManager_.ResetRuntimeState(gameObjectId);
		}

		if (resetConfiguration != nullptr && resetConfiguration->isActive &&
			!resetConfiguration->runtimeResetActionName.empty()) {
			const int32_t actionTargetGameObjectId = resetConfiguration->runtimeResetActionTargetGameObjectId >= 0
				? resetConfiguration->runtimeResetActionTargetGameObjectId
				: gameObjectId;
			EditorScriptActionPayload payload{};
			payload.type = EditorScriptActionPayloadTypeGameObject;
			payload.gameObjectId = gameObjectId;
			scriptManager_.QueueActionPayload(
				actionTargetGameObjectId,
				resetConfiguration->runtimeResetActionName,
				payload);
		}
	});
	cameraEffectManager_.Initialize(editorScene_);
	localMoveManager_.Initialize(editorScene_, &physicsManager_);
	railMovementManager_.Initialize(editorScene_, &physicsManager_, &inputManager_, &scriptManager_);
	physicsManager_.SetPreFixedStepCallback([this](float fixedDeltaTime) {
		railMovementManager_.FixedUpdate(fixedDeltaTime);
	});
	scriptManager_.SetRailMovementManager(&railMovementManager_);
	scriptManager_.SetGameplayManagers(
		&targetingManager_,
		&damageManager_,
		&objectPoolManager_,
		&weaponManager_,
		&cameraEffectManager_,
		&railBranchManager_);
	railBranchManager_.Initialize(editorScene_, &railMovementManager_, &scriptManager_);
	actionSequenceManager_.Initialize(editorScene_, &railMovementManager_, &scriptManager_);
	waveSpawnerManager_.Initialize(
		editorScene_,
		&railMovementManager_,
		&physicsManager_,
		&objectPoolManager_,
		&scriptManager_);
	gameplayEventManager_.Initialize(editorScene_, &railMovementManager_, &scriptManager_);
	uiBindingManager_.Initialize(editorScene_, &railMovementManager_);
	rollingMoveManager_.Initialize(editorScene_, &physicsManager_);
	navigationManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
	saveManager_.Initialize(editorScene_, &physicsManager_, &scriptManager_, consoleMessages_);
	scriptManager_.SetWorkflowManagers(&actionSequenceManager_, &saveManager_);
	scriptManager_.SetReusableGameplayManagers(
		&weaponLoadoutManager_,
		&targetingManager_,
		&runtimePropertyManager_,
		&waveSpawnerManager_);
	PublishSceneRuntimeState();
}

#pragma warning(push)
#pragma warning(disable : 5045)
void EditorRuntimeManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isPlaying_ || editorScene_ == nullptr) {
		return;
	}

	if (UpdateSceneLoading()) {
		return;
	}

	if (UpdateAutomaticSceneStreaming()) {
		return;
	}

	keyState = replayManager_.ResolveFrameInput(keyState, deltaTime);
	const float unscaledDeltaTime = deltaTime;
	const float gameTimeScale = runtimePropertyManager_.UpdateTimeScale(unscaledDeltaTime);
	deltaTime = unscaledDeltaTime * gameTimeScale;

	auto processSceneRequests = [this]() {
		EditorSceneLoadRequest sceneLoadRequest{};

		if (scriptManager_.ConsumeSceneLoadRequest(sceneLoadRequest)) {
			if (sceneLoadRequest.isAsynchronous || sceneLoadRequest.isAdditive) {
				RequestSceneLoadAsync(sceneLoadRequest.scenePath, sceneLoadRequest.isAdditive);
			}
			else {
				LoadSceneForPlay(sceneLoadRequest.scenePath);
			}

			return true;
		}

		std::string sceneUnloadPath;
		if (scriptManager_.ConsumeSceneUnloadRequest(sceneUnloadPath)) {
			RequestSceneUnload(sceneUnloadPath);
			return true;
		}

		return false;
	};

	auto profileUpdate = [this](const char* sampleName, auto&& updateFunction) {
		EditorProfilerManager::Scope profilerScope(profilerManager_, sampleName);
		updateFunction();
	};

	physicsManager_.BeginDebugFrame();  // この後に Script / AI / Audio が発行する Cast だけを今フレームの表示対象にする。

	// Input Action を最初に確定し、同じフレームの Script と移動 Component から読めるようにする。
	profileUpdate("Input", [this, keyState, unscaledDeltaTime]() {
		inputManager_.Update(keyState, unscaledDeltaTime);
	});
	profileUpdate("Scene Optimization", [this]() {
		sceneOptimizationManager_.Update();
	});
	profileUpdate("Targeting", [this, deltaTime]() {
		targetingManager_.Update(deltaTime);
	});
	profileUpdate("C++ Script Update", [this, keyState, deltaTime]() {
		scriptManager_.Update(keyState, deltaTime);
	});
	profileUpdate("Damage", [this, deltaTime]() {
		damageManager_.Update(deltaTime);
	});

	if (processSceneRequests()) {
		return;
	}

	profileUpdate("Movement and Rail", [this, deltaTime]() {
		localMoveManager_.Update(deltaTime);
		railMovementManager_.Update(deltaTime);
		railBranchManager_.Update();
		rollingMoveManager_.Update(deltaTime);
	});
	profileUpdate("Sequence and Wave", [this, deltaTime]() {
		actionSequenceManager_.Update(deltaTime);
		waveSpawnerManager_.Update(deltaTime);
		objectPoolManager_.Update(deltaTime);
		gameplayEventManager_.Update(deltaTime);
	});
	profileUpdate("Weapon", [this, deltaTime]() {
		weaponManager_.Update(deltaTime);
		weaponLoadoutManager_.Update(deltaTime);
	});
	profileUpdate("Runtime Property", [this, deltaTime]() {
		runtimePropertyManager_.Update(deltaTime);
	});
	profileUpdate("AI and Navigation", [this, deltaTime]() {
		aiManager_.Update(deltaTime);
		navigationManager_.Update(deltaTime);
	});

	if (processSceneRequests()) {
		return;
	}

	int32_t fixedStepCount = 0;
	profileUpdate("Physics", [this, deltaTime, &fixedStepCount]() {
		fixedStepCount = runtimePropertyManager_.IsPhysicsPaused()
			? 0
			: physicsManager_.Update(deltaTime);  // Pause設定でPhysicsだけを独立停止できる
	});
	float fixedTimeStep = physicsManager_.GetFixedTimeStep();  // 物理と同じ固定時間を Script 側へ渡す
	scriptManager_.SetPhysicsEvents(physicsManager_.GetFrameEvents());  // このフレームで発生した接触イベントを FixedUpdate から参照できるようにする

	if (fixedStepCount >= 4) {
		scriptManager_.FixedUpdate(fixedTimeStep);  // 1 フレーム内の最大固定更新回数は 4 回に制限しているため、ここで 4 回目を処理する
	}

	if (fixedStepCount >= 3) {
		scriptManager_.FixedUpdate(fixedTimeStep);  // 3 回以上進んだ場合も、物理後の Script 固定更新を同じ回数だけ呼ぶ
	}

	if (fixedStepCount >= 2) {
		scriptManager_.FixedUpdate(fixedTimeStep);  // 2 回分の固定更新が必要だったフレームを取りこぼさない
	}

	if (fixedStepCount >= 1) {
		scriptManager_.FixedUpdate(fixedTimeStep);  // 物理結果の後に FixedUpdate を呼び、OnCollision 相当の判定に使える順へそろえる
	}

	// Physics で確定した速度と Transform を Animator が読み、その Event から同じフレームの Effect を発生させる。
	profileUpdate("Animation and Constraint", [this, deltaTime]() {
		animationManager_.Update(deltaTime);
		constraintManager_.Update(deltaTime);
	});
	profileUpdate("Effect", [this, deltaTime]() {
		effectManager_.Update(deltaTime);
		effekseerManager_.Update(deltaTime);
	});
	profileUpdate("Audio and Haptics", [this, deltaTime]() {
		audioManager_.Update(deltaTime);
		UpdateHapticSources(deltaTime);
	});
	profileUpdate("UI and Camera", [this, deltaTime, keyState]() {
		freeTransformManager_.Update(deltaTime, keyState);
		uiBindingManager_.Update();
		cameraEffectManager_.Update(deltaTime);
	});
	profileUpdate("Log Monitor", [this, deltaTime]() {
		logMonitorManager_.Update(deltaTime);
	});
	PublishSceneRuntimeState();
}
#pragma warning(pop)

void EditorRuntimeManager::Draw() {
	if (!isPlaying_) {
		return;
	}

	EditorProfilerManager::Scope profilerScope(profilerManager_, "Runtime Debug Draw");
	inputManager_.Draw();
	effectManager_.Draw();
	audioManager_.Draw();
	aiManager_.Draw();
	localMoveManager_.Draw();
	railMovementManager_.Draw();
	rollingMoveManager_.Draw();
	navigationManager_.Draw();
	physicsManager_.Draw();
}

void EditorRuntimeManager::TogglePlay() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (isPlaying_) {
		// Stop 時は Play 開始前の Scene に戻す
		CancelPendingSceneLoad();
		StopRuntimeSystems();
		railMovementManager_.ResetSessionState();
		actionSequenceManager_.ResetSessionState();
		saveManager_.ResetSessionValues();
		if (hasSceneBackup_) {
			*editorScene_ = sceneBackup_;
			EditorSharedState::g_currentScenePath = sceneBackupPath_;
		}

		hasSceneBackup_ = false;
		additiveScenes_.clear();
		sceneLoadProgress_ = 0.0f;
		PublishSceneRuntimeState();
		return;
	}

	sceneBackup_ = *editorScene_;  // Play 開始前の編集状態を保存する
	sceneBackupPath_ = EditorSharedState::g_currentScenePath;
	hasSceneBackup_ = true;
	additiveScenes_.clear();
	sceneLoadProgress_ = 0.0f;
	railMovementManager_.ResetSessionState();
	actionSequenceManager_.ResetSessionState();
	saveManager_.ResetSessionValues();
	StartRuntimeSystems(true);
}

EditorAudioManager& EditorRuntimeManager::GetAudioManager() {
	return audioManager_;
}

const EditorAudioManager& EditorRuntimeManager::GetAudioManager() const {
	return audioManager_;
}

EditorRailMovementManager& EditorRuntimeManager::GetRailMovementManager() {
	return railMovementManager_;
}

const EditorRailMovementManager& EditorRuntimeManager::GetRailMovementManager() const {
	return railMovementManager_;
}

EditorPhysicsManager& EditorRuntimeManager::GetPhysicsManager() {
	return physicsManager_;
}

const EditorPhysicsManager& EditorRuntimeManager::GetPhysicsManager() const {
	return physicsManager_;
}

EditorProfilerManager& EditorRuntimeManager::GetProfilerManager() {
	return profilerManager_;
}

const EditorProfilerManager& EditorRuntimeManager::GetProfilerManager() const {
	return profilerManager_;
}

EditorLogMonitorManager& EditorRuntimeManager::GetLogMonitorManager() {
	return logMonitorManager_;
}

const EditorLogMonitorManager& EditorRuntimeManager::GetLogMonitorManager() const {
	return logMonitorManager_;
}

EditorReplayManager& EditorRuntimeManager::GetReplayManager() {
	return replayManager_;
}

const EditorReplayManager& EditorRuntimeManager::GetReplayManager() const {
	return replayManager_;
}

void EditorRuntimeManager::StartRuntimeSystems(bool shouldReinitializeScript) {
	if (editorScene_ == nullptr) {
		return;
	}

	if (shouldReinitializeScript) {
		scriptManager_.Initialize(
			editorScene_,
			&inputManager_,
			&animationManager_,
			&effectManager_,
			&aiManager_,
			&physicsManager_,
			consoleMessages_);
		scriptManager_.SetRailMovementManager(&railMovementManager_);
		scriptManager_.SetGameplayManagers(
			&targetingManager_,
			&damageManager_,
			&objectPoolManager_,
			&weaponManager_,
			&cameraEffectManager_,
			&railBranchManager_);
		scriptManager_.SetWorkflowManagers(&actionSequenceManager_, &saveManager_);
		scriptManager_.SetReusableGameplayManagers(
			&weaponLoadoutManager_,
			&targetingManager_,
			&runtimePropertyManager_,
			&waveSpawnerManager_);
	}

	isPlaying_ = true;
	objectPoolManager_.PreparePools();
	physicsManager_.StartSimulation();
	sceneOptimizationManager_.Start();
	objectPoolManager_.Start();
	damageManager_.Start();
	targetingManager_.Start();
	weaponManager_.Start();
	weaponLoadoutManager_.Start();
	cameraEffectManager_.Start();
	effectManager_.Start();
	effekseerManager_.Start();
	animationManager_.Start();
	localMoveManager_.Start();
	railMovementManager_.Start();
	railBranchManager_.Start();
	actionSequenceManager_.Start();
	waveSpawnerManager_.Start();
	rollingMoveManager_.Start();
	aiManager_.Start();
	navigationManager_.Start();
	audioManager_.Start();
	StartHapticSources();
	scriptManager_.Start();
	runtimePropertyManager_.Start();
	gameplayEventManager_.Start();
	saveManager_.Start();
	logMonitorManager_.Start();
	PublishSceneRuntimeState();
}

void EditorRuntimeManager::StopRuntimeSystems() {
	logMonitorManager_.Stop();
	sceneOptimizationManager_.Stop();
	saveManager_.Stop();
	actionSequenceManager_.Stop();
	cameraEffectManager_.Stop();
	weaponManager_.Stop();
	weaponLoadoutManager_.Stop();
	runtimePropertyManager_.Stop();
	targetingManager_.Stop();
	damageManager_.Stop();
	objectPoolManager_.Stop();
	railBranchManager_.Stop();
	gameplayEventManager_.Stop();
	waveSpawnerManager_.Stop();
	physicsManager_.StopSimulation();
	effectManager_.Stop();
	effekseerManager_.Stop();
	animationManager_.Stop();
	audioManager_.Stop();
	StopHapticSources();
	scriptManager_.Stop();
	localMoveManager_.Stop();
	railMovementManager_.Stop();
	rollingMoveManager_.Stop();
	aiManager_.Stop();
	navigationManager_.Stop();
	isPlaying_ = false;
}

void EditorRuntimeManager::StartHapticSources() {
	hapticLoopTimers_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* hapticSource = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::HapticSource);

		if (!gameObject.isActive ||
			hapticSource == nullptr ||
			!hapticSource->isActive ||
			!hapticSource->audioPlayOnAwake) {
			continue;
		}

		PlayHapticSource(gameObject, *hapticSource);
	}
}

void EditorRuntimeManager::UpdateHapticSources(float deltaTime) {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* hapticSource = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::HapticSource);

		if (!gameObject.isActive ||
			hapticSource == nullptr ||
			!hapticSource->isActive ||
			!hapticSource->hapticLoop) {
			hapticLoopTimers_.erase(gameObject.id);
			continue;
		}

		float& loopTimer = hapticLoopTimers_[gameObject.id];
		loopTimer -= (std::max)(deltaTime, 0.0f);

		if (loopTimer <= 0.0f) {
			PlayHapticSource(gameObject, *hapticSource);
		}
	}
}

void EditorRuntimeManager::StopHapticSources() {
	EditorSharedState::g_feelKitHaptics.stop();
	hapticLoopTimers_.clear();
}

bool EditorRuntimeManager::PlayHapticSource(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	FeelKitHapticsVibrationDesc vibrationDesc{};
	vibrationDesc.leftStrength = (std::clamp)(component.hapticStrength, 0.0f, 1.0f);
	vibrationDesc.rightStrength = vibrationDesc.leftStrength;
	vibrationDesc.durationMs = (std::max)(component.hapticDurationMs, 1);
	vibrationDesc.isEnabled = true;

	const bool wasPlayed = component.assetPath.empty()
		? EditorSharedState::g_feelKitHaptics.playOneShot(vibrationDesc)
		: EditorSharedState::g_feelKitHaptics.vibrateSound(component.assetPath.c_str(), vibrationDesc);

	if (component.hapticLoop) {
		hapticLoopTimers_[gameObject.id] =
			(std::max)(static_cast<float>(vibrationDesc.durationMs) / 1000.0f, 0.016f);
	}

	return wasPlayed;
}

bool EditorRuntimeManager::LoadSceneForPlay(const std::string& scenePath) {
	if (editorScene_ == nullptr || !isPlaying_) {
		return false;
	}

	const EditorScene previousScene = *editorScene_;
	const std::string previousScenePath = EditorSharedState::g_currentScenePath;
	StopRuntimeSystems();

	if (!editorScene_->LoadScene(scenePath)) {
		*editorScene_ = previousScene;
		EditorSharedState::g_currentScenePath = previousScenePath;
		Initialize(editorScene_, consoleMessages_);
		StartRuntimeSystems(false);

		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back("Scene: 遷移に失敗 " + scenePath);
		}

		return false;
	}

	railMovementManager_.ResetSessionState();
	actionSequenceManager_.ResetSessionState();
	saveManager_.ResetSceneState();
	EditorSharedState::g_currentScenePath = scenePath;
	additiveScenes_.clear();
	Initialize(editorScene_, consoleMessages_);
	StartRuntimeSystems(false);

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back("Scene: 遷移 " + scenePath);
	}

	return true;
}

bool EditorRuntimeManager::IsPlaying() const {
	return isPlaying_;
}

EditorScriptManager& EditorRuntimeManager::GetScriptManager() {
	return scriptManager_;
}

const EditorScriptManager& EditorRuntimeManager::GetScriptManager() const {
	return scriptManager_;
}

EditorAnimationManager& EditorRuntimeManager::GetAnimationManager() {
	return animationManager_;
}

const EditorAnimationManager& EditorRuntimeManager::GetAnimationManager() const {
	return animationManager_;
}

EditorEffectManager& EditorRuntimeManager::GetEffectManager() {
	return effectManager_;
}

const EditorEffectManager& EditorRuntimeManager::GetEffectManager() const {
	return effectManager_;
}

EditorEffekseerManager& EditorRuntimeManager::GetEffekseerManager() {
	return effekseerManager_;
}

const EditorEffekseerManager& EditorRuntimeManager::GetEffekseerManager() const {
	return effekseerManager_;
}

bool EditorRuntimeManager::PlayEffect(int32_t gameObjectId) {
	const bool hasBuiltInEffect = effectManager_.PlayEffect(gameObjectId);
	const bool hasEffekseerEffect = effekseerManager_.PlayEffect(gameObjectId);
	return hasBuiltInEffect || hasEffekseerEffect;
}

void EditorRuntimeManager::StopEffect(int32_t gameObjectId) {
	effectManager_.StopEffect(gameObjectId);
	effekseerManager_.StopEffect(gameObjectId);
}

int32_t EditorRuntimeManager::GetAliveEffectCount(int32_t gameObjectId) const {
	return effectManager_.GetAliveParticleCount(gameObjectId) +
		effekseerManager_.GetAliveEffectCount(gameObjectId);
}

bool EditorRuntimeManager::RequestSceneLoad(const std::string& scenePath) {
	if (!isPlaying_) {
		return false;
	}

	return scriptManager_.RequestSceneLoad(scenePath);
}

bool EditorRuntimeManager::RequestSceneLoadAsync(
	const std::string& scenePath,
	bool isAdditive) {
	if (!isPlaying_ || scenePath.empty() || isSceneLoading_) {
		return false;
	}

	const std::string normalizedScenePath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();
	if (!std::filesystem::exists(normalizedScenePath)) {
		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back("Scene: 非同期読込対象がありません " + normalizedScenePath);
		}

		return false;
	}

	if (isAdditive && IsSceneLoaded(normalizedScenePath)) {
		return false;
	}

	pendingScenePath_ = normalizedScenePath;
	pendingSceneIsAdditive_ = isAdditive;
	isSceneLoading_ = true;
	sceneLoadProgress_ = 0.1f;
	try {
		pendingSceneLoadFuture_ = std::async(
			std::launch::async,
			[normalizedScenePath]() {
				AsyncSceneLoadResult result{};
				result.wasLoaded = result.loadedScene.LoadScene(normalizedScenePath);
				return result;
			});
	}
	catch (const std::exception&) {
		pendingScenePath_.clear();
		pendingSceneIsAdditive_ = false;
		isSceneLoading_ = false;
		sceneLoadProgress_ = 0.0f;
		PublishSceneRuntimeState();
		return false;
	}
	PublishSceneRuntimeState();
	return true;
}

bool EditorRuntimeManager::RequestSceneUnload(const std::string& scenePath) {
	if (!isPlaying_ || editorScene_ == nullptr || isSceneLoading_) {
		return false;
	}

	const std::string normalizedScenePath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();
	const auto sceneIterator = std::find_if(
		additiveScenes_.begin(),
		additiveScenes_.end(),
		[&normalizedScenePath](const AdditiveSceneRecord& record) {
			return record.scenePath == normalizedScenePath;
		});

	if (sceneIterator == additiveScenes_.end()) {
		return false;
	}

	const std::vector<int32_t> removingGameObjectIds = sceneIterator->gameObjectIds;
	StopRuntimeSystems();

	for (const int32_t gameObjectId : removingGameObjectIds) {
		editorScene_->DeleteGameObject(gameObjectId);
	}

	additiveScenes_.erase(sceneIterator);
	Initialize(editorScene_, consoleMessages_);
	StartRuntimeSystems(false);

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back("Scene: Additive破棄 " + normalizedScenePath);
	}

	return true;
}

float EditorRuntimeManager::GetSceneLoadProgress() const {
	return sceneLoadProgress_;
}

bool EditorRuntimeManager::IsSceneLoading() const {
	return isSceneLoading_;
}

bool EditorRuntimeManager::IsSceneLoaded(const std::string& scenePath) const {
	const std::string normalizedScenePath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();
	if (EditorSharedState::g_currentScenePath == normalizedScenePath) {
		return true;
	}

	return std::any_of(
		additiveScenes_.begin(),
		additiveScenes_.end(),
		[&normalizedScenePath](const AdditiveSceneRecord& record) {
			return record.scenePath == normalizedScenePath;
		});
}

bool EditorRuntimeManager::UpdateAutomaticSceneStreaming() {
	if (editorScene_ == nullptr || isSceneLoading_) {
		return false;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* streamingComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SceneStreaming);

		if (streamingComponent == nullptr || !streamingComponent->isActive ||
			streamingComponent->sceneStreamingScenePath.empty()) {
			continue;
		}

		streamingComponent->sceneStreamingRuntimeLoaded = IsSceneLoaded(
			streamingComponent->sceneStreamingScenePath);
		streamingComponent->sceneStreamingRuntimePending =
			isSceneLoading_ &&
			std::filesystem::path(pendingScenePath_).lexically_normal() ==
			std::filesystem::path(streamingComponent->sceneStreamingScenePath).lexically_normal();

		Vector3 referencePosition{};

		if (!ResolveStreamingReferencePosition(
				streamingComponent->sceneStreamingReferenceGameObjectId,
				referencePosition)) {
			continue;
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
		const float differenceX = gameObjectPosition.x - referencePosition.x;
		const float differenceY = gameObjectPosition.y - referencePosition.y;
		const float differenceZ = gameObjectPosition.z - referencePosition.z;
		const float distanceSquared =
			differenceX * differenceX + differenceY * differenceY + differenceZ * differenceZ;
		const float loadDistance = (std::max)(streamingComponent->sceneStreamingLoadDistance, 0.0f);
		const float unloadDistance = (std::max)(
			streamingComponent->sceneStreamingUnloadDistance,
			loadDistance);

		if (!streamingComponent->sceneStreamingRuntimeLoaded &&
			distanceSquared <= loadDistance * loadDistance) {
			streamingComponent->sceneStreamingRuntimePending = RequestSceneLoadAsync(
				streamingComponent->sceneStreamingScenePath,
				true);
			return streamingComponent->sceneStreamingRuntimePending;
		}

		if (streamingComponent->sceneStreamingRuntimeLoaded &&
			streamingComponent->sceneStreamingUnloadWhenFar &&
			distanceSquared > unloadDistance * unloadDistance &&
			std::filesystem::path(EditorSharedState::g_currentScenePath).lexically_normal() !=
			std::filesystem::path(streamingComponent->sceneStreamingScenePath).lexically_normal()) {
			return RequestSceneUnload(streamingComponent->sceneStreamingScenePath);
		}
	}

	return false;
}

bool EditorRuntimeManager::ResolveStreamingReferencePosition(
	int32_t gameObjectId,
	Vector3& position) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* referenceGameObject = gameObjectId >= 0
		? editorScene_->FindGameObject(gameObjectId)
		: nullptr;

	if (referenceGameObject == nullptr) {
		int32_t highestPriority = INT32_MIN;

		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::Camera);

			if (cameraComponent == nullptr || !cameraComponent->isActive) {
				cameraComponent = EditorComponentUtility::FindComponent(
					gameObject,
					EditorComponentType::CinemachineCamera);
			}

			if (cameraComponent != nullptr && cameraComponent->isActive &&
				cameraComponent->cameraPriority > highestPriority) {
				highestPriority = cameraComponent->cameraPriority;
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

bool EditorRuntimeManager::UpdateSceneLoading() {
	if (!isSceneLoading_ || !pendingSceneLoadFuture_.valid()) {
		return false;
	}

	if (pendingSceneLoadFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
		sceneLoadProgress_ = (std::min)(sceneLoadProgress_ + 0.01f, 0.9f);
		PublishSceneRuntimeState();
		return false;
	}

	AsyncSceneLoadResult result{};
	try {
		result = pendingSceneLoadFuture_.get();
	}
	catch (const std::exception&) {
		pendingScenePath_.clear();
		pendingSceneIsAdditive_ = false;
		isSceneLoading_ = false;
		sceneLoadProgress_ = 0.0f;
		PublishSceneRuntimeState();

		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back("Scene: 非同期読込中に例外が発生しました");
		}

		return false;
	}
	const std::string completedScenePath = pendingScenePath_;
	const bool completedSceneWasAdditive = pendingSceneIsAdditive_;
	pendingScenePath_.clear();
	pendingSceneIsAdditive_ = false;
	isSceneLoading_ = false;

	if (!result.wasLoaded) {
		sceneLoadProgress_ = 0.0f;
		PublishSceneRuntimeState();

		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back("Scene: 非同期読込に失敗 " + completedScenePath);
		}

		return false;
	}

	sceneLoadProgress_ = 1.0f;
	return ApplyLoadedScene(
		completedScenePath,
		completedSceneWasAdditive,
		std::move(result.loadedScene));
}

bool EditorRuntimeManager::ApplyLoadedScene(
	const std::string& scenePath,
	bool isAdditive,
	EditorScene&& loadedScene) {
	if (editorScene_ == nullptr || !isPlaying_) {
		return false;
	}

	StopRuntimeSystems();

	if (isAdditive) {
		AdditiveSceneRecord additiveScene{};
		additiveScene.scenePath = scenePath;

		if (!editorScene_->MergeScene(loadedScene, additiveScene.gameObjectIds)) {
			Initialize(editorScene_, consoleMessages_);
			StartRuntimeSystems(false);
			return false;
		}

		additiveScenes_.push_back(additiveScene);
	}
	else {
		railMovementManager_.ResetSessionState();
		actionSequenceManager_.ResetSessionState();
		saveManager_.ResetSceneState();
		*editorScene_ = std::move(loadedScene);
		EditorSharedState::g_currentScenePath = scenePath;
		additiveScenes_.clear();
	}

	Initialize(editorScene_, consoleMessages_);
	StartRuntimeSystems(false);

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(
			isAdditive
				? "Scene: Additive読込 " + scenePath
				: "Scene: 非同期遷移 " + scenePath);
	}

	return true;
}

void EditorRuntimeManager::CancelPendingSceneLoad() {
	if (pendingSceneLoadFuture_.valid()) {
		try {
			pendingSceneLoadFuture_.wait();
			pendingSceneLoadFuture_.get();
		}
		catch (const std::exception&) {
			// 停止処理では結果を破棄し、次回Playへ例外を持ち越さない。
		}
	}

	pendingScenePath_.clear();
	pendingSceneIsAdditive_ = false;
	isSceneLoading_ = false;
}

void EditorRuntimeManager::PublishSceneRuntimeState() {
	std::vector<std::string> loadedScenePaths;

	if (!EditorSharedState::g_currentScenePath.empty()) {
		loadedScenePaths.push_back(EditorSharedState::g_currentScenePath);
	}

	for (const AdditiveSceneRecord& record : additiveScenes_) {
		loadedScenePaths.push_back(record.scenePath);
	}

	scriptManager_.SetSceneRuntimeState(
		sceneLoadProgress_,
		isSceneLoading_,
		loadedScenePaths);
}
