#include "EditorRuntimeManager.h"

void EditorRuntimeManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play / Stop のたびに操作する Scene
	consoleMessages_ = consoleMessages;  // Runtime 内のイベントログ出力先
	scriptManager_.Initialize(editorScene_, &inputManager_, &physicsManager_, consoleMessages_);
	inputManager_.Initialize(editorScene_, consoleMessages_);  // PlayerInput の Fire なども Console に出せるように同じ出力先を渡す
	physicsManager_.Initialize(editorScene_, consoleMessages_);
}

#pragma warning(push)
#pragma warning(disable : 5045)
void EditorRuntimeManager::Update(const uint8_t* keyState, float deltaTime) {
	if (!isPlaying_ || editorScene_ == nullptr) {
		return;
	}

	scriptManager_.Update(keyState, deltaTime);  // Script / MonoBehaviour の毎フレーム更新を呼ぶ
	inputManager_.Update(keyState, deltaTime);  // 入力で GameObject の平面移動やジャンプ速度を更新する
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
	physicsManager_.Draw();
}

void EditorRuntimeManager::TogglePlay() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (isPlaying_) {
		// Stop 時は Play 開始前の Scene に戻す
		physicsManager_.StopSimulation();
		if (hasSceneBackup_) {
			*editorScene_ = sceneBackup_;
		}

		scriptManager_.Stop();
		isPlaying_ = false;
		hasSceneBackup_ = false;
		return;
	}

	sceneBackup_ = *editorScene_;  // Play 開始前の編集状態を保存する
	hasSceneBackup_ = true;
	isPlaying_ = true;
	scriptManager_.Initialize(editorScene_, &inputManager_, &physicsManager_, consoleMessages_);
	physicsManager_.StartSimulation();
	scriptManager_.Start();
}

bool EditorRuntimeManager::IsPlaying() const {
	return isPlaying_;
}
