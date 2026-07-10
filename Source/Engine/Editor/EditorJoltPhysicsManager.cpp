#include "EditorJoltPhysicsManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorMeshCollision.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#pragma warning(pop)

#pragma warning(disable : 4355 4625 4626 5026 5027 4820 5045)

namespace {
	constexpr float kJoltMinimumShapeSize = 0.01f;  // 0 サイズ Shape は Jolt で無効なので、最小値を持たせる
	constexpr float kJoltFloorY = 0.0f;  // Unity 風の簡易 Scene で床として使う高さ
	constexpr uint32_t kJoltMaxBodies = 4096;  // Editor 内で扱う GameObject 数の上限
	constexpr uint32_t kJoltMaxBodyPairs = 4096;  // BroadPhase が保持する接触候補数
	constexpr uint32_t kJoltMaxContactConstraints = 4096;  // Solver が扱う接触制約数
	constexpr uint32_t kJoltTempAllocatorSize = 10u * 1024u * 1024u;  // Jolt Update 中の一時メモリ
	constexpr uint32_t kJoltMaxJobs = 1024;  // SingleThreaded JobSystem が保持する Job 数
	constexpr int32_t kPhysicsLayerCount = 8;  // Inspector に出している物理レイヤー数
	constexpr int32_t kPhysicsLayerGround = 3;  // 自動床に使う Ground レイヤー
	constexpr int32_t kPhysicsLayerUi = 6;  // UI は物理衝突させない
	constexpr int32_t kPhysicsLayerIgnoreRaycast = 7;  // Raycast 無視用レイヤー
	constexpr size_t kMaxPhysicsConsoleMessages = 240;  // 物理イベントで Console を無制限に増やさない上限

	namespace JoltBroadPhaseLayers {
		static constexpr JPH::BroadPhaseLayer NonMoving(0);  // 静的物体用 BroadPhase
		static constexpr JPH::BroadPhaseLayer Moving(1);  // 動的物体用 BroadPhase
		static constexpr JPH::uint Count = 2;  // BroadPhaseLayer 数
	}

	int32_t ClampPhysicsLayer(int32_t layer) {
		// 保存データが壊れていても ObjectLayer 配列外へ出さない
		return (std::clamp)(layer, 0, kPhysicsLayerCount - 1);
	}

	bool IsDynamicObjectLayer(JPH::ObjectLayer objectLayer) {
		// ObjectLayer の下位 1 bit を Moving / NonMoving の区別に使う
		return (objectLayer % 2u) != 0u;
	}

	int32_t GetPhysicsLayerFromObjectLayer(JPH::ObjectLayer objectLayer) {
		return static_cast<int32_t>(objectLayer / 2u);
	}

	JPH::ObjectLayer MakeJoltObjectLayer(int32_t physicsLayer, bool isDynamic) {
		// physicsLayer * 2 + movingBit で、物理レイヤーと動的/静的を 1 つの ObjectLayer に詰める
		int32_t clampedLayer = ClampPhysicsLayer(physicsLayer);
		return static_cast<JPH::ObjectLayer>((clampedLayer * 2) + (isDynamic ? 1 : 0));
	}

	bool ShouldPhysicsLayersCollide(int32_t firstLayer, int32_t secondLayer) {
		// UI と IgnoreRaycast は Scene 上の補助用なので物理応答から外す
		if (firstLayer == kPhysicsLayerUi || secondLayer == kPhysicsLayerUi) {
			return false;
		}
		if (firstLayer == kPhysicsLayerIgnoreRaycast || secondLayer == kPhysicsLayerIgnoreRaycast) {
			return false;
		}

		return true;
	}

	class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer firstLayer, JPH::ObjectLayer secondLayer) const override {
			// 静的同士は動かないので判定せず、動的物体が関わる組み合わせだけ衝突させる
			if (!IsDynamicObjectLayer(firstLayer) && !IsDynamicObjectLayer(secondLayer)) {
				return false;
			}

			return ShouldPhysicsLayersCollide(
				GetPhysicsLayerFromObjectLayer(firstLayer),
				GetPhysicsLayerFromObjectLayer(secondLayer));
		}
	};

	class JoltBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
	public:
		JPH::uint GetNumBroadPhaseLayers() const override {
			return JoltBroadPhaseLayers::Count;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
			return IsDynamicObjectLayer(layer) ? JoltBroadPhaseLayers::Moving : JoltBroadPhaseLayers::NonMoving;
		}
	};

	class JoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
			// 静的物体は動的物体の BroadPhase だけを見る。動的物体はすべてを見る。
			if (!IsDynamicObjectLayer(objectLayer)) {
				return broadPhaseLayer == JoltBroadPhaseLayers::Moving;
			}

			return true;
		}
	};

	class JoltQueryObjectLayerFilter final : public JPH::ObjectLayerFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer objectLayer) const override {
			int32_t physicsLayer = GetPhysicsLayerFromObjectLayer(objectLayer);
			return physicsLayer != kPhysicsLayerUi && physicsLayer != kPhysicsLayerIgnoreRaycast;
		}
	};

	struct JoltBodyLink {
		int32_t gameObjectId;  // Jolt Body の結果を書き戻す GameObject ID
		JPH::BodyID bodyId;  // Jolt PhysicsSystem 内の Body ID
		Vector3 colliderCenter;  // Body 位置は Collider 中心なので GameObject 位置へ戻す時に引く
		bool isDynamic;  // Dynamic Body だけ Transform と速度を書き戻す
	};

	struct PreciseMeshCollisionBody {
		int32_t gameObjectId;  // BVH の元になった GameObject ID
		EditorMeshCollisionMesh collisionMesh;  // FBX / OBJ 三角形から作った BVH
	};

	struct JoltCharacterVirtualLink {
		int32_t gameObjectId;  // CharacterVirtual の結果を書き戻す GameObject ID
		JPH::Ref<JPH::CharacterVirtual> character;  // Rigidbody を使わない CharacterController の Jolt 実体
		Vector3 colliderCenter;  // CharacterVirtual 位置から GameObject 原点へ戻すための中心オフセット
	};

	struct PhysicsBodyMaterial {
		float dynamicFriction = 0.6f;  // 動いている接触の摩擦
		float staticFriction = 0.6f;  // 静止している接触の摩擦
		float bounciness = 0.0f;  // 反発係数
		int32_t frictionCombineMode = 0;  // 摩擦合成モード
		int32_t bouncinessCombineMode = 0;  // 反発合成モード
		bool isTrigger = false;  // Sensor / Trigger なら true
		bool generateContactEvents = true;  // Console へイベントを出すか
	};

	struct ActiveContactPair {
		int32_t firstGameObjectId = -1;  // 接触した片方の GameObject ID
		int32_t secondGameObjectId = -1;  // 接触したもう片方の GameObject ID
		bool isTrigger = false;  // Trigger イベントなら true
		bool shouldLog = false;  // Console へ出力するなら true
		int32_t stayFrameCount = 0;  // Stay ログを間引くための接触継続回数
		Vector3 point = {0.0f, 0.0f, 0.0f};  // 接触イベントをゲーム側へ渡す代表点
		Vector3 normal = {0.0f, 1.0f, 0.0f};  // first -> second の方向を表す接触法線
		Vector3 relativeVelocity = {0.0f, 0.0f, 0.0f};  // second - first の相対速度
		float separation = 0.0f;  // 貫通深さ
	};

	void EnsureJoltGlobalInitialized() {
		static bool isJoltInitialized = false;  // Jolt の Factory / 型登録はプロセス中 1 回だけ行う
		if (isJoltInitialized) {
			return;
		}

		JPH::RegisterDefaultAllocator();  // Jolt の内部 allocation を有効化する
		JPH::Factory::sInstance = new JPH::Factory();  // Shape などの型生成に使う Factory
		JPH::RegisterTypes();  // BoxShape / SphereShape / CapsuleShape / RigidBody などの型登録
		isJoltInitialized = true;
	}

	float GetScaledShapeSize(float colliderSize, float objectScale) {
		// Collider サイズに Transform Scale を掛け、Jolt が受け取れる下限で止める
		float scaledSize = colliderSize * std::fabs(objectScale);
		return (std::max)(scaledSize, kJoltMinimumShapeSize);
	}

	float GetAverageFriction(const PhysicsBodyMaterial& material) {
		// Jolt は Body に 1 つの摩擦値を持つため、動摩擦と静止摩擦の平均を基準値にする
		return (std::max)((material.dynamicFriction + material.staticFriction) * 0.5f, 0.0f);
	}

	float CombinePhysicsValue(float firstValue, float secondValue, int32_t firstMode, int32_t secondMode) {
		// Unity の Combine に近い規則。どちらかが強い指定を持つ場合はそれを優先する。
		if (firstMode == 3 || secondMode == 3) {
			return firstValue * secondValue;
		}
		if (firstMode == 2 || secondMode == 2) {
			return (std::max)(firstValue, secondValue);
		}
		if (firstMode == 1 || secondMode == 1) {
			return (std::min)(firstValue, secondValue);
		}

		return (firstValue + secondValue) * 0.5f;
	}

	uint64_t MakeContactPairKey(JPH::BodyID firstBodyId, JPH::BodyID secondBodyId) {
		uint32_t firstId = firstBodyId.GetIndexAndSequenceNumber();
		uint32_t secondId = secondBodyId.GetIndexAndSequenceNumber();
		if (secondId < firstId) {
			std::swap(firstId, secondId);
		}

		return (static_cast<uint64_t>(firstId) << 32) | static_cast<uint64_t>(secondId);
	}

	uint32_t MakeBodyMapKey(JPH::BodyID bodyId) {
		return bodyId.GetIndexAndSequenceNumber();
	}

	JPH::Quat MakeJoltRotation(const Vector3& rotate) {
		// Editor 側の Euler 回転値を Jolt の Quaternion へ変換する
		return JPH::Quat::sEulerAngles(JPH::Vec3(rotate.x, rotate.y, rotate.z));
	}

	Vector3 MakeEditorRotation(JPH::QuatArg rotation) {
		// Jolt の Quaternion を Editor 側の Euler 回転値へ戻す
		JPH::Vec3 euler = rotation.GetEulerAngles();
		return Vector3{euler.GetX(), euler.GetY(), euler.GetZ()};
	}

	JPH::Vec3 MakeJoltVector(const Vector3& vector) {
		return JPH::Vec3(vector.x, vector.y, vector.z);
	}

	JPH::RVec3 MakeJoltPosition(const Vector3& position) {
		return JPH::RVec3(
			static_cast<JPH::Real>(position.x),
			static_cast<JPH::Real>(position.y),
			static_cast<JPH::Real>(position.z));
	}

	Vector3 MakeEditorVector(JPH::Vec3Arg vector) {
		return Vector3{vector.GetX(), vector.GetY(), vector.GetZ()};
	}

	Vector3 MakeEditorPosition(JPH::RVec3Arg position) {
		return Vector3{
			static_cast<float>(position.GetX()),
			static_cast<float>(position.GetY()),
			static_cast<float>(position.GetZ())};
	}

	bool NormalizeDirection(const Vector3& direction, Vector3& normalizedDirection) {
		float length = std::sqrt(
			direction.x * direction.x +
			direction.y * direction.y +
			direction.z * direction.z);
		if (length <= kJoltMinimumShapeSize) {
			return false;
		}

		normalizedDirection = {
			direction.x / length,
			direction.y / length,
			direction.z / length};
		return true;
	}

	Vector3 AddVector3(const Vector3& firstVector, const Vector3& secondVector) {
		return Vector3{
			firstVector.x + secondVector.x,
			firstVector.y + secondVector.y,
			firstVector.z + secondVector.z};
	}

	Vector3 MakeJointAxis(const Vector3& axis) {
		Vector3 normalizedAxis{};
		if (!NormalizeDirection(axis, normalizedAxis)) {
			return Vector3{0.0f, 1.0f, 0.0f};
		}

		return normalizedAxis;
	}

	JPH::Vec3 MakeJointNormalAxis(const Vector3& normalizedAxis) {
		JPH::Vec3 hingeAxis = MakeJoltVector(normalizedAxis);
		JPH::Vec3 referenceAxis = std::fabs(hingeAxis.Dot(JPH::Vec3::sAxisX())) < 0.9f
			                          ? JPH::Vec3::sAxisX()
			                          : JPH::Vec3::sAxisZ();

		// Hinge は回転軸と直交する法線軸も必要。軸が X に近い場合は Z を基準にしてゼロ外積を避ける。
		return hingeAxis.Cross(referenceAxis).NormalizedOr(JPH::Vec3::sAxisZ());
	}
}

