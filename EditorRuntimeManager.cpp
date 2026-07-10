#include "EditorRuntimeManager.h"

void EditorRuntimeManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play / Stop のたびに操作する Scene
	consoleMessages_ = consoleMessages;  // Runtime 内のイベントログ出力先
	aiManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
	scriptManager_.Initialize(editorScene_, &inputManager_, &animationManager_, &aiManager_, &physicsManager_, consoleMessages_);
	inputManager_.Initialize(editorScene_, consoleMessages_);
	animationManager_.Initialize(editorScene_);
	audioManager_.Initialize(editorScene_);
	constraintManager_.Initialize(editorScene_);
	physicsManager_.Initialize(editorScene_, consoleMessages_);
	localMoveManager_.Initialize(editorScene_, &physicsManager_);
	rollingMoveManager_.Initialize(editorScene_, &physicsManager_);
	navigationManager_.Initialize(editorScene_, &physicsManager_, consoleMessages_);
}

#pragma warning(push)
#pragma warning(disable : 5045)
void EditorRuntimeManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isPlaying_ || editorScene_ == nullptr) {
		return;
	}

	scriptManager_.Update(keyState, deltaTime);
	animationManager_.Update(deltaTime);
	audioManager_.Update(deltaTime);
	inputManager_.Update(keyState, deltaTime);
	constraintManager_.Update(deltaTime);
	localMoveManager_.Update(deltaTime);
	rollingMoveManager_.Update(deltaTime);
	aiManager_.Update(deltaTime);
	navigationManager_.Update(deltaTime);
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
}
#pragma warning(pop)

void EditorRuntimeManager::Draw() {
	if (!isPlaying_) {
		return;
	}

	inputManager_.Draw();
	audioManager_.Draw();
	aiManager_.Draw();
	localMoveManager_.Draw();
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
		physicsManager_.StopSimulation();
		localMoveManager_.Stop();
		rollingMoveManager_.Stop();
		aiManager_.Stop();
		navigationManager_.Stop();
		if (hasSceneBackup_) {
			*editorScene_ = sceneBackup_;
		}

		animationManager_.Stop();
		audioManager_.Stop();
		scriptManager_.Stop();
		isPlaying_ = false;
		hasSceneBackup_ = false;
		return;
	}

	sceneBackup_ = *editorScene_;  // Play 開始前の編集状態を保存する
	hasSceneBackup_ = true;
	isPlaying_ = true;
	scriptManager_.Initialize(editorScene_, &inputManager_, &animationManager_, &aiManager_, &physicsManager_, consoleMessages_);
	physicsManager_.StartSimulation();
	animationManager_.Start();
	localMoveManager_.Start();
	rollingMoveManager_.Start();
	aiManager_.Start();
	navigationManager_.Start();
	audioManager_.Start();
	scriptManager_.Start();
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
