#include "EditorRuntimeManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <numbers>

void EditorRuntimeManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play / Stop のたびに操作する Scene
	consoleMessages_ = consoleMessages;  // Runtime 内のイベントログ出力先
	effectManager_.Initialize(editorScene_, consoleMessages_);
	effekseerManager_.InitializeScene(editorScene_, consoleMessages_);
	vfxManager_.Initialize(editorScene_, consoleMessages_);
	aiManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
	scriptManager_.Initialize(editorScene_, &inputManager_, &animationManager_, &effectManager_, &audioManager_, &aiManager_, &physicsManager_, consoleMessages_);
	scriptManager_.SetProfilerManager(&profilerManager_);
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
		&vfxManager_,
		&audioManager_,
		&cameraEffectManager_,
		consoleMessages_);
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
	physicsManager_.SetPostFixedStepCallback([this](float fixedDeltaTime) {
		railMovementManager_.PostFixedUpdate(fixedDeltaTime);
	});
	scriptManager_.SetRailMovementManager(&railMovementManager_);
	scriptManager_.SetEffekseerManager(&effekseerManager_);
	scriptManager_.SetVfxManager(&vfxManager_);
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
	uiBindingManager_.Initialize(editorScene_, &railMovementManager_, &weaponLoadoutManager_);
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

	profilerManager_.UpdateMeasurement();

	if (sceneTransitionState_.active) {
		// 演出中はSceneが切り替わる可能性があるため、他のGameplay系Updateを止めて専念する。
		UpdateSceneTransition(deltaTime);
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
	deltaTime = unscaledDeltaTime * gameTimeScale * EditorSharedState::g_pvShootManualTimeScale;

	auto processSceneRequests = [this]() {
		EditorSceneLoadRequest sceneLoadRequest{};

		if (scriptManager_.ConsumeSceneLoadRequest(sceneLoadRequest)) {
			const bool isPlainSyncLoad = !sceneLoadRequest.isAsynchronous && !sceneLoadRequest.isAdditive;

			if (isPlainSyncLoad && TryStartSceneTransitionForRequest(sceneLoadRequest.scenePath)) {
				return true;
			}

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

	profileUpdate("Movement and Rail", [this, deltaTime, &profileUpdate]() {
		profileUpdate("LocalMove.Update", [this, deltaTime]() { localMoveManager_.Update(deltaTime); });
		profileUpdate("RailMovement.Update", [this, deltaTime]() { railMovementManager_.Update(deltaTime); });
		profileUpdate("RailBranch.Update", [this]() { railBranchManager_.Update(); });
		profileUpdate("RollingMove.Update", [this, deltaTime]() { rollingMoveManager_.Update(deltaTime); });
	});
	profileUpdate("Sequence and Wave", [this, deltaTime, &profileUpdate]() {
		profileUpdate("ActionSequence.Update", [this, deltaTime]() { actionSequenceManager_.Update(deltaTime); });
		profileUpdate("WaveSpawner.Update", [this, deltaTime]() { waveSpawnerManager_.Update(deltaTime); });
		profileUpdate("ObjectPool.Update", [this, deltaTime]() { objectPoolManager_.Update(deltaTime); });
		profileUpdate("GameplayEvent.Update", [this, deltaTime]() { gameplayEventManager_.Update(deltaTime); });
	});
	profileUpdate("Weapon", [this, deltaTime, &profileUpdate]() {
		profileUpdate("Weapon.Update", [this, deltaTime]() { weaponManager_.Update(deltaTime); });
		profileUpdate("WeaponLoadout.Update", [this, deltaTime]() { weaponLoadoutManager_.Update(deltaTime); });
	});
	profileUpdate("Runtime Property", [this, deltaTime]() {
		runtimePropertyManager_.Update(deltaTime);
	});
	profileUpdate("AI and Navigation", [this, deltaTime, &profileUpdate]() {
		profileUpdate("AI.Update", [this, deltaTime]() { aiManager_.Update(deltaTime); });
		profileUpdate("Navigation.Update", [this, deltaTime]() { navigationManager_.Update(deltaTime); });
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
	scriptManager_.SetWireEvents(physicsManager_.GetFrameWireEvents());  // 同じ固定更新で確定したWireイベントも通知する

	profileUpdate("C++ Script FixedUpdate", [this, fixedStepCount, fixedTimeStep]() {
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
	});

	// Physics で確定した速度と Transform を Animator が読み、その Event から同じフレームの Effect を発生させる。
	profileUpdate("Animation and Constraint", [this, deltaTime, &profileUpdate]() {
		profileUpdate("Animation.Update", [this, deltaTime]() { animationManager_.Update(deltaTime); });
		profileUpdate("Constraint.Update", [this, deltaTime]() { constraintManager_.Update(deltaTime); });
	});
	profileUpdate("Effect", [this, deltaTime, &profileUpdate]() {
		profileUpdate("Effect.Update", [this, deltaTime]() { effectManager_.Update(deltaTime); });
		profileUpdate("Effekseer.Update", [this, deltaTime]() { effekseerManager_.Update(deltaTime); });
		profileUpdate("Vfx.Update", [this, deltaTime]() { vfxManager_.Update(deltaTime); });
	});
	profileUpdate("Audio and Haptics", [this, deltaTime, &profileUpdate]() {
		profileUpdate("Audio.Update", [this, deltaTime]() { audioManager_.Update(deltaTime); });
		profileUpdate("Haptics.Update", [this, deltaTime]() { UpdateHapticSources(deltaTime); });
	});
	profileUpdate("UI and Camera", [this, deltaTime, keyState, &profileUpdate]() {
		profileUpdate("FreeTransform.Update", [this, deltaTime, keyState]() { freeTransformManager_.Update(deltaTime, keyState); });
		profileUpdate("UiBinding.Update", [this]() { uiBindingManager_.Update(); });
		profileUpdate("CameraEffect.Update", [this, deltaTime, keyState]() {
			cameraEffectManager_.Update(deltaTime, keyState);
		});
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
			&audioManager_,
			&aiManager_,
			&physicsManager_,
			consoleMessages_);
		scriptManager_.SetRailMovementManager(&railMovementManager_);
		scriptManager_.SetEffekseerManager(&effekseerManager_);
		scriptManager_.SetVfxManager(&vfxManager_);
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
	vfxManager_.Start();
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
	// Pool Itemの複製(Duplicate + 物理/Script登録)はPlay中に行うと単発のHitchになるため、
	// Physics/ScriptのStartが済んだこの時点で初期容量分をまとめて実体化しておく。
	objectPoolManager_.PrewarmAllPools();
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
	vfxManager_.Stop();
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

EditorVfxManager& EditorRuntimeManager::GetVfxManager() {
	return vfxManager_;
}

const EditorVfxManager& EditorRuntimeManager::GetVfxManager() const {
	return vfxManager_;
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
			[normalizedScenePath, profilerManager = &profilerManager_]() {
				EditorProfilerManager::Scope profilerScope(
					*profilerManager,
					"Scene Load Worker");
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

namespace {
	// EditorGameViewManager.cpp の FindRuntimeCameraComponent 相当。
	// SceneTransitionのDive着地姿勢を求めるためだけに、最優先Cameraの生Transformを直接読む。
	bool FindHighestPriorityCameraRawTransform(const EditorScene& scene, Transforms& outTransform) {
		const EditorGameObject* selectedGameObject = nullptr;
		const EditorComponent* selectedComponent = nullptr;
		int32_t selectedPriority = INT32_MIN;

		for (const EditorGameObject& gameObject : scene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* cameraComponent =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
			if (cameraComponent == nullptr || !cameraComponent->isActive) {
				cameraComponent =
					EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CinemachineCamera);
			}

			if (cameraComponent == nullptr || !cameraComponent->isActive ||
				cameraComponent->cameraPriority <= selectedPriority) {
				continue;
			}

			selectedGameObject = &gameObject;
			selectedComponent = cameraComponent;
			selectedPriority = cameraComponent->cameraPriority;
		}

		if (selectedGameObject == nullptr || selectedComponent == nullptr) {
			return false;
		}

		outTransform.translate = selectedGameObject->translate;
		outTransform.rotate = selectedGameObject->rotate;
		outTransform.scale = {1.0f, 1.0f, 1.0f};
		return true;
	}

	Transforms LerpTransformsLocal(const Transforms& sourceTransform, const Transforms& targetTransform, float ratio) {
		const auto lerpFloat = [](float sourceValue, float targetValue, float lerpRatio) {
			return sourceValue + (targetValue - sourceValue) * lerpRatio;
		};
		Transforms result{};
		result.translate = {
			lerpFloat(sourceTransform.translate.x, targetTransform.translate.x, ratio),
			lerpFloat(sourceTransform.translate.y, targetTransform.translate.y, ratio),
			lerpFloat(sourceTransform.translate.z, targetTransform.translate.z, ratio)};
		result.rotate = {
			lerpFloat(sourceTransform.rotate.x, targetTransform.rotate.x, ratio),
			lerpFloat(sourceTransform.rotate.y, targetTransform.rotate.y, ratio),
			lerpFloat(sourceTransform.rotate.z, targetTransform.rotate.z, ratio)};
		result.scale = {1.0f, 1.0f, 1.0f};
		return result;
	}

	float SmoothStepRatio(float ratio) {
		const float clamped = (std::clamp)(ratio, 0.0f, 1.0f);
		return clamped * clamped * (3.0f - 2.0f * clamped);
	}
}

bool EditorRuntimeManager::StartSceneTransition(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& transitionComponent) {
	(void)ownerGameObject;

	if (!isPlaying_ || editorScene_ == nullptr) {
		return false;
	}

	if (sceneTransitionState_.active) {
		return false;  // 多重起動は禁止。先に開始した演出を優先する。
	}

	if (transitionComponent.sceneTransitionType == 0 || transitionComponent.sceneTransitionTargetScenePath.empty()) {
		return false;
	}

	sceneTransitionState_ = SceneTransitionRuntimeState{};
	sceneTransitionState_.active = true;
	sceneTransitionState_.type = transitionComponent.sceneTransitionType;
	sceneTransitionState_.targetScenePath = transitionComponent.sceneTransitionTargetScenePath;
	sceneTransitionState_.outDuration = (std::max)(transitionComponent.sceneTransitionOutDuration, 0.001f);
	sceneTransitionState_.holdSeconds = (std::max)(transitionComponent.sceneTransitionHoldSeconds, 0.0f);
	sceneTransitionState_.inDuration = (std::max)(transitionComponent.sceneTransitionInDuration, 0.001f);
	sceneTransitionState_.color = transitionComponent.sceneTransitionColor;
	sceneTransitionState_.cameraDiveSourceGameObjectId = transitionComponent.sceneTransitionCameraDiveSourceGameObjectId;
	sceneTransitionState_.cameraDivePositionOffset = transitionComponent.sceneTransitionCameraDivePositionOffset;
	sceneTransitionState_.cameraDiveRotationDegrees = transitionComponent.sceneTransitionCameraDiveRotationDegrees;
	sceneTransitionState_.diveActive = (sceneTransitionState_.type == 3);
	sceneTransitionState_.phase = 0;
	sceneTransitionState_.elapsed = 0.0f;

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back("Scene: 遷移演出開始 -> " + sceneTransitionState_.targetScenePath);
	}

	return true;
}

bool EditorRuntimeManager::TryStartSceneTransitionForRequest(const std::string& scenePath) {
	if (editorScene_ == nullptr || sceneTransitionState_.active) {
		return false;
	}

	const std::string normalizedTargetPath =
		std::filesystem::path(scenePath).lexically_normal().generic_string();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* transitionComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SceneTransition);

		if (transitionComponent == nullptr || !transitionComponent->isActive ||
			transitionComponent->sceneTransitionType == 0) {
			continue;
		}

		const std::string normalizedComponentPath = std::filesystem::path(
			transitionComponent->sceneTransitionTargetScenePath).lexically_normal().generic_string();

		if (normalizedComponentPath == normalizedTargetPath) {
			return StartSceneTransition(gameObject, *transitionComponent);
		}
	}

	return false;
}

bool EditorRuntimeManager::IsSceneTransitionActive() const {
	return sceneTransitionState_.active;
}

void EditorRuntimeManager::GetSceneTransitionOverlay(Vector3& color, float& alpha) const {
	color = sceneTransitionState_.color;

	if (!sceneTransitionState_.active) {
		alpha = 0.0f;
		return;
	}

	if (sceneTransitionState_.phase == 0) {
		alpha = (std::clamp)(sceneTransitionState_.elapsed / sceneTransitionState_.outDuration, 0.0f, 1.0f);
	}
	else if (sceneTransitionState_.phase == 1) {
		alpha = 1.0f;
	}
	else {
		alpha = 1.0f - (std::clamp)(sceneTransitionState_.elapsed / sceneTransitionState_.inDuration, 0.0f, 1.0f);
	}
}

void EditorRuntimeManager::BeginSceneTransitionRevealPhase() {
	SceneTransitionRuntimeState& state = sceneTransitionState_;
	state.phase = 1;
	state.elapsed = 0.0f;

	if (!state.diveActive || editorScene_ == nullptr) {
		return;
	}

	Transforms landingTransform{};
	bool foundLandingTransform = FindHighestPriorityCameraRawTransform(*editorScene_, landingTransform);

	if (state.cameraDiveSourceGameObjectId >= 0) {
		const EditorGameObject* sourceObject = editorScene_->FindGameObject(state.cameraDiveSourceGameObjectId);
		if (sourceObject != nullptr) {
			landingTransform.translate = sourceObject->translate;
			foundLandingTransform = true;
		}
	}

	if (!foundLandingTransform) {
		state.diveActive = false;  // Cameraが見つからない場合はOverlayのみの演出にする。
		return;
	}

	state.diveEndTransform = landingTransform;
	state.diveOverheadTransform.translate = {
		landingTransform.translate.x + state.cameraDivePositionOffset.x,
		landingTransform.translate.y + state.cameraDivePositionOffset.y,
		landingTransform.translate.z + state.cameraDivePositionOffset.z};
	constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
	state.diveOverheadTransform.rotate = {
		state.cameraDiveRotationDegrees.x * degreesToRadians,
		state.cameraDiveRotationDegrees.y * degreesToRadians,
		state.cameraDiveRotationDegrees.z * degreesToRadians};
	state.diveOverheadTransform.scale = {1.0f, 1.0f, 1.0f};

	EditorSharedState::g_runtimeGameCameraOverrideTransform = state.diveOverheadTransform;
	EditorSharedState::g_runtimeGameCameraOverrideActive = true;
}

void EditorRuntimeManager::UpdateSceneTransition(float deltaTime) {
	SceneTransitionRuntimeState& state = sceneTransitionState_;
	state.elapsed += deltaTime;

	if (state.phase == 0) {
		if (state.elapsed >= state.outDuration) {
			const std::string targetPath = state.targetScenePath;
			LoadSceneForPlay(targetPath);  // このタイミングで実際にSceneを差し替える(画面は覆われている)
			BeginSceneTransitionRevealPhase();
		}
	}
	else if (state.phase == 1) {
		if (state.diveActive) {
			EditorSharedState::g_runtimeGameCameraOverrideTransform = state.diveOverheadTransform;
			EditorSharedState::g_runtimeGameCameraOverrideActive = true;
		}

		if (state.elapsed >= state.holdSeconds) {
			state.phase = 2;
			state.elapsed = 0.0f;
		}
	}
	else {
		const float ratio = SmoothStepRatio(state.elapsed / state.inDuration);

		if (state.diveActive) {
			EditorSharedState::g_runtimeGameCameraOverrideTransform =
				LerpTransformsLocal(state.diveOverheadTransform, state.diveEndTransform, ratio);
			EditorSharedState::g_runtimeGameCameraOverrideActive = true;
		}

		if (state.elapsed >= state.inDuration) {
			state.active = false;

			if (state.diveActive) {
				// 最終姿勢は既に着地Cameraと一致しているため、上書きを止めても見た目は連続する。
				EditorSharedState::g_runtimeGameCameraOverrideActive = false;
			}
		}
	}
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

void EditorRuntimeManager::CollectAreaGameObjectIds(
	int32_t rootGameObjectId,
	std::vector<int32_t>& outGameObjectIds) const {
	if (editorScene_ == nullptr) {
		return;
	}

	const EditorGameObject* rootGameObject = editorScene_->FindGameObject(rootGameObjectId);
	if (rootGameObject == nullptr) {
		return;
	}

	outGameObjectIds.push_back(rootGameObjectId);

	// Hookは子GameObjectとして置く構成が前提なので、Root配下を再帰的に全て対象にする。
	for (const int32_t childGameObjectId : rootGameObject->children) {
		CollectAreaGameObjectIds(childGameObjectId, outGameObjectIds);
	}
}

bool EditorRuntimeManager::CaptureAreaState(int32_t areaRootGameObjectId) {
	if (editorScene_ == nullptr || areaRootGameObjectId < 0) {
		return false;
	}

	std::vector<int32_t> areaGameObjectIds;
	CollectAreaGameObjectIds(areaRootGameObjectId, areaGameObjectIds);

	if (areaGameObjectIds.empty()) {
		return false;
	}

	std::vector<AreaObjectState> capturedStates;
	capturedStates.reserve(areaGameObjectIds.size());

	for (const int32_t gameObjectId : areaGameObjectIds) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr) {
			continue;
		}

		AreaObjectState state{};
		state.gameObjectId = gameObjectId;
		// Transformは親空間のローカル値で控える。復元時も同じ親子構成へ書き戻す。
		state.translate = gameObject->translate;
		state.rotate = gameObject->rotate;
		state.scale = gameObject->scale;
		state.isActive = gameObject->isActive;

		const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::RigidBody);
		if (rigidBody != nullptr) {
			state.hasRigidBody = true;
			state.velocity = rigidBody->velocity;
			state.angularVelocity = rigidBody->angularVelocity;
		}

		capturedStates.push_back(state);
	}

	areaStates_[areaRootGameObjectId] = std::move(capturedStates);
	return true;
}

bool EditorRuntimeManager::HasAreaState(int32_t areaRootGameObjectId) const {
	return areaStates_.find(areaRootGameObjectId) != areaStates_.end();
}

bool EditorRuntimeManager::ResetArea(int32_t areaRootGameObjectId) {
	if (editorScene_ == nullptr) {
		return false;
	}

	const auto areaStateIterator = areaStates_.find(areaRootGameObjectId);
	if (areaStateIterator == areaStates_.end()) {
		return false;
	}

	// 先にこのエリアのHookへ繋がっているWireを破棄する。
	// 物体を戻した後にWireが残っていると、保存時と噛み合わない長さのまま張力が発生する。
	std::vector<EditorPhysicsManager::WireHandle> wireHandlesToDestroy;

	for (const auto& [wireHandle, wireState] : physicsManager_.GetRuntimeWires()) {
		const bool isFirstInsideArea = std::any_of(
			areaStateIterator->second.begin(),
			areaStateIterator->second.end(),
			[&wireState](const AreaObjectState& state) {
				return state.gameObjectId == wireState.desc.firstGameObjectId;
			});
		const bool isSecondInsideArea = std::any_of(
			areaStateIterator->second.begin(),
			areaStateIterator->second.end(),
			[&wireState](const AreaObjectState& state) {
				return state.gameObjectId == wireState.desc.secondGameObjectId;
			});

		if (isFirstInsideArea || isSecondInsideArea) {
			wireHandlesToDestroy.push_back(wireHandle);
		}
	}

	for (const EditorPhysicsManager::WireHandle wireHandle : wireHandlesToDestroy) {
		physicsManager_.DestroyWire(wireHandle);
	}

	for (const AreaObjectState& state : areaStateIterator->second) {
		EditorGameObject* gameObject = editorScene_->FindGameObject(state.gameObjectId);
		if (gameObject == nullptr) {
			continue;
		}

		gameObject->translate = state.translate;
		gameObject->rotate = state.rotate;
		gameObject->scale = state.scale;
		gameObject->isActive = state.isActive;

		if (state.hasRigidBody) {
			EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
				*gameObject,
				EditorComponentType::RigidBody);
			if (rigidBody != nullptr) {
				rigidBody->velocity = state.velocity;
				rigidBody->angularVelocity = state.angularVelocity;
			}

			// Component側の値だけ戻してもJolt内部のBodyは動き続けるため、実物理へも反映する。
			physicsManager_.SetVelocity(state.gameObjectId, state.velocity);
			physicsManager_.SetAngularVelocity(state.gameObjectId, state.angularVelocity);
		}

		// Animationで動かしている物体は再生位置も先頭へ戻す。Animation非所持なら何も起きない。
		animationManager_.SetAnimationTime(state.gameObjectId, 0.0f);
	}

	return true;
}
