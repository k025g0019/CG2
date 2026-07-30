#include "EditorPhysicsManager.h"

#include "EditorComponentUtility.h"
#include "EditorOceanSystem.h"
#include "Source/Engine/Core/Vector&Matrix.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
	//================================================================
	// 実行時の浮力設定
	// Buoyancy がなければ Dynamic Rigidbody の3D Colliderから安全な既定値を作る
	//================================================================
	struct RuntimeBuoyancySettings {
		int32_t oceanGameObjectId = -1;
		Vector3 centerOffset = {0.0f, 0.0f, 0.0f};
		Vector3 hullSize = {1.0f, 1.0f, 1.0f};
		float strength = 24.0f;
		float damping = 7.0f;
		float waterDrag = 1.4f;
		float angularDrag = 1.8f;
	};

	struct BuoyancyGridDimensions {
		int32_t countX = 2;
		int32_t countY = 2;
		int32_t countZ = 2;
	};

	constexpr float kBuoyancyTargetCellSize = 1.0f;
	constexpr int32_t kBuoyancyMaximumHorizontalCount = 16;
	constexpr int32_t kBuoyancyMaximumVerticalCount = 8;
	constexpr int32_t kBuoyancyMaximumCellCount = 512;

	int32_t CalculateBuoyancyAxisCount(float worldLength, int32_t maximumCount) {
		const int32_t requestedCount = static_cast<int32_t>(std::ceil(
			(std::max)(worldLength, 0.05f) / kBuoyancyTargetCellSize));
		return (std::clamp)(requestedCount, 2, maximumCount);
	}

	BuoyancyGridDimensions BuildBuoyancyGridDimensions(
		const Vector3& localHullSize,
		const Vector3& objectScale) {
		const Vector3 worldHullSize{
			localHullSize.x * (std::max)(std::fabs(objectScale.x), 0.01f),
			localHullSize.y * (std::max)(std::fabs(objectScale.y), 0.01f),
			localHullSize.z * (std::max)(std::fabs(objectScale.z), 0.01f)};
		BuoyancyGridDimensions gridDimensions{};
		gridDimensions.countX = CalculateBuoyancyAxisCount(
			worldHullSize.x,
			kBuoyancyMaximumHorizontalCount);
		gridDimensions.countY = CalculateBuoyancyAxisCount(
			worldHullSize.y,
			kBuoyancyMaximumVerticalCount);
		gridDimensions.countZ = CalculateBuoyancyAxisCount(
			worldHullSize.z,
			kBuoyancyMaximumHorizontalCount);

		// 大型船でも精度を上げるが、1 Body が物理更新を占有しないよう最大セル数だけ制限する。
		while (gridDimensions.countX * gridDimensions.countY * gridDimensions.countZ >
			kBuoyancyMaximumCellCount) {
			if (gridDimensions.countX >= gridDimensions.countY &&
				gridDimensions.countX >= gridDimensions.countZ &&
				gridDimensions.countX > 2) {
				gridDimensions.countX--;
			}
			else if (gridDimensions.countZ >= gridDimensions.countY &&
				gridDimensions.countZ > 2) {
				gridDimensions.countZ--;
			}
			else if (gridDimensions.countY > 2) {
				gridDimensions.countY--;
			}
			else {
				break;
			}
		}

		return gridDimensions;
	}

	const EditorComponent* FindActiveWaterCollider(const EditorGameObject& gameObject) {
		constexpr std::array<EditorComponentType, 5u> kWaterColliderTypes = {
			EditorComponentType::AutoConvexCollision,
			EditorComponentType::BoxCollider,
			EditorComponentType::SphereCollider,
			EditorComponentType::CapsuleCollider,
			EditorComponentType::MeshCollider};

		for (EditorComponentType colliderType : kWaterColliderTypes) {
			const EditorComponent* colliderComponent =
				EditorComponentUtility::FindComponent(gameObject, colliderType);

			if (colliderComponent != nullptr &&
				colliderComponent->isActive &&
				!colliderComponent->isTrigger) {
				return colliderComponent;
			}
		}

		return nullptr;
	}

	bool BuildRuntimeBuoyancySettings(
		const EditorGameObject& gameObject,
		const EditorComponent* buoyancyComponent,
		RuntimeBuoyancySettings& buoyancySettings) {
		if (buoyancyComponent != nullptr) {
			// 無効化した Buoyancy を自動設定で復活させず、Component の有効状態を優先する。
			if (!buoyancyComponent->isActive) {
				return false;
			}

			buoyancySettings.oceanGameObjectId = buoyancyComponent->buoyancyOceanGameObjectId;
			buoyancySettings.centerOffset = buoyancyComponent->buoyancyCenterOffset;
			buoyancySettings.hullSize = buoyancyComponent->buoyancyHullSize;
			buoyancySettings.strength = buoyancyComponent->buoyancyStrength;
			buoyancySettings.damping = buoyancyComponent->buoyancyDamping;
			buoyancySettings.waterDrag = buoyancyComponent->buoyancyWaterDrag;
			buoyancySettings.angularDrag = buoyancyComponent->buoyancyAngularDrag;
			return true;
		}

		const EditorComponent* colliderComponent = FindActiveWaterCollider(gameObject);

		if (colliderComponent == nullptr) {
			return false;
		}

		buoyancySettings.centerOffset = colliderComponent->colliderCenter;

		if (colliderComponent->type == EditorComponentType::SphereCollider) {
			const float diameter = (std::max)(std::fabs(colliderComponent->colliderRadius) * 2.0f, 0.05f);
			buoyancySettings.hullSize = {diameter, diameter, diameter};
		}
		else if (colliderComponent->type == EditorComponentType::CapsuleCollider) {
			const float diameter = (std::max)(std::fabs(colliderComponent->colliderRadius) * 2.0f, 0.05f);
			buoyancySettings.hullSize = {
				diameter,
				(std::max)(std::fabs(colliderComponent->colliderSize.y), diameter),
				diameter};
		}
		else {
			buoyancySettings.hullSize = colliderComponent->colliderSize;
		}

		return true;
	}
}

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
		// 水面上の船へ位置付き浮力を先に加え、その力を含めて Jolt を進める。
		ApplyBuoyancyForces(fixedTimeStep_);
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