class EditorJoltPhysicsManager::Impl {
public:
	Impl() : contactListener_(this) {
		tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(kJoltTempAllocatorSize);
		jobSystem_ = std::make_unique<JPH::JobSystemSingleThreaded>(kJoltMaxJobs);
	}

	~Impl() {
		Stop();
	}

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
		editorScene_ = editorScene;  // Play 開始時に Jolt Body を作る元 Scene
		consoleMessages_ = consoleMessages;  // Collision / Trigger イベントの出力先
	}

	void Start() {
		if (editorScene_ == nullptr) {
			return;
		}

		Stop();  // Play を連続で押しても古い Body を残さない
		physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
		physicsSystem_->Init(
			kJoltMaxBodies,
			0,
			kJoltMaxBodyPairs,
			kJoltMaxContactConstraints,
			broadPhaseLayerInterface_,
			objectVsBroadPhaseLayerFilter_,
			objectLayerPairFilter_);
		const EditorPhysicsSettings& physicsSettings = editorScene_->GetPhysicsSettings();
		physicsSystem_->SetGravity(MakeJoltVector(physicsSettings.gravity));
		physicsSystem_->SetContactListener(&contactListener_);

		AddFloorBody();
		AddSceneBodies();
		AddSceneConstraints();
		physicsSystem_->OptimizeBroadPhase();
		isActive_ = true;
		PushConsoleMessage("物理: JoltPhysics-5.5.0 で Play 物理を開始");
	}

	void Update(float deltaTime) {
		if (!isActive_ || editorScene_ == nullptr || physicsSystem_ == nullptr) {
			return;
		}

		const EditorPhysicsSettings& physicsSettings = editorScene_->GetPhysicsSettings();
		int32_t collisionStepCount = (std::clamp)(physicsSettings.collisionStepCount, 1, 8);
		physicsSystem_->SetGravity(MakeJoltVector(physicsSettings.gravity));
		stepEvents_.clear();  // この固定ステップ中に発生した Enter / Stay / Exit をここから積み直す
		ApplyComponentVelocitiesToBodies();
		physicsSystem_->Update(deltaTime, collisionStepCount, tempAllocator_.get(), jobSystem_.get());
		WriteBackDynamicBodies();
		UpdateCharacterVirtuals(deltaTime);
	}

	void Stop() {
		if (physicsSystem_ != nullptr) {
			physicsSystem_->SetContactListener(nullptr);
			JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

			for (const JPH::Ref<JPH::Constraint>& constraint : constraints_) {
				if (constraint != nullptr) {
					physicsSystem_->RemoveConstraint(constraint);
				}
			}
			constraints_.clear();
			characterVirtualLinks_.clear();

			for (const JoltBodyLink& bodyLink : bodyLinks_) {
				bodyInterface.RemoveBody(bodyLink.bodyId);
				bodyInterface.DestroyBody(bodyLink.bodyId);
			}
			bodyLinks_.clear();

			if (!floorBodyId_.IsInvalid()) {
				bodyInterface.RemoveBody(floorBodyId_);
				bodyInterface.DestroyBody(floorBodyId_);
				floorBodyId_ = JPH::BodyID();
			}
		}

		bodyMaterials_.clear();
		preciseMeshCollisionBodies_.clear();
		primaryBodyIdByGameObjectId_.clear();
		activeContactPairs_.clear();
		stepEvents_.clear();
		physicsSystem_.reset();
		isActive_ = false;
	}

	bool IsActive() const {
		return isActive_;
	}

	bool Raycast(const Vector3& origin, const Vector3& direction, float distance, PhysicsHit& hit) const {
		if (!CanRunQuery(distance)) {
			return false;
		}

		Vector3 normalizedDirection{};
		if (!NormalizeDirection(direction, normalizedDirection)) {
			return false;
		}

		JPH::RRayCast ray(
			MakeJoltPosition(origin),
			MakeJoltVector({
				normalizedDirection.x * distance,
				normalizedDirection.y * distance,
				normalizedDirection.z * distance}));
		JPH::RayCastResult rayResult{};
		JoltQueryObjectLayerFilter queryObjectLayerFilter;

		if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, rayResult, {}, queryObjectLayerFilter)) {
			return false;
		}

		JPH::RVec3 point = ray.GetPointOnRay(rayResult.mFraction);
		hit.gameObjectId = GetGameObjectId(rayResult.mBodyID);
		hit.point = MakeEditorPosition(point);
		hit.normal = GetHitNormal(rayResult.mBodyID, rayResult.mSubShapeID2, point);
		hit.distance = distance * rayResult.mFraction;
		hit.isTrigger = IsTriggerBody(rayResult.mBodyID);
		return true;
	}

	bool SphereCast(const Vector3& origin, float radius, const Vector3& direction, float distance, PhysicsHit& hit) const {
		if (!CanRunQuery(distance)) {
			return false;
		}

		float clampedRadius = (std::max)(radius, kJoltMinimumShapeSize);
		JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(clampedRadius);
		return ShapeCast(shape, origin, direction, distance, hit);
	}

	bool CapsuleCast(const Vector3& origin, float radius, float height, const Vector3& direction, float distance, PhysicsHit& hit) const {
		if (!CanRunQuery(distance)) {
			return false;
		}

		float clampedRadius = (std::max)(radius, kJoltMinimumShapeSize);
		float halfHeightOfCylinder = (std::max)((height * 0.5f) - clampedRadius, kJoltMinimumShapeSize);
		JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(halfHeightOfCylinder, clampedRadius);
		return ShapeCast(shape, origin, direction, distance, hit);
	}

	bool OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const {
		float clampedRadius = (std::max)(radius, kJoltMinimumShapeSize);
		JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(clampedRadius);
		return OverlapShape(shape, center, hitGameObjectIds);
	}

	bool OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const {
		JPH::Vec3 halfExtent(
			(std::max)(size.x * 0.5f, kJoltMinimumShapeSize),
			(std::max)(size.y * 0.5f, kJoltMinimumShapeSize),
			(std::max)(size.z * 0.5f, kJoltMinimumShapeSize));
		JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(halfExtent, 0.0f);
		return OverlapShape(shape, center, hitGameObjectIds);
	}

	bool AddForce(int32_t gameObjectId, const Vector3& force) {
		JPH::BodyID bodyId;
		if (!TryFindPrimaryBodyId(gameObjectId, bodyId)) {
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		bodyInterface.AddForce(bodyId, MakeJoltVector(force), JPH::EActivation::Activate);
		return true;
	}

	bool AddImpulse(int32_t gameObjectId, const Vector3& impulse) {
		JPH::BodyID bodyId;
		if (!TryFindPrimaryBodyId(gameObjectId, bodyId)) {
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		bodyInterface.AddImpulse(bodyId, MakeJoltVector(impulse));
		return true;
	}

	bool AddTorque(int32_t gameObjectId, const Vector3& torque) {
		JPH::BodyID bodyId;
		if (!TryFindPrimaryBodyId(gameObjectId, bodyId)) {
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		bodyInterface.AddTorque(bodyId, MakeJoltVector(torque));
		bodyInterface.ActivateBody(bodyId);
		return true;
	}

	bool SetVelocity(int32_t gameObjectId, const Vector3& velocity) {
		JPH::BodyID bodyId;
		if (!TryFindPrimaryBodyId(gameObjectId, bodyId)) {
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		bodyInterface.SetLinearVelocity(bodyId, MakeJoltVector(velocity));
		return true;
	}

	bool SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity) {
		JPH::BodyID bodyId;
		if (!TryFindPrimaryBodyId(gameObjectId, bodyId)) {
			return false;
		}

		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		bodyInterface.SetAngularVelocity(bodyId, MakeJoltVector(angularVelocity));
		return true;
	}

	const std::vector<PhysicsEvent>& GetStepEvents() const {
		return stepEvents_;
	}

	void ClearStepEvents() {
		stepEvents_.clear();
	}

	void ApplyContactSettings(const JPH::Body& firstBody, const JPH::Body& secondBody, JPH::ContactSettings& settings) const {
		const PhysicsBodyMaterial firstMaterial = GetMaterial(firstBody.GetID());
		const PhysicsBodyMaterial secondMaterial = GetMaterial(secondBody.GetID());

		settings.mCombinedFriction = (std::max)(
			CombinePhysicsValue(
				GetAverageFriction(firstMaterial),
				GetAverageFriction(secondMaterial),
				firstMaterial.frictionCombineMode,
				secondMaterial.frictionCombineMode),
			0.0f);
		settings.mCombinedRestitution = (std::clamp)(
			CombinePhysicsValue(
				firstMaterial.bounciness,
				secondMaterial.bounciness,
				firstMaterial.bouncinessCombineMode,
				secondMaterial.bouncinessCombineMode),
			0.0f,
			1.0f);
		settings.mIsSensor = firstBody.IsSensor() || secondBody.IsSensor();
	}

	void OnContactAdded(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold) {
		ActiveContactPair contactPair = MakeActiveContactPair(firstBody, secondBody, manifold);
		uint64_t contactPairKey = MakeContactPairKey(firstBody.GetID(), secondBody.GetID());
		activeContactPairs_[contactPairKey] = contactPair;
		PushStepEvents(contactPair, contactPair.isTrigger ? PhysicsEventType::TriggerEnter : PhysicsEventType::CollisionEnter);
	}

	void OnContactPersisted(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold) {
		uint64_t contactPairKey = MakeContactPairKey(firstBody.GetID(), secondBody.GetID());
		auto contactIterator = activeContactPairs_.find(contactPairKey);
		if (contactIterator == activeContactPairs_.end()) {
			activeContactPairs_[contactPairKey] = MakeActiveContactPair(firstBody, secondBody, manifold);
			PushStepEvents(activeContactPairs_[contactPairKey], activeContactPairs_[contactPairKey].isTrigger ? PhysicsEventType::TriggerEnter : PhysicsEventType::CollisionEnter);
			return;
		}

		ActiveContactPair updatedContactPair = MakeActiveContactPair(firstBody, secondBody, manifold);
		updatedContactPair.stayFrameCount = contactIterator->second.stayFrameCount + 1;
		contactIterator->second = updatedContactPair;
		PushStepEvents(contactIterator->second, contactIterator->second.isTrigger ? PhysicsEventType::TriggerStay : PhysicsEventType::CollisionStay);
	}

	void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) {
		uint64_t contactPairKey = MakeContactPairKey(subShapePair.GetBody1ID(), subShapePair.GetBody2ID());
		auto contactIterator = activeContactPairs_.find(contactPairKey);
		if (contactIterator == activeContactPairs_.end()) {
			return;
		}

		PushStepEvents(contactIterator->second, contactIterator->second.isTrigger ? PhysicsEventType::TriggerExit : PhysicsEventType::CollisionExit);
		activeContactPairs_.erase(contactIterator);
	}

private:
	class ContactListener final : public JPH::ContactListener {
	public:
		explicit ContactListener(Impl* owner) : owner_(owner) {
		}

		JPH::ValidateResult OnContactValidate(
			const JPH::Body& firstBody,
			const JPH::Body& secondBody,
			JPH::RVec3Arg baseOffset,
			const JPH::CollideShapeResult& collisionResult) override {
			if (owner_ == nullptr) {
				return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
			}
			if (!owner_->ShouldLayerPairCollide(
				    GetPhysicsLayerFromObjectLayer(firstBody.GetObjectLayer()),
				    GetPhysicsLayerFromObjectLayer(secondBody.GetObjectLayer()))) {
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			}

			if (owner_->HasPreciseMeshCollisionBody(firstBody) ||
				owner_->HasPreciseMeshCollisionBody(secondBody)) {
				if (!owner_->ShouldAcceptPreciseMeshContact(firstBody, secondBody, baseOffset, collisionResult)) {
					return JPH::ValidateResult::RejectContact;
				}

				return JPH::ValidateResult::AcceptContact;
			}

			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		void OnContactAdded(
			const JPH::Body& firstBody,
			const JPH::Body& secondBody,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override {
			if (owner_ == nullptr) {
				return;
			}

			owner_->ApplyContactSettings(firstBody, secondBody, settings);
			owner_->OnContactAdded(firstBody, secondBody, manifold);
		}

		void OnContactPersisted(
			const JPH::Body& firstBody,
			const JPH::Body& secondBody,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override {
			if (owner_ == nullptr) {
				return;
			}

			owner_->ApplyContactSettings(firstBody, secondBody, settings);
			owner_->OnContactPersisted(firstBody, secondBody, manifold);
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override {
			if (owner_ == nullptr) {
				return;
			}

			owner_->OnContactRemoved(subShapePair);
		}

	private:
		Impl* owner_;  // Jolt callback から Manager 本体へ戻すための所有者
	};

	EditorScene* editorScene_ = nullptr;  // Jolt の結果を書き戻す Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // Console パネルへ出す物理ログ
	std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;  // Jolt Update 用一時メモリ
	std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem_;  // まずは安定優先の単一スレッド JobSystem
	JoltBroadPhaseLayerInterface broadPhaseLayerInterface_;  // ObjectLayer と BroadPhaseLayer の変換
	JoltObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter_;  // BroadPhase の衝突フィルター
	JoltObjectLayerPairFilter objectLayerPairFilter_;  // ObjectLayer 同士の衝突フィルター
	ContactListener contactListener_;  // Collision / Trigger イベントを受け取る Listener
	std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;  // Jolt の物理 World
	std::vector<JoltBodyLink> bodyLinks_;  // GameObject と Jolt Body の対応表
	std::vector<JoltCharacterVirtualLink> characterVirtualLinks_;  // Rigidbody なし CharacterController の Jolt 管理
	std::unordered_map<uint32_t, int32_t> gameObjectIdByBodyId_;  // Query / Event から GameObject ID を戻す対応表
	std::unordered_map<int32_t, JPH::BodyID> primaryBodyIdByGameObjectId_;  // Joint Component が接続先 ID から Body を引くための対応表
	std::unordered_map<uint32_t, PhysicsBodyMaterial> bodyMaterials_;  // Body ごとの摩擦・反発・Trigger 設定
	std::unordered_map<uint32_t, PreciseMeshCollisionBody> preciseMeshCollisionBodies_;  // ConvexHull の接触を実メッシュ BVH で検証する
	std::unordered_map<uint64_t, ActiveContactPair> activeContactPairs_;  // Enter 済み接触の Stay / Exit 管理
	std::vector<PhysicsEvent> stepEvents_;  // 1 固定更新中に発生した接触イベントを Script へ渡すために保持する
	std::vector<JPH::Ref<JPH::Constraint>> constraints_;  // Play 中に Jolt World へ追加した Joint 制約
	JPH::BodyID floorBodyId_;  // Play 中だけ使う床 Body
	bool isActive_ = false;  // Start 済みなら true

	void AddFloorBody() {
		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
		JPH::BodyCreationSettings floorSettings(
			new JPH::BoxShape(JPH::Vec3(100.0f, 0.5f, 100.0f), 0.0f),
			JPH::RVec3(
				static_cast<JPH::Real>(0.0f),
				static_cast<JPH::Real>(kJoltFloorY - 0.5f),
				static_cast<JPH::Real>(0.0f)),
			JPH::Quat::sIdentity(),
			JPH::EMotionType::Static,
			MakeJoltObjectLayer(kPhysicsLayerGround, false));

		PhysicsBodyMaterial floorMaterial{};
		floorMaterial.dynamicFriction = 0.6f;
		floorMaterial.staticFriction = 0.8f;
		floorMaterial.bounciness = 0.0f;

		floorSettings.mFriction = GetAverageFriction(floorMaterial);
		floorSettings.mRestitution = floorMaterial.bounciness;
		floorSettings.mUserData = 0;
		floorBodyId_ = bodyInterface.CreateAndAddBody(floorSettings, JPH::EActivation::DontActivate);
		RegisterBody(floorBodyId_, -1, floorMaterial);
	}

	void AddSceneBodies() {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			EditorComponent* rigidBody =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
			EditorComponent implicitMeshCollider{};
			EditorComponent* collider = FindMainCollider(gameObject);
			if (collider == nullptr) {
				if (rigidBody == nullptr ||
					!rigidBody->isActive ||
					!MakeImplicitMeshCollider(gameObject, implicitMeshCollider)) {
					continue;
				}

				collider = &implicitMeshCollider;
				PushConsoleMessage("物理: Rigidbody 付きモデルへ暗黙のメッシュ当たり判定を追加 " + gameObject.name);
			}

			EditorComponent runtimeCollider = *collider;  // Play 中だけ使う Collider 設定。Scene に保存済みの古い Mesh 範囲はここで補正する。
			RefreshRuntimeMeshColliderBounds(gameObject, runtimeCollider);

			if (runtimeCollider.type == EditorComponentType::CharacterController &&
			    (rigidBody == nullptr || !rigidBody->isActive)) {
				AddCharacterVirtual(gameObject, runtimeCollider);
				continue;
			}

			bool isDynamic = rigidBody != nullptr && rigidBody->isActive && !rigidBody->isKinematic;
			if (isDynamic && IsAllDofsFrozen(*rigidBody)) {
				isDynamic = false;  // Jolt は DOF None の Dynamic を許可しないため、全固定は Static として扱う
			}

			JPH::BodyID bodyId = CreateColliderBody(gameObject, runtimeCollider, rigidBody, isDynamic);
			if (!bodyId.IsInvalid()) {
				bodyLinks_.push_back(JoltBodyLink{gameObject.id, bodyId, runtimeCollider.colliderCenter, isDynamic});
				RegisterBody(bodyId, gameObject.id, MakeMaterial(runtimeCollider));
			}
		}
	}

	void AddCharacterVirtual(EditorGameObject& gameObject, const EditorComponent& collider) {
		float maxHorizontalScale = (std::max)(std::fabs(gameObject.scale.x), std::fabs(gameObject.scale.z));
		float radius = (std::max)(collider.colliderRadius * maxHorizontalScale, kJoltMinimumShapeSize);
		float height = GetScaledShapeSize(collider.colliderSize.y, gameObject.scale.y);
		float halfHeightOfCylinder = (std::max)((height * 0.5f) - radius, kJoltMinimumShapeSize);

		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = new JPH::CapsuleShape(halfHeightOfCylinder, radius);
		settings->mMass = (std::max)(collider.mass, 1.0f);
		settings->mMaxSlopeAngle = 50.0f * 3.1415926f / 180.0f;
		settings->mEnhancedInternalEdgeRemoval = true;

		JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
			settings,
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			static_cast<JPH::uint64>(gameObject.id),
			physicsSystem_.get());
		character->SetLinearVelocity(MakeJoltVector(collider.velocity));
		characterVirtualLinks_.push_back(JoltCharacterVirtualLink{gameObject.id, character, collider.colliderCenter});
		PushConsoleMessage("物理: CharacterController を Jolt CharacterVirtual として追加 " + gameObject.name);
	}

	void AddSceneConstraints() {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			JPH::BodyID ownerBodyId{};
			if (!TryFindPrimaryBodyId(gameObject.id, ownerBodyId)) {
				continue;
			}

			for (EditorComponent& component : gameObject.components) {
				if (!component.isActive || !IsSupportedJoint(component.type)) {
					continue;
				}

				JPH::BodyID connectedBodyId{};
				if (!TryFindPrimaryBodyId(component.connectedGameObjectId, connectedBodyId)) {
					PushConsoleMessage("物理: Joint の接続先が見つかりません ID=" + std::to_string(component.connectedGameObjectId));
					continue;
				}

				AddJointConstraint(gameObject, component, ownerBodyId, connectedBodyId);
			}
		}
	}

	bool IsSupportedJoint(EditorComponentType componentType) const {
		return
			componentType == EditorComponentType::FixedJoint ||
			componentType == EditorComponentType::HingeJoint ||
			componentType == EditorComponentType::SpringJoint ||
			componentType == EditorComponentType::CharacterJoint;
	}

	void AddJointConstraint(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& joint,
		JPH::BodyID ownerBodyId,
		JPH::BodyID connectedBodyId) {
		if (ownerBodyId == connectedBodyId) {
			return;
		}

		const JPH::BodyLockInterface& lockInterface = physicsSystem_->GetBodyLockInterface();
		JPH::BodyLockWrite ownerBodyLock(lockInterface, ownerBodyId);
		JPH::BodyLockWrite connectedBodyLock(lockInterface, connectedBodyId);
		if (!ownerBodyLock.Succeeded() || !connectedBodyLock.Succeeded()) {
			return;
		}

		JPH::Constraint* createdConstraint = nullptr;  // settings.Create が返す Jolt Constraint。Ref へ渡して Play 中保持する。
		if (joint.type == EditorComponentType::FixedJoint) {
			createdConstraint = CreateFixedConstraint(ownerGameObject, joint, ownerBodyLock.GetBody(), connectedBodyLock.GetBody());
		}
		else if (joint.type == EditorComponentType::HingeJoint ||
		         joint.type == EditorComponentType::CharacterJoint) {
			createdConstraint = CreateHingeConstraint(ownerGameObject, joint, ownerBodyLock.GetBody(), connectedBodyLock.GetBody());
		}
		else if (joint.type == EditorComponentType::SpringJoint) {
			createdConstraint = CreateSpringConstraint(ownerGameObject, joint, ownerBodyLock.GetBody(), connectedBodyLock.GetBody());
		}

		if (createdConstraint == nullptr) {
			return;
		}

		JPH::Ref<JPH::Constraint> constraint = createdConstraint;
		physicsSystem_->AddConstraint(constraint);
		constraints_.push_back(constraint);
		PushConsoleMessage("物理: Joint を追加 " + ownerGameObject.name + " -> ID " + std::to_string(joint.connectedGameObjectId));
	}

	JPH::Constraint* CreateFixedConstraint(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& joint,
		JPH::Body& ownerBody,
		JPH::Body& connectedBody) const {
		Vector3 anchorPosition = AddVector3(ownerGameObject.translate, joint.colliderCenter);
		JPH::FixedConstraintSettings fixedSettings{};
		fixedSettings.mSpace = JPH::EConstraintSpace::WorldSpace;
		fixedSettings.mAutoDetectPoint = false;
		fixedSettings.mPoint1 = MakeJoltPosition(anchorPosition);
		fixedSettings.mPoint2 = MakeJoltPosition(anchorPosition);
		fixedSettings.mAxisX1 = JPH::Vec3::sAxisX();
		fixedSettings.mAxisY1 = JPH::Vec3::sAxisY();
		fixedSettings.mAxisX2 = JPH::Vec3::sAxisX();
		fixedSettings.mAxisY2 = JPH::Vec3::sAxisY();
		return fixedSettings.Create(ownerBody, connectedBody);
	}

	JPH::Constraint* CreateHingeConstraint(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& joint,
		JPH::Body& ownerBody,
		JPH::Body& connectedBody) const {
		Vector3 anchorPosition = AddVector3(ownerGameObject.translate, joint.colliderCenter);
		Vector3 normalizedAxis = MakeJointAxis(joint.jointAxis);
		JPH::Vec3 hingeAxis = MakeJoltVector(normalizedAxis);
		JPH::Vec3 normalAxis = MakeJointNormalAxis(normalizedAxis);

		JPH::HingeConstraintSettings hingeSettings{};
		hingeSettings.mSpace = JPH::EConstraintSpace::WorldSpace;
		hingeSettings.mPoint1 = MakeJoltPosition(anchorPosition);
		hingeSettings.mPoint2 = MakeJoltPosition(anchorPosition);
		hingeSettings.mHingeAxis1 = hingeAxis;
		hingeSettings.mHingeAxis2 = hingeAxis;
		hingeSettings.mNormalAxis1 = normalAxis;
		hingeSettings.mNormalAxis2 = normalAxis;
		hingeSettings.mLimitsMin = (std::clamp)(joint.jointMinLimit, -3.1415926f, 0.0f);
		hingeSettings.mLimitsMax = (std::clamp)(joint.jointMaxLimit, 0.0f, 3.1415926f);
		hingeSettings.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
		hingeSettings.mLimitsSpringSettings.mFrequency = (std::max)(joint.jointSpringFrequency, 0.0f);
		hingeSettings.mLimitsSpringSettings.mDamping = (std::max)(joint.jointSpringDamping, 0.0f);
		return hingeSettings.Create(ownerBody, connectedBody);
	}

	JPH::Constraint* CreateSpringConstraint(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& joint,
		JPH::Body& ownerBody,
		JPH::Body& connectedBody) const {
		Vector3 anchorPosition = AddVector3(ownerGameObject.translate, joint.colliderCenter);
		float minDistance = (std::max)(joint.jointMinDistance, 0.0f);
		float maxDistance = (std::max)(joint.jointMaxDistance, minDistance);

		JPH::DistanceConstraintSettings distanceSettings{};
		distanceSettings.mSpace = JPH::EConstraintSpace::WorldSpace;
		distanceSettings.mPoint1 = MakeJoltPosition(anchorPosition);
		distanceSettings.mPoint2 = MakeJoltPosition(anchorPosition);
		distanceSettings.mMinDistance = minDistance;
		distanceSettings.mMaxDistance = maxDistance;
		distanceSettings.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
		distanceSettings.mLimitsSpringSettings.mFrequency = (std::max)(joint.jointSpringFrequency, 0.0f);
		distanceSettings.mLimitsSpringSettings.mDamping = (std::max)(joint.jointSpringDamping, 0.0f);
		return distanceSettings.Create(ownerBody, connectedBody);
	}

	EditorComponent* FindMainCollider(EditorGameObject& gameObject) const {
		const EditorComponentType colliderTypes[] = {
			EditorComponentType::BoxCollider,
			EditorComponentType::SphereCollider,
			EditorComponentType::CapsuleCollider,
			EditorComponentType::MeshCollider,
			EditorComponentType::TerrainCollider,
			EditorComponentType::CharacterController};

		for (EditorComponentType colliderType : colliderTypes) {
			EditorComponent* collider = EditorComponentUtility::FindComponent(gameObject, colliderType);
			if (collider != nullptr && collider->isActive) {
				return collider;
			}
		}

		return nullptr;
	}

	bool MakeImplicitMeshCollider(
		const EditorGameObject& gameObject,
		EditorComponent& collider) const {
		const std::string modelAssetPath = GetRenderModelAssetPath(gameObject);
		if (modelAssetPath.empty()) {
			return false;
		}

		collider = {};
		collider.type = EditorComponentType::MeshCollider;
		collider.isActive = true;
		collider.assetPath = modelAssetPath;
		collider.colliderCenter = {0.0f, 0.0f, 0.0f};
		collider.colliderSize = {1.0f, 1.0f, 1.0f};
		collider.colliderRadius = 0.5f;
		collider.dynamicFriction = 0.6f;
		collider.staticFriction = 0.6f;
		collider.bounciness = 0.0f;
		collider.frictionCombineMode = 0;
		collider.bouncinessCombineMode = 0;
		collider.physicsLayer = 0;
		collider.generateContactEvents = true;
		TryGetModelColliderBounds(gameObject, collider.colliderCenter, collider.colliderSize);
		return true;
	}

	void RefreshRuntimeMeshColliderBounds(
		const EditorGameObject& gameObject,
		EditorComponent& collider) const {
		if (collider.type != EditorComponentType::MeshCollider &&
			collider.type != EditorComponentType::TerrainCollider) {
			return;
		}

		Vector3 modelColliderCenter{};  // 現在の FBX / OBJ 頂点から取り直す Collider 中心。
		Vector3 modelColliderSize{};  // Scene 保存値が古くても Play 物理は見た目と同じ ModelData に合わせる。
		if (!TryGetModelColliderBounds(gameObject, modelColliderCenter, modelColliderSize)) {
			return;
		}

		collider.colliderCenter = modelColliderCenter;
		collider.colliderSize = modelColliderSize;
		if (collider.assetPath.empty()) {
			collider.assetPath = GetRenderModelAssetPath(gameObject);
		}
	}

	JPH::BodyID CreateColliderBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody,
		bool isDynamic) {
		if (collider.type == EditorComponentType::SphereCollider) {
			return CreateSphereBody(gameObject, collider, rigidBody, isDynamic);
		}
		if (collider.type == EditorComponentType::CapsuleCollider ||
		    collider.type == EditorComponentType::CharacterController) {
			return CreateCapsuleBody(gameObject, collider, rigidBody, isDynamic);
		}
		if (collider.type == EditorComponentType::MeshCollider ||
		    collider.type == EditorComponentType::TerrainCollider) {
			return CreateMeshBody(gameObject, collider, rigidBody, isDynamic);
		}

		return CreateBoxBody(gameObject, collider, rigidBody, isDynamic);
	}

	JPH::BodyID CreateBoxBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody,
		bool isDynamic) {
		Vector3 halfSize = {
			GetScaledShapeSize(collider.colliderSize.x, gameObject.scale.x) * 0.5f,
			GetScaledShapeSize(collider.colliderSize.y, gameObject.scale.y) * 0.5f,
			GetScaledShapeSize(collider.colliderSize.z, gameObject.scale.z) * 0.5f};
		JPH::BodyCreationSettings settings(
			new JPH::BoxShape(JPH::Vec3(halfSize.x, halfSize.y, halfSize.z), 0.0f),
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
			MakeJoltObjectLayer(collider.physicsLayer, isDynamic));
		ApplyBodySettings(settings, collider, rigidBody);
		return AddBody(settings, isDynamic);
	}

	JPH::BodyID CreateSphereBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody,
		bool isDynamic) {
		float maxScale = (std::max)((std::max)(std::fabs(gameObject.scale.x), std::fabs(gameObject.scale.y)), std::fabs(gameObject.scale.z));
		float radius = (std::max)(collider.colliderRadius * maxScale, kJoltMinimumShapeSize);
		JPH::BodyCreationSettings settings(
			new JPH::SphereShape(radius),
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
			MakeJoltObjectLayer(collider.physicsLayer, isDynamic));
		ApplyBodySettings(settings, collider, rigidBody);
		return AddBody(settings, isDynamic);
	}

	JPH::BodyID CreateCapsuleBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody,
		bool isDynamic) {
		float maxHorizontalScale = (std::max)(std::fabs(gameObject.scale.x), std::fabs(gameObject.scale.z));
		float radius = (std::max)(collider.colliderRadius * maxHorizontalScale, kJoltMinimumShapeSize);
		float height = GetScaledShapeSize(collider.colliderSize.y, gameObject.scale.y);
		float halfHeightOfCylinder = (std::max)((height * 0.5f) - radius, kJoltMinimumShapeSize);
		JPH::BodyCreationSettings settings(
			new JPH::CapsuleShape(halfHeightOfCylinder, radius),
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
			MakeJoltObjectLayer(collider.physicsLayer, isDynamic));
		ApplyBodySettings(settings, collider, rigidBody);
		return AddBody(settings, isDynamic);
	}

	JPH::BodyID CreateMeshBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody,
		bool isDynamic) {
		if (isDynamic) {
			return CreateDynamicMeshBody(gameObject, collider, rigidBody);
		}

		JPH::TriangleList triangles;
		if (!BuildMeshTrianglesFromModelAsset(gameObject, collider, triangles)) {
			Vector3 halfSize = {
				GetScaledShapeSize(collider.colliderSize.x, gameObject.scale.x) * 0.5f,
				GetScaledShapeSize(collider.colliderSize.y, gameObject.scale.y) * 0.5f,
				GetScaledShapeSize(collider.colliderSize.z, gameObject.scale.z) * 0.5f};
			AddBoxMeshTriangles(halfSize, triangles);
		}

		JPH::BodyCreationSettings settings(
			new JPH::MeshShapeSettings(triangles),
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			JPH::EMotionType::Static,
			MakeJoltObjectLayer(collider.physicsLayer, false));
		ApplyBodySettings(settings, collider, rigidBody);
		return AddBody(settings, false);
	}

	JPH::BodyID CreateDynamicMeshBody(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const EditorComponent* rigidBody) {
		// Jolt の MeshShape は静的専用なので、動くモデルは FBX 頂点から凸包を作る。
		// Box 近似より丸いモデルや斜め形状へ近く、Dynamic Body として重力・摩擦・反発も使える。
		JPH::Array<JPH::Vec3> hullPoints;
		if (!BuildConvexHullPointsFromModelAsset(gameObject, collider, hullPoints)) {
			PushConsoleMessage("物理: ConvexHull 作成に失敗したため Box 近似にします " + gameObject.name);
			return CreateBoxBody(gameObject, collider, rigidBody, true);
		}

		JPH::ConvexHullShapeSettings hullSettings(hullPoints, 0.0f);
		JPH::ShapeSettings::ShapeResult hullResult = hullSettings.Create();
		if (hullResult.HasError()) {
			PushConsoleMessage("物理: ConvexHull が無効なため Box 近似にします " + gameObject.name);
			return CreateBoxBody(gameObject, collider, rigidBody, true);
		}

		JPH::RefConst<JPH::Shape> hullShape = hullResult.Get();
		JPH::BodyCreationSettings settings(
			hullShape,
			GetBodyPosition(gameObject, collider.colliderCenter),
			MakeJoltRotation(gameObject.rotate),
			JPH::EMotionType::Dynamic,
			MakeJoltObjectLayer(collider.physicsLayer, true));
		ApplyBodySettings(settings, collider, rigidBody);
		JPH::BodyID bodyId = AddBody(settings, true);
		if (!bodyId.IsInvalid()) {
			RegisterPreciseMeshCollisionBody(bodyId, gameObject, collider, MakeEditorVector(hullShape->GetCenterOfMass()));
		}

		return bodyId;
	}

	bool BuildMeshTrianglesFromModelAsset(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		JPH::TriangleList& triangles) const {
		std::string modelAssetPath = GetCollisionModelAssetPath(gameObject, collider);
		if (modelAssetPath.empty()) {
			return false;
		}

		ModelData modelData{};
		if (!EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData) ||
			modelData.vertices.size() < 3u) {
			return false;
		}

		const Vector3 objectScale = {
			gameObject.scale.x,
			gameObject.scale.y,
			gameObject.scale.z};

		for (size_t vertexIndex = 0;
		     vertexIndex < modelData.vertices.size();
		     vertexIndex += 3u) {
			if (modelData.vertices.size() - vertexIndex < 3u) {
				break;
			}

			const VertexData* triangleVertices = modelData.vertices.data() + vertexIndex;
			const VertexData& firstVertex = triangleVertices[0];
			const VertexData& secondVertex = triangleVertices[1];
			const VertexData& thirdVertex = triangleVertices[2];

			const JPH::Float3 firstPosition(
				(firstVertex.position.x - collider.colliderCenter.x) * objectScale.x,
				(firstVertex.position.y - collider.colliderCenter.y) * objectScale.y,
				(firstVertex.position.z - collider.colliderCenter.z) * objectScale.z);
			const JPH::Float3 secondPosition(
				(secondVertex.position.x - collider.colliderCenter.x) * objectScale.x,
				(secondVertex.position.y - collider.colliderCenter.y) * objectScale.y,
				(secondVertex.position.z - collider.colliderCenter.z) * objectScale.z);
			const JPH::Float3 thirdPosition(
				(thirdVertex.position.x - collider.colliderCenter.x) * objectScale.x,
				(thirdVertex.position.y - collider.colliderCenter.y) * objectScale.y,
				(thirdVertex.position.z - collider.colliderCenter.z) * objectScale.z);

			triangles.push_back(JPH::Triangle(firstPosition, secondPosition, thirdPosition));
		}

		return !triangles.empty();
	}

	bool BuildConvexHullPointsFromModelAsset(
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		JPH::Array<JPH::Vec3>& hullPoints) const {
		const std::string modelAssetPath = GetCollisionModelAssetPath(gameObject, collider);
		if (modelAssetPath.empty()) {
			return false;
		}

		ModelData modelData{};  // ConvexHull は頂点群から Jolt が外側形状を作る。
		if (!EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData) ||
			modelData.vertices.size() < 4u) {
			return false;
		}

		const Vector3 objectScale = {
			gameObject.scale.x,
			gameObject.scale.y,
			gameObject.scale.z};
		hullPoints.reserve(modelData.vertices.size());
		for (const VertexData& vertex : modelData.vertices) {
			hullPoints.push_back(JPH::Vec3(
				(vertex.position.x - collider.colliderCenter.x) * objectScale.x,
				(vertex.position.y - collider.colliderCenter.y) * objectScale.y,
				(vertex.position.z - collider.colliderCenter.z) * objectScale.z));
		}

		return hullPoints.size() >= 4u;
	}

	std::string GetRenderModelAssetPath(const EditorGameObject& gameObject) const {
		const EditorComponent* modelRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* meshFilter =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
		if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
			return meshFilter->assetPath;
		}

		return "";
	}

	std::string GetCollisionModelAssetPath(
		const EditorGameObject& gameObject,
		const EditorComponent& collider) const {
		if (!collider.assetPath.empty()) {
			return collider.assetPath;  // MeshCollider 側で個別指定があれば、描画メッシュより優先する。
		}

		return GetRenderModelAssetPath(gameObject);
	}

	bool TryGetModelColliderBounds(
		const EditorGameObject& gameObject,
		Vector3& colliderCenter,
		Vector3& colliderSize) const {
		const EditorComponent* meshCollider =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshCollider);
		const std::string modelAssetPath =
			meshCollider != nullptr ? GetCollisionModelAssetPath(gameObject, *meshCollider) : GetRenderModelAssetPath(gameObject);
		if (modelAssetPath.empty()) {
			return false;
		}

		ModelData modelData{};  // 実メッシュの外形を、動的 MeshCollider の Box 近似サイズにも使う。
		if (!EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData) ||
			modelData.vertices.empty()) {
			return false;
		}

		Vector3 minimumPosition = {
			modelData.vertices[0].position.x,
			modelData.vertices[0].position.y,
			modelData.vertices[0].position.z};
		Vector3 maximumPosition = minimumPosition;
		for (const VertexData& vertex : modelData.vertices) {
			minimumPosition.x = (std::min)(minimumPosition.x, vertex.position.x);
			minimumPosition.y = (std::min)(minimumPosition.y, vertex.position.y);
			minimumPosition.z = (std::min)(minimumPosition.z, vertex.position.z);
			maximumPosition.x = (std::max)(maximumPosition.x, vertex.position.x);
			maximumPosition.y = (std::max)(maximumPosition.y, vertex.position.y);
			maximumPosition.z = (std::max)(maximumPosition.z, vertex.position.z);
		}

		colliderCenter = {
			(minimumPosition.x + maximumPosition.x) * 0.5f,
			(minimumPosition.y + maximumPosition.y) * 0.5f,
			(minimumPosition.z + maximumPosition.z) * 0.5f};
		colliderSize = {
			(std::max)(maximumPosition.x - minimumPosition.x, kJoltMinimumShapeSize),
			(std::max)(maximumPosition.y - minimumPosition.y, kJoltMinimumShapeSize),
			(std::max)(maximumPosition.z - minimumPosition.z, kJoltMinimumShapeSize)};
		return true;
	}

	void AddBoxMeshTriangles(const Vector3& halfSize, JPH::TriangleList& triangles) const {
		const JPH::Float3 p000(-halfSize.x, -halfSize.y, -halfSize.z);
		const JPH::Float3 p001(-halfSize.x, -halfSize.y, halfSize.z);
		const JPH::Float3 p010(-halfSize.x, halfSize.y, -halfSize.z);
		const JPH::Float3 p011(-halfSize.x, halfSize.y, halfSize.z);
		const JPH::Float3 p100(halfSize.x, -halfSize.y, -halfSize.z);
		const JPH::Float3 p101(halfSize.x, -halfSize.y, halfSize.z);
		const JPH::Float3 p110(halfSize.x, halfSize.y, -halfSize.z);
		const JPH::Float3 p111(halfSize.x, halfSize.y, halfSize.z);

		triangles.push_back(JPH::Triangle(p000, p010, p110));
		triangles.push_back(JPH::Triangle(p000, p110, p100));
		triangles.push_back(JPH::Triangle(p100, p110, p111));
		triangles.push_back(JPH::Triangle(p100, p111, p101));
		triangles.push_back(JPH::Triangle(p101, p111, p011));
		triangles.push_back(JPH::Triangle(p101, p011, p001));
		triangles.push_back(JPH::Triangle(p001, p011, p010));
		triangles.push_back(JPH::Triangle(p001, p010, p000));
		triangles.push_back(JPH::Triangle(p010, p011, p111));
		triangles.push_back(JPH::Triangle(p010, p111, p110));
		triangles.push_back(JPH::Triangle(p001, p000, p100));
		triangles.push_back(JPH::Triangle(p001, p100, p101));
	}

	JPH::RVec3 GetBodyPosition(const EditorGameObject& gameObject, const Vector3& colliderCenter) const {
		// Jolt Body の原点は Collider 中心にする。Collider 中心は Transform Scale の影響を受ける。
		return JPH::RVec3(
			static_cast<JPH::Real>(gameObject.translate.x + colliderCenter.x * gameObject.scale.x),
			static_cast<JPH::Real>(gameObject.translate.y + colliderCenter.y * gameObject.scale.y),
			static_cast<JPH::Real>(gameObject.translate.z + colliderCenter.z * gameObject.scale.z));
	}

	void ApplyBodySettings(
		JPH::BodyCreationSettings& settings,
		const EditorComponent& collider,
		const EditorComponent* rigidBody) const {
		PhysicsBodyMaterial material = MakeMaterial(collider);
		settings.mFriction = GetAverageFriction(material);
		settings.mRestitution = material.bounciness;
		settings.mIsSensor = collider.isTrigger;
		settings.mCollideKinematicVsNonDynamic = true;
		settings.mEnhancedInternalEdgeRemoval = collider.type == EditorComponentType::MeshCollider;

		if (rigidBody == nullptr || !rigidBody->isActive) {
			return;
		}

		settings.mLinearVelocity = MakeJoltVector(rigidBody->velocity);
		settings.mAngularVelocity = MakeJoltVector(rigidBody->angularVelocity);
		settings.mLinearDamping = (std::max)(rigidBody->drag, 0.0f);
		settings.mAngularDamping = (std::max)(rigidBody->angularDrag, 0.0f);
		settings.mGravityFactor = rigidBody->useGravity ? 1.0f : 0.0f;

		// Dynamic な MeshCollider は三角形ベースの静的地形へ高速で当たる場面が多く、
		// Inspector の既定値が離散のままだとユーザー設定前に貫通しやすい。
		// そのため MeshCollider を動かす時は、最低限 LinearCast を強制してすり抜けを抑える。
		const bool shouldForceContinuousForMesh =
			collider.type == EditorComponentType::MeshCollider &&
			!rigidBody->isKinematic;
		settings.mMotionQuality =
			(rigidBody->collisionDetectionMode == 1 || shouldForceContinuousForMesh) ?
				JPH::EMotionQuality::LinearCast :
				JPH::EMotionQuality::Discrete;
		settings.mAllowedDOFs = MakeAllowedDofs(*rigidBody);
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = (std::max)(rigidBody->mass, 0.01f);
	}

	JPH::EAllowedDOFs MakeAllowedDofs(const EditorComponent& rigidBody) const {
		JPH::EAllowedDOFs allowedDofs = JPH::EAllowedDOFs::All;
		if (rigidBody.freezePositionX) {
			allowedDofs &= ~JPH::EAllowedDOFs::TranslationX;
		}
		if (rigidBody.freezePositionY) {
			allowedDofs &= ~JPH::EAllowedDOFs::TranslationY;
		}
		if (rigidBody.freezePositionZ) {
			allowedDofs &= ~JPH::EAllowedDOFs::TranslationZ;
		}
		if (rigidBody.freezeRotationX) {
			allowedDofs &= ~JPH::EAllowedDOFs::RotationX;
		}
		if (rigidBody.freezeRotationY) {
			allowedDofs &= ~JPH::EAllowedDOFs::RotationY;
		}
		if (rigidBody.freezeRotationZ) {
			allowedDofs &= ~JPH::EAllowedDOFs::RotationZ;
		}

		if (allowedDofs == JPH::EAllowedDOFs::None) {
			return JPH::EAllowedDOFs::All;  // Jolt は None を Dynamic Body に使えないため、呼び出し側で Static 化する
		}

		return allowedDofs;
	}

	bool IsAllDofsFrozen(const EditorComponent& rigidBody) const {
		return
			rigidBody.freezePositionX &&
			rigidBody.freezePositionY &&
			rigidBody.freezePositionZ &&
			rigidBody.freezeRotationX &&
			rigidBody.freezeRotationY &&
			rigidBody.freezeRotationZ;
	}

	bool ShouldLayerPairCollide(int32_t firstLayer, int32_t secondLayer) const {
		int32_t clampedFirstLayer = ClampPhysicsLayer(firstLayer);
		int32_t clampedSecondLayer = ClampPhysicsLayer(secondLayer);
		if (editorScene_ == nullptr) {
			return ShouldPhysicsLayersCollide(clampedFirstLayer, clampedSecondLayer);
		}

		const EditorPhysicsSettings& physicsSettings = editorScene_->GetPhysicsSettings();
		return physicsSettings.layerCollisionMatrix[clampedFirstLayer][clampedSecondLayer];
	}

	JPH::BodyID AddBody(const JPH::BodyCreationSettings& settings, bool isDynamic) {
		return physicsSystem_->GetBodyInterface().CreateAndAddBody(
			settings,
			isDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	PhysicsBodyMaterial MakeMaterial(const EditorComponent& collider) const {
		PhysicsBodyMaterial material{};
		material.dynamicFriction = (std::max)(collider.dynamicFriction, 0.0f);
		material.staticFriction = (std::max)(collider.staticFriction, 0.0f);
		material.bounciness = (std::clamp)(collider.bounciness, 0.0f, 1.0f);
		material.frictionCombineMode = (std::clamp)(collider.frictionCombineMode, 0, 3);
		material.bouncinessCombineMode = (std::clamp)(collider.bouncinessCombineMode, 0, 3);
		material.isTrigger = collider.isTrigger;
		material.generateContactEvents = collider.generateContactEvents;
		return material;
	}

	void RegisterBody(JPH::BodyID bodyId, int32_t gameObjectId, const PhysicsBodyMaterial& material) {
		uint32_t bodyKey = MakeBodyMapKey(bodyId);
		gameObjectIdByBodyId_[bodyKey] = gameObjectId;
		bodyMaterials_[bodyKey] = material;
		if (gameObjectId >= 0 && primaryBodyIdByGameObjectId_.find(gameObjectId) == primaryBodyIdByGameObjectId_.end()) {
			primaryBodyIdByGameObjectId_[gameObjectId] = bodyId;
		}
	}

	void RegisterPreciseMeshCollisionBody(
		JPH::BodyID bodyId,
		const EditorGameObject& gameObject,
		const EditorComponent& collider,
		const Vector3& shapeCenterOfMass) {
		const std::string modelAssetPath = GetCollisionModelAssetPath(gameObject, collider);
		if (modelAssetPath.empty()) {
			return;
		}

		ModelData modelData{};  // ConvexHull の接触を、実三角形 BVH で検証するための元データ。
		if (!EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData)) {
			return;
		}

		PreciseMeshCollisionBody preciseMeshCollisionBody{};
		preciseMeshCollisionBody.gameObjectId = gameObject.id;
		if (!preciseMeshCollisionBody.collisionMesh.BuildFromModelData(modelData, collider.colliderCenter, gameObject.scale, shapeCenterOfMass)) {
			return;
		}

		const uint32_t bodyKey = MakeBodyMapKey(bodyId);
		const size_t triangleCount = preciseMeshCollisionBody.collisionMesh.GetTriangleCount();
		preciseMeshCollisionBodies_[bodyKey] = std::move(preciseMeshCollisionBody);
		PushConsoleMessage(
			"物理: MeshCollider の BVH を作成 " +
			gameObject.name +
			" 三角形=" +
			std::to_string(triangleCount));
	}

	bool HasPreciseMeshCollisionBody(const JPH::Body& body) const {
		return preciseMeshCollisionBodies_.find(MakeBodyMapKey(body.GetID())) != preciseMeshCollisionBodies_.end();
	}

	bool ShouldAcceptPreciseMeshContact(
		const JPH::Body& firstBody,
		const JPH::Body& secondBody,
		JPH::RVec3Arg baseOffset,
		const JPH::CollideShapeResult& collisionResult) const {
		const uint64_t contactPairKey = MakeContactPairKey(firstBody.GetID(), secondBody.GetID());
		const bool isPersistingContact = activeContactPairs_.find(contactPairKey) != activeContactPairs_.end();
		return
			IsPreciseMeshContactPointNearSurface(firstBody, baseOffset + collisionResult.mContactPointOn1, isPersistingContact) &&
			IsPreciseMeshContactPointNearSurface(secondBody, baseOffset + collisionResult.mContactPointOn2, isPersistingContact);
	}

	bool IsPreciseMeshContactPointNearSurface(
		const JPH::Body& body,
		JPH::RVec3Arg worldPoint,
		bool isPersistingContact) const {
		const auto meshIterator = preciseMeshCollisionBodies_.find(MakeBodyMapKey(body.GetID()));
		if (meshIterator == preciseMeshCollisionBodies_.end()) {
			return true;
		}

		const Vector3 localPoint = MakeBodyLocalPoint(body, worldPoint);

		// 継続接触は少し広い許容距離で受け、境界上での Reject/Accept の振動を減らす。
		const float additionalTolerance =
			isPersistingContact ?
				meshIterator->second.collisionMesh.GetContactTolerance() * 0.5f :
				0.0f;
		return meshIterator->second.collisionMesh.IsPointNearSurface(localPoint, additionalTolerance);
	}

	Vector3 MakeBodyLocalPoint(const JPH::Body& body, JPH::RVec3Arg worldPoint) const {
		// Jolt の接触点は Shape の重心ローカルで作られるため、BVH も同じ重心ローカルへ戻して比較する。
		const JPH::RVec3 bodyPosition = body.GetCenterOfMassPosition();
		const JPH::Vec3 relativePosition(
			static_cast<float>(worldPoint.GetX() - bodyPosition.GetX()),
			static_cast<float>(worldPoint.GetY() - bodyPosition.GetY()),
			static_cast<float>(worldPoint.GetZ() - bodyPosition.GetZ()));
		return MakeEditorVector(body.GetRotation().InverseRotate(relativePosition));
	}

	bool TryFindPrimaryBodyId(int32_t gameObjectId, JPH::BodyID& bodyId) const {
		auto bodyIterator = primaryBodyIdByGameObjectId_.find(gameObjectId);
		if (bodyIterator == primaryBodyIdByGameObjectId_.end()) {
			return false;
		}

		bodyId = bodyIterator->second;
		return !bodyId.IsInvalid();
	}

	void ApplyComponentVelocitiesToBodies() {
		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

		for (const JoltBodyLink& bodyLink : bodyLinks_) {
			if (!bodyLink.isDynamic) {
				continue;
			}

			EditorGameObject* gameObject = editorScene_->FindGameObject(bodyLink.gameObjectId);
			if (gameObject == nullptr) {
				continue;
			}

			EditorComponent* rigidBody =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
			if (rigidBody == nullptr || !rigidBody->isActive) {
				continue;
			}

			bodyInterface.SetLinearVelocity(bodyLink.bodyId, MakeJoltVector(rigidBody->velocity));
			bodyInterface.SetAngularVelocity(bodyLink.bodyId, MakeJoltVector(rigidBody->angularVelocity));
			bodyInterface.ActivateBody(bodyLink.bodyId);
		}
	}

	void UpdateCharacterVirtuals(float deltaTime) {
		if (characterVirtualLinks_.empty()) {
			return;
		}

		const EditorPhysicsSettings& physicsSettings = editorScene_->GetPhysicsSettings();
		JPH::BodyFilter bodyFilter;  // CharacterVirtual 用の追加 Body 除外はまだないため、既定ですべて候補にする
		JPH::ShapeFilter shapeFilter;  // Shape 単位の除外も現状は使わない

		for (JoltCharacterVirtualLink& characterLink : characterVirtualLinks_) {
			EditorGameObject* gameObject = editorScene_->FindGameObject(characterLink.gameObjectId);
			if (gameObject == nullptr || characterLink.character == nullptr) {
				continue;
			}

			EditorComponent* characterController =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::CharacterController);
			if (characterController == nullptr || !characterController->isActive) {
				continue;
			}

			Vector3 characterVelocity = characterController->velocity;
			if (characterController->useGravity) {
				characterVelocity.x += physicsSettings.gravity.x * deltaTime;
				characterVelocity.y += physicsSettings.gravity.y * deltaTime;
				characterVelocity.z += physicsSettings.gravity.z * deltaTime;
			}

			JPH::ObjectLayer characterObjectLayer = MakeJoltObjectLayer(characterController->physicsLayer, true);
			JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(objectVsBroadPhaseLayerFilter_, characterObjectLayer);
			JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, characterObjectLayer);
			JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings{};

			characterLink.character->SetLinearVelocity(MakeJoltVector(characterVelocity));
			characterLink.character->ExtendedUpdate(
				deltaTime,
				MakeJoltVector(physicsSettings.gravity),
				updateSettings,
				broadPhaseLayerFilter,
				objectLayerFilter,
				bodyFilter,
				shapeFilter,
				*tempAllocator_);

			Vector3 characterPosition = MakeEditorPosition(characterLink.character->GetPosition());
			gameObject->translate = {
				characterPosition.x - characterLink.colliderCenter.x * gameObject->scale.x,
				characterPosition.y - characterLink.colliderCenter.y * gameObject->scale.y,
				characterPosition.z - characterLink.colliderCenter.z * gameObject->scale.z};
			gameObject->rotate = MakeEditorRotation(characterLink.character->GetRotation());
			characterController->velocity = MakeEditorVector(characterLink.character->GetLinearVelocity());
		}
	}

	void WriteBackDynamicBodies() {
		JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

		for (const JoltBodyLink& bodyLink : bodyLinks_) {
			if (!bodyLink.isDynamic) {
				continue;
			}

			EditorGameObject* gameObject = editorScene_->FindGameObject(bodyLink.gameObjectId);
			if (gameObject == nullptr) {
				continue;
			}

			JPH::RVec3 bodyPosition{};
			JPH::Quat bodyRotation{};
			bodyInterface.GetPositionAndRotation(bodyLink.bodyId, bodyPosition, bodyRotation);
			gameObject->translate = {
				static_cast<float>(bodyPosition.GetX()) - bodyLink.colliderCenter.x * gameObject->scale.x,
				static_cast<float>(bodyPosition.GetY()) - bodyLink.colliderCenter.y * gameObject->scale.y,
				static_cast<float>(bodyPosition.GetZ()) - bodyLink.colliderCenter.z * gameObject->scale.z};
			gameObject->rotate = MakeEditorRotation(bodyRotation);

			EditorComponent* rigidBody =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::RigidBody);
			if (rigidBody != nullptr) {
				rigidBody->velocity = MakeEditorVector(bodyInterface.GetLinearVelocity(bodyLink.bodyId));
				rigidBody->angularVelocity = MakeEditorVector(bodyInterface.GetAngularVelocity(bodyLink.bodyId));
			}
		}
	}

	bool CanRunQuery(float distance) const {
		return isActive_ && physicsSystem_ != nullptr && distance > kJoltMinimumShapeSize;
	}

	bool ShapeCast(
		JPH::RefConst<JPH::Shape> shape,
		const Vector3& origin,
		const Vector3& direction,
		float distance,
		PhysicsHit& hit) const {
		Vector3 normalizedDirection{};
		if (!NormalizeDirection(direction, normalizedDirection)) {
			return false;
		}

		JPH::Vec3 castDirection = MakeJoltVector({
			normalizedDirection.x * distance,
			normalizedDirection.y * distance,
			normalizedDirection.z * distance});
		JPH::RMat44 startTransform = JPH::RMat44::sTranslation(MakeJoltPosition(origin));
		JPH::RShapeCast shapeCast(shape, JPH::Vec3::sReplicate(1.0f), startTransform, castDirection);
		JPH::ShapeCastSettings shapeCastSettings{};
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		JoltQueryObjectLayerFilter queryObjectLayerFilter;
		physicsSystem_->GetNarrowPhaseQuery().CastShape(
			shapeCast,
			shapeCastSettings,
			MakeJoltPosition(origin),
			collector,
			{},
			queryObjectLayerFilter);

		if (!collector.HadHit()) {
			return false;
		}

		const JPH::ShapeCastResult& result = collector.mHit;
		hit.gameObjectId = GetGameObjectId(result.mBodyID2);
		hit.point = MakeEditorVector(result.mContactPointOn2);
		hit.normal = MakeEditorVector(result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()));
		hit.distance = distance * result.mFraction;
		hit.isTrigger = IsTriggerBody(result.mBodyID2);
		return true;
	}

	bool OverlapShape(
		JPH::RefConst<JPH::Shape> shape,
		const Vector3& center,
		std::vector<int32_t>& hitGameObjectIds) const {
		hitGameObjectIds.clear();
		if (!isActive_ || physicsSystem_ == nullptr) {
			return false;
		}

		JPH::CollideShapeSettings collideShapeSettings{};
		collideShapeSettings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
		collideShapeSettings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		JoltQueryObjectLayerFilter queryObjectLayerFilter;
		physicsSystem_->GetNarrowPhaseQuery().CollideShape(
			shape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::RMat44::sTranslation(MakeJoltPosition(center)),
			collideShapeSettings,
			MakeJoltPosition(center),
			collector,
			{},
			queryObjectLayerFilter);

		if (!collector.HadHit()) {
			return false;
		}

		for (const JPH::CollideShapeResult& hitResult : collector.mHits) {
			int32_t gameObjectId = GetGameObjectId(hitResult.mBodyID2);
			if (gameObjectId < 0) {
				continue;
			}
			if (std::find(hitGameObjectIds.begin(), hitGameObjectIds.end(), gameObjectId) != hitGameObjectIds.end()) {
				continue;
			}

			hitGameObjectIds.push_back(gameObjectId);
		}

		return !hitGameObjectIds.empty();
	}

	int32_t GetGameObjectId(JPH::BodyID bodyId) const {
		auto idIterator = gameObjectIdByBodyId_.find(bodyId.GetIndexAndSequenceNumber());
		if (idIterator == gameObjectIdByBodyId_.end()) {
			return -1;
		}

		return idIterator->second;
	}

	PhysicsBodyMaterial GetMaterial(JPH::BodyID bodyId) const {
		auto materialIterator = bodyMaterials_.find(bodyId.GetIndexAndSequenceNumber());
		if (materialIterator == bodyMaterials_.end()) {
			return PhysicsBodyMaterial{};
		}

		return materialIterator->second;
	}

	bool IsTriggerBody(JPH::BodyID bodyId) const {
		return GetMaterial(bodyId).isTrigger;
	}

	Vector3 GetHitNormal(JPH::BodyID bodyId, JPH::SubShapeID subShapeId, JPH::RVec3Arg hitPoint) const {
		const JPH::BodyLockInterface& lockInterface = physicsSystem_->GetBodyLockInterface();
		JPH::BodyLockRead bodyLock(lockInterface, bodyId);
		if (!bodyLock.Succeeded()) {
			return Vector3{0.0f, 1.0f, 0.0f};
		}

		return MakeEditorVector(bodyLock.GetBody().GetWorldSpaceSurfaceNormal(subShapeId, hitPoint));
	}

	Vector3 GetRelativeVelocity(const JPH::Body& firstBody, const JPH::Body& secondBody) const {
		return {
			secondBody.GetLinearVelocity().GetX() - firstBody.GetLinearVelocity().GetX(),
			secondBody.GetLinearVelocity().GetY() - firstBody.GetLinearVelocity().GetY(),
			secondBody.GetLinearVelocity().GetZ() - firstBody.GetLinearVelocity().GetZ()};
	}

	ActiveContactPair MakeActiveContactPair(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold) const {
		PhysicsBodyMaterial firstMaterial = GetMaterial(firstBody.GetID());
		PhysicsBodyMaterial secondMaterial = GetMaterial(secondBody.GetID());

		ActiveContactPair contactPair{};
		contactPair.firstGameObjectId = GetGameObjectId(firstBody.GetID());
		contactPair.secondGameObjectId = GetGameObjectId(secondBody.GetID());
		contactPair.isTrigger = firstBody.IsSensor() || secondBody.IsSensor();
		contactPair.shouldLog = firstMaterial.generateContactEvents || secondMaterial.generateContactEvents;
		contactPair.normal = MakeEditorVector(manifold.mWorldSpaceNormal);
		contactPair.relativeVelocity = GetRelativeVelocity(firstBody, secondBody);
		contactPair.separation = manifold.mPenetrationDepth;
		if (!manifold.mRelativeContactPointsOn1.empty()) {
			contactPair.point = MakeEditorPosition(manifold.GetWorldSpaceContactPointOn1(0));
		}
		return contactPair;
	}

	CollisionInfo MakeCollisionInfoForFirst(const ActiveContactPair& contactPair) const {
		CollisionInfo collisionInfo{};
		collisionInfo.selfGameObjectId = contactPair.firstGameObjectId;
		collisionInfo.otherGameObjectId = contactPair.secondGameObjectId;
		collisionInfo.point = contactPair.point;
		collisionInfo.normal = contactPair.normal;
		collisionInfo.relativeVelocity = contactPair.relativeVelocity;
		collisionInfo.separation = contactPair.separation;
		collisionInfo.isTrigger = contactPair.isTrigger;
		return collisionInfo;
	}

	CollisionInfo MakeCollisionInfoForSecond(const ActiveContactPair& contactPair) const {
		CollisionInfo collisionInfo{};
		collisionInfo.selfGameObjectId = contactPair.secondGameObjectId;
		collisionInfo.otherGameObjectId = contactPair.firstGameObjectId;
		collisionInfo.point = contactPair.point;
		collisionInfo.normal = {
			-contactPair.normal.x,
			-contactPair.normal.y,
			-contactPair.normal.z};
		collisionInfo.relativeVelocity = {
			-contactPair.relativeVelocity.x,
			-contactPair.relativeVelocity.y,
			-contactPair.relativeVelocity.z};
		collisionInfo.separation = contactPair.separation;
		collisionInfo.isTrigger = contactPair.isTrigger;
		return collisionInfo;
	}

	void PushPhysicsEvent(PhysicsEventType eventType, const CollisionInfo& collisionInfo) {
		if (collisionInfo.selfGameObjectId < 0) {
			return;
		}

		PhysicsEvent physicsEvent{};
		physicsEvent.type = eventType;
		physicsEvent.collision = collisionInfo;
		stepEvents_.push_back(physicsEvent);
	}

	void PushStepEvents(const ActiveContactPair& contactPair, PhysicsEventType eventType) {
		PushPhysicsEvent(eventType, MakeCollisionInfoForFirst(contactPair));
		PushPhysicsEvent(eventType, MakeCollisionInfoForSecond(contactPair));
	}

	[[maybe_unused]] std::string MakeContactMessage(const char* eventName, const ActiveContactPair& contactPair) const {
		return
			std::string("物理: ") +
			eventName +
			" " +
			GetObjectName(contactPair.firstGameObjectId) +
			" <-> " +
			GetObjectName(contactPair.secondGameObjectId);
	}

	std::string GetObjectName(int32_t gameObjectId) const {
		if (gameObjectId < 0) {
			return "床";
		}

		const EditorGameObject* gameObject = editorScene_ != nullptr ? editorScene_->FindGameObject(gameObjectId) : nullptr;
		if (gameObject == nullptr) {
			return "不明";
		}

		return gameObject->name;
	}

	void PushConsoleMessage(const std::string& message) {
		if (consoleMessages_ == nullptr) {
			return;
		}

		consoleMessages_->push_back(message);
		if (consoleMessages_->size() > kMaxPhysicsConsoleMessages) {
			consoleMessages_->erase(consoleMessages_->begin());
		}
	}
};

