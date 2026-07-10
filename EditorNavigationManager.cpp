#include "EditorNavigationManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int32_t kNavigationInvalidGameObjectId = -1;  // Scene 内で未設定 ID を表す値。
	constexpr float kNavigationMinimumDistance = 0.0001f;  // 0 除算を避けるための最小距離。
	constexpr float kNavigationFallbackSurfaceHalfSize = 50.0f;  // Surface 未配置時に使う仮平面の半径。

	float AbsFloat(float value) {
		return std::fabs(value);
	}

	float ClampFloat(float value, float minValue, float maxValue) {
		return (std::max)(minValue, (std::min)(value, maxValue));
	}

	float LengthSquaredXZ(const Vector3& vector) {
		return vector.x * vector.x + vector.z * vector.z;
	}

	float LengthXZ(const Vector3& vector) {
		return std::sqrt(LengthSquaredXZ(vector));
	}

	Vector3 MakeHorizontalVector(const Vector3& from, const Vector3& to) {
		return {to.x - from.x, 0.0f, to.z - from.z};
	}

	Vector3 NormalizeXZ(const Vector3& vector) {
		const float length = LengthXZ(vector);  // XZ 平面上の向きだけを正規化する。
		if (length <= kNavigationMinimumDistance) {
			return {0.0f, 0.0f, 0.0f};
		}

		return {vector.x / length, 0.0f, vector.z / length};
	}

	Vector3 AddXZ(const Vector3& first, const Vector3& second) {
		return {first.x + second.x, first.y, first.z + second.z};
	}

	Vector3 MultiplyXZ(const Vector3& vector, float scalar) {
		return {vector.x * scalar, 0.0f, vector.z * scalar};
	}

	Vector3 MoveTowardsVelocity(const Vector3& currentVelocity, const Vector3& targetVelocity, float maxDelta) {
		Vector3 difference = {
			targetVelocity.x - currentVelocity.x,
			0.0f,
			targetVelocity.z - currentVelocity.z};
		const float distance = LengthXZ(difference);
		if (distance <= maxDelta || distance <= kNavigationMinimumDistance) {
			return targetVelocity;
		}

		Vector3 direction = NormalizeXZ(difference);  // 最大加速度で速度差を詰める向き。
		return {
			currentVelocity.x + direction.x * maxDelta,
			0.0f,
			currentVelocity.z + direction.z * maxDelta};
	}

	bool ContainsXZ(float minX, float maxX, float minZ, float maxZ, const Vector3& position) {
		return
			position.x >= minX &&
			position.x <= maxX &&
			position.z >= minZ &&
			position.z <= maxZ;
	}

	float MakeScaledSize(float localSize, float scale, float fallbackSize) {
		const float scaledSize = AbsFloat(localSize * scale);  // Component のサイズと Transform Scale を掛けた実寸。
		if (scaledSize <= kNavigationMinimumDistance) {
			return fallbackSize;
		}

		return scaledSize;
	}

	Vector3 RotateY(const Vector3& vector, float rotationY) {
		const float cosValue = std::cos(rotationY);  // 障害物 Box を GameObject の Y 回転へ合わせる。
		const float sinValue = std::sin(rotationY);
		return {
			vector.x * cosValue + vector.z * sinValue,
			vector.y,
			-vector.x * sinValue + vector.z * cosValue};
	}

	Vector3 InverseRotateY(const Vector3& vector, float rotationY) {
		return RotateY(vector, -rotationY);
	}

	Vector3 MakeWorldCenter(const EditorGameObject& gameObject, const Vector3& localCenter) {
		const Vector3 scaledCenter = {
			localCenter.x * gameObject.scale.x,
			localCenter.y * gameObject.scale.y,
			localCenter.z * gameObject.scale.z};
		const Vector3 rotatedCenter = RotateY(scaledCenter, gameObject.rotate.y);
		return {
			gameObject.translate.x + rotatedCenter.x,
			gameObject.translate.y + rotatedCenter.y,
			gameObject.translate.z + rotatedCenter.z};
	}

	Vector3 MakeHalfSize(const Vector3& localSize, const Vector3& scale, float fallbackSize) {
		return {
			MakeScaledSize(localSize.x, scale.x, fallbackSize) * 0.5f,
			MakeScaledSize(localSize.y, scale.y, fallbackSize) * 0.5f,
			MakeScaledSize(localSize.z, scale.z, fallbackSize) * 0.5f};
	}

	bool TryGetModelAssetPath(const EditorGameObject& gameObject, std::string& assetPath) {
		const EditorComponent* meshCollider = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshCollider);
		if (meshCollider != nullptr && meshCollider->isActive && !meshCollider->assetPath.empty()) {
			assetPath = meshCollider->assetPath;
			return true;
		}

		const EditorComponent* meshFilter = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
		if (meshFilter != nullptr && meshFilter->isActive && !meshFilter->assetPath.empty()) {
			assetPath = meshFilter->assetPath;
			return true;
		}

		const EditorComponent* modelRenderer = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && modelRenderer->isActive && !modelRenderer->assetPath.empty()) {
			assetPath = modelRenderer->assetPath;
			return true;
		}

		return false;
	}

	bool TryCalculateModelBounds(const std::string& assetPath, Vector3& localCenter, Vector3& localSize) {
		ModelData modelData{};
		if (!EditorAssetUtility::LoadModelAsset(assetPath, modelData) || modelData.vertices.empty()) {
			return false;
		}

		Vector3 minPosition = {
			modelData.vertices[0].position.x,
			modelData.vertices[0].position.y,
			modelData.vertices[0].position.z};
		Vector3 maxPosition = minPosition;
		for (const VertexData& vertex : modelData.vertices) {
			const Vector3 position = {
				vertex.position.x,
				vertex.position.y,
				vertex.position.z};
			minPosition.x = (std::min)(minPosition.x, position.x);
			minPosition.y = (std::min)(minPosition.y, position.y);
			minPosition.z = (std::min)(minPosition.z, position.z);
			maxPosition.x = (std::max)(maxPosition.x, position.x);
			maxPosition.y = (std::max)(maxPosition.y, position.y);
			maxPosition.z = (std::max)(maxPosition.z, position.z);
		}

		localCenter = {
			(minPosition.x + maxPosition.x) * 0.5f,
			(minPosition.y + maxPosition.y) * 0.5f,
			(minPosition.z + maxPosition.z) * 0.5f};
		localSize = {
			maxPosition.x - minPosition.x,
			maxPosition.y - minPosition.y,
			maxPosition.z - minPosition.z};
		return
			localSize.x > kNavigationMinimumDistance ||
			localSize.z > kNavigationMinimumDistance;
	}

	bool TryMakeMeshObstacle(const EditorGameObject& gameObject, EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		std::string assetPath;
		if (!TryGetModelAssetPath(gameObject, assetPath)) {
			return false;
		}

		Vector3 localCenter{};
		Vector3 localSize{};
		if (!TryCalculateModelBounds(assetPath, localCenter, localSize)) {
			return false;
		}

		navigationObstacle.center = MakeWorldCenter(gameObject, localCenter);
		navigationObstacle.halfSize = MakeHalfSize(localSize, gameObject.scale, 1.0f);
		navigationObstacle.radius = (std::max)(navigationObstacle.halfSize.x, navigationObstacle.halfSize.z);
		navigationObstacle.rotationY = gameObject.rotate.y;
		navigationObstacle.shape = EditorNavigationManager::NavigationObstacleShape::Box;
		return true;
	}

	void ApplyBoxObstacleFromComponent(
		const EditorGameObject& gameObject,
		const EditorComponent& component,
		EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		navigationObstacle.center = MakeWorldCenter(gameObject, component.colliderCenter);
		navigationObstacle.halfSize = MakeHalfSize(component.colliderSize, gameObject.scale, 1.0f);
		navigationObstacle.radius = (std::max)(navigationObstacle.halfSize.x, navigationObstacle.halfSize.z);
		navigationObstacle.rotationY = gameObject.rotate.y;
		navigationObstacle.shape = EditorNavigationManager::NavigationObstacleShape::Box;
	}

	void ApplyCircleObstacleFromComponent(
		const EditorGameObject& gameObject,
		const EditorComponent& component,
		EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		const float maxScale = (std::max)(AbsFloat(gameObject.scale.x), AbsFloat(gameObject.scale.z));
		navigationObstacle.center = MakeWorldCenter(gameObject, component.colliderCenter);
		navigationObstacle.halfSize = {0.0f, 0.0f, 0.0f};
		navigationObstacle.radius = (std::max)(component.colliderRadius * maxScale, 0.01f);
		navigationObstacle.rotationY = gameObject.rotate.y;
		navigationObstacle.shape = EditorNavigationManager::NavigationObstacleShape::Circle;
	}

	bool TryBuildObstacleFromCollider(const EditorGameObject& gameObject, EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		const EditorComponent* meshCollider = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshCollider);
		if (meshCollider != nullptr && meshCollider->isActive) {
			if (TryMakeMeshObstacle(gameObject, navigationObstacle)) {
				return true;
			}

			ApplyBoxObstacleFromComponent(gameObject, *meshCollider, navigationObstacle);
			return true;
		}

		const EditorComponent* boxCollider = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::BoxCollider);
		if (boxCollider != nullptr && boxCollider->isActive) {
			ApplyBoxObstacleFromComponent(gameObject, *boxCollider, navigationObstacle);
			return true;
		}

		const EditorComponent* sphereCollider = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SphereCollider);
		if (sphereCollider != nullptr && sphereCollider->isActive) {
			ApplyCircleObstacleFromComponent(gameObject, *sphereCollider, navigationObstacle);
			return true;
		}

		const EditorComponent* capsuleCollider = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CapsuleCollider);
		if (capsuleCollider != nullptr && capsuleCollider->isActive) {
			ApplyCircleObstacleFromComponent(gameObject, *capsuleCollider, navigationObstacle);
			return true;
		}

		return false;
	}

	EditorNavigationManager::NavigationObstacle MakeObstacleBase(const EditorGameObject& gameObject, bool canCarve) {
		EditorNavigationManager::NavigationObstacle navigationObstacle{};
		navigationObstacle.gameObjectId = gameObject.id;
		navigationObstacle.center = gameObject.translate;
		navigationObstacle.canCarve = canCarve;
		navigationObstacle.rotationY = gameObject.rotate.y;
		return navigationObstacle;
	}

	bool TryBuildNavigationObstacle(
		const EditorGameObject& gameObject,
		const EditorComponent& navMeshObstacle,
		EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		navigationObstacle = MakeObstacleBase(gameObject, navMeshObstacle.navCarve);
		if (TryBuildObstacleFromCollider(gameObject, navigationObstacle)) {
			navigationObstacle.canCarve = navMeshObstacle.navCarve;
			return true;
		}

		if (TryMakeMeshObstacle(gameObject, navigationObstacle)) {
			navigationObstacle.canCarve = navMeshObstacle.navCarve;
			return true;
		}

		if (navMeshObstacle.colliderSize.x > kNavigationMinimumDistance ||
			navMeshObstacle.colliderSize.z > kNavigationMinimumDistance) {
			ApplyBoxObstacleFromComponent(gameObject, navMeshObstacle, navigationObstacle);
			navigationObstacle.canCarve = navMeshObstacle.navCarve;
			return true;
		}

		ApplyCircleObstacleFromComponent(gameObject, navMeshObstacle, navigationObstacle);
		navigationObstacle.canCarve = navMeshObstacle.navCarve;
		return true;
	}

	bool TryBuildImplicitMeshObstacle(
		const EditorGameObject& gameObject,
		EditorNavigationManager::NavigationObstacle& navigationObstacle) {
		const EditorComponent* surface = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshSurface);
		if (surface != nullptr && surface->isActive) {
			return false;
		}

		navigationObstacle = MakeObstacleBase(gameObject, true);
		return TryBuildObstacleFromCollider(gameObject, navigationObstacle);
	}
}

