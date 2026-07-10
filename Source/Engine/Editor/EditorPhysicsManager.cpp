#include "EditorPhysicsManager.h"

#include <algorithm>

void EditorPhysicsManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // RuntimeManager から渡された Play 対象 Scene
	consoleMessages_ = consoleMessages;  // Jolt の接触イベントを Console へ流す
	joltPhysicsManager_.Initialize(editorScene_, consoleMessages_);
	frameEvents_.clear();
}

void EditorPhysicsManager::StartSimulation() {
	// Play 開始時の Scene 状態から Jolt Body を作る
	fixedTimeAccumulator_ = 0.0f;
	frameEvents_.clear();
	joltPhysicsManager_.Start();
}

int32_t EditorPhysicsManager::Update(float deltaTime) {
	if (editorScene_ == nullptr) {
		return 0;
	}

	const EditorPhysicsSettings& physicsSettings = editorScene_->GetPhysicsSettings();  // Inspector の物理設定を固定更新へ反映する
	fixedTimeStep_ = (std::clamp)(physicsSettings.fixedTimeStep, 0.001f, 0.1f);
	fixedTimeAccumulator_ += deltaTime;  // 描画フレーム時間を物理用の固定時間へ貯める
	int32_t fixedStepCount = 0;  // 1 フレーム内で実行した固定更新回数
	frameEvents_.clear();  // この描画フレームで発生した接触イベントをここから再収集する

	while (fixedTimeAccumulator_ >= fixedTimeStep_ && fixedStepCount < maxFixedSubSteps_) {
		// Jolt が重力、接触、押し戻し、速度更新を固定時間で担当する
		joltPhysicsManager_.Update(fixedTimeStep_);
		const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& stepEvents = joltPhysicsManager_.GetStepEvents();
		frameEvents_.insert(frameEvents_.end(), stepEvents.begin(), stepEvents.end());
		fixedTimeAccumulator_ -= fixedTimeStep_;
		fixedStepCount++;
	}

	if (fixedStepCount >= maxFixedSubSteps_) {
		// 長い停止後に未処理時間を抱え続けると以後のフレームも詰まるため、上限到達時は残りを捨てる
		fixedTimeAccumulator_ = 0.0f;
	}

	return fixedStepCount;
}

void EditorPhysicsManager::Draw() {
	// Jolt DebugRenderer の線描画接続前でも、設定は Scene の PhysicsSettings に保存される
}

void EditorPhysicsManager::StopSimulation() {
	// Play 停止時に Jolt World を破棄する。Scene 自体は RuntimeManager がバックアップから復元する。
	fixedTimeAccumulator_ = 0.0f;
	frameEvents_.clear();
	joltPhysicsManager_.Stop();
}

bool EditorPhysicsManager::Raycast(
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	return joltPhysicsManager_.Raycast(origin, direction, distance, hit);
}

bool EditorPhysicsManager::SphereCast(
	const Vector3& origin,
	float radius,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	return joltPhysicsManager_.SphereCast(origin, radius, direction, distance, hit);
}

bool EditorPhysicsManager::CapsuleCast(
	const Vector3& origin,
	float radius,
	float height,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	return joltPhysicsManager_.CapsuleCast(origin, radius, height, direction, distance, hit);
}

bool EditorPhysicsManager::OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const {
	return joltPhysicsManager_.OverlapSphere(center, radius, hitGameObjectIds);
}

bool EditorPhysicsManager::OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const {
	return joltPhysicsManager_.OverlapBox(center, size, hitGameObjectIds);
}

bool EditorPhysicsManager::AddForce(int32_t gameObjectId, const Vector3& force) {
	return joltPhysicsManager_.AddForce(gameObjectId, force);
}

bool EditorPhysicsManager::AddImpulse(int32_t gameObjectId, const Vector3& impulse) {
	return joltPhysicsManager_.AddImpulse(gameObjectId, impulse);
}

bool EditorPhysicsManager::AddTorque(int32_t gameObjectId, const Vector3& torque) {
	return joltPhysicsManager_.AddTorque(gameObjectId, torque);
}

bool EditorPhysicsManager::SetVelocity(int32_t gameObjectId, const Vector3& velocity) {
	return joltPhysicsManager_.SetVelocity(gameObjectId, velocity);
}

bool EditorPhysicsManager::SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity) {
	return joltPhysicsManager_.SetAngularVelocity(gameObjectId, angularVelocity);
}

const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& EditorPhysicsManager::GetFrameEvents() const {
	return frameEvents_;
}

float EditorPhysicsManager::GetFixedTimeStep() const {
	return fixedTimeStep_;
}