EditorJoltPhysicsManager::EditorJoltPhysicsManager() {
	EnsureJoltGlobalInitialized();
	impl_ = std::make_unique<Impl>();
}

EditorJoltPhysicsManager::~EditorJoltPhysicsManager() = default;

void EditorJoltPhysicsManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	impl_->Initialize(editorScene, consoleMessages);
}

void EditorJoltPhysicsManager::Start() {
	impl_->Start();
}

void EditorJoltPhysicsManager::Update(float deltaTime) {
	impl_->Update(deltaTime);
}

void EditorJoltPhysicsManager::Stop() {
	impl_->Stop();
}

bool EditorJoltPhysicsManager::IsActive() const {
	return impl_->IsActive();
}

bool EditorJoltPhysicsManager::Raycast(
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	PhysicsHit& hit) const {
	return impl_->Raycast(origin, direction, distance, hit);
}

bool EditorJoltPhysicsManager::SphereCast(
	const Vector3& origin,
	float radius,
	const Vector3& direction,
	float distance,
	PhysicsHit& hit) const {
	return impl_->SphereCast(origin, radius, direction, distance, hit);
}

bool EditorJoltPhysicsManager::CapsuleCast(
	const Vector3& origin,
	float radius,
	float height,
	const Vector3& direction,
	float distance,
	PhysicsHit& hit) const {
	return impl_->CapsuleCast(origin, radius, height, direction, distance, hit);
}