void EditorNavigationManager::Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play 中に直接動かす Scene。
	physicsManager_ = physicsManager;  // Rigidbody 付き Agent の速度反映先。
	consoleMessages_ = consoleMessages;  // Navigation の状態ログ出力先。
}

void EditorNavigationManager::Start() {
	agentVelocities_.clear();
	agentDestinations_.clear();
	isStarted_ = true;
	BuildNavigationData(true);
}

void EditorNavigationManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	BuildNavigationData(false);

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* agent = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavigationAgent);
		if (agent == nullptr || !agent->isActive) {
			continue;
		}

		UpdateAgent(gameObject, *agent, deltaTime);
	}
}

void EditorNavigationManager::Draw() {
	// NavMesh / 経路 / Link の線描画は RenderManager 側に DebugLine が入った後で接続する。
}

void EditorNavigationManager::Stop() {
	surfaces_.clear();
	obstacles_.clear();
	modifierVolumes_.clear();
	links_.clear();
	agentVelocities_.clear();
	agentDestinations_.clear();
	isStarted_ = false;
}

void EditorNavigationManager::BuildNavigationData(bool shouldLog) {
	surfaces_.clear();
	obstacles_.clear();
	modifierVolumes_.clear();
	links_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* modifier = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshModifier);
		const bool shouldIgnoreSurface =
			modifier != nullptr &&
			modifier->isActive &&
			modifier->navIgnoreFromBuild;
		const int32_t surfaceArea =
			modifier != nullptr && modifier->isActive && modifier->navAreaOverride ? modifier->navArea : 0;

		const EditorComponent* surface = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshSurface);
		if (surface != nullptr && surface->isActive && !shouldIgnoreSurface) {
			const Vector3 center = {
				gameObject.translate.x + surface->colliderCenter.x * gameObject.scale.x,
				gameObject.translate.y + surface->colliderCenter.y * gameObject.scale.y,
				gameObject.translate.z + surface->colliderCenter.z * gameObject.scale.z};
			const float width = MakeScaledSize(surface->colliderSize.x, gameObject.scale.x, 20.0f);
			const float depth = MakeScaledSize(surface->colliderSize.z, gameObject.scale.z, 20.0f);
			surfaces_.push_back(NavigationSurface{
				gameObject.id,
				center.x - width * 0.5f,
				center.x + width * 0.5f,
				center.z - depth * 0.5f,
				center.z + depth * 0.5f,
				surfaceArea});
		}

		const EditorComponent* obstacle = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshObstacle);
		if (obstacle != nullptr && obstacle->isActive) {
			NavigationObstacle navigationObstacle{};
			if (TryBuildNavigationObstacle(gameObject, *obstacle, navigationObstacle)) {
				obstacles_.push_back(navigationObstacle);
			}
		}
		else {
			NavigationObstacle navigationObstacle{};
			if (TryBuildImplicitMeshObstacle(gameObject, navigationObstacle)) {
				obstacles_.push_back(navigationObstacle);
			}
		}

		const EditorComponent* volume = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshModifierVolume);
		if (volume != nullptr && volume->isActive) {
			const Vector3 center = {
				gameObject.translate.x + volume->colliderCenter.x * gameObject.scale.x,
				gameObject.translate.y + volume->colliderCenter.y * gameObject.scale.y,
				gameObject.translate.z + volume->colliderCenter.z * gameObject.scale.z};
			const float width = MakeScaledSize(volume->colliderSize.x, gameObject.scale.x, 1.0f);
			const float depth = MakeScaledSize(volume->colliderSize.z, gameObject.scale.z, 1.0f);
			modifierVolumes_.push_back(NavigationModifierVolume{
				gameObject.id,
				center.x - width * 0.5f,
				center.x + width * 0.5f,
				center.z - depth * 0.5f,
				center.z + depth * 0.5f,
				volume->navArea});
		}

		const EditorComponent* link = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::NavMeshLink);
		if (link != nullptr && link->isActive && link->connectedGameObjectId != kNavigationInvalidGameObjectId) {
			const EditorGameObject* targetGameObject = editorScene_->FindGameObject(link->connectedGameObjectId);
			if (targetGameObject != nullptr && targetGameObject->isActive) {
				links_.push_back(NavigationLink{
					gameObject.id,
					targetGameObject->id,
					gameObject.translate,
					targetGameObject->translate,
					(std::max)(link->colliderRadius, 0.01f),
					(std::max)(link->navCostModifier, 0.01f),
					link->navBidirectional});
			}
		}
	}

	if (surfaces_.empty()) {
		surfaces_.push_back(NavigationSurface{
			kNavigationInvalidGameObjectId,
			-kNavigationFallbackSurfaceHalfSize,
			kNavigationFallbackSurfaceHalfSize,
			-kNavigationFallbackSurfaceHalfSize,
			kNavigationFallbackSurfaceHalfSize,
			0});

		if (shouldLog) {
			PushConsoleMessage("ナビゲーション: NavMesh サーフェスが無いため、原点中心の仮平面を使用します。");
		}
	}

	if (shouldLog) {
		PushConsoleMessage(
			"ナビゲーション: Surface " + std::to_string(surfaces_.size()) +
			" / 障害物 " + std::to_string(obstacles_.size()) +
			" / Volume " + std::to_string(modifierVolumes_.size()) +
			" / Link " + std::to_string(links_.size()) +
			" を読み込みました。");
	}
}

