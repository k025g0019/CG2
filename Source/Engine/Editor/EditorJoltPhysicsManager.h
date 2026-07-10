#pragma once

#include "EditorScene.h"

#include <memory>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorJoltPhysicsManager {
public:
	EditorJoltPhysicsManager();  // Jolt の共通初期化を済ませ、内部実装を作る
	~EditorJoltPhysicsManager();  // Pimpl の完全型が見える cpp 側で破棄する
	EditorJoltPhysicsManager(const EditorJoltPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー禁止
	EditorJoltPhysicsManager& operator=(const EditorJoltPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー代入禁止
	EditorJoltPhysicsManager(EditorJoltPhysicsManager&&) = delete;  // 内部ポインタの所有先を動かさない
	EditorJoltPhysicsManager& operator=(EditorJoltPhysicsManager&&) = delete;  // 内部ポインタの所有先を動かさない

	struct PhysicsHit {
		int32_t gameObjectId = -1;  // 命中した GameObject ID。床など Scene 外 Body は -1
		Vector3 point = {0.0f, 0.0f, 0.0f};  // World 空間の命中点
		Vector3 normal = {0.0f, 1.0f, 0.0f};  // 命中面の法線。取れない場合は上向き
		float distance = 0.0f;  // Cast 開始点から命中点までの距離
		bool isTrigger = false;  // 命中 Body が Trigger / Sensor なら true
	};

	enum class PhysicsEventType {
		CollisionEnter,  // 押し返しを伴う接触の開始
		CollisionStay,  // 押し返しを伴う接触の継続
		CollisionExit,  // 押し返しを伴う接触の終了
		TriggerEnter,  // Trigger 接触の開始
		TriggerStay,  // Trigger 接触の継続
		TriggerExit  // Trigger 接触の終了
	};

	struct CollisionInfo {
		int32_t selfGameObjectId = -1;  // このイベントを受け取る側の GameObject ID
		int32_t otherGameObjectId = -1;  // 接触相手の GameObject ID
		Vector3 point = {0.0f, 0.0f, 0.0f};  // World 空間の代表接触点
		Vector3 normal = {0.0f, 1.0f, 0.0f};  // self から見た接触法線
		Vector3 relativeVelocity = {0.0f, 0.0f, 0.0f};  // other - self の相対速度
		float separation = 0.0f;  // 貫通深さ。Exit では 0
		bool isTrigger = false;  // Trigger 接触なら true
	};

	struct PhysicsEvent {
		PhysicsEventType type = PhysicsEventType::CollisionEnter;  // 接触イベント種別
		CollisionInfo collision{};  // ゲームコードへ渡す接触情報
	};

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // Play 対象の Scene と Console 出力先を受け取る
	void Start();  // Scene の Component から Jolt Body を作って物理 World を開始する
	void Update(float deltaTime);  // Jolt の PhysicsSystem を進め、結果を GameObject へ戻す
	void Stop();  // Jolt Body と PhysicsSystem を破棄する
	bool IsActive() const;  // Jolt World が Play 用に作成済みか返す
	bool Raycast(const Vector3& origin, const Vector3& direction, float distance, PhysicsHit& hit) const;  // Scene 内 Collider に Ray を飛ばす
	bool SphereCast(const Vector3& origin, float radius, const Vector3& direction, float distance, PhysicsHit& hit) const;  // 太さのある Ray を飛ばす
	bool CapsuleCast(const Vector3& origin, float radius, float height, const Vector3& direction, float distance, PhysicsHit& hit) const;  // Capsule 形状を移動させる
	bool OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const;  // 球の範囲に重なった GameObject を列挙する
	bool OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const;  // 箱の範囲に重なった GameObject を列挙する
	bool AddForce(int32_t gameObjectId, const Vector3& force);  // Dynamic Rigidbody に継続力を加える
	bool AddImpulse(int32_t gameObjectId, const Vector3& impulse);  // Dynamic Rigidbody に瞬間力を加える
	bool AddTorque(int32_t gameObjectId, const Vector3& torque);  // Dynamic Rigidbody に回転力を加える
	bool SetVelocity(int32_t gameObjectId, const Vector3& velocity);  // Rigidbody の速度を直接設定する
	bool SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity);  // Rigidbody の角速度を直接設定する
	const std::vector<PhysicsEvent>& GetStepEvents() const;  // 直近の固定ステップで発生した接触イベント一覧
	void ClearStepEvents();  // 次の固定ステップ前に接触イベントを空にする

private:
	class Impl;  // Jolt の巨大なヘッダー依存を .cpp に閉じ込める
	std::unique_ptr<Impl> impl_;  // 実際の Jolt PhysicsSystem と Body 対応表
};

#pragma warning(pop)
