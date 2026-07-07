#pragma once

#include "EditorJoltPhysicsManager.h"
#include "EditorScene.h"

#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager {
public:
	EditorPhysicsManager() = default;  // RuntimeManager が直接保持する通常コンストラクタ
	~EditorPhysicsManager() = default;  // Jolt の破棄は joltPhysicsManager_ が担当する
	EditorPhysicsManager(const EditorPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー禁止
	EditorPhysicsManager& operator=(const EditorPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー代入禁止
	EditorPhysicsManager(EditorPhysicsManager&&) = delete;  // 内部 Jolt World の所有先を動かさない
	EditorPhysicsManager& operator=(EditorPhysicsManager&&) = delete;  // 内部 Jolt World の所有先を動かさない

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // 物理更新対象の Scene と Console 出力先を受け取る
	void StartSimulation();  // Play 開始時に Scene の Component から Jolt Body を作る
	int32_t Update(float deltaTime);  // Jolt PhysicsSystem を進め、結果を GameObject へ戻し、実行した固定更新回数を返す
	void Draw();  // 現時点では Jolt のデバッグ描画なし
	void StopSimulation();  // Play 停止時に Jolt Body を破棄する
	bool Raycast(const Vector3& origin, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.Raycast 相当を呼べる入口
	bool SphereCast(const Vector3& origin, float radius, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.SphereCast 相当を呼べる入口
	bool CapsuleCast(const Vector3& origin, float radius, float height, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.CapsuleCast 相当を呼べる入口
	bool OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const;  // Runtime から Physics.OverlapSphere 相当を呼べる入口
	bool OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const;  // Runtime から Physics.OverlapBox 相当を呼べる入口
	bool AddForce(int32_t gameObjectId, const Vector3& force);  // Runtime から Rigidbody.AddForce 相当を呼べる入口
	bool AddImpulse(int32_t gameObjectId, const Vector3& impulse);  // Runtime から Rigidbody.AddImpulse 相当を呼べる入口
	bool AddTorque(int32_t gameObjectId, const Vector3& torque);  // Runtime から Rigidbody.AddTorque 相当を呼べる入口
	bool SetVelocity(int32_t gameObjectId, const Vector3& velocity);  // Runtime から Rigidbody.velocity 相当を呼べる入口
	bool SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity);  // Runtime から Rigidbody.angularVelocity 相当を呼べる入口
	const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& GetFrameEvents() const;  // 直近フレームの全固定更新で集めた接触イベント一覧
	float GetFixedTimeStep() const;  // Script 側の FixedUpdate とそろえる固定時間

private:
	EditorScene* editorScene_ = nullptr;  // 物理 Component を検索する対象 Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // 物理イベントを Console へ出すための出力先
	EditorJoltPhysicsManager joltPhysicsManager_;  // JoltPhysics-5.5.0 を使う実物理 World
	std::vector<EditorJoltPhysicsManager::PhysicsEvent> frameEvents_;  // 1 描画フレーム中に起きた固定更新イベントをまとめて保持する
	float fixedTimeStep_ = 1.0f / 60.0f;  // 物理だけを進める固定時間。Unity の FixedUpdate 相当
	float fixedTimeAccumulator_ = 0.0f;  // 可変 deltaTime を固定時間へ分割するための蓄積時間
	int32_t maxFixedSubSteps_ = 4;  // フレーム落ち時に 1 フレームで回す物理回数の上限
};

#pragma warning(pop)