void EditorNavigationManager::UpdateAgent(EditorGameObject& gameObject, EditorComponent& agent, float deltaTime) {
	if (agent.connectedGameObjectId == kNavigationInvalidGameObjectId) {
		agentVelocities_[gameObject.id] = {0.0f, 0.0f, 0.0f};
		return;
	}

	const EditorGameObject* targetGameObject = editorScene_->FindGameObject(agent.connectedGameObjectId);
	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		agentVelocities_[gameObject.id] = {0.0f, 0.0f, 0.0f};
		return;
	}

	Vector3 targetPosition = targetGameObject->translate;  // Agent が向かう最終目的地。
	if (!agent.navAutoRepath) {
		if (agentDestinations_.find(gameObject.id) == agentDestinations_.end()) {
			agentDestinations_[gameObject.id] = targetPosition;
		}

		targetPosition = agentDestinations_[gameObject.id];
	}
	else {
		agentDestinations_[gameObject.id] = targetPosition;
	}

	TryClampToSurface(targetPosition, agent.navAgentRadius);

	if (TryApplyLink(gameObject, agent, targetPosition)) {
		agentVelocities_[gameObject.id] = {0.0f, 0.0f, 0.0f};
		return;
	}

	const Vector3 toTarget = MakeHorizontalVector(gameObject.translate, targetPosition);
	const float distanceToTarget = LengthXZ(toTarget);
	if (distanceToTarget <= (std::max)(agent.navStoppingDistance, 0.0f)) {
		agentVelocities_[gameObject.id] = {0.0f, 0.0f, 0.0f};

		EditorComponent* rigidBody = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		if (rigidBody != nullptr && rigidBody->isActive && physicsManager_ != nullptr) {
			rigidBody->velocity.x = 0.0f;
			rigidBody->velocity.z = 0.0f;
			physicsManager_->SetVelocity(gameObject.id, rigidBody->velocity);
		}
		return;
	}

	const float areaSpeedScale = GetAreaSpeedScale(gameObject.translate);
	if (areaSpeedScale <= 0.0f) {
		agentVelocities_[gameObject.id] = {0.0f, 0.0f, 0.0f};
		return;
	}

	const Vector3 direction = NormalizeXZ(toTarget);  // XZ 平面上の目的地方向。
	const float maxSpeed = (std::max)(agent.navMaxSpeed, 0.0f) * areaSpeedScale;
	const Vector3 desiredVelocity = MultiplyXZ(direction, maxSpeed);
	const float maxVelocityDelta = (std::max)(agent.navMaxAcceleration, 0.0f) * deltaTime;
	Vector3 currentVelocity = agentVelocities_[gameObject.id];
	Vector3 nextVelocity = MoveTowardsVelocity(currentVelocity, desiredVelocity, maxVelocityDelta);
	Vector3 nextPosition = AddXZ(gameObject.translate, MultiplyXZ(nextVelocity, deltaTime));

	TryClampToSurface(nextPosition, agent.navAgentRadius);
	nextPosition = AvoidObstacles(gameObject, agent, gameObject.translate, nextPosition);
	TryClampToSurface(nextPosition, agent.navAgentRadius);

	const Vector3 fixedMovement = MakeHorizontalVector(gameObject.translate, nextPosition);
	if (deltaTime > kNavigationMinimumDistance) {
		nextVelocity = {fixedMovement.x / deltaTime, 0.0f, fixedMovement.z / deltaTime};
	}

	agentVelocities_[gameObject.id] = nextVelocity;

	EditorComponent* rigidBody = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
	if (rigidBody != nullptr && rigidBody->isActive && !rigidBody->isKinematic && physicsManager_ != nullptr) {
		rigidBody->velocity.x = nextVelocity.x;
		rigidBody->velocity.z = nextVelocity.z;
		physicsManager_->SetVelocity(gameObject.id, rigidBody->velocity);
		return;
	}

	gameObject.translate.x = nextPosition.x;
	gameObject.translate.z = nextPosition.z;
}