bool EditorPhysicsManager::SetGameObjectSimulationActive(int32_t gameObjectId, bool isActive) {
	return joltPhysicsManager_.SetGameObjectSimulationActive(gameObjectId, isActive);
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

bool EditorPhysicsManager::AddForceAtPosition(
	int32_t gameObjectId,
	const Vector3& force,
	const Vector3& worldPosition) {
	return joltPhysicsManager_.AddForceAtPosition(gameObjectId, force, worldPosition);
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

void EditorPhysicsManager::ApplyBuoyancyForces(float fixedDeltaTime) {
	if (editorScene_ == nullptr || fixedDeltaTime <= 0.0f) {
		return;
	}

	const float oceanElapsedTime = GetEditorOceanElapsedTime();

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RigidBody);

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		const EditorComponent* buoyancyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Buoyancy);
		RuntimeBuoyancySettings buoyancySettings{};

		if (!BuildRuntimeBuoyancySettings(gameObject, buoyancyComponent, buoyancySettings)) {
			continue;
		}

		const Vector3 safeHullSize{
			(std::max)(std::fabs(buoyancySettings.hullSize.x), 0.05f),
			(std::max)(std::fabs(buoyancySettings.hullSize.y), 0.05f),
			(std::max)(std::fabs(buoyancySettings.hullSize.z), 0.05f)};
		const BuoyancyGridDimensions gridDimensions = BuildBuoyancyGridDimensions(
			safeHullSize,
			gameObject.scale);
		const int32_t floatPointCount =
			gridDimensions.countX * gridDimensions.countY * gridDimensions.countZ;
		const Vector3 localCellSize{
			safeHullSize.x / static_cast<float>(gridDimensions.countX),
			safeHullSize.y / static_cast<float>(gridDimensions.countY),
			safeHullSize.z / static_cast<float>(gridDimensions.countZ)};
		const Vector3 localCellHalfSize = Multiply(0.5f, localCellSize);
		const Vector3 localHullMinimum{
			buoyancySettings.centerOffset.x - safeHullSize.x * 0.5f,
			buoyancySettings.centerOffset.y - safeHullSize.y * 0.5f,
			buoyancySettings.centerOffset.z - safeHullSize.z * 0.5f};
		const float safeMass = (std::max)(rigidBodyComponent->mass, 0.01f);
		const float pointMass = safeMass / static_cast<float>(floatPointCount);
		const float buoyancyStrength = (std::max)(buoyancySettings.strength, 0.0f);
		const float buoyancyDamping = (std::max)(buoyancySettings.damping, 0.0f);
		const Matrix4x4 boatWorldMatrix = MakeAffineMatrix(
			gameObject.scale,
			gameObject.rotate,
			gameObject.translate);
		float totalSubmersionRatio = 0.0f;
		Vector3 accumulatedWaterVelocity{0.0f, 0.0f, 0.0f};
		int32_t submergedPointCount = 0;
		int32_t resolvedOceanGameObjectId =
			buoyancySettings.oceanGameObjectId;

		for (int32_t gridIndexZ = 0; gridIndexZ < gridDimensions.countZ; gridIndexZ++) {
			for (int32_t gridIndexX = 0; gridIndexX < gridDimensions.countX; gridIndexX++) {
				const Vector3 localColumnCenter{
					localHullMinimum.x +
						(static_cast<float>(gridIndexX) + 0.5f) * localCellSize.x,
					buoyancySettings.centerOffset.y,
					localHullMinimum.z +
						(static_cast<float>(gridIndexZ) + 0.5f) * localCellSize.z};
				const Vector3 worldColumnCenter = Transform(localColumnCenter, boatWorldMatrix);
				const int32_t columnIndex =
					gridIndexZ * gridDimensions.countX + gridIndexX;
				const uint32_t objectSampleKey =
					static_cast<uint32_t>(gameObject.id) * 4099u;
				const uint64_t surfaceSampleKey = static_cast<uint64_t>(
					objectSampleKey + static_cast<uint32_t>(columnIndex));
				EditorOceanSurfaceSample surfaceSample{};

				// 同じ X/Z 列にある縦セルは同じ FFT 水面を共有し、GPU 読み戻しを重複させない。
				if (!SampleEditorOceanSurface(
						*editorScene_,
						resolvedOceanGameObjectId,
						worldColumnCenter,
						surfaceSampleKey,
						oceanElapsedTime,
						surfaceSample)) {
					continue;
				}

				if (resolvedOceanGameObjectId < 0) {
					resolvedOceanGameObjectId = surfaceSample.oceanGameObjectId;
				}

				for (int32_t gridIndexY = 0; gridIndexY < gridDimensions.countY; gridIndexY++) {
					const Vector3 localFloatPoint{
						localColumnCenter.x,
						localHullMinimum.y +
							(static_cast<float>(gridIndexY) + 0.5f) * localCellSize.y,
						localColumnCenter.z};
					const Vector3 worldFloatPoint = Transform(localFloatPoint, boatWorldMatrix);
					const Vector3 worldCellExtentX = Transform(
						Add(localFloatPoint, {localCellHalfSize.x, 0.0f, 0.0f}),
						boatWorldMatrix);
					const Vector3 worldCellExtentY = Transform(
						Add(localFloatPoint, {0.0f, localCellHalfSize.y, 0.0f}),
						boatWorldMatrix);
					const Vector3 worldCellExtentZ = Transform(
						Add(localFloatPoint, {0.0f, 0.0f, localCellHalfSize.z}),
						boatWorldMatrix);
					const float projectedCellHalfHeight = (std::max)(
						std::fabs(worldCellExtentX.y - worldFloatPoint.y) +
							std::fabs(worldCellExtentY.y - worldFloatPoint.y) +
							std::fabs(worldCellExtentZ.y - worldFloatPoint.y),
						0.005f);
					const float cellBottomPositionY =
						worldFloatPoint.y - projectedCellHalfHeight;
					const float submergedCellHeight = (std::clamp)(
						surfaceSample.position.y - cellBottomPositionY,
						0.0f,
						projectedCellHalfHeight * 2.0f);

					if (submergedCellHeight <= 0.0f) {
						continue;
					}

					const float submersionRatio =
						submergedCellHeight / (projectedCellHalfHeight * 2.0f);
					Vector3 worldForcePoint = worldFloatPoint;
					worldForcePoint.y = cellBottomPositionY + submergedCellHeight * 0.5f;
					const Vector3 centerToPoint = Subtract(worldForcePoint, gameObject.translate);
					const Vector3 pointAngularVelocity = Cross(
						rigidBodyComponent->angularVelocity,
						centerToPoint);
					const Vector3 pointVelocity = Add(
						rigidBodyComponent->velocity,
						pointAngularVelocity);
					const float relativeVerticalVelocity = Subtract(
						pointVelocity,
						surfaceSample.velocity).y;
					const float pointAcceleration = (std::clamp)(
						buoyancyStrength * submersionRatio -
							relativeVerticalVelocity * buoyancyDamping * submersionRatio,
						0.0f,
						(std::max)(buoyancyStrength * 2.0f, 39.24f));
					const Vector3 buoyancyForce = Multiply(
						pointMass * pointAcceleration,
						Vector3{0.0f, 1.0f, 0.0f});

					AddForceAtPosition(gameObject.id, buoyancyForce, worldForcePoint);
					totalSubmersionRatio += submersionRatio;
					accumulatedWaterVelocity = Add(
						accumulatedWaterVelocity,
						surfaceSample.velocity);
					submergedPointCount++;
				}
			}
		}

		if (submergedPointCount <= 0) {
			continue;
		}

		const float averageSubmersionRatio =
			totalSubmersionRatio / static_cast<float>(floatPointCount);
		const float waterDrag = (std::max)(buoyancySettings.waterDrag, 0.0f);
		const float angularDrag = (std::max)(buoyancySettings.angularDrag, 0.0f);
		const Vector3 averageWaterVelocity = Multiply(
			1.0f / static_cast<float>(submergedPointCount),
			accumulatedWaterVelocity);
		const Vector3 relativeBodyVelocity = Subtract(
			rigidBodyComponent->velocity,
			averageWaterVelocity);
		const Vector3 waterDragForce = Multiply(
			-safeMass * waterDrag * averageSubmersionRatio,
			relativeBodyVelocity);
		const Vector3 waterAngularDragTorque = Multiply(
			-safeMass * angularDrag * averageSubmersionRatio,
			rigidBodyComponent->angularVelocity);

		AddForce(gameObject.id, waterDragForce);
		AddTorque(gameObject.id, waterAngularDragTorque);
	}
}
