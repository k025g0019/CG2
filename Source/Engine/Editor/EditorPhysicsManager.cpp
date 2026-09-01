#include "EditorPhysicsManager.h"

#include "EditorComponentUtility.h"
#include "EditorOceanSystem.h"
#include "Source/Engine/Core/Vector&Matrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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
		float normalInfluence = 0.2f;
		float lateralDrag = 4.0f;
		float verticalDrag = 2.5f;
		float slammingStrength = 2.0f;
		bool automaticPhysicalProperties = false;
		float waterDensity = 1025.0f;
		bool limitDraftHeight = false;  // Collider から自動で作る船体は喫水高さに縦範囲を制限する
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
	constexpr float kBuoyancyDraftOverheadRatio = 1.25f;  // 平衡喫水の上に波の頂上分の余裕を持たせる
	constexpr float kBuoyancyDraftMinimumHeight = 0.3f;  // 平たい物体にも縦方向の最低セル高さを確保する
	constexpr int32_t kBuoyancySurfaceProbeAxisCount = 5;  // 局所FFT水面を補間する固定Probe数
	constexpr size_t kBuoyancySurfaceProbeCount =
		static_cast<size_t>(kBuoyancySurfaceProbeAxisCount * kBuoyancySurfaceProbeAxisCount);
	constexpr float kWaterKinematicViscosity = 1.004e-6f;  // 20度付近の水の動粘性係数（m^2/s）

	struct SubmergedSurfacePanel {
		Vector3 center = {0.0f, 0.0f, 0.0f};  // 水面で切った面の面積重心
		Vector3 outwardNormal = {0.0f, 1.0f, 0.0f};  // 実Shape外側を向く面法線
		float area = 0.0f;  // 水没部分だけの重み付き面積
	};

	bool BuildSubmergedSurfacePanel(
		const EditorJoltPhysicsManager::HydrodynamicSurfaceTriangle& surfaceTriangle,
		const std::array<float, 3u>& vertexSurfaceDistances,
		SubmergedSurfacePanel& submergedPanel) {
		const std::array<Vector3, 3u> inputVertices = {
			surfaceTriangle.first,
			surfaceTriangle.second,
			surfaceTriangle.third};
		std::array<Vector3, 4u> clippedVertices{};
		int32_t clippedVertexCount = 0;

		for (size_t vertexIndex = 0u; vertexIndex < inputVertices.size(); vertexIndex++) {
			const size_t nextVertexIndex = (vertexIndex + 1u) % inputVertices.size();
			const Vector3& currentVertex = inputVertices[vertexIndex];
			const Vector3& nextVertex = inputVertices[nextVertexIndex];
			const float currentDistance = vertexSurfaceDistances[vertexIndex];
			const float nextDistance = vertexSurfaceDistances[nextVertexIndex];
			const bool isCurrentSubmerged = currentDistance <= 0.0f;
			const bool isNextSubmerged = nextDistance <= 0.0f;

			if (isCurrentSubmerged &&
				clippedVertexCount < static_cast<int32_t>(clippedVertices.size())) {
				clippedVertices[static_cast<size_t>(clippedVertexCount)] = currentVertex;
				clippedVertexCount++;
			}

			if (isCurrentSubmerged == isNextSubmerged) {
				continue;
			}

			const float edgeDistanceDifference = currentDistance - nextDistance;
			if (std::fabs(edgeDistanceDifference) <= 0.000001f ||
				clippedVertexCount >= static_cast<int32_t>(clippedVertices.size())) {
				continue;
			}

			const float intersectionRatio = (std::clamp)(
				currentDistance / edgeDistanceDifference,
				0.0f,
				1.0f);
			clippedVertices[static_cast<size_t>(clippedVertexCount)] = Add(
				currentVertex,
				Multiply(
					intersectionRatio,
					Subtract(nextVertex, currentVertex)));
			clippedVertexCount++;
		}

		if (clippedVertexCount < 3) {
			return false;
		}

		Vector3 outwardNormal = Cross(
			Subtract(surfaceTriangle.second, surfaceTriangle.first),
			Subtract(surfaceTriangle.third, surfaceTriangle.first));
		const float normalLength = Length(outwardNormal);

		if (normalLength <= 0.000001f) {
			return false;
		}

		outwardNormal = Multiply(1.0f / normalLength, outwardNormal);
		float totalArea = 0.0f;
		Vector3 areaWeightedCenter{};

		for (int32_t fanIndex = 1; fanIndex < clippedVertexCount - 1; fanIndex++) {
			const Vector3& firstVertex = clippedVertices[0u];
			const Vector3& secondVertex = clippedVertices[static_cast<size_t>(fanIndex)];
			const Vector3& thirdVertex = clippedVertices[static_cast<size_t>(fanIndex + 1)];
			const float triangleArea = 0.5f * Length(Cross(
				Subtract(secondVertex, firstVertex),
				Subtract(thirdVertex, firstVertex)));

			if (triangleArea <= 0.000001f) {
				continue;
			}

			const Vector3 triangleCenter = Multiply(
				1.0f / 3.0f,
				Add(Add(firstVertex, secondVertex), thirdVertex));
			areaWeightedCenter = Add(
				areaWeightedCenter,
				Multiply(triangleArea, triangleCenter));
			totalArea += triangleArea;
		}

		if (totalArea <= 0.000001f) {
			return false;
		}

		submergedPanel.center = Multiply(1.0f / totalArea, areaWeightedCenter);
		submergedPanel.outwardNormal = outwardNormal;
		submergedPanel.area = totalArea * (std::max)(surfaceTriangle.areaScale, 0.0f);
		return submergedPanel.area > 0.000001f;
	}

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
			buoyancySettings.normalInfluence = buoyancyComponent->buoyancyNormalInfluence;
			buoyancySettings.lateralDrag = buoyancyComponent->buoyancyLateralDrag;
			buoyancySettings.verticalDrag = buoyancyComponent->buoyancyVerticalDrag;
			buoyancySettings.slammingStrength = buoyancyComponent->buoyancySlammingStrength;
			buoyancySettings.automaticPhysicalProperties =
				buoyancyComponent->buoyancyAutomaticPhysicalProperties;
			buoyancySettings.waterDensity = buoyancyComponent->buoyancyWaterDensity;

			if (buoyancySettings.automaticPhysicalProperties) {
				const EditorComponent* colliderComponent = FindActiveWaterCollider(gameObject);

				if (colliderComponent != nullptr) {
					buoyancySettings.centerOffset = colliderComponent->colliderCenter;

					if (colliderComponent->type == EditorComponentType::SphereCollider) {
						const float diameter = (std::max)(
							std::fabs(colliderComponent->colliderRadius) * 2.0f,
							0.05f);
						buoyancySettings.hullSize = {diameter, diameter, diameter};
					}
					else if (colliderComponent->type == EditorComponentType::CapsuleCollider) {
						const float diameter = (std::max)(
							std::fabs(colliderComponent->colliderRadius) * 2.0f,
							0.05f);
						buoyancySettings.hullSize = {
							diameter,
							(std::max)(std::fabs(colliderComponent->colliderSize.y), diameter),
							diameter};
					}
					else {
						buoyancySettings.hullSize = colliderComponent->colliderSize;
						buoyancySettings.limitDraftHeight = true;
					}
				}
			}
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

			// Collider から自動で作る船体は「モデル全体の AABB」をそのまま使うため、
			// マストなどを含む水面より上の構造まで縦グリッドが広がり転覆しやすい。
			// 浮力計算側で縦範囲を平衡喫水に制限させるフラグを立てる。
			buoyancySettings.limitDraftHeight = true;
		}

		return true;
	}

	constexpr float kPhysicsPi = 3.14159265358979323846f;
	constexpr float kDegreesToRadians = kPhysicsPi / 180.0f;
	constexpr float kMinimumPhysicsVectorLength = 0.0001f;

	float WrapAngleRadians(float angle) {
		while (angle > kPhysicsPi) {
			angle -= 2.0f * kPhysicsPi;
		}

		while (angle < -kPhysicsPi) {
			angle += 2.0f * kPhysicsPi;
		}

		return angle;
	}

	Vector3 ClampVectorLength(const Vector3& value, float maximumLength) {
		const float valueLength = Length(value);

		if (maximumLength <= 0.0f || valueLength <= maximumLength ||
			valueLength <= kMinimumPhysicsVectorLength) {
			return value;
		}

		return Multiply(maximumLength / valueLength, value);
	}

	float CalculateSphericalFieldAttenuation(float distance, float radius) {
		if (radius <= 0.0f) {
			return 1.0f;
		}

		const float normalizedDistance = (std::clamp)(distance / radius, 0.0f, 1.0f);
		const float remainingInfluence = 1.0f - normalizedDistance;
		return remainingInfluence * remainingInfluence;
	}

	void ResolveWorldTransform(
		const EditorScene& editorScene,
		const EditorGameObject& gameObject,
		Vector3& worldScale,
		Vector3& worldRotation,
		Vector3& worldPosition) {
		worldScale = gameObject.scale;
		worldRotation = gameObject.rotate;
		worldPosition = gameObject.translate;
		editorScene.GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			worldPosition);
	}

	Vector3 ResolveWorldPosition(
		const EditorScene& editorScene,
		const EditorGameObject& gameObject) {
		const Matrix4x4 worldMatrix = editorScene.GetWorldMatrix(gameObject.id);
		return Vector3{
			worldMatrix.matrix[3][0],
			worldMatrix.matrix[3][1],
			worldMatrix.matrix[3][2]};
	}

	std::array<Vector3, 3u> BuildObjectAxes(
		const EditorScene& editorScene,
		const EditorGameObject& gameObject) {
		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		ResolveWorldTransform(
			editorScene,
			gameObject,
			worldScale,
			worldRotation,
			worldPosition);
		(void)worldScale;
		(void)worldPosition;
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			worldRotation,
			{0.0f, 0.0f, 0.0f});
		return {
			Normalize(Transform({1.0f, 0.0f, 0.0f}, rotationMatrix)),
			Normalize(Transform({0.0f, 1.0f, 0.0f}, rotationMatrix)),
			Normalize(Transform({0.0f, 0.0f, 1.0f}, rotationMatrix))};
	}

	float CalculateProjectedRadius(
		const std::array<Vector3, 3u>& bodyAxes,
		const Vector3& bodyHalfSize,
		const Vector3& projectionAxis) {
		return std::fabs(Dot(bodyAxes[0], projectionAxis)) * bodyHalfSize.x +
			std::fabs(Dot(bodyAxes[1], projectionAxis)) * bodyHalfSize.y +
			std::fabs(Dot(bodyAxes[2], projectionAxis)) * bodyHalfSize.z;
	}

	float CalculateIntervalOverlap(
		float bodyCenter,
		float bodyRadius,
		float volumeHalfSize,
		float& overlapCenter) {
		const float overlapStart = (std::max)(bodyCenter - bodyRadius, -volumeHalfSize);
		const float overlapEnd = (std::min)(bodyCenter + bodyRadius, volumeHalfSize);

		if (overlapEnd <= overlapStart) {
			overlapCenter = 0.0f;
			return 0.0f;
		}

		overlapCenter = (overlapStart + overlapEnd) * 0.5f;
		return overlapEnd - overlapStart;
	}

	Vector3 CalculatePointVelocity(
		const EditorScene& editorScene,
		const EditorGameObject& gameObject,
		const EditorComponent& rigidBodyComponent,
		const Vector3& worldPoint) {
		const Vector3 centerToPoint = Subtract(
			worldPoint,
			ResolveWorldPosition(editorScene, gameObject));
		return Add(
			rigidBodyComponent.velocity,
			Cross(rigidBodyComponent.angularVelocity, centerToPoint));
	}
}

void EditorPhysicsManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // RuntimeManager から渡された Play 対象 Scene
	consoleMessages_ = consoleMessages;  // Jolt の接触イベントを Console へ流す
	joltPhysicsManager_.Initialize(editorScene_, consoleMessages_);
	frameEvents_.clear();
	buoyancyRuntimeStates_.clear();
	physicsStepObjects_.clear();
	windZoneObjects_.clear();
	gravityFieldObjects_.clear();
	rotatingFrameObjects_.clear();
	fluidVolumeObjects_.clear();
	vortexFieldObjects_.clear();
	pressureFieldObjects_.clear();
	electromagneticFieldObjects_.clear();
	simulationElapsedTime_ = 0.0f;
}

void EditorPhysicsManager::BeginDebugFrame() {
	frameDebugCasts_.clear();
	frameWireEvents_.clear();
}

void EditorPhysicsManager::StartSimulation() {
	// Play 開始時の Scene 状態から Jolt Body を作る
	fixedTimeAccumulator_ = 0.0f;
	simulationElapsedTime_ = 0.0f;
	frameEvents_.clear();
	contactDebugEvents_.clear();
	frameDebugCasts_.clear();
	buoyancyRuntimeStates_.clear();
	physicsStepObjects_.clear();
	windZoneObjects_.clear();
	gravityFieldObjects_.clear();
	rotatingFrameObjects_.clear();
	fluidVolumeObjects_.clear();
	vortexFieldObjects_.clear();
	pressureFieldObjects_.clear();
	electromagneticFieldObjects_.clear();
	runtimeWires_.clear();
	frameWireEvents_.clear();
	nextWireHandle_ = 1ULL;

	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::RopeConstraint);

			if (ropeComponent != nullptr) {
				ropeComponent->ropeIsBroken = false;
				ropeComponent->ropeCurrentLength = 0.0f;
				ropeComponent->ropeCurrentTension = 0.0f;
			}

			EditorComponent* suspensionComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::Suspension);

			if (suspensionComponent != nullptr) {
				suspensionComponent->suspensionIsGrounded = false;
				suspensionComponent->suspensionCurrentLength =
					(std::max)(suspensionComponent->suspensionMaximumLength, 0.0f);
			}

			EditorComponent* pulleyComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::PulleyConstraint);

			if (pulleyComponent != nullptr) {
				pulleyComponent->pulleyIsBroken = false;
			}
		}
	}

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

	// Componentの構成は固定Stepの途中では変わらないため、1描画フレームに最大4回の全Scene走査を行わない。
	if (fixedTimeAccumulator_ >= fixedTimeStep_) {
		RebuildPhysicsStepCache();
	}

	while (fixedTimeAccumulator_ >= fixedTimeStep_ && fixedStepCount < maxFixedSubSteps_) {
		// 外部の物理制御も必ず Jolt と同じ固定時間で力を加える。
		if (preFixedStepCallback_) {
			preFixedStepCallback_(fixedTimeStep_);
		}

		// ConstantForce は描画フレームではなく物理の固定時間ごとに加える。
		ApplyConstantForces();
		// 点重力と空気力学も同じ固定時間で加え、描画 FPS による力積差をなくす。
		ApplyGravityFieldForces();
		ApplyRotatingFrameForces();
		ApplyElectromagneticForces();
		ApplySpringForces();
		ApplyRopeForces();
		ApplyRuntimeWireForces(fixedTimeStep_);
		ApplyTorsionSpringTorques();
		ApplyThrusterForces();
		ApplyPulleyForces();
		ApplyPhysicsServoForces();
		ApplyVortexFieldForces();
		ApplyPressureFieldForces();
		ApplySuspensionForces();
		ApplyUprightStabilizerTorques();
		ApplyAerodynamicForces();
		ApplyFluidVolumeForces();
		// 水面上の船へ位置付き浮力を先に加え、その力を含めて Jolt を進める。
		ApplyBuoyancyForces(fixedTimeStep_);
		// Jolt が重力、接触、押し戻し、速度更新を固定時間で担当する
		joltPhysicsManager_.Update(fixedTimeStep_);
		// この時点でJoltの積分・最終姿勢確定が完了している。Rail絶対角度制限のような
		// 「最終結果に対するHard Clamp」はここでのみ正しく機能する(積分前だとBuoyancy等の
		// Torqueがこの後さらに角度を動かしてしまう)。
		if (postFixedStepCallback_) {
			postFixedStepCallback_(fixedTimeStep_);
		}
		simulationElapsedTime_ += fixedTimeStep_;
		const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& stepEvents = joltPhysicsManager_.GetStepEvents();
		frameEvents_.insert(frameEvents_.end(), stepEvents.begin(), stepEvents.end());
		fixedTimeAccumulator_ -= fixedTimeStep_;
		fixedStepCount++;
	}

	if (fixedStepCount >= maxFixedSubSteps_) {
		// 長い停止後に未処理時間を抱え続けると以後のフレームも詰まるため、上限到達時は残りを捨てる
		fixedTimeAccumulator_ = 0.0f;
	}

	if (fixedStepCount > 0) {
		contactDebugEvents_ = frameEvents_;
	}

	return fixedStepCount;
}

void EditorPhysicsManager::Draw() {
	// Jolt DebugRenderer の線描画接続前でも、設定は Scene の PhysicsSettings に保存される
}

void EditorPhysicsManager::StopSimulation() {
	// Play 停止時に Jolt World を破棄する。Scene 自体は RuntimeManager がバックアップから復元する。
	fixedTimeAccumulator_ = 0.0f;
	simulationElapsedTime_ = 0.0f;
	frameEvents_.clear();
	contactDebugEvents_.clear();
	frameDebugCasts_.clear();
	buoyancyRuntimeStates_.clear();
	physicsStepObjects_.clear();
	windZoneObjects_.clear();
	gravityFieldObjects_.clear();
	rotatingFrameObjects_.clear();
	fluidVolumeObjects_.clear();
	vortexFieldObjects_.clear();
	pressureFieldObjects_.clear();
	electromagneticFieldObjects_.clear();
	ClearRuntimeWires();
	joltPhysicsManager_.Stop();
}

void EditorPhysicsManager::RegisterRuntimeHierarchy(int32_t rootGameObjectId) {
	if (editorScene_ == nullptr || !joltPhysicsManager_.IsActive()) {
		return;
	}

	std::vector<int32_t> pendingGameObjectIds{rootGameObjectId};

	while (!pendingGameObjectIds.empty()) {
		const int32_t gameObjectId = pendingGameObjectIds.back();
		pendingGameObjectIds.pop_back();
		EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

		if (gameObject == nullptr) {
			continue;
		}

		joltPhysicsManager_.RegisterRuntimeGameObject(gameObjectId);

		for (const int32_t childGameObjectId : gameObject->children) {
			pendingGameObjectIds.push_back(childGameObjectId);
		}
	}

	// DuplicateGameObjectによるScene配列の再確保後は、Componentへの一時Pointerを必ず取り直す。
	RebuildPhysicsStepCache();
}

bool EditorPhysicsManager::SetGameObjectSimulationActive(int32_t gameObjectId, bool isActive) {
	if (!isActive) {
		buoyancyRuntimeStates_.erase(gameObjectId);
	}

	return joltPhysicsManager_.SetGameObjectSimulationActive(gameObjectId, isActive);
}

bool EditorPhysicsManager::SetGameObjectTransform(
	int32_t gameObjectId,
	const Vector3& position,
	const Vector3& rotation) {
	const bool wasTransformSet = joltPhysicsManager_.SetGameObjectTransform(
		gameObjectId,
		position,
		rotation);

	if (wasTransformSet) {
		buoyancyRuntimeStates_.erase(gameObjectId);
	}

	return wasTransformSet;
}

bool EditorPhysicsManager::Raycast(
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.Raycast(origin, direction, distance, hit);
	RecordDebugCast(PhysicsDebugCastType::Ray, origin, direction, distance, 0.0f, 0.0f, hasHit, hit);
	return hasHit;
}

bool EditorPhysicsManager::RaycastIgnoringGameObject(
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	int32_t ignoredGameObjectId,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.RaycastIgnoringGameObject(
		origin,
		direction,
		distance,
		ignoredGameObjectId,
		hit);
	RecordDebugCast(PhysicsDebugCastType::Ray, origin, direction, distance, 0.0f, 0.0f, hasHit, hit);
	return hasHit;
}

bool EditorPhysicsManager::RaycastIgnoringGameObjects(
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	const std::vector<int32_t>& ignoredGameObjectIds,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.RaycastIgnoringGameObjects(
		origin,
		direction,
		distance,
		ignoredGameObjectIds,
		hit);
	RecordDebugCast(PhysicsDebugCastType::Ray, origin, direction, distance, 0.0f, 0.0f, hasHit, hit);
	return hasHit;
}

bool EditorPhysicsManager::SphereCast(
	const Vector3& origin,
	float radius,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.SphereCast(origin, radius, direction, distance, hit);
	RecordDebugCast(PhysicsDebugCastType::Sphere, origin, direction, distance, radius, 0.0f, hasHit, hit);
	return hasHit;
}

bool EditorPhysicsManager::SphereCastIgnoringGameObjects(
	const Vector3& origin,
	float radius,
	const Vector3& direction,
	float distance,
	const std::vector<int32_t>& ignoredGameObjectIds,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.SphereCastIgnoringGameObjects(
		origin,
		radius,
		direction,
		distance,
		ignoredGameObjectIds,
		hit);
	RecordDebugCast(PhysicsDebugCastType::Sphere, origin, direction, distance, radius, 0.0f, hasHit, hit);
	return hasHit;
}

