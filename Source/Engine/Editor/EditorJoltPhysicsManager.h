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
		int32_t gameObjectId = -1;  // 命中した GameObject ID。Scene 外 Body は -1
		Vector3 point = {0.0f, 0.0f, 0.0f};  // World 空間の命中点
		Vector3 normal = {0.0f, 1.0f, 0.0f};  // 命中面の法線。取れない場合は上向き
		float distance = 0.0f;  // Cast 開始点から命中点までの距離
		bool isTrigger = false;  // 命中 Body が Trigger / Sensor なら true
	};

	struct SubmergedVolumeInfo {
		float totalVolume = 0.0f;  // Jolt Shape 全体の体積
		float submergedVolume = 0.0f;  // 指定水面より下にある実 Shape の体積
		Vector3 centerOfBuoyancy = {0.0f, 0.0f, 0.0f};  // 水没体積の World 空間重心
		Vector3 centerOfMass = {0.0f, 0.0f, 0.0f};  // Jolt Body の World 空間重心
		Vector3 shapeSize = {0.0f, 0.0f, 0.0f};  // 実 Physics Shape のローカル境界寸法（Body生成時のScale適用済み）
	};

	struct HydrodynamicSurfaceTriangle {
		Vector3 first = {0.0f, 0.0f, 0.0f};  // World空間の第1頂点
		Vector3 second = {0.0f, 0.0f, 0.0f};  // World空間の第2頂点
		Vector3 third = {0.0f, 0.0f, 0.0f};  // World空間の第3頂点
		float areaScale = 1.0f;  // 面数縮約時に元の表面積を保つ重み
	};

	enum class RuntimeJointType : int32_t {
		Fixed = 0,
		Hinge = 1,
		Spring = 2,
		Configurable = 3,
		Character = 4
	};

	struct RuntimeJointSettings {
		Vector3 ownerAnchor = {0.0f, 0.0f, 0.0f};
		Vector3 connectedAnchor = {0.0f, 0.0f, 0.0f};
		Vector3 axis = {1.0f, 0.0f, 0.0f};
		float minDistance = 0.0f;
		float maxDistance = 1.0f;
		float minAngle = -3.1415926f;
		float maxAngle = 3.1415926f;
		float frequency = 5.0f;
		float damping = 0.7f;
		bool freezePositionX = false;
		bool freezePositionY = false;
		bool freezePositionZ = false;
		bool freezeRotationX = false;
		bool freezeRotationY = false;
		bool freezeRotationZ = false;
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
		float contactImpulse = 0.0f;  // 接触法線方向の推定Impulse N*s
		float selfMass = 0.0f;  // Dynamic Bodyの質量kg。Static/Triggerは0
		float otherMass = 0.0f;  // 接触相手のDynamic Body質量kg。Static/Triggerは0
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
	bool RegisterRuntimeGameObject(int32_t gameObjectId);  // Play中に追加されたGameObjectのColliderをJolt Worldへ登録する
	bool SetGameObjectSimulationActive(int32_t gameObjectId, bool isActive);  // 出現待ち Object の Body を物理 Worldへ出し入れする
	bool SetGameObjectTransform(int32_t gameObjectId, const Vector3& position, const Vector3& rotation);  // Pool再利用時にJolt Bodyも新しいWorld姿勢へ移す
	bool Raycast(const Vector3& origin, const Vector3& direction, float distance, PhysicsHit& hit) const;  // Scene 内 Collider に Ray を飛ばす
	bool RaycastIgnoringGameObject(const Vector3& origin, const Vector3& direction, float distance, int32_t ignoredGameObjectId, PhysicsHit& hit) const;  // 車体から地面へ飛ばす時など、所有者自身の全 Collider を除外する
	bool RaycastIgnoringGameObjects(const Vector3& origin, const Vector3& direction, float distance, const std::vector<int32_t>& ignoredGameObjectIds, PhysicsHit& hit) const;  // 攻撃者階層など複数Objectを除外する
	bool SphereCast(const Vector3& origin, float radius, const Vector3& direction, float distance, PhysicsHit& hit) const;  // 太さのある Ray を飛ばす
	bool SphereCastIgnoringGameObjects(const Vector3& origin, float radius, const Vector3& direction, float distance, const std::vector<int32_t>& ignoredGameObjectIds, PhysicsHit& hit) const;  // Projectile半径を保ったまま複数Objectを除外する
	bool CapsuleCast(const Vector3& origin, float radius, float height, const Vector3& direction, float distance, PhysicsHit& hit) const;  // Capsule 形状を移動させる
	bool OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const;  // 球の範囲に重なった GameObject を列挙する
	bool OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const;  // 箱の範囲に重なった GameObject を列挙する
	bool GetBodyMass(int32_t gameObjectId, float& bodyMass) const;  // Joltへ反映済みの実質量を返す
	// Jolt World上のBody実座標と、現在Worldへ追加済みかを返す(命中しない原因の切り分け用)。
	// GameObjectのTransformとBodyの座標がズレていれば、Castが当たらないのは当然になる。
	bool GetBodyDiagnostics(int32_t gameObjectId, Vector3& bodyPosition, bool& isAddedToWorld) const;
	bool GetSubmergedVolume(int32_t gameObjectId, const Vector3& surfacePosition, const Vector3& surfaceNormal, SubmergedVolumeInfo& volumeInfo) const;  // 実 Physics Shape を水面 Plane で切り、体積と浮心を返す
	bool GetHydrodynamicSurfaceTriangles(int32_t gameObjectId, std::vector<HydrodynamicSurfaceTriangle>& surfaceTriangles) const;  // 実Shape表面を面積分布保持パネルとしてWorld空間で返す
	bool AddForce(int32_t gameObjectId, const Vector3& force);  // Dynamic Rigidbody に継続力を加える
	bool AddForceAtPosition(int32_t gameObjectId, const Vector3& force, const Vector3& worldPosition);  // World 位置へ力を加え、重心との差から回転も発生させる
	bool AddImpulse(int32_t gameObjectId, const Vector3& impulse);  // Dynamic Rigidbody に瞬間力を加える
	bool AddTorque(int32_t gameObjectId, const Vector3& torque);  // Dynamic Rigidbody に回転力を加える
	bool SetVelocity(int32_t gameObjectId, const Vector3& velocity);  // Rigidbody の速度を直接設定する
	bool SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity);  // Rigidbody の角速度を直接設定する
	uint64_t CreateSpringJoint(int32_t ownerGameObjectId, int32_t connectedGameObjectId, const Vector3& ownerAnchor, const Vector3& connectedAnchor, float minDistance, float maxDistance, float frequency, float damping);  // 実行中の2 Body間へSpringJointを生成しHandleを返す
	bool DestroyJoint(uint64_t jointHandle);  // Handleで指定したRuntime Jointだけを破棄する
	bool SetSpringJointSettings(uint64_t jointHandle, const Vector3& ownerAnchor, const Vector3& connectedAnchor, float minDistance, float maxDistance, float frequency, float damping);  // Handleを維持したままRuntime SpringJointを再設定する
	bool IsJointValid(uint64_t jointHandle) const;  // Runtime Joint Handleが現在有効か返す
	uint64_t CreateJoint(RuntimeJointType jointType, int32_t ownerGameObjectId, int32_t connectedGameObjectId, const RuntimeJointSettings& jointSettings);  // 任意の対応Jointを実行中に生成する
	bool SetJointSettings(uint64_t jointHandle, const RuntimeJointSettings& jointSettings);  // Joint種別を維持して設定を更新する
	const std::vector<PhysicsEvent>& GetStepEvents() const;  // 直近の固定ステップで発生した接触イベント一覧
	void ClearStepEvents();  // 次の固定ステップ前に接触イベントを空にする

private:
	class Impl;  // Jolt の巨大なヘッダー依存を .cpp に閉じ込める
	std::unique_ptr<Impl> impl_;  // 実際の Jolt PhysicsSystem と Body 対応表
};

#pragma warning(pop)
