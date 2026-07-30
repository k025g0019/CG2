#include "EditorRuntimeManager.h"

#include "EditorSharedState.h"

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
	localMoveManager_.Initialize(editorScene_, &physicsManager_);
	railMovementManager_.Initialize(editorScene_);
	scriptManager_.SetRailMovementManager(&railMovementManager_);
	waveSpawnerManager_.Initialize(editorScene_, &railMovementManager_, &physicsManager_, &scriptManager_);
	gameplayEventManager_.Initialize(editorScene_, &railMovementManager_, &scriptManager_);
	uiBindingManager_.Initialize(editorScene_, &railMovementManager_);
	rollingMoveManager_.Initialize(editorScene_, &physicsManager_);
	navigationManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
}

#pragma warning(push)
#pragma warning(disable : 5045)
void EditorRuntimeManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isPlaying_ || editorScene_ == nullptr) {
		return;
	}

	// Input Action を最初に確定し、同じフレームの Script と移動 Component から読めるようにする。
	inputManager_.Update(keyState, deltaTime);
	scriptManager_.Update(keyState, deltaTime);

	std::string requestedScenePath;
	if (scriptManager_.ConsumeSceneLoadRequest(requestedScenePath)) {
		LoadSceneForPlay(requestedScenePath);
		return;
	}

	localMoveManager_.Update(deltaTime);
	railMovementManager_.Update(deltaTime);
	waveSpawnerManager_.Update(deltaTime);
	rollingMoveManager_.Update(deltaTime);
	aiManager_.Update(deltaTime);
	navigationManager_.Update(deltaTime);
	gameplayEventManager_.Update(deltaTime);

	if (scriptManager_.ConsumeSceneLoadRequest(requestedScenePath)) {
		LoadSceneForPlay(requestedScenePath);
		return;
	}

	int32_t fixedStepCount = physicsManager_.Update(deltaTime);  // 入力後の速度を使って物理位置を更新する
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
	animationManager_.Update(deltaTime);
	constraintManager_.Update(deltaTime);
	effectManager_.Update(deltaTime);
	effekseerManager_.Update(deltaTime);
	audioManager_.Update(deltaTime);
	freeTransformManager_.Update(deltaTime, keyState);
	uiBindingManager_.Update();
}
#pragma warning(pop)

void EditorRuntimeManager::Draw() {
	if (!isPlaying_) {
		return;
	}

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
		StopRuntimeSystems();
		if (hasSceneBackup_) {
			*editorScene_ = sceneBackup_;
			EditorSharedState::g_currentScenePath = sceneBackupPath_;
		}

		hasSceneBackup_ = false;
		return;
	}

	sceneBackup_ = *editorScene_;  // Play 開始前の編集状態を保存する
	sceneBackupPath_ = EditorSharedState::g_currentScenePath;
	hasSceneBackup_ = true;
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
	}

	isPlaying_ = true;
	physicsManager_.StartSimulation();
	effectManager_.Start();
	effekseerManager_.Start();
	animationManager_.Start();
	localMoveManager_.Start();
	railMovementManager_.Start();
	waveSpawnerManager_.Start();
	rollingMoveManager_.Start();
	aiManager_.Start();
	navigationManager_.Start();
	audioManager_.Start();
	scriptManager_.Start();
	gameplayEventManager_.Start();
}

void EditorRuntimeManager::StopRuntimeSystems() {
	gameplayEventManager_.Stop();
	waveSpawnerManager_.Stop();
	physicsManager_.StopSimulation();
	effectManager_.Stop();
	effekseerManager_.Stop();
	animationManager_.Stop();
	audioManager_.Stop();
	scriptManager_.Stop();
	localMoveManager_.Stop();
	railMovementManager_.Stop();
	rollingMoveManager_.Stop();
	aiManager_.Stop();
	navigationManager_.Stop();
	isPlaying_ = false;
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

	EditorSharedState::g_currentScenePath = scenePath;
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