bool EditorPhysicsManager::CapsuleCast(
	const Vector3& origin,
	float radius,
	float height,
	const Vector3& direction,
	float distance,
	EditorJoltPhysicsManager::PhysicsHit& hit) const {
	const bool hasHit = joltPhysicsManager_.CapsuleCast(origin, radius, height, direction, distance, hit);
	RecordDebugCast(PhysicsDebugCastType::Capsule, origin, direction, distance, radius, height, hasHit, hit);
	return hasHit;
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

bool EditorPhysicsManager::GetBodyDiagnostics(
	int32_t gameObjectId,
	Vector3& bodyPosition,
	bool& isAddedToWorld) const {
	return joltPhysicsManager_.GetBodyDiagnostics(gameObjectId, bodyPosition, isAddedToWorld);
}

bool EditorPhysicsManager::GetBodyMass(int32_t gameObjectId, float& bodyMass) const {
	return joltPhysicsManager_.GetBodyMass(gameObjectId, bodyMass);
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

int32_t EditorPhysicsManager::AddExplosionImpulse(
	const Vector3& center,
	float radius,
	float impulseStrength,
	float upwardModifier) {
	if (editorScene_ == nullptr || radius <= 0.0f || impulseStrength == 0.0f) {
		return 0;
	}

	std::vector<int32_t> hitGameObjectIds;
	if (!OverlapSphere(center, radius, hitGameObjectIds)) {
		return 0;
	}

	const Vector3 adjustedCenter{center.x, center.y - upwardModifier, center.z};
	int32_t affectedBodyCount = 0;

	for (int32_t gameObjectId : hitGameObjectIds) {
		const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
		if (gameObject == nullptr || !gameObject->isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
			*gameObject,
			EditorComponentType::RigidBody);
		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive || rigidBodyComponent->isKinematic) {
			continue;
		}

		const Vector3 bodyPosition = ResolveWorldPosition(*editorScene_, *gameObject);
		const Vector3 explosionDirection = Subtract(bodyPosition, adjustedCenter);
		const float explosionDistance = Length(explosionDirection);
		if (explosionDistance > radius) {
			continue;
		}

		const float distanceAttenuation = 1.0f - (std::clamp)(explosionDistance / radius, 0.0f, 1.0f);
		const Vector3 normalizedExplosionDirection = explosionDistance > kMinimumPhysicsVectorLength
			? Multiply(1.0f / explosionDistance, explosionDirection)
			: Vector3{0.0f, 1.0f, 0.0f};
		const Vector3 explosionImpulse = Multiply(
			impulseStrength * distanceAttenuation,
			normalizedExplosionDirection);

		if (AddImpulse(gameObjectId, explosionImpulse)) {
			affectedBodyCount++;
		}
	}

	return affectedBodyCount;
}

uint64_t EditorPhysicsManager::CreateSpringJoint(
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const Vector3& ownerAnchor,
	const Vector3& connectedAnchor,
	float minDistance,
	float maxDistance,
	float frequency,
	float damping) {
	return joltPhysicsManager_.CreateSpringJoint(
		ownerGameObjectId,
		connectedGameObjectId,
		ownerAnchor,
		connectedAnchor,
		minDistance,
		maxDistance,
		frequency,
		damping);
}

bool EditorPhysicsManager::DestroyJoint(uint64_t jointHandle) {
	return joltPhysicsManager_.DestroyJoint(jointHandle);
}

bool EditorPhysicsManager::SetSpringJointSettings(
	uint64_t jointHandle,
	const Vector3& ownerAnchor,
	const Vector3& connectedAnchor,
	float minDistance,
	float maxDistance,
	float frequency,
	float damping) {
	return joltPhysicsManager_.SetSpringJointSettings(
		jointHandle,
		ownerAnchor,
		connectedAnchor,
		minDistance,
		maxDistance,
		frequency,
		damping);
}

bool EditorPhysicsManager::IsJointValid(uint64_t jointHandle) const {
	return joltPhysicsManager_.IsJointValid(jointHandle);
}

uint64_t EditorPhysicsManager::CreateJoint(
	EditorJoltPhysicsManager::RuntimeJointType jointType,
	int32_t ownerGameObjectId,
	int32_t connectedGameObjectId,
	const EditorJoltPhysicsManager::RuntimeJointSettings& jointSettings) {
	return joltPhysicsManager_.CreateJoint(
		jointType,
		ownerGameObjectId,
		connectedGameObjectId,
		jointSettings);
}

bool EditorPhysicsManager::SetJointSettings(
	uint64_t jointHandle,
	const EditorJoltPhysicsManager::RuntimeJointSettings& jointSettings) {
	return joltPhysicsManager_.SetJointSettings(jointHandle, jointSettings);
}

bool EditorPhysicsManager::AttachRope(
	int32_t ownerGameObjectId,
	int32_t targetGameObjectId,
	const Vector3& ownerLocalAnchor,
	const Vector3& targetAnchor,
	float maximumLength) {
	if (editorScene_ == nullptr || maximumLength < 0.0f || ownerGameObjectId == targetGameObjectId) {
		return false;
	}

	EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	if (targetGameObjectId >= 0 && editorScene_->FindGameObject(targetGameObjectId) == nullptr) {
		return false;
	}

	EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::RopeConstraint);
	if (ropeComponent == nullptr) {
		return false;
	}

	ropeComponent->isActive = true;
	ropeComponent->ropeTargetGameObjectId = targetGameObjectId;
	ropeComponent->ropeLocalAnchor = ownerLocalAnchor;
	ropeComponent->ropeMaximumLength = maximumLength;
	ropeComponent->ropeIsBroken = false;
	ropeComponent->ropeCurrentLength = 0.0f;
	ropeComponent->ropeCurrentTension = 0.0f;

	if (targetGameObjectId >= 0) {
		ropeComponent->ropeTargetLocalAnchor = targetAnchor;
	}
	else {
		ropeComponent->ropeWorldAnchor = targetAnchor;
	}

	return true;
}

bool EditorPhysicsManager::DetachRope(int32_t ownerGameObjectId) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::RopeConstraint);
	if (ropeComponent == nullptr) {
		return false;
	}

	ropeComponent->isActive = false;
	ropeComponent->ropeCurrentTension = 0.0f;
	return true;
}

bool EditorPhysicsManager::SetRopeLength(int32_t ownerGameObjectId, float maximumLength) {
	if (editorScene_ == nullptr || maximumLength < 0.0f) {
		return false;
	}

	EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::RopeConstraint);
	if (ropeComponent == nullptr) {
		return false;
	}

	ropeComponent->ropeMaximumLength = maximumLength;
	return true;
}

bool EditorPhysicsManager::RepairRope(int32_t ownerGameObjectId) {
	if (editorScene_ == nullptr) {
		return false;
	}

	EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::RopeConstraint);
	if (ropeComponent == nullptr) {
		return false;
	}

	ropeComponent->isActive = true;
	ropeComponent->ropeIsBroken = false;
	ropeComponent->ropeCurrentTension = 0.0f;
	return true;
}

bool EditorPhysicsManager::GetRopeState(
	int32_t ownerGameObjectId,
	bool& isActive,
	bool& isBroken,
	int32_t& targetGameObjectId,
	float& maximumLength,
	float& currentLength,
	float& currentTension) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(ownerGameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	const EditorComponent* ropeComponent = EditorComponentUtility::FindComponent(
		*ownerGameObject,
		EditorComponentType::RopeConstraint);
	if (ropeComponent == nullptr) {
		return false;
	}

	isActive = ropeComponent->isActive;
	isBroken = ropeComponent->ropeIsBroken;
	targetGameObjectId = ropeComponent->ropeTargetGameObjectId;
	maximumLength = ropeComponent->ropeMaximumLength;
	currentLength = ropeComponent->ropeCurrentLength;
	currentTension = ropeComponent->ropeCurrentTension;
	return true;
}

EditorPhysicsManager::WireHandle EditorPhysicsManager::CreateWire(
	const RuntimeWireDesc& wireDesc) {
	if (editorScene_ == nullptr ||
		wireDesc.firstGameObjectId < 0 ||
		wireDesc.secondGameObjectId < 0 ||
		wireDesc.firstGameObjectId == wireDesc.secondGameObjectId ||
		wireDesc.maximumLength < 0.0f) {
		return kInvalidWireHandle;
	}

	const EditorGameObject* firstGameObject =
		editorScene_->FindGameObject(wireDesc.firstGameObjectId);
	const EditorGameObject* secondGameObject =
		editorScene_->FindGameObject(wireDesc.secondGameObjectId);

	if (firstGameObject == nullptr || secondGameObject == nullptr ||
		!firstGameObject->isActive || !secondGameObject->isActive) {
		return kInvalidWireHandle;
	}

	if (wireDesc.requireConnectable &&
		(!CanConnectWire(wireDesc.firstGameObjectId) ||
		 !CanConnectWire(wireDesc.secondGameObjectId))) {
		return kInvalidWireHandle;
	}

	RuntimeWireState wireState{};
	wireState.handle = nextWireHandle_++;
	wireState.desc = wireDesc;
	wireState.desc.minimumLength = (std::max)(wireDesc.minimumLength, 0.0f);
	wireState.desc.maximumLength = (std::max)(
		wireDesc.maximumLength,
		wireState.desc.minimumLength);
	wireState.desc.stiffness = (std::max)(wireDesc.stiffness, 0.0f);
	wireState.desc.damping = (std::max)(wireDesc.damping, 0.0f);
	wireState.desc.maximumTension = (std::max)(wireDesc.maximumTension, 0.0f);
	wireState.desc.breakingTension = (std::max)(wireDesc.breakingTension, 0.0f);
	wireState.desc.shrinkSpeed = (std::max)(wireDesc.shrinkSpeed, 0.0f);

	const EditorComponent* firstConnectable = EditorComponentUtility::FindComponent(
		*firstGameObject,
		EditorComponentType::WireConnectable);
	const EditorComponent* secondConnectable = EditorComponentUtility::FindComponent(
		*secondGameObject,
		EditorComponentType::WireConnectable);

	const auto applyConnectionStrength = [&wireState](const EditorComponent* connectable) {
		if (connectable == nullptr || connectable->wireConnectableStrength <= 0.0f) {
			return;
		}

		if (wireState.desc.breakingTension <= 0.0f) {
			wireState.desc.breakingTension = connectable->wireConnectableStrength;
		}
		else {
			wireState.desc.breakingTension = (std::min)(
				wireState.desc.breakingTension,
				connectable->wireConnectableStrength);
		}
	};

	applyConnectionStrength(firstConnectable);
	applyConnectionStrength(secondConnectable);

	wireState.firstWorldAnchor = Transform(
		wireState.desc.firstLocalAnchor,
		editorScene_->GetWorldMatrix(wireState.desc.firstGameObjectId));
	wireState.secondWorldAnchor = Transform(
		wireState.desc.secondLocalAnchor,
		editorScene_->GetWorldMatrix(wireState.desc.secondGameObjectId));
	wireState.currentLength = Length(Subtract(
		wireState.secondWorldAnchor,
		wireState.firstWorldAnchor));

	const WireHandle wireHandle = wireState.handle;
	runtimeWires_[wireHandle] = wireState;
	PushWireEvent(RuntimeWireEventType::Connected, runtimeWires_[wireHandle]);
	return wireHandle;
}

bool EditorPhysicsManager::DestroyWire(WireHandle wireHandle) {
	auto wireIterator = runtimeWires_.find(wireHandle);

	if (wireIterator == runtimeWires_.end()) {
		return false;
	}

	PushWireEvent(RuntimeWireEventType::Destroyed, wireIterator->second);
	runtimeWires_.erase(wireIterator);
	return true;
}

bool EditorPhysicsManager::SetWireLength(WireHandle wireHandle, float maximumLength) {
	auto wireIterator = runtimeWires_.find(wireHandle);

	if (wireIterator == runtimeWires_.end() || maximumLength < 0.0f) {
		return false;
	}

	wireIterator->second.desc.maximumLength = (std::max)(
		maximumLength,
		wireIterator->second.desc.minimumLength);
	return true;
}

bool EditorPhysicsManager::SetWireShrinkSpeed(WireHandle wireHandle, float shrinkSpeed) {
	auto wireIterator = runtimeWires_.find(wireHandle);

	if (wireIterator == runtimeWires_.end() || shrinkSpeed < 0.0f) {
		return false;
	}

	wireIterator->second.desc.shrinkSpeed = shrinkSpeed;
	return true;
}

bool EditorPhysicsManager::RepairWire(WireHandle wireHandle) {
	auto wireIterator = runtimeWires_.find(wireHandle);

	if (wireIterator == runtimeWires_.end()) {
		return false;
	}

	wireIterator->second.isActive = true;
	wireIterator->second.isBroken = false;
	wireIterator->second.currentTension = 0.0f;
	return true;
}

bool EditorPhysicsManager::GetWireState(
	WireHandle wireHandle,
	RuntimeWireState& wireState) const {
	const auto wireIterator = runtimeWires_.find(wireHandle);

	if (wireIterator == runtimeWires_.end()) {
		return false;
	}

	wireState = wireIterator->second;
	return true;
}

int32_t EditorPhysicsManager::GetWireCountForGameObject(int32_t gameObjectId) const {
	int32_t wireCount = 0;

	for (const auto& [wireHandle, wireState] : runtimeWires_) {
		(void)wireHandle;

		if (wireState.isActive && !wireState.isBroken &&
			(wireState.desc.firstGameObjectId == gameObjectId ||
			 wireState.desc.secondGameObjectId == gameObjectId)) {
			wireCount++;
		}
	}

	return wireCount;
}

bool EditorPhysicsManager::GetWireForGameObject(
	int32_t gameObjectId,
	int32_t wireIndex,
	RuntimeWireState& wireState) const {
	if (wireIndex < 0) {
		return false;
	}

	int32_t currentIndex = 0;

	for (const auto& [wireHandle, currentWireState] : runtimeWires_) {
		(void)wireHandle;

		if (!currentWireState.isActive || currentWireState.isBroken ||
			(currentWireState.desc.firstGameObjectId != gameObjectId &&
			 currentWireState.desc.secondGameObjectId != gameObjectId)) {
			continue;
		}

		if (currentIndex == wireIndex) {
			wireState = currentWireState;
			return true;
		}

		currentIndex++;
	}

	return false;
}

bool EditorPhysicsManager::CanConnectWire(int32_t gameObjectId) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr || !gameObject->isActive) {
		return false;
	}

	const EditorComponent* connectable = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::WireConnectable);

	if (connectable == nullptr || !connectable->isActive ||
		!connectable->wireConnectableAllowSelection) {
		return false;
	}

	return connectable->wireConnectableMaximumConnections <= 0 ||
		GetWireCountForGameObject(gameObjectId) < connectable->wireConnectableMaximumConnections;
}

const std::unordered_map<EditorPhysicsManager::WireHandle, EditorPhysicsManager::RuntimeWireState>&
EditorPhysicsManager::GetRuntimeWires() const {
	return runtimeWires_;
}

const std::vector<EditorPhysicsManager::RuntimeWireEvent>&
EditorPhysicsManager::GetFrameWireEvents() const {
	return frameWireEvents_;
}

void EditorPhysicsManager::ClearRuntimeWires() {
	runtimeWires_.clear();
	frameWireEvents_.clear();
	nextWireHandle_ = 1ULL;
}

void EditorPhysicsManager::PushWireEvent(
	RuntimeWireEventType eventType,
	const RuntimeWireState& wireState) {
	RuntimeWireEvent wireEvent{};
	wireEvent.type = eventType;
	wireEvent.handle = wireState.handle;
	wireEvent.firstGameObjectId = wireState.desc.firstGameObjectId;
	wireEvent.secondGameObjectId = wireState.desc.secondGameObjectId;
	wireEvent.ownerGameObjectId = wireState.desc.ownerGameObjectId;
	wireEvent.tension = wireState.currentTension;
	frameWireEvents_.push_back(wireEvent);
}