void EditorNavigationManager::PushConsoleMessage(const std::string& message) const {
	if (consoleMessages_ == nullptr) {
		return;
	}

	consoleMessages_->push_back(message);
}

bool EditorNavigationManager::TryClampToSurface(Vector3& position, float radius) const {
	if (surfaces_.empty()) {
		return false;
	}

	const float safeRadius = (std::max)(radius, 0.0f);
	for (const NavigationSurface& surface : surfaces_) {
		float minX = surface.minX + safeRadius;  // Agent 半径ぶん内側に寄せた移動可能左端。
		float maxX = surface.maxX - safeRadius;  // Agent 半径ぶん内側に寄せた移動可能右端。
		float minZ = surface.minZ + safeRadius;  // Agent 半径ぶん内側に寄せた移動可能手前端。
		float maxZ = surface.maxZ - safeRadius;  // Agent 半径ぶん内側に寄せた移動可能奥端。
		if (minX > maxX) {
			const float centerX = (surface.minX + surface.maxX) * 0.5f;
			minX = centerX;
			maxX = centerX;
		}

		if (minZ > maxZ) {
			const float centerZ = (surface.minZ + surface.maxZ) * 0.5f;
			minZ = centerZ;
			maxZ = centerZ;
		}

		if (ContainsXZ(
			minX,
			maxX,
			minZ,
			maxZ,
			position)) {
			return true;
		}
	}

	float bestDistanceSquared = 0.0f;
	bool hasBestSurface = false;
	Vector3 bestPosition = position;
	for (const NavigationSurface& surface : surfaces_) {
		float minX = surface.minX + safeRadius;  // Agent 半径ぶん内側に寄せた移動可能左端。
		float maxX = surface.maxX - safeRadius;  // Agent 半径ぶん内側に寄せた移動可能右端。
		float minZ = surface.minZ + safeRadius;  // Agent 半径ぶん内側に寄せた移動可能手前端。
		float maxZ = surface.maxZ - safeRadius;  // Agent 半径ぶん内側に寄せた移動可能奥端。
		if (minX > maxX) {
			const float centerX = (surface.minX + surface.maxX) * 0.5f;
			minX = centerX;
			maxX = centerX;
		}

		if (minZ > maxZ) {
			const float centerZ = (surface.minZ + surface.maxZ) * 0.5f;
			minZ = centerZ;
			maxZ = centerZ;
		}

		const Vector3 clampedPosition = {
			ClampFloat(position.x, minX, maxX),
			position.y,
			ClampFloat(position.z, minZ, maxZ)};
		const Vector3 difference = MakeHorizontalVector(position, clampedPosition);
		const float distanceSquared = LengthSquaredXZ(difference);
		if (!hasBestSurface || distanceSquared < bestDistanceSquared) {
			bestDistanceSquared = distanceSquared;
			bestPosition = clampedPosition;
			hasBestSurface = true;
		}
	}

	if (!hasBestSurface) {
		return false;
	}

	position = bestPosition;
	return true;
}