bool EditorJoltPhysicsManager::OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const {
	return impl_->OverlapSphere(center, radius, hitGameObjectIds);
}

bool EditorJoltPhysicsManager::OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const {
	return impl_->OverlapBox(center, size, hitGameObjectIds);
}

bool EditorJoltPhysicsManager::AddForce(int32_t gameObjectId, const Vector3& force) {
	return impl_->AddForce(gameObjectId, force);
}

bool EditorJoltPhysicsManager::AddImpulse(int32_t gameObjectId, const Vector3& impulse) {
	return impl_->AddImpulse(gameObjectId, impulse);
}

bool EditorJoltPhysicsManager::AddTorque(int32_t gameObjectId, const Vector3& torque) {
	return impl_->AddTorque(gameObjectId, torque);
}

bool EditorJoltPhysicsManager::SetVelocity(int32_t gameObjectId, const Vector3& velocity) {
	return impl_->SetVelocity(gameObjectId, velocity);
}

bool EditorJoltPhysicsManager::SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity) {
	return impl_->SetAngularVelocity(gameObjectId, angularVelocity);
}

const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& EditorJoltPhysicsManager::GetStepEvents() const {
	return impl_->GetStepEvents();
}

void EditorJoltPhysicsManager::ClearStepEvents() {
	impl_->ClearStepEvents();
}