void EditorPhysicsManager::ApplyRuntimeWireForces(float fixedDeltaTime) {
	if (editorScene_ == nullptr) {
		return;
	}

	for (auto& [wireHandle, wireState] : runtimeWires_) {
		(void)wireHandle;

		if (!wireState.isActive || wireState.isBroken) {
			continue;
		}

		EditorGameObject* firstGameObject =
			editorScene_->FindGameObject(wireState.desc.firstGameObjectId);
		EditorGameObject* secondGameObject =
			editorScene_->FindGameObject(wireState.desc.secondGameObjectId);

		if (firstGameObject == nullptr || secondGameObject == nullptr ||
			!firstGameObject->isActive || !secondGameObject->isActive) {
			wireState.isActive = false;
			wireState.currentTension = 0.0f;
			PushWireEvent(RuntimeWireEventType::TargetLost, wireState);
			continue;
		}

		wireState.firstWorldAnchor = Transform(
			wireState.desc.firstLocalAnchor,
			editorScene_->GetWorldMatrix(firstGameObject->id));
		wireState.secondWorldAnchor = Transform(
			wireState.desc.secondLocalAnchor,
			editorScene_->GetWorldMatrix(secondGameObject->id));

		// Hookは選択判定と表示を担当し、物理Forceは指定された親Rigidbodyへ渡す。
		// 未指定時は従来どおりHook自身を物理Bodyとして扱う。
		auto resolvePhysicsBody = [this](EditorGameObject& hookGameObject) -> EditorGameObject* {
			const EditorComponent* hookComponent = EditorComponentUtility::FindComponent(
				hookGameObject,
				EditorComponentType::WireConnectable);
			const int32_t physicsBodyGameObjectId =
				hookComponent != nullptr && hookComponent->wireConnectablePhysicsBodyGameObjectId >= 0
				? hookComponent->wireConnectablePhysicsBodyGameObjectId
				: hookGameObject.id;
			return editorScene_->FindGameObject(physicsBodyGameObjectId);
		};

		EditorGameObject* firstPhysicsBody = resolvePhysicsBody(*firstGameObject);
		EditorGameObject* secondPhysicsBody = resolvePhysicsBody(*secondGameObject);

		if (wireState.desc.shrinkSpeed > 0.0f) {
			wireState.desc.maximumLength = (std::max)(
				wireState.desc.minimumLength,
				wireState.desc.maximumLength -
					wireState.desc.shrinkSpeed * (std::max)(fixedDeltaTime, 0.0f));
		}

		const Vector3 firstToSecond = Subtract(
			wireState.secondWorldAnchor,
			wireState.firstWorldAnchor);
		wireState.currentLength = Length(firstToSecond);
		const float previousTension = wireState.currentTension;
		wireState.currentTension = 0.0f;

		if (wireState.currentLength <= wireState.desc.maximumLength ||
			wireState.currentLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const Vector3 wireDirection = Multiply(
			1.0f / wireState.currentLength,
			firstToSecond);
		const EditorComponent* firstRigidBody = firstPhysicsBody != nullptr
			? EditorComponentUtility::FindComponent(*firstPhysicsBody, EditorComponentType::RigidBody)
			: nullptr;
		const EditorComponent* secondRigidBody = secondPhysicsBody != nullptr
			? EditorComponentUtility::FindComponent(*secondPhysicsBody, EditorComponentType::RigidBody)
			: nullptr;
		const bool canMoveFirst = firstRigidBody != nullptr && firstRigidBody->isActive &&
			!firstRigidBody->isKinematic;
		const bool canMoveSecond = secondRigidBody != nullptr && secondRigidBody->isActive &&
			!secondRigidBody->isKinematic;

		Vector3 firstPointVelocity{0.0f, 0.0f, 0.0f};
		Vector3 secondPointVelocity{0.0f, 0.0f, 0.0f};

		if (firstPhysicsBody != nullptr && firstRigidBody != nullptr && firstRigidBody->isActive) {
			firstPointVelocity = CalculatePointVelocity(
				*editorScene_,
				*firstPhysicsBody,
				*firstRigidBody,
				wireState.firstWorldAnchor);
		}

		if (secondPhysicsBody != nullptr && secondRigidBody != nullptr && secondRigidBody->isActive) {
			secondPointVelocity = CalculatePointVelocity(
				*editorScene_,
				*secondPhysicsBody,
				*secondRigidBody,
				wireState.secondWorldAnchor);
		}

		const float separationSpeed = Dot(
			Subtract(secondPointVelocity, firstPointVelocity),
			wireDirection);
		const float stretchLength =
			wireState.currentLength - wireState.desc.maximumLength;
		float tension = (std::max)(
			wireState.desc.stiffness * stretchLength +
				wireState.desc.damping * separationSpeed,
			0.0f);

		if (wireState.desc.breakingTension > 0.0f &&
			tension >= wireState.desc.breakingTension) {
			wireState.currentTension = tension;
			wireState.isBroken = true;
			PushWireEvent(RuntimeWireEventType::Broken, wireState);
			continue;
		}

		if (wireState.desc.maximumTension > 0.0f) {
			tension = (std::min)(tension, wireState.desc.maximumTension);
		}

		wireState.currentTension = tension;
		const Vector3 wireForce = Multiply(tension, wireDirection);

		if (canMoveFirst && firstPhysicsBody != nullptr) {
			AddForceAtPosition(
				firstPhysicsBody->id,
				wireForce,
				wireState.firstWorldAnchor);
		}

		if (wireState.desc.applyReaction && canMoveSecond && secondPhysicsBody != nullptr) {
			AddForceAtPosition(
				secondPhysicsBody->id,
				Multiply(-1.0f, wireForce),
				wireState.secondWorldAnchor);
		}

		const float tensionChange = std::fabs(tension - previousTension);
		const float notificationThreshold = (std::max)(1.0f, previousTension * 0.05f);

		if (tensionChange >= notificationThreshold) {
			PushWireEvent(RuntimeWireEventType::TensionChanged, wireState);
		}
	}
}

const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& EditorPhysicsManager::GetFrameEvents() const {
	return frameEvents_;
}

const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& EditorPhysicsManager::GetContactDebugEvents() const {
	return contactDebugEvents_;
}

const std::vector<EditorPhysicsManager::PhysicsDebugCast>& EditorPhysicsManager::GetFrameDebugCasts() const {
	return frameDebugCasts_;
}

void EditorPhysicsManager::RecordDebugCast(
	PhysicsDebugCastType type,
	const Vector3& origin,
	const Vector3& direction,
	float distance,
	float radius,
	float height,
	bool hasHit,
	const EditorJoltPhysicsManager::PhysicsHit& hit) const {
	if (editorScene_ == nullptr || !editorScene_->GetPhysicsSettings().drawCastDebug) {
		return;
	}

	constexpr size_t kMaximumDebugCastCount = 256;
	if (frameDebugCasts_.size() >= kMaximumDebugCastCount) {
		return;
	}

	PhysicsDebugCast debugCast{};
	debugCast.type = type;
	debugCast.origin = origin;
	debugCast.direction = direction;
	debugCast.distance = distance;
	debugCast.radius = radius;
	debugCast.height = height;
	debugCast.hasHit = hasHit;
	debugCast.hit = hit;
	frameDebugCasts_.push_back(debugCast);
}

float EditorPhysicsManager::GetFixedTimeStep() const {
	return fixedTimeStep_;
}

void EditorPhysicsManager::SetPreFixedStepCallback(std::function<void(float)> callback) {
	preFixedStepCallback_ = std::move(callback);
}

void EditorPhysicsManager::SetPostFixedStepCallback(std::function<void(float)> callback) {
	postFixedStepCallback_ = std::move(callback);
}

void EditorPhysicsManager::RebuildPhysicsStepCache() {
	physicsStepObjects_.clear();
	windZoneObjects_.clear();
	gravityFieldObjects_.clear();
	rotatingFrameObjects_.clear();
	fluidVolumeObjects_.clear();
	vortexFieldObjects_.clear();
	pressureFieldObjects_.clear();
	electromagneticFieldObjects_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	std::vector<EditorGameObject>& gameObjects = editorScene_->GetGameObjects();
	physicsStepObjects_.reserve(gameObjects.size());

	// 1 GameObjectにつきComponent配列を一度だけ走査する。
	// RopeやSuspensionは無効時にも実行値をResetするため、GameObjectのActive状態では除外しない。
	for (EditorGameObject& gameObject : gameObjects) {
		PhysicsStepObject physicsObject{};
		physicsObject.gameObject = &gameObject;
		bool hasPhysicsComponent = false;

		for (EditorComponent& component : gameObject.components) {
			switch (component.type) {
			case EditorComponentType::RigidBody:
				physicsObject.rigidBody = &component;
				break;
			case EditorComponentType::ConstantForce:
				physicsObject.constantForce = &component;
				break;
			case EditorComponentType::Aerodynamics:
				physicsObject.aerodynamics = &component;
				break;
			case EditorComponentType::WindZone:
				physicsObject.windZone = &component;
				break;
			case EditorComponentType::GravityField:
				physicsObject.gravityField = &component;
				break;
			case EditorComponentType::RotatingFrame:
				physicsObject.rotatingFrame = &component;
				break;
			case EditorComponentType::FluidVolume:
				physicsObject.fluidVolume = &component;
				break;
			case EditorComponentType::SpringForce:
				physicsObject.springForce = &component;
				break;
			case EditorComponentType::RopeConstraint:
				physicsObject.ropeConstraint = &component;
				break;
			case EditorComponentType::TorsionSpring:
				physicsObject.torsionSpring = &component;
				break;
			case EditorComponentType::Thruster:
				physicsObject.thruster = &component;
				break;
			case EditorComponentType::PulleyConstraint:
				physicsObject.pulleyConstraint = &component;
				break;
			case EditorComponentType::PhysicsServo:
				physicsObject.physicsServo = &component;
				break;
			case EditorComponentType::VortexField:
				physicsObject.vortexField = &component;
				break;
			case EditorComponentType::PressureField:
				physicsObject.pressureField = &component;
				break;
			case EditorComponentType::Suspension:
				physicsObject.suspension = &component;
				break;
			case EditorComponentType::UprightStabilizer:
				physicsObject.uprightStabilizer = &component;
				break;
			case EditorComponentType::ElectromagneticField:
				physicsObject.electromagneticField = &component;
				break;
			case EditorComponentType::ElectromagneticBody:
				physicsObject.electromagneticBody = &component;
				break;
			case EditorComponentType::Buoyancy:
				physicsObject.buoyancy = &component;
				break;
			default:
				continue;
			}

			hasPhysicsComponent = true;
		}

		if (hasPhysicsComponent) {
			physicsStepObjects_.push_back(physicsObject);
		}
	}

	// 配列構築完了後にPointer索引を作り、vector再配置による無効化を避ける。
	for (PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const bool isGameObjectActive = physicsObject.gameObject != nullptr &&
			physicsObject.gameObject->isActive;

		if (!isGameObjectActive) {
			continue;
		}

		if (physicsObject.windZone != nullptr && physicsObject.windZone->isActive) {
			windZoneObjects_.push_back(&physicsObject);
		}

		if (physicsObject.gravityField != nullptr && physicsObject.gravityField->isActive) {
			gravityFieldObjects_.push_back(&physicsObject);
		}

		if (physicsObject.rotatingFrame != nullptr && physicsObject.rotatingFrame->isActive) {
			rotatingFrameObjects_.push_back(&physicsObject);
		}

		if (physicsObject.fluidVolume != nullptr && physicsObject.fluidVolume->isActive) {
			fluidVolumeObjects_.push_back(&physicsObject);
		}

		if (physicsObject.vortexField != nullptr && physicsObject.vortexField->isActive) {
			vortexFieldObjects_.push_back(&physicsObject);
		}

		if (physicsObject.pressureField != nullptr && physicsObject.pressureField->isActive) {
			pressureFieldObjects_.push_back(&physicsObject);
		}

		if (physicsObject.electromagneticField != nullptr &&
			physicsObject.electromagneticField->isActive) {
			electromagneticFieldObjects_.push_back(&physicsObject);
		}
	}
}

void EditorPhysicsManager::ApplyConstantForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* constantForceComponent = physicsObject.constantForce;

		if (rigidBodyComponent == nullptr ||
			!rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic ||
			constantForceComponent == nullptr ||
			!constantForceComponent->isActive) {
			continue;
		}

		AddForce(gameObject.id, constantForceComponent->velocity);
	}
}

void EditorPhysicsManager::ApplyAerodynamicForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* aerodynamicsComponent = physicsObject.aerodynamics;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || aerodynamicsComponent == nullptr ||
			!aerodynamicsComponent->isActive) {
			continue;
		}

		Vector3 bodyWorldScale{};
		Vector3 bodyWorldRotation{};
		Vector3 bodyWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			bodyWorldScale,
			bodyWorldRotation,
			bodyWorldPosition);
		(void)bodyWorldScale;
		Vector3 windVelocity = aerodynamicsComponent->aerodynamicAmbientWindVelocity;

		for (const PhysicsStepObject* windZone : windZoneObjects_) {
			const Vector3 zoneToBody = Subtract(
				bodyWorldPosition,
				ResolveWorldPosition(*editorScene_, *windZone->gameObject));
			const float zoneDistance = Length(zoneToBody);
			const float zoneRadius = (std::max)(windZone->windZone->windZoneRadius, 0.0f);

			if (zoneRadius > 0.0f && zoneDistance >= zoneRadius) {
				continue;
			}

			Vector3 windDirection = Normalize(windZone->windZone->windZoneDirection);

			if (windZone->windZone->windZoneMode == 1) {
				if (zoneDistance <= kMinimumPhysicsVectorLength) {
					continue;
				}

				windDirection = Multiply(1.0f / zoneDistance, zoneToBody);
			}

			if (Length(windDirection) <= kMinimumPhysicsVectorLength) {
				continue;
			}

			const float zoneAttenuation = CalculateSphericalFieldAttenuation(zoneDistance, zoneRadius);
			const Vector3 steadyWind = Multiply(
				windZone->windZone->windZoneSpeed * zoneAttenuation,
				windDirection);
			windVelocity = Add(windVelocity, steadyWind);

			const float turbulenceStrength =
				(std::max)(windZone->windZone->windZoneTurbulenceStrength, 0.0f) * zoneAttenuation;

			if (turbulenceStrength <= 0.0f) {
				continue;
			}

			// 位相の異なる低周波成分を重ね、フレーム乱数による速度の跳びを避ける。
			const float turbulenceFrequency =
				(std::max)(windZone->windZone->windZoneTurbulenceFrequency, 0.0f);
			const float spatialPhase =
				bodyWorldPosition.x * 0.173f +
				bodyWorldPosition.y * 0.117f +
				bodyWorldPosition.z * 0.197f;
			const float turbulencePhase =
				simulationElapsedTime_ * turbulenceFrequency + spatialPhase;
			const Vector3 turbulenceVelocity{
				std::sin(turbulencePhase),
				std::sin(turbulencePhase * 1.371f + 2.094f),
				std::sin(turbulencePhase * 0.917f + 4.188f)};
			const Vector3 normalizedTurbulenceVelocity = Normalize(turbulenceVelocity);
			windVelocity = Add(
				windVelocity,
				Multiply(turbulenceStrength, normalizedTurbulenceVelocity));
		}

		const float airDensity = (std::max)(aerodynamicsComponent->aerodynamicAirDensity, 0.0f);
		const float referenceArea = (std::max)(aerodynamicsComponent->aerodynamicReferenceArea, 0.0001f);
		const Vector3 relativeAirVelocity = Subtract(rigidBodyComponent->velocity, windVelocity);
		const float relativeAirSpeed = Length(relativeAirVelocity);
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			bodyWorldRotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 bodyRight = Normalize(Transform({1.0f, 0.0f, 0.0f}, rotationMatrix));
		const Vector3 bodyUp = Normalize(Transform({0.0f, 1.0f, 0.0f}, rotationMatrix));
		const Vector3 bodyForward = Normalize(Transform({0.0f, 0.0f, 1.0f}, rotationMatrix));
		Vector3 aerodynamicForce{0.0f, 0.0f, 0.0f};

		if (relativeAirSpeed > kMinimumPhysicsVectorLength && airDensity > 0.0f) {
			const Vector3 relativeAirDirection = Multiply(1.0f / relativeAirSpeed, relativeAirVelocity);
			const float dynamicPressure = 0.5f * airDensity * relativeAirSpeed * relativeAirSpeed;
			const float dragMagnitude = dynamicPressure *
				(std::max)(aerodynamicsComponent->aerodynamicDragCoefficient, 0.0f) * referenceArea;
			const Vector3 dragForce = Multiply(-dragMagnitude, relativeAirDirection);
			aerodynamicForce = Add(aerodynamicForce, dragForce);

			// 迎角は機体 +Z と相対風速の上下差から求め、設定した失速角を越えると揚力を落とす。
			const float forwardSpeed = Dot(relativeAirVelocity, bodyForward);
			const float verticalSpeed = Dot(relativeAirVelocity, bodyUp);
			const float angleOfAttack = std::atan2(
				-verticalSpeed,
				(std::max)(std::fabs(forwardSpeed), kMinimumPhysicsVectorLength));
			const float zeroLiftAngle =
				aerodynamicsComponent->aerodynamicZeroLiftAngleDegrees * kDegreesToRadians;
			const float stallAngle = (std::clamp)(
				std::fabs(aerodynamicsComponent->aerodynamicStallAngleDegrees) * kDegreesToRadians,
				1.0f * kDegreesToRadians,
				89.0f * kDegreesToRadians);
			float liftCoefficient = aerodynamicsComponent->aerodynamicBaseLiftCoefficient +
				aerodynamicsComponent->aerodynamicLiftSlope * (angleOfAttack - zeroLiftAngle);
			const float absoluteAngleOfAttack = std::fabs(angleOfAttack - zeroLiftAngle);

			if (absoluteAngleOfAttack > stallAngle) {
				const float stallRange = (std::max)(kPhysicsPi * 0.5f - stallAngle, 0.001f);
				const float stallProgress = (std::clamp)(
					(absoluteAngleOfAttack - stallAngle) / stallRange,
					0.0f,
					1.0f);
				liftCoefficient *= 1.0f - stallProgress;
			}

			const Vector3 unnormalizedLiftDirection = Subtract(
				bodyUp,
				Multiply(Dot(bodyUp, relativeAirDirection), relativeAirDirection));
			const float liftDirectionLength = Length(unnormalizedLiftDirection);

			if (liftDirectionLength > kMinimumPhysicsVectorLength) {
				const Vector3 liftDirection = Multiply(
					1.0f / liftDirectionLength,
					unnormalizedLiftDirection);
				const float liftMagnitude = dynamicPressure * liftCoefficient *
					(std::max)(aerodynamicsComponent->aerodynamicLiftArea, 0.0f);
				aerodynamicForce = Add(
					aerodynamicForce,
					Multiply(liftMagnitude, liftDirection));
			}

			const float sideSlip = Dot(relativeAirDirection, bodyRight);
			const float sideForceMagnitude = dynamicPressure *
				(std::max)(aerodynamicsComponent->aerodynamicSideForceCoefficient, 0.0f) *
				(std::max)(aerodynamicsComponent->aerodynamicSideArea, 0.0f) * sideSlip;
			aerodynamicForce = Add(
				aerodynamicForce,
				Multiply(-sideForceMagnitude, bodyRight));

			// 回転軸と並進速度の外積から、回転する球や弾へ働く Magnus 力を近似する。
			const float characteristicLength = std::sqrt(referenceArea);
			const Vector3 magnusForce = Multiply(
				aerodynamicsComponent->aerodynamicMagnusCoefficient * airDensity *
					referenceArea * characteristicLength,
				Cross(rigidBodyComponent->angularVelocity, relativeAirVelocity));
			aerodynamicForce = Add(aerodynamicForce, magnusForce);
		}

		aerodynamicForce = ClampVectorLength(
			aerodynamicForce,
			aerodynamicsComponent->aerodynamicMaximumForce);
		const Matrix4x4 worldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Vector3 worldCenterOfPressure = Transform(
			aerodynamicsComponent->aerodynamicCenterOfPressure,
			worldMatrix);
		AddForceAtPosition(gameObject.id, aerodynamicForce, worldCenterOfPressure);

		// M = 1/2*rho*Cw*A*L^3*|omega|*omega とし、角速度が大きいほど強く減衰させる。
		const float angularSpeed = Length(rigidBodyComponent->angularVelocity);

		if (angularSpeed > kMinimumPhysicsVectorLength && airDensity > 0.0f) {
			const float characteristicLength = std::sqrt(referenceArea);
			const float angularTorqueScale = 0.5f * airDensity *
				(std::max)(aerodynamicsComponent->aerodynamicAngularDragCoefficient, 0.0f) *
				referenceArea * characteristicLength * characteristicLength * characteristicLength;
			const Vector3 angularDragTorque = Multiply(
				-angularTorqueScale * angularSpeed,
				rigidBodyComponent->angularVelocity);
			AddTorque(gameObject.id, angularDragTorque);
		}
	}
}

void EditorPhysicsManager::ApplyGravityFieldForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (gravityFieldObjects_.empty()) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		Vector3 totalGravityForce{0.0f, 0.0f, 0.0f};
		const Vector3 bodyWorldPosition = ResolveWorldPosition(*editorScene_, gameObject);

		for (const PhysicsStepObject* gravityField : gravityFieldObjects_) {
			if (gravityField->gameObject->id == gameObject.id) {
				continue;
			}

			const Vector3 bodyToSource = Subtract(
				ResolveWorldPosition(*editorScene_, *gravityField->gameObject),
				bodyWorldPosition);
			const float sourceDistance = Length(bodyToSource);

			if (sourceDistance <= kMinimumPhysicsVectorLength) {
				continue;
			}

			const float influenceRadius =
				(std::max)(gravityField->gravityField->gravityFieldInfluenceRadius, 0.0f);

			if (influenceRadius > 0.0f && sourceDistance > influenceRadius) {
				continue;
			}

			const float calculationDistance = (std::max)(
				sourceDistance,
				(std::max)(gravityField->gravityField->gravityFieldMinimumDistance, 0.001f));
			float gravityAcceleration = gravityField->gravityField->gravityFieldAcceleration;

			if (gravityField->gravityField->gravityFieldMode == 0) {
				gravityAcceleration =
					gravityField->gravityField->gravityFieldGravitationalConstant *
					gravityField->gravityField->gravityFieldSourceMass /
					(calculationDistance * calculationDistance);
			}

			const float maximumAcceleration =
				(std::max)(gravityField->gravityField->gravityFieldMaximumAcceleration, 0.0f);

			if (maximumAcceleration > 0.0f) {
				gravityAcceleration = (std::clamp)(
					gravityAcceleration,
					-maximumAcceleration,
					maximumAcceleration);
			}

			const Vector3 gravityDirection = Multiply(1.0f / sourceDistance, bodyToSource);
			const Vector3 gravityForce = Multiply(
				(std::max)(rigidBodyComponent->mass, 0.01f) * gravityAcceleration,
				gravityDirection);
			totalGravityForce = Add(totalGravityForce, gravityForce);
		}

		AddForce(gameObject.id, totalGravityForce);
	}
}

void EditorPhysicsManager::ApplyRotatingFrameForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (rotatingFrameObjects_.empty()) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		Vector3 totalFrameAcceleration{0.0f, 0.0f, 0.0f};
		const Vector3 bodyWorldPosition = ResolveWorldPosition(*editorScene_, gameObject);

		for (const PhysicsStepObject* rotatingFrame : rotatingFrameObjects_) {
			if (rotatingFrame->gameObject->id == gameObject.id) {
				continue;
			}

			const Vector3 relativePosition = Subtract(
				bodyWorldPosition,
				ResolveWorldPosition(*editorScene_, *rotatingFrame->gameObject));
			const float frameDistance = Length(relativePosition);
			const float frameRadius =
				(std::max)(rotatingFrame->rotatingFrame->rotatingFrameRadius, 0.0f);

			if (frameRadius > 0.0f && frameDistance > frameRadius) {
				continue;
			}

			const Vector3 angularVelocity = rotatingFrame->rotatingFrame->rotatingFrameAngularVelocity;
			const Vector3 angularAcceleration = rotatingFrame->rotatingFrame->rotatingFrameAngularAcceleration;
			const Vector3 framePointVelocity = Add(
				rotatingFrame->rotatingFrame->rotatingFrameLinearVelocity,
				Cross(angularVelocity, relativePosition));
			const Vector3 velocityInRotatingFrame = Subtract(
				rigidBodyComponent->velocity,
				framePointVelocity);

			// aCoriolis=-2*omega*v、aCentrifugal=-omega*(omega*r)、aEuler=-alpha*r。
			const Vector3 coriolisAcceleration = Multiply(
				-2.0f,
				Cross(angularVelocity, velocityInRotatingFrame));
			const Vector3 centrifugalAcceleration = Multiply(
				-1.0f,
				Cross(angularVelocity, Cross(angularVelocity, relativePosition)));
			const Vector3 eulerAcceleration = Multiply(
				-1.0f,
				Cross(angularAcceleration, relativePosition));
			Vector3 frameAcceleration = Add(
				Add(coriolisAcceleration, centrifugalAcceleration),
				eulerAcceleration);
			frameAcceleration = ClampVectorLength(
				frameAcceleration,
				rotatingFrame->rotatingFrame->rotatingFrameMaximumAcceleration);
			totalFrameAcceleration = Add(totalFrameAcceleration, frameAcceleration);
		}

		const Vector3 rotatingFrameForce = Multiply(
			(std::max)(rigidBodyComponent->mass, 0.01f),
			totalFrameAcceleration);
		AddForce(gameObject.id, rotatingFrameForce);
	}
}