bool EditorNavigationManager::TryApplyLink(EditorGameObject& gameObject, const EditorComponent& agent, const Vector3& targetPosition) {
	for (const NavigationLink& link : links_) {
		if (link.startGameObjectId == gameObject.id) {
			continue;
		}

		const float linkRadius = link.radius + (std::max)(agent.navAgentRadius, 0.0f);
		const float startDistance = LengthXZ(MakeHorizontalVector(gameObject.translate, link.start));
		const float endDistanceToTarget = LengthXZ(MakeHorizontalVector(link.end, targetPosition));
		const float currentDistanceToTarget = LengthXZ(MakeHorizontalVector(gameObject.translate, targetPosition));
		if (startDistance <= linkRadius && endDistanceToTarget * link.costModifier < currentDistanceToTarget) {
			gameObject.translate.x = link.end.x;
			gameObject.translate.z = link.end.z;
			return true;
		}

		if (!link.isBidirectional) {
			continue;
		}

		const float endDistance = LengthXZ(MakeHorizontalVector(gameObject.translate, link.end));
		const float startDistanceToTarget = LengthXZ(MakeHorizontalVector(link.start, targetPosition));
		if (endDistance <= linkRadius && startDistanceToTarget * link.costModifier < currentDistanceToTarget) {
			gameObject.translate.x = link.start.x;
			gameObject.translate.z = link.start.z;
			return true;
		}
	}

	return false;
}