void EditorPhysicsManager::ApplyFluidVolumeForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (fluidVolumeObjects_.empty()) {
		return;
	}

	const Vector3 gravity = editorScene_->GetPhysicsSettings().gravity;
	const float gravityMagnitude = Length(gravity);
	const Vector3 buoyancyDirection = gravityMagnitude > kMinimumPhysicsVectorLength ?
		Normalize(Multiply(-1.0f, gravity)) : Vector3{0.0f, 1.0f, 0.0f};

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		RuntimeBuoyancySettings bodyShape{};

		if (!BuildRuntimeBuoyancySettings(gameObject, nullptr, bodyShape)) {
			continue;
		}

		Vector3 bodyWorldScale{};
		Vector3 bodyWorldRotation{};
		Vector3 bodyWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			bodyWorldScale,
			bodyWorldRotation,
			bodyWorldPosition);
		(void)bodyWorldRotation;
		(void)bodyWorldPosition;
		const Matrix4x4 bodyWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Vector3 bodyCenter = Transform(bodyShape.centerOffset, bodyWorldMatrix);
		const std::array<Vector3, 3u> bodyAxes = BuildObjectAxes(*editorScene_, gameObject);
		const Vector3 bodyHalfSize{
			(std::max)(std::fabs(bodyShape.hullSize.x * bodyWorldScale.x) * 0.5f, 0.005f),
			(std::max)(std::fabs(bodyShape.hullSize.y * bodyWorldScale.y) * 0.5f, 0.005f),
			(std::max)(std::fabs(bodyShape.hullSize.z * bodyWorldScale.z) * 0.5f, 0.005f)};
		const float bodyVolume = (std::max)(
			bodyHalfSize.x * bodyHalfSize.y * bodyHalfSize.z * 8.0f,
			0.000001f);

		for (const PhysicsStepObject* fluidVolume : fluidVolumeObjects_) {
			if (fluidVolume->gameObject->id == gameObject.id) {
				continue;
			}

			Vector3 fluidWorldScale{};
			Vector3 fluidWorldRotation{};
			Vector3 fluidWorldPosition{};
			ResolveWorldTransform(
				*editorScene_,
				*fluidVolume->gameObject,
				fluidWorldScale,
				fluidWorldRotation,
				fluidWorldPosition);
			(void)fluidWorldRotation;
			const std::array<Vector3, 3u> fluidAxes = BuildObjectAxes(
				*editorScene_,
				*fluidVolume->gameObject);
			const Vector3 fluidHalfSize{
				(std::max)(std::fabs(
					fluidVolume->fluidVolume->fluidVolumeSize.x * fluidWorldScale.x) * 0.5f, 0.005f),
				(std::max)(std::fabs(
					fluidVolume->fluidVolume->fluidVolumeSize.y * fluidWorldScale.y) * 0.5f, 0.005f),
				(std::max)(std::fabs(
					fluidVolume->fluidVolume->fluidVolumeSize.z * fluidWorldScale.z) * 0.5f, 0.005f)};
			const Vector3 volumeToBody = Subtract(bodyCenter, fluidWorldPosition);
			const Vector3 bodyCenterInFluid{
				Dot(volumeToBody, fluidAxes[0]),
				Dot(volumeToBody, fluidAxes[1]),
				Dot(volumeToBody, fluidAxes[2])};
			const Vector3 bodyRadiusInFluid{
				CalculateProjectedRadius(bodyAxes, bodyHalfSize, fluidAxes[0]),
				CalculateProjectedRadius(bodyAxes, bodyHalfSize, fluidAxes[1]),
				CalculateProjectedRadius(bodyAxes, bodyHalfSize, fluidAxes[2])};
			Vector3 overlapCenterInFluid{0.0f, 0.0f, 0.0f};
			const float overlapX = CalculateIntervalOverlap(
				bodyCenterInFluid.x,
				bodyRadiusInFluid.x,
				fluidHalfSize.x,
				overlapCenterInFluid.x);
			const float overlapY = CalculateIntervalOverlap(
				bodyCenterInFluid.y,
				bodyRadiusInFluid.y,
				fluidHalfSize.y,
				overlapCenterInFluid.y);
			const float overlapZ = CalculateIntervalOverlap(
				bodyCenterInFluid.z,
				bodyRadiusInFluid.z,
				fluidHalfSize.z,
				overlapCenterInFluid.z);

			if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f) {
				continue;
			}

			// OBBの各軸投影重なりから浸水体積を近似し、本体体積を越えないよう制限する。
			const float displacedVolume = (std::min)(
				overlapX * overlapY * overlapZ,
				bodyVolume);
			const float submergedFraction = (std::clamp)(
				displacedVolume / bodyVolume,
				0.0f,
				1.0f);
			Vector3 submergedCenter = fluidWorldPosition;
			submergedCenter = Add(
				submergedCenter,
				Multiply(overlapCenterInFluid.x, fluidAxes[0]));
			submergedCenter = Add(
				submergedCenter,
				Multiply(overlapCenterInFluid.y, fluidAxes[1]));
			submergedCenter = Add(
				submergedCenter,
				Multiply(overlapCenterInFluid.z, fluidAxes[2]));

			const float fluidDensity = (std::max)(fluidVolume->fluidVolume->fluidDensity, 0.0f);
			const Vector3 buoyancyForce = Multiply(
				fluidDensity * displacedVolume * gravityMagnitude,
				buoyancyDirection);
			const Vector3 relativeFluidVelocity = Subtract(
				rigidBodyComponent->velocity,
				fluidVolume->fluidVolume->fluidFlowVelocity);
			const float relativeFluidSpeed = Length(relativeFluidVelocity);
			const float equivalentRadius = std::cbrt(
				3.0f * displacedVolume / (4.0f * kPhysicsPi));
			const float dynamicViscosity =
				(std::max)(fluidVolume->fluidVolume->fluidDynamicViscosity, 0.0f);
			const Vector3 stokesDragForce = Multiply(
				-6.0f * kPhysicsPi * dynamicViscosity * equivalentRadius,
				relativeFluidVelocity);
			Vector3 quadraticDragForce{0.0f, 0.0f, 0.0f};

			if (relativeFluidSpeed > kMinimumPhysicsVectorLength && fluidDensity > 0.0f) {
				const float projectedArea = std::pow(displacedVolume, 2.0f / 3.0f);
				const float dragMagnitude = 0.5f * fluidDensity *
					(std::max)(fluidVolume->fluidVolume->fluidDragCoefficient, 0.0f) *
					projectedArea * relativeFluidSpeed * relativeFluidSpeed;
				quadraticDragForce = Multiply(
					-dragMagnitude / relativeFluidSpeed,
					relativeFluidVelocity);
			}

			Vector3 totalFluidForce = Add(
				Add(buoyancyForce, stokesDragForce),
				quadraticDragForce);
			totalFluidForce = ClampVectorLength(
				totalFluidForce,
				fluidVolume->fluidVolume->fluidMaximumForce);
			AddForceAtPosition(gameObject.id, totalFluidForce, submergedCenter);

			// 回転球のStokes抵抗 T=-8*pi*mu*r^3*omega を浸水率と設定係数で調整する。
			const float angularTorqueScale = 8.0f * kPhysicsPi * dynamicViscosity *
				equivalentRadius * equivalentRadius * equivalentRadius * submergedFraction *
				(std::max)(fluidVolume->fluidVolume->fluidAngularViscosity, 0.0f);
			const Vector3 angularViscosityTorque = Multiply(
				-angularTorqueScale,
				rigidBodyComponent->angularVelocity);
			AddTorque(gameObject.id, angularViscosityTorque);
		}
	}
}

void EditorPhysicsManager::ApplySpringForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* springComponent = physicsObject.springForce;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || springComponent == nullptr ||
			!springComponent->isActive) {
			continue;
		}

		const Matrix4x4 ownerWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Vector3 ownerAnchor = Transform(
			springComponent->springForceLocalAnchor,
			ownerWorldMatrix);
		const EditorGameObject* targetGameObject = nullptr;
		const EditorComponent* targetRigidBodyComponent = nullptr;
		Vector3 targetAnchor = springComponent->springForceWorldAnchor;
		Vector3 targetPointVelocity{0.0f, 0.0f, 0.0f};

		if (springComponent->springForceTargetGameObjectId >= 0) {
			targetGameObject = editorScene_->FindGameObject(
				springComponent->springForceTargetGameObjectId);

			if (targetGameObject == nullptr || !targetGameObject->isActive ||
				targetGameObject->id == gameObject.id) {
				continue;
			}

			const Matrix4x4 targetWorldMatrix =
				editorScene_->GetWorldMatrix(targetGameObject->id);
			targetAnchor = Transform(
				springComponent->springForceTargetLocalAnchor,
				targetWorldMatrix);
			targetRigidBodyComponent = EditorComponentUtility::FindComponent(
				*targetGameObject,
				EditorComponentType::RigidBody);

			if (targetRigidBodyComponent != nullptr && targetRigidBodyComponent->isActive) {
				targetPointVelocity = CalculatePointVelocity(
					*editorScene_,
					*targetGameObject,
					*targetRigidBodyComponent,
					targetAnchor);
			}
		}

		const Vector3 ownerToTarget = Subtract(targetAnchor, ownerAnchor);
		const float springLength = Length(ownerToTarget);

		if (springLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const Vector3 springDirection = Multiply(1.0f / springLength, ownerToTarget);
		const Vector3 ownerPointVelocity = CalculatePointVelocity(
			*editorScene_,
			gameObject,
			*rigidBodyComponent,
			ownerAnchor);
		const float relativeSpeed = Dot(
			Subtract(ownerPointVelocity, targetPointVelocity),
			springDirection);
		float springForceMagnitude =
			(std::max)(springComponent->springForceStiffness, 0.0f) *
				(springLength - (std::max)(springComponent->springForceRestLength, 0.0f)) -
			(std::max)(springComponent->springForceDamping, 0.0f) * relativeSpeed;
		const float maximumForce = (std::max)(springComponent->springForceMaximumForce, 0.0f);

		if (maximumForce > 0.0f) {
			springForceMagnitude = (std::clamp)(
				springForceMagnitude,
				-maximumForce,
				maximumForce);
		}

		const Vector3 springForce = Multiply(springForceMagnitude, springDirection);
		AddForceAtPosition(gameObject.id, springForce, ownerAnchor);

		const bool canApplyReaction = springComponent->springForceApplyReaction &&
			targetGameObject != nullptr && targetRigidBodyComponent != nullptr &&
			targetRigidBodyComponent->isActive && !targetRigidBodyComponent->isKinematic;

		if (canApplyReaction) {
			AddForceAtPosition(
				targetGameObject->id,
				Multiply(-1.0f, springForce),
				targetAnchor);
		}
	}
}

void EditorPhysicsManager::ApplyRopeForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (PhysicsStepObject& physicsObject : physicsStepObjects_) {
		EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		EditorComponent* ropeComponent = physicsObject.ropeConstraint;

		if (ropeComponent != nullptr) {
			ropeComponent->ropeCurrentTension = 0.0f;
		}

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || ropeComponent == nullptr ||
			!ropeComponent->isActive || ropeComponent->ropeIsBroken) {
			continue;
		}

		const Matrix4x4 ownerWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Vector3 ownerAnchor = Transform(ropeComponent->ropeLocalAnchor, ownerWorldMatrix);
		EditorGameObject* targetGameObject = nullptr;
		const EditorComponent* targetRigidBodyComponent = nullptr;
		Vector3 targetAnchor = ropeComponent->ropeWorldAnchor;
		Vector3 targetPointVelocity{0.0f, 0.0f, 0.0f};

		if (ropeComponent->ropeTargetGameObjectId >= 0) {
			targetGameObject = editorScene_->FindGameObject(ropeComponent->ropeTargetGameObjectId);

			if (targetGameObject == nullptr || !targetGameObject->isActive ||
				targetGameObject->id == gameObject.id) {
				continue;
			}

			const Matrix4x4 targetWorldMatrix = editorScene_->GetWorldMatrix(targetGameObject->id);
			targetAnchor = Transform(ropeComponent->ropeTargetLocalAnchor, targetWorldMatrix);
			targetRigidBodyComponent = EditorComponentUtility::FindComponent(
				*targetGameObject,
				EditorComponentType::RigidBody);

			if (targetRigidBodyComponent != nullptr && targetRigidBodyComponent->isActive) {
				targetPointVelocity = CalculatePointVelocity(
					*editorScene_,
					*targetGameObject,
					*targetRigidBodyComponent,
					targetAnchor);
			}
		}

		const Vector3 ownerToTarget = Subtract(targetAnchor, ownerAnchor);
		const float ropeLength = Length(ownerToTarget);
		const float maximumLength = (std::max)(ropeComponent->ropeMaximumLength, 0.0f);
		ropeComponent->ropeCurrentLength = ropeLength;

		// ロープは圧縮力を伝えない。たるんでいる間は完全に無力とする。
		if (ropeLength <= maximumLength || ropeLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const Vector3 ropeDirection = Multiply(1.0f / ropeLength, ownerToTarget);
		const Vector3 ownerPointVelocity = CalculatePointVelocity(
			*editorScene_,
			gameObject,
			*rigidBodyComponent,
			ownerAnchor);
		const float separationSpeed = Dot(
			Subtract(targetPointVelocity, ownerPointVelocity),
			ropeDirection);
		const float stretchLength = ropeLength - maximumLength;
		const float rawTension =
			(std::max)(ropeComponent->ropeStiffness, 0.0f) * stretchLength +
			(std::max)(ropeComponent->ropeDamping, 0.0f) * separationSpeed;
		float tension = (std::max)(rawTension, 0.0f);
		const float breakingTension = (std::max)(ropeComponent->ropeBreakingTension, 0.0f);

		if (breakingTension > 0.0f && tension >= breakingTension) {
			ropeComponent->ropeCurrentTension = tension;
			ropeComponent->ropeIsBroken = true;

			if (consoleMessages_ != nullptr) {
				consoleMessages_->push_back("ロープ拘束が破断しました: " + gameObject.name);
			}

			continue;
		}

		const float maximumTension = (std::max)(ropeComponent->ropeMaximumTension, 0.0f);

		if (maximumTension > 0.0f) {
			tension = (std::min)(tension, maximumTension);
		}

		ropeComponent->ropeCurrentTension = tension;

		const Vector3 ropeForce = Multiply(tension, ropeDirection);
		AddForceAtPosition(gameObject.id, ropeForce, ownerAnchor);

		const bool canApplyReaction = ropeComponent->ropeApplyReaction &&
			targetGameObject != nullptr && targetRigidBodyComponent != nullptr &&
			targetRigidBodyComponent->isActive && !targetRigidBodyComponent->isKinematic;

		if (canApplyReaction) {
			AddForceAtPosition(
				targetGameObject->id,
				Multiply(-1.0f, ropeForce),
				targetAnchor);
		}
	}
}

void EditorPhysicsManager::ApplyTorsionSpringTorques() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* torsionComponent = physicsObject.torsionSpring;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || torsionComponent == nullptr ||
			!torsionComponent->isActive) {
			continue;
		}

		Vector3 ownerWorldScale{};
		Vector3 ownerWorldRotation{};
		Vector3 ownerWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			ownerWorldScale,
			ownerWorldRotation,
			ownerWorldPosition);
		(void)ownerWorldScale;
		(void)ownerWorldPosition;
		Vector3 desiredWorldRotation = torsionComponent->torsionRestRotation;
		Vector3 targetAngularVelocity{0.0f, 0.0f, 0.0f};
		const EditorGameObject* targetGameObject = nullptr;
		const EditorComponent* targetRigidBodyComponent = nullptr;

		if (torsionComponent->torsionTargetGameObjectId >= 0) {
			targetGameObject = editorScene_->FindGameObject(torsionComponent->torsionTargetGameObjectId);

			if (targetGameObject == nullptr || !targetGameObject->isActive ||
				targetGameObject->id == gameObject.id) {
				continue;
			}

			Vector3 targetWorldScale{};
			Vector3 targetWorldRotation{};
			Vector3 targetWorldPosition{};
			ResolveWorldTransform(
				*editorScene_,
				*targetGameObject,
				targetWorldScale,
				targetWorldRotation,
				targetWorldPosition);
			(void)targetWorldScale;
			(void)targetWorldPosition;
			desiredWorldRotation = Add(targetWorldRotation, torsionComponent->torsionRestRotation);
			targetRigidBodyComponent = EditorComponentUtility::FindComponent(
				*targetGameObject,
				EditorComponentType::RigidBody);

			if (targetRigidBodyComponent != nullptr && targetRigidBodyComponent->isActive) {
				targetAngularVelocity = targetRigidBodyComponent->angularVelocity;
			}
		}

		const Vector3 rotationError{
			WrapAngleRadians(desiredWorldRotation.x - ownerWorldRotation.x),
			WrapAngleRadians(desiredWorldRotation.y - ownerWorldRotation.y),
			WrapAngleRadians(desiredWorldRotation.z - ownerWorldRotation.z)};
		const Vector3 relativeAngularVelocity = Subtract(
			rigidBodyComponent->angularVelocity,
			targetAngularVelocity);
		Vector3 springTorque = Subtract(
			Multiply((std::max)(torsionComponent->torsionStiffness, 0.0f), rotationError),
			Multiply((std::max)(torsionComponent->torsionDamping, 0.0f), relativeAngularVelocity));
		springTorque = ClampVectorLength(
			springTorque,
			(std::max)(torsionComponent->torsionMaximumTorque, 0.0f));
		AddTorque(gameObject.id, springTorque);

		const bool canApplyReaction = torsionComponent->torsionApplyReaction &&
			targetGameObject != nullptr && targetRigidBodyComponent != nullptr &&
			targetRigidBodyComponent->isActive && !targetRigidBodyComponent->isKinematic;

		if (canApplyReaction) {
			AddTorque(targetGameObject->id, Multiply(-1.0f, springTorque));
		}
	}
}

void EditorPhysicsManager::ApplyThrusterForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* thrusterComponent = physicsObject.thruster;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || thrusterComponent == nullptr ||
			!thrusterComponent->isActive) {
			continue;
		}

		Vector3 thrustDirection = thrusterComponent->thrusterDirection;

		if (thrusterComponent->thrusterUseLocalDirection) {
			Vector3 worldScale{};
			Vector3 worldRotation{};
			Vector3 worldPosition{};
			ResolveWorldTransform(
				*editorScene_,
				gameObject,
				worldScale,
				worldRotation,
				worldPosition);
			(void)worldScale;
			(void)worldPosition;
			const Matrix4x4 directionMatrix = MakeAffineMatrix(
				{1.0f, 1.0f, 1.0f},
				worldRotation,
				{0.0f, 0.0f, 0.0f});
			thrustDirection = Transform(thrustDirection, directionMatrix);
		}

		if (Length(thrustDirection) <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const float throttle = (std::clamp)(thrusterComponent->thrusterThrottle, 0.0f, 1.0f);
		const Vector3 thrustForce = Multiply(
			thrusterComponent->thrusterForce * throttle,
			Normalize(thrustDirection));
		const Vector3 applicationPoint = Transform(
			thrusterComponent->thrusterLocalApplicationPoint,
			editorScene_->GetWorldMatrix(gameObject.id));
		AddForceAtPosition(gameObject.id, thrustForce, applicationPoint);
	}
}

void EditorPhysicsManager::ApplyPulleyForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (PhysicsStepObject& physicsObject : physicsStepObjects_) {
		EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* ownerRigidBody = physicsObject.rigidBody;
		EditorComponent* pulleyComponent = physicsObject.pulleyConstraint;

		if (ownerRigidBody == nullptr || !ownerRigidBody->isActive || ownerRigidBody->isKinematic ||
			pulleyComponent == nullptr || !pulleyComponent->isActive || pulleyComponent->pulleyIsBroken) {
			continue;
		}

		EditorGameObject* targetGameObject = editorScene_->FindGameObject(
			pulleyComponent->pulleyTargetGameObjectId);

		if (targetGameObject == nullptr || !targetGameObject->isActive ||
			targetGameObject->id == gameObject.id) {
			continue;
		}

		const EditorComponent* targetRigidBody = EditorComponentUtility::FindComponent(
			*targetGameObject,
			EditorComponentType::RigidBody);
		const Vector3 ownerAnchor = Transform(
			pulleyComponent->pulleyOwnerLocalAnchor,
			editorScene_->GetWorldMatrix(gameObject.id));
		const Vector3 targetAnchor = Transform(
			pulleyComponent->pulleyTargetLocalAnchor,
			editorScene_->GetWorldMatrix(targetGameObject->id));
		const Vector3 ownerSupportToAnchor = Subtract(
			ownerAnchor,
			pulleyComponent->pulleyOwnerWorldSupport);
		const Vector3 targetSupportToAnchor = Subtract(
			targetAnchor,
			pulleyComponent->pulleyTargetWorldSupport);
		const float ownerSegmentLength = Length(ownerSupportToAnchor);
		const float targetSegmentLength = Length(targetSupportToAnchor);

		if (ownerSegmentLength <= kMinimumPhysicsVectorLength ||
			targetSegmentLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const Vector3 ownerOutwardDirection = Multiply(
			1.0f / ownerSegmentLength,
			ownerSupportToAnchor);
		const Vector3 targetOutwardDirection = Multiply(
			1.0f / targetSegmentLength,
			targetSupportToAnchor);
		const Vector3 ownerPointVelocity = CalculatePointVelocity(
			*editorScene_,
			gameObject,
			*ownerRigidBody,
			ownerAnchor);
		Vector3 targetPointVelocity{0.0f, 0.0f, 0.0f};

		if (targetRigidBody != nullptr && targetRigidBody->isActive) {
			targetPointVelocity = CalculatePointVelocity(
				*editorScene_,
				*targetGameObject,
				*targetRigidBody,
				targetAnchor);
		}

		const float pulleyRatio = (std::max)(pulleyComponent->pulleyRatio, 0.0001f);
		const float constrainedLength = ownerSegmentLength + pulleyRatio * targetSegmentLength;
		const float lengthError = constrainedLength - (std::max)(pulleyComponent->pulleyTotalLength, 0.0f);

		if (lengthError <= 0.0f) {
			continue;
		}

		const float constrainedLengthRate = Dot(ownerPointVelocity, ownerOutwardDirection) +
			pulleyRatio * Dot(targetPointVelocity, targetOutwardDirection);
		float tension = (std::max)(
			(std::max)(pulleyComponent->pulleyStiffness, 0.0f) * lengthError +
				(std::max)(pulleyComponent->pulleyDamping, 0.0f) * constrainedLengthRate,
			0.0f);
		const float breakingTension = (std::max)(pulleyComponent->pulleyBreakingTension, 0.0f);

		if (breakingTension > 0.0f && tension >= breakingTension) {
			pulleyComponent->pulleyIsBroken = true;

			if (consoleMessages_ != nullptr) {
				consoleMessages_->push_back("滑車拘束が破断しました: " + gameObject.name);
			}

			continue;
		}

		const float maximumTension = (std::max)(pulleyComponent->pulleyMaximumTension, 0.0f);

		if (maximumTension > 0.0f) {
			tension = (std::min)(tension, maximumTension);
		}

		AddForceAtPosition(
			gameObject.id,
			Multiply(-tension, ownerOutwardDirection),
			ownerAnchor);

		if (targetRigidBody != nullptr && targetRigidBody->isActive && !targetRigidBody->isKinematic) {
			AddForceAtPosition(
				targetGameObject->id,
				Multiply(-tension * pulleyRatio, targetOutwardDirection),
				targetAnchor);
		}
	}
}

void EditorPhysicsManager::ApplyPhysicsServoForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* servoComponent = physicsObject.physicsServo;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || servoComponent == nullptr || !servoComponent->isActive) {
			continue;
		}

		Vector3 ownerWorldScale{};
		Vector3 ownerWorldRotation{};
		Vector3 ownerWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			ownerWorldScale,
			ownerWorldRotation,
			ownerWorldPosition);
		(void)ownerWorldScale;
		Vector3 desiredPosition = servoComponent->servoTargetPosition;
		Vector3 desiredRotation = servoComponent->servoTargetRotation;
		Vector3 targetVelocity{0.0f, 0.0f, 0.0f};
		Vector3 targetAngularVelocity{0.0f, 0.0f, 0.0f};
		const EditorGameObject* targetGameObject = nullptr;
		const EditorComponent* targetRigidBody = nullptr;

		if (servoComponent->servoTargetGameObjectId >= 0) {
			targetGameObject = editorScene_->FindGameObject(servoComponent->servoTargetGameObjectId);

			if (targetGameObject == nullptr || !targetGameObject->isActive ||
				targetGameObject->id == gameObject.id) {
				continue;
			}

			Vector3 targetWorldScale{};
			Vector3 targetWorldRotation{};
			Vector3 targetWorldPosition{};
			ResolveWorldTransform(
				*editorScene_,
				*targetGameObject,
				targetWorldScale,
				targetWorldRotation,
				targetWorldPosition);
			(void)targetWorldScale;
			desiredPosition = Add(targetWorldPosition, servoComponent->servoTargetPosition);
			desiredRotation = Add(targetWorldRotation, servoComponent->servoTargetRotation);
			targetRigidBody = EditorComponentUtility::FindComponent(
				*targetGameObject,
				EditorComponentType::RigidBody);

			if (targetRigidBody != nullptr && targetRigidBody->isActive) {
				targetVelocity = targetRigidBody->velocity;
				targetAngularVelocity = targetRigidBody->angularVelocity;
			}
		}

		Vector3 servoForce = Add(
			Multiply(
				(std::max)(servoComponent->servoPositionStiffness, 0.0f),
				Subtract(desiredPosition, ownerWorldPosition)),
			Multiply(
				(std::max)(servoComponent->servoPositionDamping, 0.0f),
				Subtract(targetVelocity, rigidBodyComponent->velocity)));
		servoForce = ClampVectorLength(
			servoForce,
			(std::max)(servoComponent->servoMaximumForce, 0.0f));
		const Vector3 rotationError{
			WrapAngleRadians(desiredRotation.x - ownerWorldRotation.x),
			WrapAngleRadians(desiredRotation.y - ownerWorldRotation.y),
			WrapAngleRadians(desiredRotation.z - ownerWorldRotation.z)};
		Vector3 servoTorque = Add(
			Multiply((std::max)(servoComponent->servoRotationStiffness, 0.0f), rotationError),
			Multiply(
				(std::max)(servoComponent->servoRotationDamping, 0.0f),
				Subtract(targetAngularVelocity, rigidBodyComponent->angularVelocity)));
		servoTorque = ClampVectorLength(
			servoTorque,
			(std::max)(servoComponent->servoMaximumTorque, 0.0f));
		AddForce(gameObject.id, servoForce);
		AddTorque(gameObject.id, servoTorque);

		const bool canApplyReaction = servoComponent->servoApplyReaction &&
			targetGameObject != nullptr && targetRigidBody != nullptr &&
			targetRigidBody->isActive && !targetRigidBody->isKinematic;

		if (canApplyReaction) {
			AddForce(targetGameObject->id, Multiply(-1.0f, servoForce));
			AddTorque(targetGameObject->id, Multiply(-1.0f, servoTorque));
		}
	}
}

void EditorPhysicsManager::ApplyVortexFieldForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (vortexFieldObjects_.empty()) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (!gameObject.isActive || rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		for (const PhysicsStepObject* vortexField : vortexFieldObjects_) {
			if (vortexField->gameObject->id == gameObject.id) {
				continue;
			}

			const Vector3 fieldCenter = ResolveWorldPosition(*editorScene_, *vortexField->gameObject);
			const std::array<Vector3, 3u> fieldAxes = BuildObjectAxes(
				*editorScene_,
				*vortexField->gameObject);
			Vector3 vortexAxis = Add(
				Add(
					Multiply(vortexField->vortexField->vortexAxis.x, fieldAxes[0]),
					Multiply(vortexField->vortexField->vortexAxis.y, fieldAxes[1])),
				Multiply(vortexField->vortexField->vortexAxis.z, fieldAxes[2]));

			if (Length(vortexAxis) <= kMinimumPhysicsVectorLength) {
				continue;
			}

			vortexAxis = Normalize(vortexAxis);
			const Vector3 centerToBody = Subtract(
				ResolveWorldPosition(*editorScene_, gameObject),
				fieldCenter);
			const Vector3 radialOffset = Subtract(
				centerToBody,
				Multiply(Dot(centerToBody, vortexAxis), vortexAxis));
			const float radialDistance = Length(radialOffset);
			const float influenceRadius = (std::max)(vortexField->vortexField->vortexRadius, 0.0f);

			if (influenceRadius > 0.0f && radialDistance >= influenceRadius) {
				continue;
			}

			Vector3 radialDirection{0.0f, 0.0f, 0.0f};
			Vector3 tangentDirection{0.0f, 0.0f, 0.0f};

			if (radialDistance > kMinimumPhysicsVectorLength) {
				radialDirection = Multiply(1.0f / radialDistance, radialOffset);
				tangentDirection = Normalize(Cross(vortexAxis, radialDirection));
			}

			const float attenuation = CalculateSphericalFieldAttenuation(
				radialDistance,
				influenceRadius);
			const Vector3 targetFlowVelocity = Add(
				Add(
					Multiply(
						vortexField->vortexField->vortexAngularVelocity * radialDistance,
						tangentDirection),
					Multiply(vortexField->vortexField->vortexAxialVelocity, vortexAxis)),
				Multiply(-vortexField->vortexField->vortexRadialInflowVelocity, radialDirection));
			Vector3 vortexAcceleration = Multiply(
				(std::max)(vortexField->vortexField->vortexVelocityCoupling, 0.0f) * attenuation,
				Subtract(targetFlowVelocity, rigidBodyComponent->velocity));
			vortexAcceleration = ClampVectorLength(
				vortexAcceleration,
				(std::max)(vortexField->vortexField->vortexMaximumAcceleration, 0.0f));
			AddForce(
				gameObject.id,
				Multiply((std::max)(rigidBodyComponent->mass, 0.01f), vortexAcceleration));
		}
	}
}

void EditorPhysicsManager::ApplyPressureFieldForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (pressureFieldObjects_.empty()) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (!gameObject.isActive || rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		RuntimeBuoyancySettings bodyShape{};

		if (!BuildRuntimeBuoyancySettings(gameObject, nullptr, bodyShape)) {
			continue;
		}

		Vector3 bodyWorldScale{};
		Vector3 bodyWorldRotation{};
		Vector3 bodyWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			bodyWorldScale,
			bodyWorldRotation,
			bodyWorldPosition);
		(void)bodyWorldRotation;
		const float bodyVolume = (std::max)(
			std::fabs(bodyShape.hullSize.x * bodyWorldScale.x) *
				std::fabs(bodyShape.hullSize.y * bodyWorldScale.y) *
				std::fabs(bodyShape.hullSize.z * bodyWorldScale.z),
			0.000001f);
		const float referenceArea = std::pow(bodyVolume, 2.0f / 3.0f);

		for (const PhysicsStepObject* pressureField : pressureFieldObjects_) {
			if (pressureField->gameObject->id == gameObject.id) {
				continue;
			}

			const Vector3 fieldToBody = Subtract(
				bodyWorldPosition,
				ResolveWorldPosition(*editorScene_, *pressureField->gameObject));
			const float distance = Length(fieldToBody);

			if (distance <= kMinimumPhysicsVectorLength) {
				continue;
			}

			const float influenceRadius = (std::max)(pressureField->pressureField->pressureFieldRadius, 0.0f);

			if (influenceRadius > 0.0f && distance >= influenceRadius) {
				continue;
			}

			float attenuation = 1.0f;

			if (influenceRadius > 0.0f) {
				const float remainingRatio = (std::clamp)(1.0f - distance / influenceRadius, 0.0f, 1.0f);
				attenuation = std::pow(
					remainingRatio,
					(std::max)(pressureField->pressureField->pressureFieldFalloffExponent, 0.0f));
			}

			Vector3 pressureForce = Multiply(
				pressureField->pressureField->pressureFieldPressure * referenceArea * attenuation,
				Multiply(1.0f / distance, fieldToBody));
			pressureForce = ClampVectorLength(
				pressureForce,
				(std::max)(pressureField->pressureField->pressureFieldMaximumForce, 0.0f));
			AddForce(gameObject.id, pressureForce);
		}
	}
}

void EditorPhysicsManager::ApplySuspensionForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (PhysicsStepObject& physicsObject : physicsStepObjects_) {
		EditorGameObject& gameObject = *physicsObject.gameObject;
		EditorComponent* suspensionComponent = physicsObject.suspension;

		if (suspensionComponent != nullptr) {
			suspensionComponent->suspensionIsGrounded = false;
			suspensionComponent->suspensionCurrentLength =
				(std::max)(suspensionComponent->suspensionMaximumLength, 0.0f);
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		if (!gameObject.isActive || rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || suspensionComponent == nullptr ||
			!suspensionComponent->isActive) {
			continue;
		}

		const Matrix4x4 ownerWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Vector3 worldAnchor = Transform(suspensionComponent->suspensionLocalAnchor, ownerWorldMatrix);
		const Vector3 directionPoint = Transform(
			Add(suspensionComponent->suspensionLocalAnchor, suspensionComponent->suspensionLocalDirection),
			ownerWorldMatrix);
		const Vector3 rawWorldDirection = Subtract(directionPoint, worldAnchor);
		const float directionLength = Length(rawWorldDirection);

		if (directionLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		const Vector3 worldDirection = Multiply(1.0f / directionLength, rawWorldDirection);
		const float maximumLength = (std::max)(suspensionComponent->suspensionMaximumLength, 0.0f);
		const float wheelRadius = (std::max)(suspensionComponent->suspensionWheelRadius, 0.0f);
		const float castDistance = maximumLength + wheelRadius;

		if (castDistance <= kMinimumPhysicsVectorLength) {
			continue;
		}

		EditorJoltPhysicsManager::PhysicsHit suspensionHit{};
		const bool hasGroundHit = RaycastIgnoringGameObject(
			worldAnchor,
			worldDirection,
			castDistance,
			gameObject.id,
			suspensionHit);

		if (!hasGroundHit || suspensionHit.isTrigger) {
			continue;
		}

		const float currentLength = (std::clamp)(
			suspensionHit.distance - wheelRadius,
			0.0f,
			maximumLength);
		suspensionComponent->suspensionIsGrounded = true;
		suspensionComponent->suspensionCurrentLength = currentLength;
		const float compressionLength =
			(std::max)(suspensionComponent->suspensionRestLength, 0.0f) - currentLength;

		if (compressionLength <= 0.0f) {
			continue;
		}

		const Vector3 ownerPointVelocity = CalculatePointVelocity(
			*editorScene_,
			gameObject,
			*rigidBodyComponent,
			worldAnchor);
		Vector3 groundPointVelocity{0.0f, 0.0f, 0.0f};
		const EditorGameObject* groundGameObject = editorScene_->FindGameObject(suspensionHit.gameObjectId);
		const EditorComponent* groundRigidBodyComponent = nullptr;

		if (groundGameObject != nullptr && groundGameObject->isActive) {
			groundRigidBodyComponent = EditorComponentUtility::FindComponent(
				*groundGameObject,
				EditorComponentType::RigidBody);

			if (groundRigidBodyComponent != nullptr && groundRigidBodyComponent->isActive) {
				groundPointVelocity = CalculatePointVelocity(
					*editorScene_,
					*groundGameObject,
					*groundRigidBodyComponent,
					suspensionHit.point);
			}
		}

		const float compressionSpeed = Dot(
			Subtract(ownerPointVelocity, groundPointVelocity),
			worldDirection);
		float suspensionForceMagnitude =
			(std::max)(suspensionComponent->suspensionStiffness, 0.0f) * compressionLength +
			(std::max)(suspensionComponent->suspensionDamping, 0.0f) * compressionSpeed;
		suspensionForceMagnitude = (std::max)(suspensionForceMagnitude, 0.0f);
		const float maximumForce = (std::max)(suspensionComponent->suspensionMaximumForce, 0.0f);

		if (maximumForce > 0.0f) {
			suspensionForceMagnitude = (std::min)(suspensionForceMagnitude, maximumForce);
		}

		Vector3 suspensionForceDirection = Multiply(-1.0f, worldDirection);
		if (suspensionComponent->suspensionUseHitNormal &&
			Length(suspensionHit.normal) > kMinimumPhysicsVectorLength) {
			suspensionForceDirection = Normalize(suspensionHit.normal);
		}

		const Vector3 suspensionForce = Multiply(
			suspensionForceMagnitude,
			suspensionForceDirection);
		AddForceAtPosition(gameObject.id, suspensionForce, worldAnchor);

		const bool canApplyReaction = suspensionComponent->suspensionApplyReaction &&
			groundGameObject != nullptr && groundRigidBodyComponent != nullptr &&
			groundRigidBodyComponent->isActive && !groundRigidBodyComponent->isKinematic;

		if (canApplyReaction) {
			AddForceAtPosition(
				groundGameObject->id,
				Multiply(-1.0f, suspensionForce),
				suspensionHit.point);
		}
	}
}

void EditorPhysicsManager::ApplyUprightStabilizerTorques() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* uprightComponent = physicsObject.uprightStabilizer;

		if (!gameObject.isActive || rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || uprightComponent == nullptr ||
			!uprightComponent->isActive) {
			continue;
		}

		const float localUpLength = Length(uprightComponent->uprightLocalUpAxis);
		const float targetUpLength = Length(uprightComponent->uprightTargetWorldUp);
		if (localUpLength <= kMinimumPhysicsVectorLength || targetUpLength <= kMinimumPhysicsVectorLength) {
			continue;
		}

		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			worldScale,
			worldRotation,
			worldPosition);
		(void)worldScale;
		(void)worldPosition;
		const Matrix4x4 worldRotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			worldRotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 currentWorldUp = Normalize(Transform(
			Multiply(1.0f / localUpLength, uprightComponent->uprightLocalUpAxis),
			worldRotationMatrix));
		const Vector3 targetWorldUp = Multiply(
			1.0f / targetUpLength,
			uprightComponent->uprightTargetWorldUp);
		const Vector3 rotationAxis = Cross(currentWorldUp, targetWorldUp);
		const float axisLength = Length(rotationAxis);
		const float alignment = (std::clamp)(Dot(currentWorldUp, targetWorldUp), -1.0f, 1.0f);
		Vector3 correctionTorque{0.0f, 0.0f, 0.0f};

		if (axisLength > kMinimumPhysicsVectorLength) {
			const float correctionAngle = std::atan2(axisLength, alignment);
			correctionTorque = Multiply(
				(std::max)(uprightComponent->uprightStiffness, 0.0f) * correctionAngle / axisLength,
				rotationAxis);
		}
		else if (alignment < 0.0f) {
			// 完全に上下反転すると外積がゼロになるため、任意の直交軸を復元方向に選ぶ。
			Vector3 fallbackAxis = Cross(currentWorldUp, {1.0f, 0.0f, 0.0f});
			if (Length(fallbackAxis) <= kMinimumPhysicsVectorLength) {
				fallbackAxis = Cross(currentWorldUp, {0.0f, 0.0f, 1.0f});
			}

			correctionTorque = Multiply(
				(std::max)(uprightComponent->uprightStiffness, 0.0f) * kPhysicsPi,
				Normalize(fallbackAxis));
		}

		const Vector3 angularVelocityWithoutYaw = Subtract(
			rigidBodyComponent->angularVelocity,
			Multiply(Dot(rigidBodyComponent->angularVelocity, targetWorldUp), targetWorldUp));
		const Vector3 dampingTorque = Multiply(
			-(std::max)(uprightComponent->uprightDamping, 0.0f),
			angularVelocityWithoutYaw);
		Vector3 uprightTorque = Add(correctionTorque, dampingTorque);
		uprightTorque = ClampVectorLength(
			uprightTorque,
			(std::max)(uprightComponent->uprightMaximumTorque, 0.0f));
		AddTorque(gameObject.id, uprightTorque);
	}
}

void EditorPhysicsManager::ApplyElectromagneticForces() {
	if (editorScene_ == nullptr) {
		return;
	}

	if (electromagneticFieldObjects_.empty()) {
		return;
	}

	for (const PhysicsStepObject& physicsObject : physicsStepObjects_) {
		const EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* rigidBodyComponent = physicsObject.rigidBody;
		const EditorComponent* electromagneticBody = physicsObject.electromagneticBody;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic || electromagneticBody == nullptr ||
			!electromagneticBody->isActive) {
			continue;
		}

		Vector3 totalElectricField{0.0f, 0.0f, 0.0f};
		Vector3 totalMagneticField{0.0f, 0.0f, 0.0f};
		Vector3 bodyWorldScale{};
		Vector3 bodyWorldRotation{};
		Vector3 bodyWorldPosition{};
		ResolveWorldTransform(
			*editorScene_,
			gameObject,
			bodyWorldScale,
			bodyWorldRotation,
			bodyWorldPosition);
		(void)bodyWorldScale;

		for (const PhysicsStepObject* electromagneticField : electromagneticFieldObjects_) {
			const Vector3 fieldToBody = Subtract(
				bodyWorldPosition,
				ResolveWorldPosition(*editorScene_, *electromagneticField->gameObject));
			const float fieldDistance = Length(fieldToBody);
			const float influenceRadius = (std::max)(
				electromagneticField->electromagneticField->electromagneticInfluenceRadius,
				0.0f);

			if (influenceRadius > 0.0f && fieldDistance >= influenceRadius) {
				continue;
			}

			const float fieldAttenuation = CalculateSphericalFieldAttenuation(
				fieldDistance,
				influenceRadius);

			if (electromagneticField->electromagneticField->electromagneticFieldMode == 1) {
				if (electromagneticField->gameObject->id == gameObject.id ||
					fieldDistance <= kMinimumPhysicsVectorLength) {
					continue;
				}

				const float calculationDistance = (std::max)(
					fieldDistance,
					(std::max)(
						electromagneticField->electromagneticField->electromagneticMinimumDistance,
						0.001f));
				const float electricFieldMagnitude =
					electromagneticField->electromagneticField->electromagneticCoulombConstant *
					electromagneticField->electromagneticField->electromagneticSourceCharge /
					(calculationDistance * calculationDistance);
				const Vector3 electricFieldDirection = Multiply(
					1.0f / fieldDistance,
					fieldToBody);
				totalElectricField = Add(
					totalElectricField,
					Multiply(
						electricFieldMagnitude * fieldAttenuation,
						electricFieldDirection));
			}
			else {
				totalElectricField = Add(
					totalElectricField,
					Multiply(
						fieldAttenuation,
						electromagneticField->electromagneticField->electromagneticElectricField));
			}

			totalMagneticField = Add(
				totalMagneticField,
				Multiply(
					fieldAttenuation,
					electromagneticField->electromagneticField->electromagneticMagneticField));
		}

		// Lorentz力 F=q(E+v*B)。Crossは右手系のv x Bとして計算する。
		Vector3 electromagneticForce = Multiply(
			electromagneticBody->electromagneticCharge,
			Add(
				totalElectricField,
				Cross(rigidBodyComponent->velocity, totalMagneticField)));
		electromagneticForce = ClampVectorLength(
			electromagneticForce,
			electromagneticBody->electromagneticMaximumForce);
		AddForce(gameObject.id, electromagneticForce);

		const Matrix4x4 bodyRotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			bodyWorldRotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 worldMagneticMoment = Transform(
			electromagneticBody->electromagneticMagneticMoment,
			bodyRotationMatrix);
		Vector3 magneticTorque = Cross(worldMagneticMoment, totalMagneticField);
		magneticTorque = ClampVectorLength(
			magneticTorque,
			electromagneticBody->electromagneticMaximumTorque);
		AddTorque(gameObject.id, magneticTorque);
	}
}