float EditorNavigationManager::GetAreaSpeedScale(const Vector3& position) const {
	float speedScale = 1.0f;  // Area 0 は通常速度。
	for (const NavigationModifierVolume& volume : modifierVolumes_) {
		if (!ContainsXZ(volume.minX, volume.maxX, volume.minZ, volume.maxZ, position)) {
			continue;
		}

		if (volume.area < 0) {
			return 0.0f;
		}

		speedScale *= 1.0f / (1.0f + static_cast<float>(volume.area));
	}

	return speedScale;
}

Vector3 EditorNavigationManager::AvoidObstacles(
	const EditorGameObject& gameObject,
	const EditorComponent& agent,
	const Vector3& currentPosition,
	const Vector3& desiredPosition) const {
	Vector3 adjustedPosition = desiredPosition;  // Obstacle と重なった時に押し出した後の候補位置。
	const float agentRadius = (std::max)(agent.navAgentRadius, 0.0f);
	const Vector3 travelDirection = NormalizeXZ(MakeHorizontalVector(currentPosition, desiredPosition));

	for (const NavigationObstacle& obstacle : obstacles_) {
		if (!obstacle.canCarve || obstacle.gameObjectId == gameObject.id) {
			continue;
		}

		if (obstacle.shape == NavigationObstacleShape::Circle) {
			const float safeRadius = obstacle.radius + agentRadius;
			const Vector3 toAgent = MakeHorizontalVector(obstacle.center, adjustedPosition);
			const float distance = LengthXZ(toAgent);
			if (distance >= safeRadius) {
				continue;
			}

			Vector3 pushDirection = NormalizeXZ(toAgent);
			if (LengthSquaredXZ(pushDirection) <= kNavigationMinimumDistance) {
				pushDirection = {-travelDirection.z, 0.0f, travelDirection.x};
			}

			adjustedPosition.x = obstacle.center.x + pushDirection.x * safeRadius;
			adjustedPosition.z = obstacle.center.z + pushDirection.z * safeRadius;
			continue;
		}

		const float safeHalfX = (std::max)(obstacle.halfSize.x, 0.01f) + agentRadius;
		const float safeHalfZ = (std::max)(obstacle.halfSize.z, 0.01f) + agentRadius;
		const Vector3 worldToAgent = MakeHorizontalVector(obstacle.center, adjustedPosition);
		Vector3 localToAgent = InverseRotateY(worldToAgent, obstacle.rotationY);
		if (AbsFloat(localToAgent.x) >= safeHalfX || AbsFloat(localToAgent.z) >= safeHalfZ) {
			continue;
		}

		const float pushDistanceX = safeHalfX - AbsFloat(localToAgent.x);
		const float pushDistanceZ = safeHalfZ - AbsFloat(localToAgent.z);
		if (pushDistanceX < pushDistanceZ) {
			const float signX = localToAgent.x >= 0.0f ? 1.0f : -1.0f;
			localToAgent.x = signX * safeHalfX;
		}
		else {
			const float signZ = localToAgent.z >= 0.0f ? 1.0f : -1.0f;
			localToAgent.z = signZ * safeHalfZ;
		}

		const Vector3 worldOffset = RotateY(localToAgent, obstacle.rotationY);
		adjustedPosition.x = obstacle.center.x + worldOffset.x;
		adjustedPosition.z = obstacle.center.z + worldOffset.z;
	}

	return adjustedPosition;
}