void EditorPhysicsManager::ApplyBuoyancyForces(float fixedDeltaTime) {
	if (editorScene_ == nullptr || fixedDeltaTime <= 0.0f) {
		return;
	}

	const float oceanElapsedTime = GetEditorOceanElapsedTime();
	const Vector3 gravity = editorScene_->GetPhysicsSettings().gravity;
	const float gravityMagnitude = Length(gravity);
	const Vector3 buoyancyUp = gravityMagnitude > 0.0001f ?
		Normalize(Multiply(-1.0f, gravity)) : Vector3{0.0f, 1.0f, 0.0f};

	for (PhysicsStepObject& physicsObject : physicsStepObjects_) {
		EditorGameObject& gameObject = *physicsObject.gameObject;
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* rigidBodyComponent = physicsObject.rigidBody;

		if (rigidBodyComponent == nullptr || !rigidBodyComponent->isActive ||
			rigidBodyComponent->isKinematic) {
			continue;
		}

		const EditorComponent* buoyancyComponent = physicsObject.buoyancy;
		RuntimeBuoyancySettings buoyancySettings{};

		if (!BuildRuntimeBuoyancySettings(gameObject, buoyancyComponent, buoyancySettings)) {
			continue;
		}

		const Vector3 safeHullSize{
			(std::max)(std::fabs(buoyancySettings.hullSize.x), 0.05f),
			(std::max)(std::fabs(buoyancySettings.hullSize.y), 0.05f),
			(std::max)(std::fabs(buoyancySettings.hullSize.z), 0.05f)};
		Vector3 worldScale = gameObject.scale;
		Vector3 worldRotation = gameObject.rotate;
		Vector3 worldPosition = gameObject.translate;
		editorScene_->GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			worldPosition);

		//================================================================
		// 実 Physics Shape を使う浮力
		//================================================================
		// 船体 AABB の空間へ仮想セルを詰めるのではなく、Jolt が実際に衝突へ使う
		// Box / Sphere / Capsule / ConvexHull を局所水面 Plane で切る。
		// これにより上部構造や凸包外の空間へ浮力が掛からず、水没体積の重心が浮心になる。
		{
			const Matrix4x4 boatWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
			const Matrix4x4 boatRotationMatrix = MakeAffineMatrix(
				{1.0f, 1.0f, 1.0f},
				worldRotation,
				{0.0f, 0.0f, 0.0f});
			const Vector3 boatRight = Normalize(Transform(
				{1.0f, 0.0f, 0.0f},
				boatRotationMatrix));
			const Vector3 boatUp = Normalize(Transform(
				{0.0f, 1.0f, 0.0f},
				boatRotationMatrix));
			const Vector3 boatForward = Normalize(Transform(
				{0.0f, 0.0f, 1.0f},
				boatRotationMatrix));

			Vector3 horizontalRight{boatRight.x, 0.0f, boatRight.z};
			Vector3 horizontalForward{boatForward.x, 0.0f, boatForward.z};

			if (Length(horizontalRight) <= 0.0001f) {
				horizontalRight = {1.0f, 0.0f, 0.0f};
			}
			else {
				horizontalRight = Normalize(horizontalRight);
			}

			if (Length(horizontalForward) <= 0.0001f) {
				horizontalForward = {0.0f, 0.0f, 1.0f};
			}
			else {
				horizontalForward = Normalize(horizontalForward);
			}

			const Vector3 worldHullCenter = Transform(
				buoyancySettings.centerOffset,
				boatWorldMatrix);
			const float probeHalfWidth = (std::max)(
				std::fabs(safeHullSize.x * worldScale.x) * 0.4f,
				0.25f);
			const float probeHalfLength = (std::max)(
				std::fabs(safeHullSize.z * worldScale.z) * 0.4f,
				0.25f);
			std::array<float, kBuoyancySurfaceProbeCount> probeRightOffsets{};
			std::array<float, kBuoyancySurfaceProbeCount> probeForwardOffsets{};
			std::array<Vector3, kBuoyancySurfaceProbeCount> probePositions{};
			std::array<EditorOceanSurfaceSample, kBuoyancySurfaceProbeCount> surfaceSamples{};
			std::array<bool, kBuoyancySurfaceProbeCount> hasSurfaceSample{};
			constexpr size_t kCenterSurfaceProbeIndex = kBuoyancySurfaceProbeCount / 2u;
			int32_t resolvedOceanGameObjectId = buoyancySettings.oceanGameObjectId;
			int32_t validSurfaceSampleCount = 0;
			Vector3 averageSurfaceVelocity{};
			Vector3 averageSurfaceNormal{};
			float averageSurfaceHeight = 0.0f;
			const uint32_t objectSampleKey =
				static_cast<uint32_t>(gameObject.id) * 4099u + 2048u;

			for (size_t probeIndex = 0u; probeIndex < probePositions.size(); probeIndex++) {
				const int32_t gridX = static_cast<int32_t>(probeIndex) %
					kBuoyancySurfaceProbeAxisCount;
				const int32_t gridZ = static_cast<int32_t>(probeIndex) /
					kBuoyancySurfaceProbeAxisCount;
				const float normalizedX =
					2.0f * static_cast<float>(gridX) /
						static_cast<float>(kBuoyancySurfaceProbeAxisCount - 1) -
					1.0f;
				const float normalizedZ =
					2.0f * static_cast<float>(gridZ) /
						static_cast<float>(kBuoyancySurfaceProbeAxisCount - 1) -
					1.0f;
				probeRightOffsets[probeIndex] = normalizedX * probeHalfWidth;
				probeForwardOffsets[probeIndex] = normalizedZ * probeHalfLength;
				probePositions[probeIndex] = Add(
					worldHullCenter,
					Add(
						Multiply(probeRightOffsets[probeIndex], horizontalRight),
						Multiply(probeForwardOffsets[probeIndex], horizontalForward)));
			}

			for (size_t probeIndex = 0u; probeIndex < probePositions.size(); probeIndex++) {
				const uint64_t surfaceSampleKey = static_cast<uint64_t>(
					objectSampleKey + static_cast<uint32_t>(probeIndex));
				hasSurfaceSample[probeIndex] = SampleEditorOceanSurface(
					*editorScene_,
					resolvedOceanGameObjectId,
					probePositions[probeIndex],
					surfaceSampleKey,
					oceanElapsedTime,
					surfaceSamples[probeIndex]);

				if (!hasSurfaceSample[probeIndex]) {
					continue;
				}

				if (resolvedOceanGameObjectId < 0) {
					resolvedOceanGameObjectId = surfaceSamples[probeIndex].oceanGameObjectId;
				}

				Vector3 upwardNormal = surfaceSamples[probeIndex].normal;
				if (Dot(upwardNormal, buoyancyUp) < 0.0f) {
					upwardNormal = Multiply(-1.0f, upwardNormal);
				}

				averageSurfaceVelocity = Add(
					averageSurfaceVelocity,
					surfaceSamples[probeIndex].velocity);
				averageSurfaceNormal = Add(averageSurfaceNormal, upwardNormal);
				averageSurfaceHeight += surfaceSamples[probeIndex].position.y;
				validSurfaceSampleCount++;
			}

			// 中央点が取れなければ実 Shape Plane を作れないため、旧グリッドへフォールバックする。
			if (hasSurfaceSample[kCenterSurfaceProbeIndex] && validSurfaceSampleCount > 0) {
				const float inverseSurfaceSampleCount =
					1.0f / static_cast<float>(validSurfaceSampleCount);
				averageSurfaceVelocity = Multiply(
					inverseSurfaceSampleCount,
					averageSurfaceVelocity);
				averageSurfaceHeight *= inverseSurfaceSampleCount;
				averageSurfaceNormal = Normalize(averageSurfaceNormal);
				Vector3 fittedSurfaceNormal = averageSurfaceNormal;
				float fittedSurfaceHeight = surfaceSamples[kCenterSurfaceProbeIndex].position.y;

				// 船体下面を覆う 3x3 FFT Sample へ最小二乗 Plane を当てる。
				// 一方向の波頭や斜め波でも、中央差分だけより外れ値と局所Normalノイズへ強くなる。
				if (validSurfaceSampleCount == static_cast<int32_t>(surfaceSamples.size())) {
					float rightSlopeNumerator = 0.0f;
					float rightSlopeDenominator = 0.0f;
					float forwardSlopeNumerator = 0.0f;
					float forwardSlopeDenominator = 0.0f;

					for (size_t probeIndex = 0u; probeIndex < surfaceSamples.size(); probeIndex++) {
						const float relativeSurfaceHeight =
							surfaceSamples[probeIndex].position.y - averageSurfaceHeight;
						rightSlopeNumerator +=
							probeRightOffsets[probeIndex] * relativeSurfaceHeight;
						rightSlopeDenominator +=
							probeRightOffsets[probeIndex] * probeRightOffsets[probeIndex];
						forwardSlopeNumerator +=
							probeForwardOffsets[probeIndex] * relativeSurfaceHeight;
						forwardSlopeDenominator +=
							probeForwardOffsets[probeIndex] * probeForwardOffsets[probeIndex];
					}

					const float rightSurfaceSlope = (rightSlopeDenominator > 0.000001f)
						? rightSlopeNumerator / rightSlopeDenominator
						: 0.0f;
					const float forwardSurfaceSlope = (forwardSlopeDenominator > 0.000001f)
						? forwardSlopeNumerator / forwardSlopeDenominator
						: 0.0f;
					const Vector3 lateralTangent = Add(
						horizontalRight,
						Multiply(rightSurfaceSlope, buoyancyUp));
					const Vector3 longitudinalTangent = Add(
						horizontalForward,
						Multiply(forwardSurfaceSlope, buoyancyUp));
					Vector3 leastSquaresNormal = Cross(longitudinalTangent, lateralTangent);

					if (Length(leastSquaresNormal) > 0.0001f) {
						leastSquaresNormal = Normalize(leastSquaresNormal);

						if (Dot(leastSquaresNormal, buoyancyUp) < 0.0f) {
							leastSquaresNormal = Multiply(-1.0f, leastSquaresNormal);
						}

						fittedSurfaceNormal = Normalize(Add(
							Multiply(0.85f, leastSquaresNormal),
							Multiply(0.15f, averageSurfaceNormal)));
						fittedSurfaceHeight = averageSurfaceHeight;
					}
				}

				if (Length(fittedSurfaceNormal) <= 0.0001f) {
					fittedSurfaceNormal = buoyancyUp;
				}

				// 固定5x5 Probeを双線形補間し、水力面数に比例したFFT評価を避けながら
				// 各面位置の高さ、法線、表面速度を取得する。
				const auto interpolateSurfaceSample = [
					&](const Vector3& queryPosition, EditorOceanSurfaceSample& interpolatedSample) {
					const Vector3 centerToQuery = Subtract(queryPosition, worldHullCenter);
					const float normalizedRight = (std::clamp)(
						Dot(centerToQuery, horizontalRight) / probeHalfWidth,
						-1.0f,
						1.0f);
					const float normalizedForward = (std::clamp)(
						Dot(centerToQuery, horizontalForward) / probeHalfLength,
						-1.0f,
						1.0f);
					const float gridX = (normalizedRight * 0.5f + 0.5f) *
						static_cast<float>(kBuoyancySurfaceProbeAxisCount - 1);
					const float gridZ = (normalizedForward * 0.5f + 0.5f) *
						static_cast<float>(kBuoyancySurfaceProbeAxisCount - 1);
					const int32_t minimumX = (std::clamp)(
						static_cast<int32_t>(std::floor(gridX)),
						0,
						kBuoyancySurfaceProbeAxisCount - 1);
					const int32_t minimumZ = (std::clamp)(
						static_cast<int32_t>(std::floor(gridZ)),
						0,
						kBuoyancySurfaceProbeAxisCount - 1);
					const int32_t maximumX = (std::min)(
						minimumX + 1,
						kBuoyancySurfaceProbeAxisCount - 1);
					const int32_t maximumZ = (std::min)(
						minimumZ + 1,
						kBuoyancySurfaceProbeAxisCount - 1);
					const float interpolationX = gridX - static_cast<float>(minimumX);
					const float interpolationZ = gridZ - static_cast<float>(minimumZ);
					const size_t minimumMinimumIndex = static_cast<size_t>(
						minimumZ * kBuoyancySurfaceProbeAxisCount + minimumX);
					const size_t maximumMinimumIndex = static_cast<size_t>(
						minimumZ * kBuoyancySurfaceProbeAxisCount + maximumX);
					const size_t minimumMaximumIndex = static_cast<size_t>(
						maximumZ * kBuoyancySurfaceProbeAxisCount + minimumX);
					const size_t maximumMaximumIndex = static_cast<size_t>(
						maximumZ * kBuoyancySurfaceProbeAxisCount + maximumX);
					const bool hasInterpolationSamples =
						hasSurfaceSample[minimumMinimumIndex] &&
						hasSurfaceSample[maximumMinimumIndex] &&
						hasSurfaceSample[minimumMaximumIndex] &&
						hasSurfaceSample[maximumMaximumIndex];

					if (hasInterpolationSamples) {
						const auto bilinearVector = [
							interpolationX,
							interpolationZ](
								const Vector3& minimumMinimum,
								const Vector3& maximumMinimum,
								const Vector3& minimumMaximum,
								const Vector3& maximumMaximum) {
							const Vector3 minimumRow = Add(
								Multiply(1.0f - interpolationX, minimumMinimum),
								Multiply(interpolationX, maximumMinimum));
							const Vector3 maximumRow = Add(
								Multiply(1.0f - interpolationX, minimumMaximum),
								Multiply(interpolationX, maximumMaximum));
							return Add(
								Multiply(1.0f - interpolationZ, minimumRow),
								Multiply(interpolationZ, maximumRow));
						};
						const Vector3 interpolatedPosition = bilinearVector(
							surfaceSamples[minimumMinimumIndex].position,
							surfaceSamples[maximumMinimumIndex].position,
							surfaceSamples[minimumMaximumIndex].position,
							surfaceSamples[maximumMaximumIndex].position);
						interpolatedSample.isValid = true;
						interpolatedSample.oceanGameObjectId = resolvedOceanGameObjectId;
						interpolatedSample.position = {
							queryPosition.x,
							interpolatedPosition.y,
							queryPosition.z};
						interpolatedSample.normal = Normalize(bilinearVector(
							surfaceSamples[minimumMinimumIndex].normal,
							surfaceSamples[maximumMinimumIndex].normal,
							surfaceSamples[minimumMaximumIndex].normal,
							surfaceSamples[maximumMaximumIndex].normal));
						interpolatedSample.velocity = bilinearVector(
							surfaceSamples[minimumMinimumIndex].velocity,
							surfaceSamples[maximumMinimumIndex].velocity,
							surfaceSamples[minimumMaximumIndex].velocity,
							surfaceSamples[maximumMaximumIndex].velocity);
						return;
					}

					const float safeNormalY = std::fabs(fittedSurfaceNormal.y) > 0.0001f
						? fittedSurfaceNormal.y
						: 1.0f;
					const float planeHeight = fittedSurfaceHeight -
						((queryPosition.x - worldHullCenter.x) * fittedSurfaceNormal.x +
						 (queryPosition.z - worldHullCenter.z) * fittedSurfaceNormal.z) /
							safeNormalY;
					interpolatedSample.isValid = true;
					interpolatedSample.oceanGameObjectId = resolvedOceanGameObjectId;
					interpolatedSample.position = {
						queryPosition.x,
						planeHeight,
						queryPosition.z};
					interpolatedSample.normal = fittedSurfaceNormal;
					interpolatedSample.velocity = averageSurfaceVelocity;
				};

				const Vector3 fittedSurfacePosition{
					worldHullCenter.x,
					fittedSurfaceHeight,
					worldHullCenter.z};
				EditorJoltPhysicsManager::SubmergedVolumeInfo volumeInfo{};
				const bool hasShapeVolume = joltPhysicsManager_.GetSubmergedVolume(
					gameObject.id,
					fittedSurfacePosition,
					fittedSurfaceNormal,
					volumeInfo);

				if (hasShapeVolume) {
					const float safeFixedDeltaTime = (std::max)(fixedDeltaTime, 0.0001f);
					float resolvedBodyMass = rigidBodyComponent->mass;
					joltPhysicsManager_.GetBodyMass(gameObject.id, resolvedBodyMass);
					const float safeMass = (std::max)(resolvedBodyMass, 0.01f);
					const float buoyancyStrength = (std::max)(buoyancySettings.strength, 0.0f);
					const float shapeWidth = (std::max)(std::fabs(volumeInfo.shapeSize.x), 0.05f);
					const float shapeHeight = (std::max)(std::fabs(volumeInfo.shapeSize.y), 0.05f);
					const float shapeLength = (std::max)(std::fabs(volumeInfo.shapeSize.z), 0.05f);
					const float shapeRadius = 0.5f * (std::max)(
						(std::max)(shapeWidth, shapeHeight),
						shapeLength);
					const float forwardProjectedArea = shapeWidth * shapeHeight;
					const float lateralProjectedArea = shapeLength * shapeHeight;
					const float verticalProjectedArea = shapeWidth * shapeLength;
					const float maximumProjectedArea = (std::max)(
						(std::max)(forwardProjectedArea, lateralProjectedArea),
						verticalProjectedArea);
					const float submergedRatio = (std::clamp)(
						volumeInfo.submergedVolume / volumeInfo.totalVolume,
						0.0f,
						1.0f);
					BuoyancyRuntimeState& buoyancyState = buoyancyRuntimeStates_[gameObject.id];
					float enteringVolumeRate = 0.0f;

					if (buoyancyState.hasPreviousSample) {
						enteringVolumeRate = (std::max)(
							(volumeInfo.submergedVolume - buoyancyState.previousSubmergedVolume) /
								safeFixedDeltaTime,
							0.0f);
					}

					buoyancyState.previousSubmergedVolume = volumeInfo.submergedVolume;
					buoyancyState.hasPreviousSample = true;

					if (submergedRatio <= 0.000001f) {
						buoyancyState.hasPreviousRelativeWaterVelocity = false;
						buoyancyState.hasPreviousAngularVelocity = false;
						buoyancyState.hasPreviousHydrostaticOffset = false;
						buoyancyState.filteredRelativeWaterAcceleration = {};
						buoyancyState.filteredAngularAcceleration = {};
						buoyancyState.filteredHydrostaticOffset = {};
						continue;
					}

					// 自動物理は水密度を直接使い、質量と排水体積の釣り合いで喫水を決める。
					// 旧Sceneの手動方式だけは、既存の浮力値から実効密度を逆算して互換性を保つ。
					float effectiveFluidDensity = 0.0f;

					if (buoyancySettings.automaticPhysicalProperties) {
						effectiveFluidDensity = (std::max)(buoyancySettings.waterDensity, 0.0f);
					}
					else if (gravityMagnitude > 0.0001f && volumeInfo.totalVolume > 0.000001f) {
						effectiveFluidDensity =
							safeMass * buoyancyStrength /
							(gravityMagnitude * volumeInfo.totalVolume);
					}

					const Vector3 centerToBuoyancy = Subtract(
						volumeInfo.centerOfBuoyancy,
						volumeInfo.centerOfMass);
					const Vector3 centerOfBuoyancyVelocity = Add(
						rigidBodyComponent->velocity,
						Cross(rigidBodyComponent->angularVelocity, centerToBuoyancy));
					const Vector3 relativeWaterVelocity = Subtract(
						centerOfBuoyancyVelocity,
						averageSurfaceVelocity);
					const float relativeVerticalVelocity = Dot(
						relativeWaterVelocity,
						buoyancyUp);
					Vector3 addedMassForce{};
					// 対角付加質量。Coriolis項でも同じ値を使うためループ外へ出す。
					float surgeAddedMass = 0.0f;
					float swayAddedMass = 0.0f;
					float heaveAddedMass = 0.0f;

					if (buoyancySettings.automaticPhysicalProperties &&
						buoyancyState.hasPreviousRelativeWaterVelocity) {
						const Vector3 measuredRelativeWaterAcceleration = Multiply(
							1.0f / safeFixedDeltaTime,
							Subtract(
								relativeWaterVelocity,
								buoyancyState.previousRelativeWaterVelocity));
						constexpr float kAccelerationFilterTime = 0.08f;
						const float accelerationFilterResponse = 1.0f - std::exp(
							-safeFixedDeltaTime / kAccelerationFilterTime);
						buoyancyState.filteredRelativeWaterAcceleration = Add(
							Multiply(
								1.0f - accelerationFilterResponse,
								buoyancyState.filteredRelativeWaterAcceleration),
							Multiply(
								accelerationFilterResponse,
								measuredRelativeWaterAcceleration));
						const float displacedFluidMass =
							effectiveFluidDensity * volumeInfo.submergedVolume;
						const auto addAxisAddedMassForce = [
							&addedMassForce,
							&buoyancyState,
							displacedFluidMass,
							maximumProjectedArea](
								const Vector3& axis,
								float projectedArea) {
							const float addedMassCoefficient = (std::clamp)(
								projectedArea / (std::max)(maximumProjectedArea, 0.0001f),
								0.1f,
								1.0f);
							const float axisAcceleration = Dot(
								buoyancyState.filteredRelativeWaterAcceleration,
								axis);
							const float axisAddedMass = displacedFluidMass * addedMassCoefficient;
							addedMassForce = Add(
								addedMassForce,
								Multiply(-axisAddedMass * axisAcceleration, axis));
							return axisAddedMass;
						};
						surgeAddedMass = addAxisAddedMassForce(boatForward, forwardProjectedArea);
						swayAddedMass = addAxisAddedMassForce(boatRight, lateralProjectedArea);
						heaveAddedMass = addAxisAddedMassForce(boatUp, verticalProjectedArea);
						const float maximumAddedMassForce = safeMass * gravityMagnitude * 6.0f;
						const float addedMassForceLength = Length(addedMassForce);

						if (addedMassForceLength > maximumAddedMassForce &&
							addedMassForceLength > 0.0001f) {
							addedMassForce = Multiply(
								maximumAddedMassForce / addedMassForceLength,
								addedMassForce);
						}
					}
					else if (!buoyancySettings.automaticPhysicalProperties) {
						buoyancyState.filteredRelativeWaterAcceleration = {};
					}

					buoyancyState.previousRelativeWaterVelocity = relativeWaterVelocity;
					buoyancyState.hasPreviousRelativeWaterVelocity =
						buoyancySettings.automaticPhysicalProperties;

					// 船体が回転すると周囲の水も角加速する。軸ごとの投影面積と排水流体質量から
					// 回転付加慣性を求め、差分Noiseは短い時定数で平滑化する。
					Vector3 rotationalAddedInertiaTorque{};

					if (buoyancySettings.automaticPhysicalProperties &&
						buoyancyState.hasPreviousAngularVelocity) {
						const Vector3 measuredAngularAcceleration = Multiply(
							1.0f / safeFixedDeltaTime,
							Subtract(
								rigidBodyComponent->angularVelocity,
								buoyancyState.previousAngularVelocity));
						constexpr float kAngularAccelerationFilterTime = 0.1f;
						const float angularFilterResponse = 1.0f - std::exp(
							-safeFixedDeltaTime / kAngularAccelerationFilterTime);
						buoyancyState.filteredAngularAcceleration = Add(
							Multiply(
								1.0f - angularFilterResponse,
								buoyancyState.filteredAngularAcceleration),
							Multiply(
								angularFilterResponse,
								measuredAngularAcceleration));
						const float displacedFluidMass =
							effectiveFluidDensity * volumeInfo.submergedVolume;
						const float rollAddedInertiaCoefficient = (std::clamp)(
							forwardProjectedArea / (std::max)(maximumProjectedArea, 0.0001f),
							0.1f,
							1.0f);
						const float pitchAddedInertiaCoefficient = (std::clamp)(
							lateralProjectedArea / (std::max)(maximumProjectedArea, 0.0001f),
							0.1f,
							1.0f);
						const float yawAddedInertiaCoefficient = (std::clamp)(
							verticalProjectedArea / (std::max)(maximumProjectedArea, 0.0001f),
							0.1f,
							1.0f);
						const float rollAddedInertia =
							rollAddedInertiaCoefficient * displacedFluidMass *
							(shapeWidth * shapeWidth + shapeHeight * shapeHeight) / 12.0f;
						const float pitchAddedInertia =
							pitchAddedInertiaCoefficient * displacedFluidMass *
							(shapeLength * shapeLength + shapeHeight * shapeHeight) / 12.0f;
						const float yawAddedInertia =
							yawAddedInertiaCoefficient * displacedFluidMass *
							(shapeWidth * shapeWidth + shapeLength * shapeLength) / 12.0f;
						rotationalAddedInertiaTorque = Add(
							Multiply(
								-rollAddedInertia * Dot(
									buoyancyState.filteredAngularAcceleration,
									boatForward),
								boatForward),
							Multiply(
								-pitchAddedInertia * Dot(
									buoyancyState.filteredAngularAcceleration,
									boatRight),
								boatRight));
						rotationalAddedInertiaTorque = Add(
							rotationalAddedInertiaTorque,
							Multiply(
								-yawAddedInertia * Dot(
									buoyancyState.filteredAngularAcceleration,
									boatUp),
								boatUp));
						const float maximumAddedInertiaTorque =
							safeMass * shapeRadius * gravityMagnitude * 4.0f;
						const float addedInertiaTorqueLength = Length(rotationalAddedInertiaTorque);

						if (addedInertiaTorqueLength > maximumAddedInertiaTorque &&
							addedInertiaTorqueLength > 0.0001f) {
							rotationalAddedInertiaTorque = Multiply(
								maximumAddedInertiaTorque / addedInertiaTorqueLength,
								rotationalAddedInertiaTorque);
						}
					}
					else if (!buoyancySettings.automaticPhysicalProperties) {
						buoyancyState.filteredAngularAcceleration = {};
					}

					buoyancyState.previousAngularVelocity = rigidBodyComponent->angularVelocity;
					buoyancyState.hasPreviousAngularVelocity =
						buoyancySettings.automaticPhysicalProperties;

					// Translational diagonal added-mass Coriolis coupling.
					// Produces Munk-type yaw/pitch/roll moments for anisotropic added mass.
					// This is not a complete 6-DOF added-mass Coriolis matrix.
					//
					// M_A*νdot + C_A(ν)*ν = τ の並進部分について、付加運動量を p_A = A*v とすると
					// 左辺のCoriolis由来Momentは v × p_A。付加質量を上で -m_A*a の外力として
					// 右辺へ移しているので、加えるTorqueは符号を反転した -(v × p_A) になる。
					//
					// 成分式(Mx/My/Mz)を書くと軸対応を誤りやすいため、既存の正規直交船体基底を
					// 使ったWorld空間の1本のベクトル式で求める。
					Vector3 addedMassCoriolisTorque{};

					if (buoyancySettings.automaticPhysicalProperties) {
						const float surgeSpeed = Dot(relativeWaterVelocity, boatForward);
						const float swaySpeed = Dot(relativeWaterVelocity, boatRight);
						const float heaveSpeed = Dot(relativeWaterVelocity, boatUp);
						const Vector3 bodyRelativeVelocity = Add(
							Multiply(surgeSpeed, boatForward),
							Add(
								Multiply(swaySpeed, boatRight),
								Multiply(heaveSpeed, boatUp)));
						const Vector3 addedMomentum = Add(
							Multiply(surgeAddedMass * surgeSpeed, boatForward),
							Add(
								Multiply(swayAddedMass * swaySpeed, boatRight),
								Multiply(heaveAddedMass * heaveSpeed, boatUp)));
						addedMassCoriolisTorque = Multiply(
							-1.0f,
							Cross(bodyRelativeVelocity, addedMomentum));
						// 付加慣性Torqueと同じ基準で頭打ちにする。
						const float maximumCoriolisTorque =
							safeMass * shapeRadius * gravityMagnitude * 4.0f;
						const float coriolisTorqueLength = Length(addedMassCoriolisTorque);

						if (coriolisTorqueLength > maximumCoriolisTorque &&
							coriolisTorqueLength > 0.0001f) {
							addedMassCoriolisTorque = Multiply(
								maximumCoriolisTorque / coriolisTorqueLength,
								addedMassCoriolisTorque);
						}
					}

					// 水面を少し上げた2回目の実Shape切断から dV/dh を求める。
					// これは自由水面における水線面積となり、上下動の復元剛性と臨界減衰を決める。
					const float waterplaneProbeHeight = (std::clamp)(
						shapeHeight * 0.02f,
						0.02f,
						0.25f);
					const Vector3 raisedSurfacePosition = Add(
						fittedSurfacePosition,
						Multiply(waterplaneProbeHeight, buoyancyUp));
					EditorJoltPhysicsManager::SubmergedVolumeInfo raisedVolumeInfo{};
					const bool hasRaisedVolume = joltPhysicsManager_.GetSubmergedVolume(
						gameObject.id,
						raisedSurfacePosition,
						fittedSurfaceNormal,
						raisedVolumeInfo);
					const float maximumWaterplaneArea = (std::max)(
						shapeWidth * shapeLength * 2.0f,
						0.01f);
					float waterplaneArea = 0.0f;

					if (hasRaisedVolume) {
						waterplaneArea = (std::clamp)(
							(raisedVolumeInfo.submergedVolume - volumeInfo.submergedVolume) /
								waterplaneProbeHeight,
							0.0f,
							maximumWaterplaneArea);
					}
					else {
						const float partialSubmersionWeight = (std::clamp)(
							4.0f * submergedRatio * (1.0f - submergedRatio),
							0.0f,
							1.0f);
						waterplaneArea = shapeWidth * shapeLength * partialSubmersionWeight;
					}

					// 静水圧は重力と反対方向へだけ働く。実浮心へ加えるため、傾斜時の復元Momentは
					// Center of BuoyancyとCenter of Massの位置関係から自然にJoltへ発生する。
					const Vector3 hydrostaticForce = Multiply(
						effectiveFluidDensity * volumeInfo.submergedVolume * gravityMagnitude,
						buoyancyUp);
					const float heaveStiffness =
						effectiveFluidDensity * gravityMagnitude * waterplaneArea;
					const float heaveDampingRatio = (std::clamp)(
						buoyancySettings.automaticPhysicalProperties
							? 0.7f
							: buoyancySettings.damping * 0.1f,
						0.0f,
						2.0f);
					const float criticalHeaveDamping =
						2.0f * heaveDampingRatio * std::sqrt((std::max)(
							heaveStiffness * safeMass,
							0.0f));
					const float maximumHeaveDampingForce =
						safeMass * std::fabs(relativeVerticalVelocity) / safeFixedDeltaTime;
					const float heaveDampingForceMagnitude = (std::clamp)(
						-criticalHeaveDamping * relativeVerticalVelocity,
						-maximumHeaveDampingForce,
						maximumHeaveDampingForce);
					const Vector3 heaveDampingForce = Multiply(
						heaveDampingForceMagnitude,
						buoyancyUp);
					Vector3 rotationalRadiationDampingTorque{};

					if (buoyancySettings.automaticPhysicalProperties &&
						volumeInfo.submergedVolume > 0.000001f &&
						waterplaneArea > 0.000001f) {
						const float buoyancyCenterAboveMass = Dot(
							Subtract(volumeInfo.centerOfBuoyancy, volumeInfo.centerOfMass),
							buoyancyUp);
						const float rollWaterplaneMoment =
							waterplaneArea * shapeWidth * shapeWidth / 12.0f;
						const float pitchWaterplaneMoment =
							waterplaneArea * shapeLength * shapeLength / 12.0f;
						const float rollMetacentricHeight = (std::max)(
							rollWaterplaneMoment / volumeInfo.submergedVolume +
								buoyancyCenterAboveMass,
							0.0f);
						const float pitchMetacentricHeight = (std::max)(
							pitchWaterplaneMoment / volumeInfo.submergedVolume +
								buoyancyCenterAboveMass,
							0.0f);
						const float rollHydrostaticStiffness =
							effectiveFluidDensity * gravityMagnitude *
							volumeInfo.submergedVolume * rollMetacentricHeight;
						const float pitchHydrostaticStiffness =
							effectiveFluidDensity * gravityMagnitude *
							volumeInfo.submergedVolume * pitchMetacentricHeight;
						const float rollBodyInertia =
							safeMass * (shapeWidth * shapeWidth + shapeHeight * shapeHeight) / 12.0f;
						const float pitchBodyInertia =
							safeMass * (shapeLength * shapeLength + shapeHeight * shapeHeight) / 12.0f;
						constexpr float kRotationalRadiationDampingRatio = 0.35f;
						const auto calculateRadiationDampingTorque = [
							safeFixedDeltaTime](
								float angularSpeed,
								float bodyInertia,
								float hydrostaticStiffness) {
							const float dampingCoefficient =
								2.0f * kRotationalRadiationDampingRatio * std::sqrt((std::max)(
									bodyInertia * hydrostaticStiffness,
									0.0f));
							const float maximumStoppingTorque =
								bodyInertia * std::fabs(angularSpeed) / safeFixedDeltaTime;
							return (std::clamp)(
								-dampingCoefficient * angularSpeed,
								-maximumStoppingTorque,
								maximumStoppingTorque);
						};
						const float rollDampingTorque = calculateRadiationDampingTorque(
							Dot(rigidBodyComponent->angularVelocity, boatForward),
							rollBodyInertia,
							rollHydrostaticStiffness);
						const float pitchDampingTorque = calculateRadiationDampingTorque(
							Dot(rigidBodyComponent->angularVelocity, boatRight),
							pitchBodyInertia,
							pitchHydrostaticStiffness);
						rotationalRadiationDampingTorque = Add(
							Multiply(rollDampingTorque, boatForward),
							Multiply(pitchDampingTorque, boatRight));
					}

					// 実Shape表面を水面で切り、各水没面の法線へ圧力抗力、接線へ摩擦抗力を加える。
					// 船首の斜面と舷側の平面は法線・水没面積が異なるため、同じ速度でも抵抗が変わる。
					const float forwardSpeed = Dot(relativeWaterVelocity, boatForward);
					const float lateralSpeed = Dot(relativeWaterVelocity, boatRight);
					const float verticalSpeed = Dot(relativeWaterVelocity, boatUp);
					Vector3 hydrodynamicDragForce{};
					Vector3 hydrodynamicDragTorque{};
					// Phase2計測用に圧力と摩擦を分けて集計する。合力は従来どおり
					// hydrodynamicDragForce へ入れるので、物理挙動は変わらない。
					Vector3 diagnosticPressureForce{};
					Vector3 diagnosticSkinFrictionForce{};
					float diagnosticWettedArea = 0.0f;
					Vector3 weightedHydrostaticApplicationPoint{};
					float hydrostaticApplicationWeight = 0.0f;
					const bool hasSurfaceTriangles =
						joltPhysicsManager_.GetHydrodynamicSurfaceTriangles(
							gameObject.id,
							buoyancyState.surfaceTriangles);
					int32_t submergedPanelCount = 0;

					if (hasSurfaceTriangles) {
						const float forwardDragCoefficient = buoyancySettings.automaticPhysicalProperties
							? 1.0f
							: (std::max)(buoyancySettings.waterDrag, 0.0f);
						const float lateralDragCoefficient = buoyancySettings.automaticPhysicalProperties
							? 1.0f
							: (std::max)(buoyancySettings.lateralDrag, 0.0f);
						const float verticalDragCoefficient = buoyancySettings.automaticPhysicalProperties
							? 1.0f
							: (std::max)(buoyancySettings.verticalDrag, 0.0f);
						const float rotationalVelocityScale = buoyancySettings.automaticPhysicalProperties
							? 1.0f
							: (std::max)(buoyancySettings.angularDrag, 0.0f);

						for (const EditorJoltPhysicsManager::HydrodynamicSurfaceTriangle&
							surfaceTriangle : buoyancyState.surfaceTriangles) {
							const std::array<Vector3, 3u> panelVertices = {
								surfaceTriangle.first,
								surfaceTriangle.second,
								surfaceTriangle.third};
							std::array<float, 3u> vertexSurfaceDistances{};

							for (size_t vertexIndex = 0u;
								vertexIndex < panelVertices.size();
								vertexIndex++) {
								EditorOceanSurfaceSample vertexSurfaceSample{};
								interpolateSurfaceSample(
									panelVertices[vertexIndex],
									vertexSurfaceSample);
								vertexSurfaceDistances[vertexIndex] = Dot(
									Subtract(
										panelVertices[vertexIndex],
										vertexSurfaceSample.position),
									buoyancyUp);
							}

							SubmergedSurfacePanel submergedPanel{};

							if (!BuildSubmergedSurfacePanel(
									surfaceTriangle,
									vertexSurfaceDistances,
									submergedPanel)) {
								continue;
							}

							EditorOceanSurfaceSample panelSurfaceSample{};
							interpolateSurfaceSample(
								submergedPanel.center,
								panelSurfaceSample);
							const Vector3 centerToPanel = Subtract(
								submergedPanel.center,
								volumeInfo.centerOfMass);
							const float panelDepth = (std::max)(
								-Dot(
									Subtract(submergedPanel.center, panelSurfaceSample.position),
									buoyancyUp),
								0.0f);
							const float upwardHydrostaticProjection = (std::max)(
								-Dot(submergedPanel.outwardNormal, buoyancyUp),
								0.0f);
							const float panelHydrostaticWeight =
								panelDepth * submergedPanel.area * upwardHydrostaticProjection;

							if (panelHydrostaticWeight > 0.000001f) {
								weightedHydrostaticApplicationPoint = Add(
									weightedHydrostaticApplicationPoint,
									Multiply(panelHydrostaticWeight, submergedPanel.center));
								hydrostaticApplicationWeight += panelHydrostaticWeight;
							}

							const Vector3 rotationalPanelVelocity = Multiply(
								rotationalVelocityScale,
								Cross(rigidBodyComponent->angularVelocity, centerToPanel));
							const Vector3 panelVelocity = Add(
								rigidBodyComponent->velocity,
								rotationalPanelVelocity);
							const Vector3 relativePanelVelocity = Subtract(
								panelVelocity,
								panelSurfaceSample.velocity);
							const float normalVelocity = Dot(
								relativePanelVelocity,
								submergedPanel.outwardNormal);
							const float enteringNormalSpeed = (std::max)(normalVelocity, 0.0f);
							const Vector3 tangentialVelocity = Subtract(
								relativePanelVelocity,
								Multiply(normalVelocity, submergedPanel.outwardNormal));
							const float tangentialSpeed = Length(tangentialVelocity);

							// 相対運動がない水没面は抗力も摩擦も0なので、高価な係数計算を省く。
							// 力は0でも濡れ面ではあるため、濡れ面積の集計だけは行う。
							if (enteringNormalSpeed <= 0.0001f && tangentialSpeed <= 0.0001f) {
								diagnosticWettedArea += submergedPanel.area;
								submergedPanelCount++;
								continue;
							}

							Vector3 pressureForce{};

							if (enteringNormalSpeed > 0.0001f) {
								const float forwardAlignment = std::fabs(Dot(
									submergedPanel.outwardNormal,
									boatForward));
								const float lateralAlignment = std::fabs(Dot(
									submergedPanel.outwardNormal,
									boatRight));
								const float verticalAlignment = std::fabs(Dot(
									submergedPanel.outwardNormal,
									boatUp));
								const float alignmentTotal = (std::max)(
									forwardAlignment + lateralAlignment + verticalAlignment,
									0.0001f);
								const float pressureDragCoefficient =
									(forwardDragCoefficient * forwardAlignment +
										lateralDragCoefficient * lateralAlignment +
										verticalDragCoefficient * verticalAlignment) /
									alignmentTotal;
								const float pressureForceMagnitude =
									0.5f * effectiveFluidDensity * pressureDragCoefficient *
									submergedPanel.area * enteringNormalSpeed * enteringNormalSpeed;
								pressureForce = Multiply(
									-pressureForceMagnitude,
									submergedPanel.outwardNormal);
							}

							Vector3 skinFrictionForce{};

							if (tangentialSpeed > 0.0001f) {
								const float reynoldsNumber = (std::max)(
									tangentialSpeed * shapeLength / kWaterKinematicViscosity,
									1.0f);
								float skinFrictionCoefficient = 0.0f;

								if (reynoldsNumber < 500000.0f) {
									skinFrictionCoefficient = 1.328f / std::sqrt(reynoldsNumber);
								}
								else {
									const float logarithmicTerm = std::log10(reynoldsNumber) - 2.0f;
									skinFrictionCoefficient = 0.075f /
										(logarithmicTerm * logarithmicTerm);
								}

								skinFrictionCoefficient = (std::clamp)(
									skinFrictionCoefficient * forwardDragCoefficient,
									0.0f,
									0.02f);
								const float skinFrictionForceMagnitude =
									0.5f * effectiveFluidDensity * skinFrictionCoefficient *
									submergedPanel.area * tangentialSpeed * tangentialSpeed;
								skinFrictionForce = Multiply(
									-skinFrictionForceMagnitude / tangentialSpeed,
									tangentialVelocity);
							}

							const Vector3 panelForce = Add(pressureForce, skinFrictionForce);
							hydrodynamicDragForce = Add(hydrodynamicDragForce, panelForce);
							hydrodynamicDragTorque = Add(
								hydrodynamicDragTorque,
								Cross(centerToPanel, panelForce));
							diagnosticPressureForce = Add(diagnosticPressureForce, pressureForce);
							diagnosticSkinFrictionForce = Add(
								diagnosticSkinFrictionForce,
								skinFrictionForce);
							diagnosticWettedArea += submergedPanel.area;
							submergedPanelCount++;
						}

						if (submergedPanelCount > 0) {
							const float maximumPanelForce = safeMass * (std::max)(
								Length(relativeWaterVelocity) / safeFixedDeltaTime,
								gravityMagnitude * 8.0f);
							const float maximumPanelTorque = safeMass * shapeRadius * (std::max)(
								Length(rigidBodyComponent->angularVelocity) * shapeRadius /
									safeFixedDeltaTime,
								gravityMagnitude * 8.0f);
							float panelForceScale = 1.0f;
							const float panelForceLength = Length(hydrodynamicDragForce);
							const float panelTorqueLength = Length(hydrodynamicDragTorque);

							if (panelForceLength > maximumPanelForce && panelForceLength > 0.0001f) {
								panelForceScale = (std::min)(
									panelForceScale,
									maximumPanelForce / panelForceLength);
							}

							if (panelTorqueLength > maximumPanelTorque && panelTorqueLength > 0.0001f) {
								panelForceScale = (std::min)(
									panelForceScale,
									maximumPanelTorque / panelTorqueLength);
							}

							hydrodynamicDragForce = Multiply(
								panelForceScale,
								hydrodynamicDragForce);
							hydrodynamicDragTorque = Multiply(
								panelForceScale,
								hydrodynamicDragTorque);
						}
					}
					Vector3 hydrostaticApplicationPoint = volumeInfo.centerOfBuoyancy;

					// 合計浮力はJoltの排水体積から変えず、局所FFT水深を積分した位置へ作用点だけを寄せる。
					// 開いたMeshや反転法線ではWeightが作れないため、Jolt浮心へ安全に戻す。
					if (hydrostaticApplicationWeight > 0.000001f) {
						const Vector3 localHydrostaticApplicationPoint = Multiply(
							1.0f / hydrostaticApplicationWeight,
							weightedHydrostaticApplicationPoint);
						Vector3 localHydrostaticOffset = Subtract(
							localHydrostaticApplicationPoint,
							volumeInfo.centerOfBuoyancy);
						const float maximumHydrostaticOffset = shapeRadius * 0.35f;
						const float localHydrostaticOffsetLength = Length(localHydrostaticOffset);

						if (localHydrostaticOffsetLength > maximumHydrostaticOffset &&
							localHydrostaticOffsetLength > 0.0001f) {
							localHydrostaticOffset = Multiply(
								maximumHydrostaticOffset / localHydrostaticOffsetLength,
								localHydrostaticOffset);
						}

						constexpr float kHydrostaticPointFilterTime = 0.08f;
						const float hydrostaticPointResponse = 1.0f - std::exp(
							-safeFixedDeltaTime / kHydrostaticPointFilterTime);

						if (buoyancyState.hasPreviousHydrostaticOffset) {
							buoyancyState.filteredHydrostaticOffset = Add(
								Multiply(
									1.0f - hydrostaticPointResponse,
									buoyancyState.filteredHydrostaticOffset),
								Multiply(hydrostaticPointResponse, localHydrostaticOffset));
						}
						else {
							buoyancyState.filteredHydrostaticOffset = localHydrostaticOffset;
						}

						buoyancyState.hasPreviousHydrostaticOffset = true;
						hydrostaticApplicationPoint = Add(
							volumeInfo.centerOfBuoyancy,
							buoyancyState.filteredHydrostaticOffset);
					}
					else {
						buoyancyState.filteredHydrostaticOffset = {};
						buoyancyState.hasPreviousHydrostaticOffset = false;
					}

					// Shape面を取得できない場合だけ、外接寸法の軸別投影面積へ戻す。
					if (submergedPanelCount <= 0) {
						const float wettedAreaRatio = std::pow(submergedRatio, 2.0f / 3.0f);
						const auto calculateQuadraticDragForce = [
							effectiveFluidDensity,
							safeMass,
							safeFixedDeltaTime,
							wettedAreaRatio](
								float axisSpeed,
								float dragCoefficient,
								float projectedArea,
								const Vector3& axisDirection) {
							const float maximumStoppingForce =
								safeMass * std::fabs(axisSpeed) / safeFixedDeltaTime;
							const float dragForceMagnitude = (std::clamp)(
								-0.5f * effectiveFluidDensity *
									(std::max)(dragCoefficient, 0.0f) * projectedArea *
									wettedAreaRatio * axisSpeed * std::fabs(axisSpeed),
								-maximumStoppingForce,
								maximumStoppingForce);
							return Multiply(dragForceMagnitude, axisDirection);
						};
						hydrodynamicDragForce = calculateQuadraticDragForce(
							forwardSpeed,
							buoyancySettings.automaticPhysicalProperties
								? 1.0f
								: buoyancySettings.waterDrag,
							shapeWidth * shapeHeight,
							boatForward);
						hydrodynamicDragForce = Add(
							hydrodynamicDragForce,
							calculateQuadraticDragForce(
								lateralSpeed,
								buoyancySettings.automaticPhysicalProperties
									? 1.0f
									: buoyancySettings.lateralDrag,
								shapeLength * shapeHeight,
								boatRight));
						hydrodynamicDragForce = Add(
							hydrodynamicDragForce,
							calculateQuadraticDragForce(
								verticalSpeed,
								buoyancySettings.automaticPhysicalProperties
									? 1.0f
									: buoyancySettings.verticalDrag,
								shapeWidth * shapeLength,
								boatUp));
					}
					Vector3 upwardSurfaceNormal = fittedSurfaceNormal;

					if (Dot(upwardSurfaceNormal, buoyancyUp) < 0.0f) {
						upwardSurfaceNormal = Multiply(-1.0f, upwardSurfaceNormal);
					}

					const float normalInfluence = (std::clamp)(
						buoyancySettings.automaticPhysicalProperties
							? 0.2f
							: buoyancySettings.normalInfluence,
						0.0f,
						1.0f);
					const Vector3 impactDirection = Normalize(Add(
						Multiply(1.0f - normalInfluence, buoyancyUp),
						Multiply(normalInfluence, upwardSurfaceNormal)));
					const float enteringWaterSpeed = (std::max)(
						-relativeVerticalVelocity,
						0.0f);
					const float maximumSlammingForce =
						safeMass * gravityMagnitude * 4.0f;
					const float resolvedSlammingStrength = buoyancySettings.automaticPhysicalProperties
						? 1.0f
						: (std::max)(buoyancySettings.slammingStrength, 0.0f);
					const float slammingForceMagnitude = (std::clamp)(
						effectiveFluidDensity *
							resolvedSlammingStrength *
							enteringVolumeRate * enteringWaterSpeed,
						0.0f,
						maximumSlammingForce);
					const Vector3 slammingForce = Multiply(
						slammingForceMagnitude,
						impactDirection);
					Vector3 waveMakingResistanceForce{};

					// 造波抵抗は面圧力や表面摩擦とは別に、排水量基準面積とFroude数から求める。
					// 船種判定は行わず、同じ式が形状寸法と速度の違いをそのまま反映する。
					if (buoyancySettings.automaticPhysicalProperties &&
						gravityMagnitude > 0.0001f &&
						std::fabs(forwardSpeed) > 0.0001f) {
						const float froudeNumber = std::fabs(forwardSpeed) / std::sqrt(
							gravityMagnitude * shapeLength);
						const float hullFullness = (std::clamp)(
							shapeWidth / shapeLength,
							0.05f,
							1.0f);
						const float displacementReferenceArea = std::pow(
							(std::max)(volumeInfo.submergedVolume, 0.000001f),
							2.0f / 3.0f);
						const float resistanceHump = std::exp(
							-std::pow((froudeNumber - 0.38f) / 0.16f, 2.0f));
						const float planingTransition = (std::clamp)(
							(froudeNumber - 0.25f) / 0.75f,
							0.0f,
							1.0f);
						const float waveResistanceCoefficient = hullFullness *
							(0.002f + 0.010f * resistanceHump + 0.004f * planingTransition);
						const float unclampedWaveResistance =
							0.5f * effectiveFluidDensity * waveResistanceCoefficient *
							displacementReferenceArea * forwardSpeed * forwardSpeed;
						const float maximumStoppingForce =
							safeMass * std::fabs(forwardSpeed) / safeFixedDeltaTime;
						const float maximumWaveResistance = (std::min)(
							maximumStoppingForce,
							safeMass * gravityMagnitude * 2.0f);
						const float waveResistanceMagnitude = (std::min)(
							unclampedWaveResistance,
							maximumWaveResistance);
						waveMakingResistanceForce = Multiply(
							-forwardSpeed / std::fabs(forwardSpeed) * waveResistanceMagnitude,
							boatForward);
					}

					// Runtime診断値を書き出す。Planing不足の原因が係数・濡れ面・圧力方向の
					// どれかを切り分けるため、圧力抗力の鉛直上向き成分を船体重量と並べて残す。
					if (physicsObject.buoyancy != nullptr) {
						EditorComponent& buoyancyDiagnostics = *physicsObject.buoyancy;
						buoyancyDiagnostics.buoyancyDebugBuoyancyForce = Length(hydrostaticForce);
						buoyancyDiagnostics.buoyancyDebugPressureDragForce =
							Length(diagnosticPressureForce);
						buoyancyDiagnostics.buoyancyDebugPressureUpwardForce = Dot(
							diagnosticPressureForce,
							buoyancyUp);
						buoyancyDiagnostics.buoyancyDebugSkinFrictionForce =
							Length(diagnosticSkinFrictionForce);
						buoyancyDiagnostics.buoyancyDebugAddedMassForce = Length(addedMassForce);
						buoyancyDiagnostics.buoyancyDebugSlammingForce = Length(slammingForce);
						buoyancyDiagnostics.buoyancyDebugWaveMakingResistance =
							Length(waveMakingResistanceForce);
						buoyancyDiagnostics.buoyancyDebugSubmergedRatio = submergedRatio;
						buoyancyDiagnostics.buoyancyDebugWettedArea = diagnosticWettedArea;
						buoyancyDiagnostics.buoyancyDebugForwardSpeed = forwardSpeed;
						buoyancyDiagnostics.buoyancyDebugWeightForce = safeMass * gravityMagnitude;
						buoyancyDiagnostics.buoyancyDebugAddedMassCoriolisTorque =
							addedMassCoriolisTorque;
						// 船首方向と水に対する進行方向の偏角。Coriolis Momentが偏角を増やす
						// (不安定化する)向きに働いているかを実測で確かめるために出す。
						buoyancyDiagnostics.buoyancyDebugSideslipAngleDegrees = std::atan2(
							Dot(relativeWaterVelocity, boatRight),
							Dot(relativeWaterVelocity, boatForward)) * 180.0f /
							3.14159265358979323846f;
						// 船首の上下角。boatForwardの鉛直成分から求め、正を船首上げとする。
						buoyancyDiagnostics.buoyancyDebugTrimAngleDegrees =
							std::asin((std::clamp)(Dot(boatForward, buoyancyUp), -1.0f, 1.0f)) *
							180.0f / 3.14159265358979323846f;
					}

					const bool usesSurfacePanels = submergedPanelCount > 0;
					const Vector3 centerAppliedDragForce = usesSurfacePanels
						? Vector3{}
						: hydrodynamicDragForce;
					const Vector3 dynamicCenterForce = Add(
						heaveDampingForce,
						Add(
							addedMassForce,
							Add(
								waveMakingResistanceForce,
								Add(centerAppliedDragForce, slammingForce))));
					AddForceAtPosition(
						gameObject.id,
						hydrostaticForce,
						hydrostaticApplicationPoint);
					AddForceAtPosition(
						gameObject.id,
						dynamicCenterForce,
						volumeInfo.centerOfBuoyancy);
					AddTorque(
						gameObject.id,
						Add(
							rotationalRadiationDampingTorque,
							Add(rotationalAddedInertiaTorque, addedMassCoriolisTorque)));

					if (usesSurfacePanels) {
						AddForce(gameObject.id, hydrodynamicDragForce);
						AddTorque(gameObject.id, hydrodynamicDragTorque);
					}
					else {
						// 面情報を得られないShapeだけ、外接寸法から回転抵抗を近似する。
						const float angularDrag = buoyancySettings.automaticPhysicalProperties
							? 1.0f
							: (std::max)(buoyancySettings.angularDrag, 0.0f);
						const float rollRadiusSquared =
							0.25f * (shapeWidth * shapeWidth + shapeHeight * shapeHeight);
						const float pitchRadiusSquared =
							0.25f * (shapeLength * shapeLength + shapeHeight * shapeHeight);
						const float yawRadiusSquared =
							0.25f * (shapeWidth * shapeWidth + shapeLength * shapeLength);
						const float rollAngularSpeed = Dot(
							rigidBodyComponent->angularVelocity,
							boatForward);
						const float pitchAngularSpeed = Dot(
							rigidBodyComponent->angularVelocity,
							boatRight);
						const float yawAngularSpeed = Dot(
							rigidBodyComponent->angularVelocity,
							boatUp);
						const float angularDragScale = safeMass * angularDrag * submergedRatio;
						Vector3 waterAngularDragTorque = Multiply(
							-angularDragScale * rollRadiusSquared * rollAngularSpeed,
							boatForward);
						waterAngularDragTorque = Add(
							waterAngularDragTorque,
							Multiply(
								-angularDragScale * pitchRadiusSquared * pitchAngularSpeed,
								boatRight));
						waterAngularDragTorque = Add(
							waterAngularDragTorque,
							Multiply(
								-angularDragScale * yawRadiusSquared * yawAngularSpeed,
								boatUp));
						AddTorque(gameObject.id, waterAngularDragTorque);
					}
					continue;
				}
			}
		}

		// 実 Shape の体積取得に対応しない特殊 Shape だけ、従来グリッドを安全策として使う。
		const BuoyancyGridDimensions gridDimensions = BuildBuoyancyGridDimensions(
			safeHullSize,
			worldScale);
		float resolvedBodyMass = rigidBodyComponent->mass;
		joltPhysicsManager_.GetBodyMass(gameObject.id, resolvedBodyMass);
		const float safeMass = (std::max)(resolvedBodyMass, 0.01f);
		const float worldHullWidth = (std::max)(
			std::fabs(safeHullSize.x * worldScale.x),
			0.05f);
		const float worldHullHeight = (std::max)(
			std::fabs(safeHullSize.y * worldScale.y),
			0.05f);
		const float worldHullLength = (std::max)(
			std::fabs(safeHullSize.z * worldScale.z),
			0.05f);
		const float approximateHullVolume =
			worldHullWidth * worldHullHeight * worldHullLength;
		const float automaticBuoyancyStrength =
			(std::max)(buoyancySettings.waterDensity, 0.0f) *
			approximateHullVolume * gravityMagnitude / safeMass;
		const float buoyancyStrength = buoyancySettings.automaticPhysicalProperties
			? automaticBuoyancyStrength
			: (std::max)(buoyancySettings.strength, 0.0f);
		const float automaticBuoyancyDamping =
			1.4f * std::sqrt((std::max)(buoyancyStrength / worldHullHeight, 0.0f));
		const float buoyancyDamping = buoyancySettings.automaticPhysicalProperties
			? automaticBuoyancyDamping
			: (std::max)(buoyancySettings.damping, 0.0f);

		// B: Collider 自動の浮力物体は、モデル全体の AABB 中心を基準にすると
		// マストなどの上部構造まで縦グリッドが広がり、転覆モーメントの原因になる。
		// 縦範囲を「船底（AABB 下端）〜平衡喫水 + 波の余裕」へ制限して配置し直す。
		Vector3 limitedHullSize = safeHullSize;
		if (buoyancySettings.limitDraftHeight) {
			const float equilibriumSubmersionRatio = (std::clamp)(
				gravityMagnitude / (std::max)(buoyancyStrength, 0.01f),
				0.2f,
				0.9f);
			const float requestedDraftHeight = std::min(
				safeHullSize.y * equilibriumSubmersionRatio * kBuoyancyDraftOverheadRatio,
				safeHullSize.y);
			limitedHullSize.y = (std::max)(requestedDraftHeight, kBuoyancyDraftMinimumHeight);
		}
		const float verticalGridHeight = limitedHullSize.y;
		const BuoyancyGridDimensions limitedGridDimensions = (buoyancySettings.limitDraftHeight)
			? BuildBuoyancyGridDimensions(limitedHullSize, worldScale)
			: gridDimensions;

		const int32_t floatPointCount =
			limitedGridDimensions.countX * limitedGridDimensions.countY * limitedGridDimensions.countZ;
		const Vector3 localCellSize{
			safeHullSize.x / static_cast<float>(limitedGridDimensions.countX),
			verticalGridHeight / static_cast<float>(limitedGridDimensions.countY),
			safeHullSize.z / static_cast<float>(limitedGridDimensions.countZ)};
		const Vector3 localCellHalfSize = Multiply(0.5f, localCellSize);
		const float modelBottomY =
			buoyancySettings.centerOffset.y - safeHullSize.y * 0.5f;
		const float gridCenterY = buoyancySettings.limitDraftHeight
			? modelBottomY + verticalGridHeight * 0.5f
			: buoyancySettings.centerOffset.y;
		const Vector3 localHullMinimum{
			buoyancySettings.centerOffset.x - safeHullSize.x * 0.5f,
			gridCenterY - verticalGridHeight * 0.5f,
			buoyancySettings.centerOffset.z - safeHullSize.z * 0.5f};
		const float pointMass = safeMass / static_cast<float>(floatPointCount);
		const Matrix4x4 boatWorldMatrix = editorScene_->GetWorldMatrix(gameObject.id);
		const Matrix4x4 boatRotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			worldRotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 boatRight = Normalize(Transform({1.0f, 0.0f, 0.0f}, boatRotationMatrix));
		const Vector3 boatUp = Normalize(Transform({0.0f, 1.0f, 0.0f}, boatRotationMatrix));
		const Vector3 boatForward = Normalize(Transform({0.0f, 0.0f, 1.0f}, boatRotationMatrix));
		const float forwardDrag = buoyancySettings.automaticPhysicalProperties
			? 1.0f
			: (std::max)(buoyancySettings.waterDrag, 0.0f);
		const float lateralDrag = buoyancySettings.automaticPhysicalProperties
			? 1.0f
			: (std::max)(buoyancySettings.lateralDrag, 0.0f);
		const float verticalDrag = buoyancySettings.automaticPhysicalProperties
			? 1.0f
			: (std::max)(buoyancySettings.verticalDrag, 0.0f);
		const float normalInfluence = (std::clamp)(
			buoyancySettings.automaticPhysicalProperties
				? 0.2f
				: buoyancySettings.normalInfluence,
			0.0f,
			1.0f);
		const float slammingStrength = buoyancySettings.automaticPhysicalProperties
			? 1.0f
			: (std::max)(buoyancySettings.slammingStrength, 0.0f);
		float totalSubmersionRatio = 0.0f;
		int32_t submergedPointCount = 0;
		int32_t resolvedOceanGameObjectId =
			buoyancySettings.oceanGameObjectId;

		for (int32_t gridIndexZ = 0; gridIndexZ < limitedGridDimensions.countZ; gridIndexZ++) {
			for (int32_t gridIndexX = 0; gridIndexX < limitedGridDimensions.countX; gridIndexX++) {
				const Vector3 localColumnCenter{
					localHullMinimum.x +
						(static_cast<float>(gridIndexX) + 0.5f) * localCellSize.x,
					gridCenterY,
					localHullMinimum.z +
						(static_cast<float>(gridIndexZ) + 0.5f) * localCellSize.z};
				const Vector3 worldColumnCenter = Transform(localColumnCenter, boatWorldMatrix);
				const int32_t columnIndex =
					gridIndexZ * limitedGridDimensions.countX + gridIndexX;
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

				for (int32_t gridIndexY = 0; gridIndexY < limitedGridDimensions.countY; gridIndexY++) {
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
					const Vector3 centerToPoint = Subtract(worldForcePoint, worldPosition);
					const Vector3 pointAngularVelocity = Cross(
						rigidBodyComponent->angularVelocity,
						centerToPoint);
					const Vector3 pointVelocity = Add(
						rigidBodyComponent->velocity,
						pointAngularVelocity);
					const Vector3 relativePointVelocity = Subtract(
						pointVelocity,
						surfaceSample.velocity);
					const float relativeVerticalVelocity = Dot(
						relativePointVelocity,
						buoyancyUp);
					const float pointAcceleration = (std::clamp)(
						buoyancyStrength * submersionRatio -
							relativeVerticalVelocity * buoyancyDamping * submersionRatio,
						0.0f,
						(std::max)(buoyancyStrength * 2.0f, gravityMagnitude * 4.0f));
					Vector3 waterNormal = surfaceSample.normal;

					if (Dot(waterNormal, buoyancyUp) < 0.0f) {
						waterNormal = Multiply(-1.0f, waterNormal);
					}

					const Vector3 buoyancyDirection = Normalize(Add(
						Multiply(1.0f - normalInfluence, buoyancyUp),
						Multiply(normalInfluence, waterNormal)));
					const float enteringWaterSpeed = (std::max)(-relativeVerticalVelocity, 0.0f);
					const float slammingAcceleration = (std::min)(
						enteringWaterSpeed * enteringWaterSpeed * slammingStrength *
							(1.0f - submersionRatio),
						gravityMagnitude * 4.0f);
					const Vector3 buoyancyForce = Multiply(
						pointMass * (pointAcceleration + slammingAcceleration),
						buoyancyDirection);

					// 船体の前後・横・上下を別係数で減衰し、前進を残しながら横滑りと着水を抑える。
					const float forwardSpeed = Dot(relativePointVelocity, boatForward);
					const float lateralSpeed = Dot(relativePointVelocity, boatRight);
					const float verticalSpeed = Dot(relativePointVelocity, boatUp);
					Vector3 dragAcceleration = Add(
						Multiply(-forwardSpeed * forwardDrag * (1.0f + std::fabs(forwardSpeed) * 0.05f), boatForward),
						Multiply(-lateralSpeed * lateralDrag * (1.0f + std::fabs(lateralSpeed) * 0.05f), boatRight));
					dragAcceleration = Add(
						dragAcceleration,
						Multiply(-verticalSpeed * verticalDrag * (1.0f + std::fabs(verticalSpeed) * 0.05f), boatUp));
					const float dragAccelerationLength = Length(dragAcceleration);
					const float maximumDragAcceleration = (std::max)(gravityMagnitude * 6.0f, 20.0f);

					if (dragAccelerationLength > maximumDragAcceleration) {
						dragAcceleration = Multiply(
							maximumDragAcceleration / dragAccelerationLength,
							dragAcceleration);
					}

					const Vector3 hydrodynamicDragForce = Multiply(
						pointMass * submersionRatio,
						dragAcceleration);
					AddForceAtPosition(
						gameObject.id,
						Add(buoyancyForce, hydrodynamicDragForce),
						worldForcePoint);
					totalSubmersionRatio += submersionRatio;
					submergedPointCount++;
				}
			}
		}

		if (submergedPointCount <= 0) {
			continue;
		}

		const float averageSubmersionRatio =
			totalSubmersionRatio / static_cast<float>(floatPointCount);
		const float angularDrag = buoyancySettings.automaticPhysicalProperties
			? 1.0f
			: (std::max)(buoyancySettings.angularDrag, 0.0f);
		const Vector3 waterAngularDragTorque = Multiply(
			-safeMass * angularDrag * averageSubmersionRatio,
			rigidBodyComponent->angularVelocity);

		AddTorque(gameObject.id, waterAngularDragTorque);
	}
}
