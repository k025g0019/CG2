#include "EditorTargetingManager.h"

#include "EditorComponentUtility.h"
#include "EditorInputManager.h"
#include "EditorOceanSystem.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"
#include "EditorSharedState.h"
#include "Source/Engine/Core/Matrix.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>

using namespace EditorSharedState;

namespace {
	constexpr float kRayEpsilon = 0.0001f;
	constexpr float kUiReferenceWidth = 1280.0f;
	constexpr float kUiReferenceHeight = 720.0f;
	constexpr float kPi = 3.1415926535f;
	constexpr float kAiTargetSelectorUpdateInterval = 0.10f;

	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 MultiplyVector3(float scalar, const Vector3& value) {
		return {scalar * value.x, scalar * value.y, scalar * value.z};
	}

	float GetVectorLength(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 NormalizeVector3(const Vector3& value) {
		const float vectorLength = GetVectorLength(value);

		if (vectorLength <= kRayEpsilon) {
			return {0.0f, 0.0f, 1.0f};
		}

		return {
			value.x / vectorLength,
			value.y / vectorLength,
			value.z / vectorLength};
	}

	float DotVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return firstValue.x * secondValue.x + firstValue.y * secondValue.y + firstValue.z * secondValue.z;
	}

	Vector3 GetForwardVector(const Vector3& rotation) {
		const float cosPitch = std::cos(rotation.x);
		return NormalizeVector3({
			std::sin(rotation.y) * cosPitch,
			-std::sin(rotation.x),
			std::cos(rotation.y) * cosPitch});
	}

	float MoveTowards(float currentValue, float targetValue, float maximumDelta) {
		if (currentValue < targetValue) {
			return (std::min)(currentValue + maximumDelta, targetValue);
		}

		return (std::max)(currentValue - maximumDelta, targetValue);
	}

	float MoveTowardsAngle(float currentValue, float targetValue, float maximumDelta) {
		float difference = std::remainder(targetValue - currentValue, 2.0f * kPi);
		difference = (std::clamp)(difference, -maximumDelta, maximumDelta);
		return currentValue + difference;
	}

	const EditorComponent* FindTargetCollider(const EditorGameObject& gameObject) {
		constexpr EditorComponentType colliderTypes[] = {
			EditorComponentType::BoxCollider,
			EditorComponentType::SphereCollider,
			EditorComponentType::CapsuleCollider,
			EditorComponentType::MeshCollider,
			EditorComponentType::AutoConvexCollision,
			EditorComponentType::CharacterController};

		for (const EditorComponentType colliderType : colliderTypes) {
			const EditorComponent* colliderComponent =
				EditorComponentUtility::FindComponent(gameObject, colliderType);

			if (colliderComponent != nullptr && colliderComponent->isActive) {
				return colliderComponent;
			}
		}

		return nullptr;
	}

	const EditorComponent* FindInheritedComponent(
		const EditorScene& editorScene,
		int32_t gameObjectId,
		EditorComponentType componentType) {
		const EditorGameObject* currentGameObject = editorScene.FindGameObject(gameObjectId);

		while (currentGameObject != nullptr) {
			const EditorComponent* component = EditorComponentUtility::FindComponent(
				*currentGameObject,
				componentType);

			if (component != nullptr && component->isActive) {
				return component;
			}

			currentGameObject = currentGameObject->parentId >= 0
				? editorScene.FindGameObject(currentGameObject->parentId)
				: nullptr;
		}

		return nullptr;
	}

	const EditorComponent* FindInheritedTargetCollider(
		const EditorScene& editorScene,
		int32_t gameObjectId) {
		const EditorGameObject* currentGameObject = editorScene.FindGameObject(gameObjectId);

		while (currentGameObject != nullptr) {
			const EditorComponent* colliderComponent = FindTargetCollider(*currentGameObject);

			if (colliderComponent != nullptr) {
				return colliderComponent;
			}

			currentGameObject = currentGameObject->parentId >= 0
				? editorScene.FindGameObject(currentGameObject->parentId)
				: nullptr;
		}

		return nullptr;
	}

	bool IsAncestorOrSelf(
		const EditorScene& editorScene,
		int32_t ancestorGameObjectId,
		int32_t gameObjectId) {
		const EditorGameObject* currentGameObject = editorScene.FindGameObject(gameObjectId);

		while (currentGameObject != nullptr) {
			if (currentGameObject->id == ancestorGameObjectId) {
				return true;
			}

			currentGameObject = currentGameObject->parentId >= 0
				? editorScene.FindGameObject(currentGameObject->parentId)
				: nullptr;
		}

		return false;
	}

	Vector3 TransformDirection(const Vector3& direction, const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = Multiply(
			Multiply(MakeRotateXMatrix(rotation.x), MakeRotateYMatrix(rotation.y)),
			MakeRotateZMatrix(rotation.z));
		return {
			direction.x * rotationMatrix.matrix[0][0] +
				direction.y * rotationMatrix.matrix[1][0] +
				direction.z * rotationMatrix.matrix[2][0],
			direction.x * rotationMatrix.matrix[0][1] +
				direction.y * rotationMatrix.matrix[1][1] +
				direction.z * rotationMatrix.matrix[2][1],
			direction.x * rotationMatrix.matrix[0][2] +
				direction.y * rotationMatrix.matrix[1][2] +
				direction.z * rotationMatrix.matrix[2][2]};
	}

	Vector3 GetTargetPosition(const EditorScene& editorScene, int32_t gameObjectId) {
		Vector3 targetScale{};
		Vector3 targetRotation{};
		Vector3 targetPosition{};
		editorScene.GetWorldTransform(
			gameObjectId,
			targetScale,
			targetRotation,
			targetPosition);
		(void)targetScale;
		const EditorGameObject* targetGameObject = editorScene.FindGameObject(gameObjectId);
		const EditorComponent* targetPoint = targetGameObject != nullptr
			? EditorComponentUtility::FindComponent(*targetGameObject, EditorComponentType::TargetPoint)
			: nullptr;

		if (targetPoint != nullptr && targetPoint->isActive) {
			const Vector3 worldOffset = TransformDirection(targetPoint->targetPointAimOffset, targetRotation);
			targetPosition.x += worldOffset.x;
			targetPosition.y += worldOffset.y;
			targetPosition.z += worldOffset.z;
		}

		return targetPosition;
	}

	bool MatchesTeamFilter(
		const EditorScene& editorScene,
		const EditorGameObject& selectorGameObject,
		const EditorComponent& selectorComponent,
		const EditorGameObject& candidateGameObject) {
		const EditorComponent* candidateTeam = FindInheritedComponent(
			editorScene,
			candidateGameObject.id,
			EditorComponentType::Team);

		if (candidateTeam != nullptr && !candidateTeam->teamTargetable) {
			return false;
		}

		if (candidateTeam == nullptr || candidateTeam->teamId < 0) {
			return selectorComponent.targetSelectorIncludeNeutral;
		}

		const EditorComponent* selectorTeam = FindInheritedComponent(
			editorScene,
			selectorGameObject.id,
			EditorComponentType::Team);
		const int32_t selectorTeamId = selectorTeam != nullptr ? selectorTeam->teamId : -1;

		if (selectorComponent.targetSelectorTeamFilter == 1) {
			return selectorTeamId < 0 || candidateTeam->teamId != selectorTeamId;
		}

		if (selectorComponent.targetSelectorTeamFilter == 2) {
			return selectorTeamId >= 0 && candidateTeam->teamId == selectorTeamId;
		}

		if (selectorComponent.targetSelectorTeamFilter == 3) {
			return candidateTeam->teamId == selectorComponent.targetSelectorSpecificTeamId;
		}

		return true;
	}

	bool MatchesCachedTeamFilter(
		const EditorComponent& selectorComponent,
		const EditorComponent* selectorTeam,
		const EditorComponent* candidateTeam) {
		if (candidateTeam != nullptr && !candidateTeam->teamTargetable) {
			return false;
		}

		if (candidateTeam == nullptr || candidateTeam->teamId < 0) {
			return selectorComponent.targetSelectorIncludeNeutral;
		}

		const int32_t selectorTeamId = selectorTeam != nullptr ? selectorTeam->teamId : -1;

		if (selectorComponent.targetSelectorTeamFilter == 1) {
			return selectorTeamId < 0 || candidateTeam->teamId != selectorTeamId;
		}

		if (selectorComponent.targetSelectorTeamFilter == 2) {
			return selectorTeamId >= 0 && candidateTeam->teamId == selectorTeamId;
		}

		if (selectorComponent.targetSelectorTeamFilter == 3) {
			return candidateTeam->teamId == selectorComponent.targetSelectorSpecificTeamId;
		}

		return true;
	}

	bool TransformClipPoint(
		const Matrix4x4& inverseViewProjection,
		float x,
		float y,
		float z,
		Vector3& worldPosition) {
		const float worldX =
			x * inverseViewProjection.matrix[0][0] +
			y * inverseViewProjection.matrix[1][0] +
			z * inverseViewProjection.matrix[2][0] +
			inverseViewProjection.matrix[3][0];
		const float worldY =
			x * inverseViewProjection.matrix[0][1] +
			y * inverseViewProjection.matrix[1][1] +
			z * inverseViewProjection.matrix[2][1] +
			inverseViewProjection.matrix[3][1];
		const float worldZ =
			x * inverseViewProjection.matrix[0][2] +
			y * inverseViewProjection.matrix[1][2] +
			z * inverseViewProjection.matrix[2][2] +
			inverseViewProjection.matrix[3][2];
		const float worldW =
			x * inverseViewProjection.matrix[0][3] +
			y * inverseViewProjection.matrix[1][3] +
			z * inverseViewProjection.matrix[2][3] +
			inverseViewProjection.matrix[3][3];

		if (std::fabs(worldW) <= kRayEpsilon) {
			return false;
		}

		worldPosition = {worldX / worldW, worldY / worldW, worldZ / worldW};
		return true;
	}

	bool ProjectWorldToViewport(const Vector3& worldPosition, EditorScriptVector2& viewportPosition) {
		const Matrix4x4 viewProjection = Multiply(g_gameViewMatrix, g_gameProjectionMatrix);
		const float clipX = worldPosition.x * viewProjection.matrix[0][0] + worldPosition.y * viewProjection.matrix[1][0] + worldPosition.z * viewProjection.matrix[2][0] + viewProjection.matrix[3][0];
		const float clipY = worldPosition.x * viewProjection.matrix[0][1] + worldPosition.y * viewProjection.matrix[1][1] + worldPosition.z * viewProjection.matrix[2][1] + viewProjection.matrix[3][1];
		const float clipW = worldPosition.x * viewProjection.matrix[0][3] + worldPosition.y * viewProjection.matrix[1][3] + worldPosition.z * viewProjection.matrix[2][3] + viewProjection.matrix[3][3];
		if (clipW <= kRayEpsilon) return false;
		viewportPosition = {clipX / clipW * 0.5f + 0.5f, 0.5f - clipY / clipW * 0.5f};
		return true;
	}
}

void EditorTargetingManager::Initialize(
	EditorScene* editorScene,
	EditorInputManager* inputManager,
	EditorPhysicsManager* physicsManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	inputManager_ = inputManager;
	physicsManager_ = physicsManager;
	scriptManager_ = scriptManager;
	isStarted_ = false;
	targetSelectorUpdateRemainingSeconds_.clear();
}

void EditorTargetingManager::Start() {
	isStarted_ = true;
	steeringElapsedSeconds_.clear();
	steeringSpeeds_.clear();
	explicitTargets_.clear();
	screenAimInputStrengths_.clear();
	targetSelectorUpdateRemainingSeconds_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* screenAimComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ScreenAim);

		if (screenAimComponent == nullptr) {
			continue;
		}

		screenAimComponent->screenAimNormalizedPosition.x = (std::clamp)(
			screenAimComponent->screenAimNormalizedPosition.x,
			0.0f,
			1.0f);
		screenAimComponent->screenAimNormalizedPosition.y = (std::clamp)(
			screenAimComponent->screenAimNormalizedPosition.y,
			0.0f,
			1.0f);
		UpdateReticleUi(*screenAimComponent);
	}

	UpdateTargetSelectors(0.0f, true);
}

void EditorTargetingManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		EditorComponent* screenAimComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ScreenAim);

		if (screenAimComponent == nullptr || !screenAimComponent->isActive) {
			continue;
		}

		const EditorScriptVector2 previousPosition = screenAimComponent->screenAimNormalizedPosition;
		EditorScriptVector2 nextPosition = previousPosition;
		float inputStrength = 0.0f;

		if (screenAimComponent->screenAimInputMode == 0 &&
			g_isGameViewVisible &&
			g_editorGameWidth > 1.0f &&
			g_editorGameHeight > 1.0f) {
			POINT cursorPosition{};

			if (GetCursorPos(&cursorPosition)) {
				const POINT screenPos = cursorPosition;
				const bool converted = ScreenToClient(g_windowHandle, &cursorPosition);

				nextPosition.x =
					(static_cast<float>(cursorPosition.x) - g_editorGameX) / g_editorGameWidth;
				nextPosition.y =
					(static_cast<float>(cursorPosition.y) - g_editorGameY) / g_editorGameHeight;
				const float inputDeltaX = nextPosition.x - previousPosition.x;
				const float inputDeltaY = nextPosition.y - previousPosition.y;
				inputStrength = (std::clamp)(
					std::sqrt(inputDeltaX * inputDeltaX + inputDeltaY * inputDeltaY) * 30.0f,
					0.0f,
					1.0f);
				(void)screenPos;
				(void)converted;
			}
		}
		else if (screenAimComponent->screenAimInputMode == 1 && inputManager_ != nullptr) {
			const int32_t inputGameObjectId = screenAimComponent->screenAimInputGameObjectId >= 0
				? screenAimComponent->screenAimInputGameObjectId
				: gameObject.id;
			float actionX = 0.0f;
			float actionY = 0.0f;
			inputManager_->TryGetActionVector2(
				inputGameObjectId,
				screenAimComponent->screenAimActionMapName,
				screenAimComponent->screenAimActionName,
				actionX,
				actionY);
			nextPosition.x += actionX * screenAimComponent->screenAimSpeed * deltaTime;
			const float verticalSign = screenAimComponent->screenAimInvertY ? 1.0f : -1.0f;
			nextPosition.y += actionY * verticalSign * screenAimComponent->screenAimSpeed * deltaTime;
			inputStrength = (std::clamp)(std::sqrt(actionX * actionX + actionY * actionY), 0.0f, 1.0f);
		}

		if (screenAimComponent->screenAimClamp) {
			nextPosition.x = (std::clamp)(nextPosition.x, 0.0f, 1.0f);
			nextPosition.y = (std::clamp)(nextPosition.y, 0.0f, 1.0f);
		}

		screenAimComponent->screenAimNormalizedPosition = nextPosition;
		screenAimInputStrengths_[gameObject.id] = inputStrength;
		UpdateReticleUi(*screenAimComponent);
	}

	UpdateTargetSelectors(deltaTime, false);
	UpdateAimAssist(deltaTime);
	UpdateInterceptPredictions();
	UpdateBallisticPredictions();
	UpdateTargetSteering(deltaTime);
}

void EditorTargetingManager::UpdateAimAssist(float deltaTime) {
	for (EditorGameObject& owner : editorScene_->GetGameObjects()) {
		const EditorComponent* assist = EditorComponentUtility::FindComponent(owner, EditorComponentType::AimAssist);
		if (!owner.isActive || assist == nullptr || !assist->isActive) continue;
		const int32_t aimId = assist->aimAssistScreenAimGameObjectId >= 0 ? assist->aimAssistScreenAimGameObjectId : owner.id;
		const int32_t selectorId = assist->aimAssistTargetSelectorGameObjectId >= 0 ? assist->aimAssistTargetSelectorGameObjectId : owner.id;
		EditorGameObject* aimObject = editorScene_->FindGameObject(aimId);
		EditorComponent* screenAim = aimObject != nullptr ? EditorComponentUtility::FindComponent(*aimObject, EditorComponentType::ScreenAim) : nullptr;
		int32_t targetId = -1;
		if (screenAim == nullptr || !screenAim->isActive || !GetCurrentTarget(selectorId, targetId)) continue;
		EditorScriptVector2 targetViewport{};
		if (!ProjectWorldToViewport(GetTargetPosition(*editorScene_, targetId), targetViewport)) continue;
		const float dx = targetViewport.x - screenAim->screenAimNormalizedPosition.x;
		const float dy = targetViewport.y - screenAim->screenAimNormalizedPosition.y;
		const float distance = std::sqrt(dx * dx + dy * dy);
		const float radius = (std::max)(assist->aimAssistRadius, 0.0001f);
		if (distance > radius) continue;
		const float edgeWeight = 1.0f - (std::clamp)(distance / radius, 0.0f, 1.0f);
		const auto inputIterator = screenAimInputStrengths_.find(aimId);
		const float inputStrength = inputIterator != screenAimInputStrengths_.end()
			? inputIterator->second
			: 0.0f;
		const float inputSuppression = 1.0f - inputStrength *
			(std::clamp)(assist->aimAssistInputSuppression, 0.0f, 1.0f);
		const float blend = (std::clamp)(
			assist->aimAssistStrength * edgeWeight * assist->aimAssistFollowSpeed *
			inputSuppression * deltaTime,
			0.0f,
			1.0f);
		screenAim->screenAimNormalizedPosition.x += dx * blend;
		screenAim->screenAimNormalizedPosition.y += dy * blend;
		UpdateReticleUi(*screenAim);
	}
}

bool EditorTargetingManager::SolveIntercept(const Vector3& origin, int32_t targetGameObjectId, float projectileSpeed, float maximumTime, Vector3& position, float& time) const {
	const EditorGameObject* target = editorScene_ != nullptr ? editorScene_->FindGameObject(targetGameObjectId) : nullptr;
	if (target == nullptr || !target->isActive || projectileSpeed <= kRayEpsilon) return false;
	const Vector3 targetPosition = GetTargetPosition(*editorScene_, targetGameObjectId);
	Vector3 velocity{};
	const EditorComponent* rigidBody = FindInheritedComponent(*editorScene_, targetGameObjectId, EditorComponentType::RigidBody);
	if (rigidBody != nullptr) velocity = rigidBody->velocity;
	const Vector3 relative = SubtractVector3(targetPosition, origin);
	const float a = DotVector3(velocity, velocity) - projectileSpeed * projectileSpeed;
	const float b = 2.0f * DotVector3(relative, velocity);
	const float c = DotVector3(relative, relative);
	float solution = -1.0f;
	if (std::fabs(a) <= kRayEpsilon) {
		if (std::fabs(b) > kRayEpsilon) solution = -c / b;
	}
	else {
		const float discriminant = b * b - 4.0f * a * c;
		if (discriminant >= 0.0f) {
			const float root = std::sqrt(discriminant);
			const float first = (-b - root) / (2.0f * a); const float second = (-b + root) / (2.0f * a);
			if (first > 0.0f) solution = first; if (second > 0.0f && (solution < 0.0f || second < solution)) solution = second;
		}
	}
	if (solution <= 0.0f || solution > (std::max)(maximumTime, 0.0f)) return false;
	time = solution; position = {targetPosition.x + velocity.x * solution, targetPosition.y + velocity.y * solution, targetPosition.z + velocity.z * solution}; return true;
}

void EditorTargetingManager::UpdateInterceptPredictions() {
	for (EditorGameObject& owner : editorScene_->GetGameObjects()) {
		EditorComponent* prediction = EditorComponentUtility::FindComponent(owner, EditorComponentType::InterceptPrediction);
		if (!owner.isActive || prediction == nullptr || !prediction->isActive) continue;
		int32_t targetId = prediction->interceptTargetGameObjectId;
		if (targetId < 0) GetCurrentTarget(prediction->interceptTargetSelectorGameObjectId >= 0 ? prediction->interceptTargetSelectorGameObjectId : owner.id, targetId);
		Vector3 scale{}, rotation{}, origin{}; editorScene_->GetWorldTransform(owner.id, scale, rotation, origin);
		prediction->interceptValid = SolveIntercept(origin, targetId, prediction->interceptProjectileSpeed, prediction->interceptMaximumTime, prediction->interceptPredictedPosition, prediction->interceptTime);
	}
}

bool EditorTargetingManager::GetInterceptPrediction(int32_t ownerGameObjectId, Vector3& position, float& time) const {
	const EditorGameObject* owner = editorScene_ != nullptr ? editorScene_->FindGameObject(ownerGameObjectId) : nullptr;
	const EditorComponent* prediction = owner != nullptr ? EditorComponentUtility::FindComponent(*owner, EditorComponentType::InterceptPrediction) : nullptr;
	if (prediction == nullptr || !prediction->isActive || !prediction->interceptValid) return false;
	position = prediction->interceptPredictedPosition; time = prediction->interceptTime; return true;
}

void EditorTargetingManager::UpdateBallisticPredictions() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& owner : editorScene_->GetGameObjects()) {
		EditorComponent* prediction = EditorComponentUtility::FindComponent(
			owner,
			EditorComponentType::BallisticPrediction);

		if (prediction == nullptr) {
			continue;
		}

		prediction->ballisticValid = false;
		prediction->ballisticTrajectoryPoints.clear();

		if (!owner.isActive || !prediction->isActive) {
			continue;
		}

		int32_t targetGameObjectId = prediction->ballisticTargetGameObjectId;

		if (targetGameObjectId < 0) {
			const int32_t selectorGameObjectId = prediction->ballisticTargetSelectorGameObjectId >= 0
				? prediction->ballisticTargetSelectorGameObjectId
				: owner.id;
			GetCurrentTarget(selectorGameObjectId, targetGameObjectId);
		}

		const EditorGameObject* targetGameObject = editorScene_->FindGameObject(targetGameObjectId);
		const float initialSpeed = (std::max)(prediction->ballisticInitialSpeed, 0.0f);

		if (targetGameObject == nullptr || !targetGameObject->isActive || initialSpeed <= kRayEpsilon) {
			continue;
		}

		Vector3 ownerScale{};
		Vector3 ownerRotation{};
		Vector3 origin{};

		if (!editorScene_->GetWorldTransform(owner.id, ownerScale, ownerRotation, origin)) {
			continue;
		}

		const Vector3 targetPosition = GetTargetPosition(*editorScene_, targetGameObjectId);
		Vector3 targetVelocity{};
		const EditorComponent* targetRigidBody = FindInheritedComponent(
			*editorScene_,
			targetGameObjectId,
			EditorComponentType::RigidBody);

		if (targetRigidBody != nullptr) {
			targetVelocity = targetRigidBody->velocity;
		}

		const Vector3 gravity = prediction->ballisticGravity;
		const Vector3 targetAcceleration = prediction->ballisticTargetAcceleration;
		const float drag = (std::max)(prediction->ballisticDrag, 0.0f);
		const float maximumTime = (std::max)(prediction->ballisticMaximumTime, 0.01f);
		const float simulationStep = (std::clamp)(prediction->ballisticSimulationStep, 0.001f, 0.25f);
		const int32_t maximumPoints = (std::clamp)(prediction->ballisticMaximumPoints, 2, 2048);
		const int32_t sourceVelocityGameObjectId = prediction->ballisticSourceVelocityGameObjectId >= 0
			? prediction->ballisticSourceVelocityGameObjectId
			: owner.id;
		const Vector3 sourceVelocity = prediction->ballisticInheritSourceVelocity
			? EditorComponentUtility::ResolveInheritedRigidBodyVelocity(
				*editorScene_,
				sourceVelocityGameObjectId,
				prediction->ballisticUseParentRigidBody,
				origin,
				prediction->ballisticLinearVelocityInheritance,
				prediction->ballisticAngularVelocityInheritance)
			: Vector3{0.0f, 0.0f, 0.0f};
		prediction->ballisticSourceVelocity = sourceVelocity;

		auto calculateRequiredVelocity = [&](float flightTime, Vector3& requiredVelocity) {
			const Vector3 targetAtImpact{
				targetPosition.x + targetVelocity.x * flightTime + 0.5f * targetAcceleration.x * flightTime * flightTime,
				targetPosition.y + targetVelocity.y * flightTime + 0.5f * targetAcceleration.y * flightTime * flightTime,
				targetPosition.z + targetVelocity.z * flightTime + 0.5f * targetAcceleration.z * flightTime * flightTime};

			if (drag <= kRayEpsilon) {
				requiredVelocity = {
					(targetAtImpact.x - origin.x - 0.5f * gravity.x * flightTime * flightTime) / flightTime,
					(targetAtImpact.y - origin.y - 0.5f * gravity.y * flightTime * flightTime) / flightTime,
					(targetAtImpact.z - origin.z - 0.5f * gravity.z * flightTime * flightTime) / flightTime};
				return;
			}

			const float velocityFactor = (1.0f - std::exp(-drag * flightTime)) / drag;
			const float gravityFactor = flightTime / drag - velocityFactor / drag;
			requiredVelocity = {
				(targetAtImpact.x - origin.x - gravity.x * gravityFactor) / velocityFactor,
				(targetAtImpact.y - origin.y - gravity.y * gravityFactor) / velocityFactor,
				(targetAtImpact.z - origin.z - gravity.z * gravityFactor) / velocityFactor};
		};

		float lowerTime = simulationStep;
		Vector3 lowerVelocity{};
		calculateRequiredVelocity(lowerTime, lowerVelocity);
		float lowerError = GetVectorLength(SubtractVector3(lowerVelocity, sourceVelocity)) - initialSpeed;
		bool foundBracket = false;
		float upperTime = lowerTime;

		for (float candidateTime = lowerTime + simulationStep;
			candidateTime <= maximumTime + kRayEpsilon;
			candidateTime += simulationStep) {
			Vector3 candidateVelocity{};
			calculateRequiredVelocity((std::min)(candidateTime, maximumTime), candidateVelocity);
			const float candidateError = GetVectorLength(SubtractVector3(candidateVelocity, sourceVelocity)) - initialSpeed;

			if ((lowerError <= 0.0f && candidateError >= 0.0f) ||
				(lowerError >= 0.0f && candidateError <= 0.0f)) {
				upperTime = (std::min)(candidateTime, maximumTime);
				foundBracket = true;
				break;
			}

			lowerTime = (std::min)(candidateTime, maximumTime);
			lowerError = candidateError;
		}

		if (!foundBracket) {
			continue;
		}

		for (int32_t iteration = 0; iteration < 20; iteration++) {
			const float middleTime = (lowerTime + upperTime) * 0.5f;
			Vector3 middleVelocity{};
			calculateRequiredVelocity(middleTime, middleVelocity);
			const float middleError = GetVectorLength(SubtractVector3(middleVelocity, sourceVelocity)) - initialSpeed;

			if ((lowerError <= 0.0f && middleError >= 0.0f) ||
				(lowerError >= 0.0f && middleError <= 0.0f)) {
				upperTime = middleTime;
			}
			else {
				lowerTime = middleTime;
				lowerError = middleError;
			}
		}

		const float flightTime = (lowerTime + upperTime) * 0.5f;
		Vector3 requiredWorldVelocity{};
		calculateRequiredVelocity(flightTime, requiredWorldVelocity);
		const Vector3 launchDirection = NormalizeVector3(SubtractVector3(requiredWorldVelocity, sourceVelocity));
		Vector3 projectilePosition = origin;
		Vector3 projectileVelocity = AddVector3(
			MultiplyVector3(initialSpeed, launchDirection),
			sourceVelocity);
		const Vector3 initialWorldVelocity = projectileVelocity;
		prediction->ballisticTrajectoryPoints.reserve(static_cast<size_t>(maximumPoints));
		prediction->ballisticTrajectoryPoints.push_back(projectilePosition);

		float elapsedTime = 0.0f;

		while (elapsedTime < flightTime &&
			static_cast<int32_t>(prediction->ballisticTrajectoryPoints.size()) < maximumPoints) {
			const float step = (std::min)(simulationStep, flightTime - elapsedTime);
			const Vector3 acceleration{
				gravity.x - drag * projectileVelocity.x,
				gravity.y - drag * projectileVelocity.y,
				gravity.z - drag * projectileVelocity.z};
			projectilePosition.x += projectileVelocity.x * step + 0.5f * acceleration.x * step * step;
			projectilePosition.y += projectileVelocity.y * step + 0.5f * acceleration.y * step * step;
			projectilePosition.z += projectileVelocity.z * step + 0.5f * acceleration.z * step * step;
			projectileVelocity.x += acceleration.x * step;
			projectileVelocity.y += acceleration.y * step;
			projectileVelocity.z += acceleration.z * step;
			elapsedTime += step;
			prediction->ballisticTrajectoryPoints.push_back(projectilePosition);
		}

		prediction->ballisticValid = true;
		prediction->ballisticLaunchDirection = launchDirection;
		prediction->ballisticLaunchVelocity = initialWorldVelocity;
		prediction->ballisticFlightTime = flightTime;
		prediction->ballisticImpactPosition = {
			targetPosition.x + targetVelocity.x * flightTime + 0.5f * targetAcceleration.x * flightTime * flightTime,
			targetPosition.y + targetVelocity.y * flightTime + 0.5f * targetAcceleration.y * flightTime * flightTime,
			targetPosition.z + targetVelocity.z * flightTime + 0.5f * targetAcceleration.z * flightTime * flightTime};
	}
}

bool EditorTargetingManager::GetBallisticPrediction(
	int32_t ownerGameObjectId,
	Vector3& launchDirection,
	Vector3& impactPosition,
	float& flightTime) const {
	const EditorGameObject* owner = editorScene_ != nullptr
		? editorScene_->FindGameObject(ownerGameObjectId)
		: nullptr;
	const EditorComponent* prediction = owner != nullptr
		? EditorComponentUtility::FindComponent(*owner, EditorComponentType::BallisticPrediction)
		: nullptr;

	if (prediction == nullptr || !prediction->isActive || !prediction->ballisticValid) {
		return false;
	}

	launchDirection = prediction->ballisticLaunchDirection;
	impactPosition = prediction->ballisticImpactPosition;
	flightTime = prediction->ballisticFlightTime;
	return true;
}

bool EditorTargetingManager::GetBallisticTrajectoryPoint(
	int32_t ownerGameObjectId,
	int32_t pointIndex,
	Vector3& point) const {
	const EditorGameObject* owner = editorScene_ != nullptr
		? editorScene_->FindGameObject(ownerGameObjectId)
		: nullptr;
	const EditorComponent* prediction = owner != nullptr
		? EditorComponentUtility::FindComponent(*owner, EditorComponentType::BallisticPrediction)
		: nullptr;

	if (prediction == nullptr || !prediction->isActive || !prediction->ballisticValid ||
		pointIndex < 0 || pointIndex >= static_cast<int32_t>(prediction->ballisticTrajectoryPoints.size())) {
		return false;
	}

	point = prediction->ballisticTrajectoryPoints[static_cast<size_t>(pointIndex)];
	return true;
}

void EditorTargetingManager::Stop() {
	isStarted_ = false;
	steeringElapsedSeconds_.clear();
	steeringSpeeds_.clear();
	steeringActiveMoveModes_.clear();
	steeringModeElapsedSeconds_.clear();
	steeringModeCompletionNotified_.clear();
	explicitTargets_.clear();
	screenAimInputStrengths_.clear();
	targetSelectorUpdateRemainingSeconds_.clear();

	if (editorScene_ != nullptr) {
		for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			EditorComponent* prediction = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::BallisticPrediction);

			if (prediction != nullptr) {
				prediction->ballisticValid = false;
				prediction->ballisticTrajectoryPoints.clear();
			}
		}
	}
}

bool EditorTargetingManager::GetCurrentTarget(
	int32_t targetSelectorGameObjectId,
	int32_t& targetGameObjectId) const {
	const EditorGameObject* selectorGameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(targetSelectorGameObjectId)
		: nullptr;
	const EditorComponent* selectorComponent = selectorGameObject != nullptr
		? EditorComponentUtility::FindComponent(*selectorGameObject, EditorComponentType::TargetSelector)
		: nullptr;

	if (selectorComponent == nullptr || !selectorComponent->isActive) {
		return false;
	}

	targetGameObjectId = selectorComponent->targetSelectorCurrentTargetGameObjectId;
	return targetGameObjectId >= 0;
}

bool EditorTargetingManager::GetCandidateTargets(
	int32_t targetSelectorGameObjectId,
	int32_t maximumCount,
	std::vector<int32_t>& targetGameObjectIds) const {
	targetGameObjectIds.clear();

	if (editorScene_ == nullptr || maximumCount <= 0) {
		return false;
	}

	const EditorGameObject* selectorGameObject = editorScene_->FindGameObject(targetSelectorGameObjectId);
	const EditorComponent* selectorComponent = selectorGameObject != nullptr
		? EditorComponentUtility::FindComponent(*selectorGameObject, EditorComponentType::TargetSelector)
		: nullptr;

	if (selectorGameObject == nullptr || !selectorGameObject->isActive ||
		selectorComponent == nullptr || !selectorComponent->isActive) {
		return false;
	}

	const int32_t referenceGameObjectId = selectorComponent->targetSelectorReferenceGameObjectId >= 0
		? selectorComponent->targetSelectorReferenceGameObjectId
		: targetSelectorGameObjectId;
	Vector3 referenceScale{};
	Vector3 referenceRotation{};
	Vector3 referencePosition{};

	if (!editorScene_->GetWorldTransform(
			referenceGameObjectId,
			referenceScale,
			referenceRotation,
			referencePosition)) {
		return false;
	}

	struct RankedTarget {
		int32_t gameObjectId;
		float score;
	};

	std::vector<RankedTarget> rankedTargets;
	const Vector3 referenceForward = GetForwardVector(referenceRotation);
	const float maximumDistance = (std::max)(selectorComponent->targetSelectorMaximumDistance, 0.0f);
	const float minimumAngleCosine = std::cos(
		(std::clamp)(selectorComponent->targetSelectorMaximumAngle, 0.0f, 180.0f) * kPi / 180.0f);

	for (const EditorGameObject& candidateGameObject : editorScene_->GetGameObjects()) {
		if (!candidateGameObject.isActive || candidateGameObject.id == targetSelectorGameObjectId ||
			candidateGameObject.id == referenceGameObjectId) {
			continue;
		}

		const EditorComponent* colliderComponent = FindInheritedTargetCollider(*editorScene_, candidateGameObject.id);
		const EditorComponent* healthComponent = FindInheritedComponent(
			*editorScene_, candidateGameObject.id, EditorComponentType::Health);
		const EditorComponent* targetPointComponent = EditorComponentUtility::FindComponent(
			candidateGameObject, EditorComponentType::TargetPoint);

		if (colliderComponent == nullptr && healthComponent == nullptr &&
			(targetPointComponent == nullptr || !targetPointComponent->isActive)) {
			continue;
		}

		if (!MatchesTeamFilter(*editorScene_, *selectorGameObject, *selectorComponent, candidateGameObject)) {
			continue;
		}

		if (selectorComponent->targetSelectorSearchLayer >= 0 &&
			(colliderComponent == nullptr || colliderComponent->physicsLayer != selectorComponent->targetSelectorSearchLayer)) {
			continue;
		}

		const Vector3 candidatePosition = GetTargetPosition(*editorScene_, candidateGameObject.id);
		const Vector3 targetOffset = SubtractVector3(candidatePosition, referencePosition);
		const float targetDistance = GetVectorLength(targetOffset);

		if (targetDistance <= kRayEpsilon || targetDistance > maximumDistance) {
			continue;
		}

		const Vector3 targetDirection = NormalizeVector3(targetOffset);
		const float forwardDot = DotVector3(referenceForward, targetDirection);

		if (forwardDot < minimumAngleCosine) {
			continue;
		}

		if (selectorComponent->targetSelectorOcclusionCheck && physicsManager_ != nullptr) {
			EditorJoltPhysicsManager::PhysicsHit hit{};

			if (physicsManager_->RaycastIgnoringGameObject(
					referencePosition,
					targetDirection,
					targetDistance + 0.05f,
					referenceGameObjectId,
					hit) &&
				hit.gameObjectId != candidateGameObject.id &&
				!IsAncestorOrSelf(*editorScene_, hit.gameObjectId, candidateGameObject.id)) {
				continue;
			}
		}

		float targetScore = targetDistance;

		if (selectorComponent->targetSelectorSelectionMode == 1) {
			targetScore = (1.0f - forwardDot) * maximumDistance + targetDistance * 0.01f;
		}
		else if (selectorComponent->targetSelectorSelectionMode == 2 && healthComponent != nullptr) {
			targetScore = healthComponent->healthCurrent;
		}
		else if (selectorComponent->targetSelectorSelectionMode == 3) {
			const float targetPriority = targetPointComponent != nullptr && targetPointComponent->isActive
				? targetPointComponent->targetPointPriority
				: static_cast<float>(colliderComponent != nullptr ? colliderComponent->physicsLayer : 0);
			targetScore = -targetPriority * maximumDistance + targetDistance;
		}

		rankedTargets.push_back(RankedTarget{candidateGameObject.id, targetScore});
	}

	std::stable_sort(
		rankedTargets.begin(),
		rankedTargets.end(),
		[](const RankedTarget& firstTarget, const RankedTarget& secondTarget) {
			return firstTarget.score < secondTarget.score;
		});

	const int32_t selectorMaximumCount = (std::max)(selectorComponent->targetSelectorMaximumTargets, 1);
	const int32_t outputCount = (std::min)(
		(std::min)(maximumCount, selectorMaximumCount),
		static_cast<int32_t>(rankedTargets.size()));
	targetGameObjectIds.reserve(static_cast<size_t>(outputCount));

	for (int32_t targetIndex = 0; targetIndex < outputCount; ++targetIndex) {
		targetGameObjectIds.push_back(rankedTargets[static_cast<size_t>(targetIndex)].gameObjectId);
	}

	return !targetGameObjectIds.empty();
}

bool EditorTargetingManager::SetExplicitTarget(
	int32_t targetSelectorGameObjectId,
	int32_t targetGameObjectId) {
	if (editorScene_ == nullptr ||
		editorScene_->FindGameObject(targetSelectorGameObjectId) == nullptr ||
		(targetGameObjectId >= 0 && editorScene_->FindGameObject(targetGameObjectId) == nullptr)) {
		return false;
	}

	explicitTargets_[targetSelectorGameObjectId] = targetGameObjectId;
	return true;
}

bool EditorTargetingManager::GetAimRay(int32_t screenAimGameObjectId, AimRay& aimRay) const {
	EditorScriptVector2 normalizedPosition{0.5f, 0.5f};

	if (screenAimGameObjectId >= 0) {
		GetNormalizedPosition(screenAimGameObjectId, normalizedPosition);
	}

	return ViewportPointToRay(normalizedPosition, aimRay);
}

bool EditorTargetingManager::ViewportPointToRay(
	const EditorScriptVector2& normalizedPosition,
	AimRay& aimRay) const {
	const Matrix4x4 viewProjection = Multiply(g_gameViewMatrix, g_gameProjectionMatrix);
	const Matrix4x4 inverseViewProjection = Inverse(viewProjection);
	const float ndcX = normalizedPosition.x * 2.0f - 1.0f;
	const float ndcY = 1.0f - normalizedPosition.y * 2.0f;
	Vector3 nearPosition{};
	Vector3 farPosition{};

	if (!TransformClipPoint(inverseViewProjection, ndcX, ndcY, 0.0f, nearPosition) ||
		!TransformClipPoint(inverseViewProjection, ndcX, ndcY, 1.0f, farPosition)) {
		return false;
	}

	const Vector3 direction = NormalizeVector3(SubtractVector3(farPosition, nearPosition));
	if (GetVectorLength(direction) <= kRayEpsilon) {
		return false;
	}

	aimRay.origin = nearPosition;
	aimRay.direction = direction;
	return true;
}

bool EditorTargetingManager::GetNormalizedPosition(
	int32_t screenAimGameObjectId,
	EditorScriptVector2& normalizedPosition) const {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(screenAimGameObjectId);
	if (gameObject == nullptr || !gameObject->isActive) {
		return false;
	}

	const EditorComponent* screenAimComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::ScreenAim);

	if (screenAimComponent == nullptr || !screenAimComponent->isActive) {
		return false;
	}

	normalizedPosition = screenAimComponent->screenAimNormalizedPosition;
	return true;
}

void EditorTargetingManager::UpdateTargetSelectors(float deltaTime, bool forceUpdate) {
	if (editorScene_ == nullptr) {
		return;
	}

	struct TargetCandidate {
		const EditorGameObject* gameObject = nullptr;
		const EditorComponent* colliderComponent = nullptr;
		const EditorComponent* healthComponent = nullptr;
		const EditorComponent* targetPointComponent = nullptr;
		const EditorComponent* teamComponent = nullptr;
		Vector3 position{0.0f, 0.0f, 0.0f};
	};

	std::vector<TargetCandidate> targetCandidates;
	targetCandidates.reserve(editorScene_->GetGameObjects().size());

	// Target候補の継承ComponentとWorld位置はSelectorごとではなく1回だけ解決する。
	for (const EditorGameObject& candidateGameObject : editorScene_->GetGameObjects()) {
		if (!candidateGameObject.isActive) {
			continue;
		}

		const EditorComponent* colliderComponent = FindInheritedTargetCollider(
			*editorScene_,
			candidateGameObject.id);
		const EditorComponent* healthComponent = FindInheritedComponent(
			*editorScene_,
			candidateGameObject.id,
			EditorComponentType::Health);
		const EditorComponent* targetPointComponent = EditorComponentUtility::FindComponent(
			candidateGameObject,
			EditorComponentType::TargetPoint);

		if (colliderComponent == nullptr && healthComponent == nullptr &&
			(targetPointComponent == nullptr || !targetPointComponent->isActive)) {
			continue;
		}

		targetCandidates.push_back(TargetCandidate{
			&candidateGameObject,
			colliderComponent,
			healthComponent,
			targetPointComponent,
			FindInheritedComponent(*editorScene_, candidateGameObject.id, EditorComponentType::Team),
			GetTargetPosition(*editorScene_, candidateGameObject.id)});
	}

	for (EditorGameObject& selectorGameObject : editorScene_->GetGameObjects()) {
		EditorComponent* selectorComponent = EditorComponentUtility::FindComponent(
			selectorGameObject,
			EditorComponentType::TargetSelector);

		if (!selectorGameObject.isActive || selectorComponent == nullptr || !selectorComponent->isActive) {
			continue;
		}

		const auto explicitTargetIterator = explicitTargets_.find(selectorGameObject.id);
		const EditorComponent* screenAimComponent = EditorComponentUtility::FindComponent(
			selectorGameObject,
			EditorComponentType::ScreenAim);
		const bool requiresEveryFrameUpdate =
			screenAimComponent != nullptr && screenAimComponent->isActive;
		float& updateRemainingSeconds =
			targetSelectorUpdateRemainingSeconds_[selectorGameObject.id];

		// プレイヤー照準は毎Frame、AIの索敵は10Hzで更新する。
		if (!forceUpdate && !requiresEveryFrameUpdate &&
			explicitTargetIterator == explicitTargets_.end()) {
			updateRemainingSeconds = (std::max)(updateRemainingSeconds - deltaTime, 0.0f);

			if (updateRemainingSeconds > 0.0f) {
				continue;
			}
		}

		updateRemainingSeconds = requiresEveryFrameUpdate ||
			explicitTargetIterator != explicitTargets_.end()
			? 0.0f
			: kAiTargetSelectorUpdateInterval;

		const int32_t previousTargetGameObjectId =
			selectorComponent->targetSelectorCurrentTargetGameObjectId;
		int32_t selectedTargetGameObjectId = explicitTargetIterator != explicitTargets_.end()
			? explicitTargetIterator->second
			: -1;

		if (explicitTargetIterator == explicitTargets_.end()) {
			const int32_t referenceGameObjectId = selectorComponent->targetSelectorReferenceGameObjectId >= 0
				? selectorComponent->targetSelectorReferenceGameObjectId
				: selectorGameObject.id;
			const EditorGameObject* referenceGameObject = editorScene_->FindGameObject(referenceGameObjectId);

			if (referenceGameObject != nullptr) {
				Vector3 referenceScale{};
				Vector3 referenceRotation{};
				Vector3 referencePosition{};
				editorScene_->GetWorldTransform(
					referenceGameObjectId,
					referenceScale,
					referenceRotation,
					referencePosition);
				const Vector3 referenceForward = GetForwardVector(referenceRotation);
				const float maximumDistance = (std::max)(selectorComponent->targetSelectorMaximumDistance, 0.0f);
				const float minimumAngleCosine = std::cos(
					(std::clamp)(selectorComponent->targetSelectorMaximumAngle, 0.0f, 180.0f) *
					kPi / 180.0f);
				float bestScore = 1000000000.0f;
				int32_t evaluatedTargetCount = 0;
				const EditorComponent* selectorTeamComponent = FindInheritedComponent(
					*editorScene_,
					selectorGameObject.id,
					EditorComponentType::Team);

				for (const TargetCandidate& candidate : targetCandidates) {
					const EditorGameObject& candidateGameObject = *candidate.gameObject;

					if (candidateGameObject.id == selectorGameObject.id ||
						candidateGameObject.id == referenceGameObjectId) {
						continue;
					}

					if (!MatchesCachedTeamFilter(
							*selectorComponent,
							selectorTeamComponent,
							candidate.teamComponent)) {
						continue;
					}

					if (selectorComponent->targetSelectorSearchLayer >= 0 &&
						(candidate.colliderComponent == nullptr ||
						 candidate.colliderComponent->physicsLayer != selectorComponent->targetSelectorSearchLayer)) {
						continue;
					}

					const Vector3 candidatePosition = candidate.position;
					const Vector3 targetOffset = SubtractVector3(candidatePosition, referencePosition);
					const float targetDistance = GetVectorLength(targetOffset);

					if (targetDistance <= kRayEpsilon || targetDistance > maximumDistance) {
						continue;
					}

					const Vector3 targetDirection = NormalizeVector3(targetOffset);
					const float forwardDot = DotVector3(referenceForward, targetDirection);

					if (forwardDot < minimumAngleCosine) {
						continue;
					}

					const int32_t occlusionMode = selectorComponent->targetSelectorOcclusionCheck
						? (std::clamp)(selectorComponent->targetSelectorOcclusionMode, 0, 3)
						: 0;

					if ((occlusionMode & 1) != 0 && physicsManager_ != nullptr) {
						EditorJoltPhysicsManager::PhysicsHit hit{};

						if (physicsManager_->RaycastIgnoringGameObject(
								referencePosition,
								targetDirection,
								targetDistance + 0.05f,
								referenceGameObjectId,
								hit) &&
							hit.gameObjectId != candidateGameObject.id &&
							!IsAncestorOrSelf(*editorScene_, hit.gameObjectId, candidateGameObject.id)) {
							continue;
						}
					}

					if ((occlusionMode & 2) != 0) {
						const float startMargin = (std::min)(targetDistance * 0.05f, 0.1f);
						const float endMargin = (std::min)(targetDistance * 0.05f, 0.5f);
						const Vector3 oceanRayStart = AddVector3(
							referencePosition,
							MultiplyVector3(startMargin, targetDirection));
						const Vector3 oceanRayEnd = AddVector3(
							candidatePosition,
							MultiplyVector3(-endMargin, targetDirection));
						const uint64_t oceanQueryKey =
							(static_cast<uint64_t>(static_cast<uint32_t>(selectorGameObject.id)) << 32u) |
							static_cast<uint32_t>(candidateGameObject.id);
						const int32_t coarseStepCount = (std::clamp)(
							static_cast<int32_t>(std::ceil(targetDistance / 8.0f)),
							4,
							16);
						EditorOceanOcclusionResult oceanOcclusion{};

						if (QueryEditorOceanOcclusion(
								*editorScene_,
								-1,
								oceanRayStart,
								oceanRayEnd,
								selectorComponent->targetSelectorOceanClearance,
								oceanQueryKey,
								GetEditorOceanElapsedTime(),
								coarseStepCount,
								4,
								oceanOcclusion) &&
							oceanOcclusion.isBlocked) {
							continue;
						}
					}

					float targetScore = targetDistance;

					if (selectorComponent->targetSelectorSelectionMode == 1) {
						targetScore = (1.0f - forwardDot) * maximumDistance + targetDistance * 0.01f;
					}
					else if (selectorComponent->targetSelectorSelectionMode == 2 && candidate.healthComponent != nullptr) {
						targetScore = candidate.healthComponent->healthCurrent;
					}
					else if (selectorComponent->targetSelectorSelectionMode == 3) {
						const float targetPriority = candidate.targetPointComponent != nullptr && candidate.targetPointComponent->isActive
							? candidate.targetPointComponent->targetPointPriority
							: static_cast<float>(candidate.colliderComponent != nullptr ? candidate.colliderComponent->physicsLayer : 0);
						targetScore = -targetPriority * maximumDistance + targetDistance;
					}

					if (targetScore < bestScore) {
						bestScore = targetScore;
						selectedTargetGameObjectId = candidateGameObject.id;
					}

					evaluatedTargetCount++;

					if (evaluatedTargetCount >= (std::max)(selectorComponent->targetSelectorMaximumTargets, 1)) {
						break;
					}
				}
			}
		}

		if (selectedTargetGameObjectId >= 0) {
			const EditorGameObject* selectedTarget = editorScene_->FindGameObject(selectedTargetGameObjectId);

			if (selectedTarget == nullptr || !selectedTarget->isActive) {
				selectedTargetGameObjectId = -1;
			}
		}

		selectorComponent->targetSelectorCurrentTargetGameObjectId = selectedTargetGameObjectId;

		if (selectedTargetGameObjectId == previousTargetGameObjectId || scriptManager_ == nullptr) {
			continue;
		}

		const int32_t actionTargetGameObjectId = selectorComponent->targetSelectorActionTargetGameObjectId >= 0
			? selectorComponent->targetSelectorActionTargetGameObjectId
			: selectorGameObject.id;

		if (previousTargetGameObjectId < 0 && selectedTargetGameObjectId >= 0) {
			scriptManager_->QueueActionEvent(actionTargetGameObjectId, selectorComponent->targetSelectorFoundActionName);
		}
		else if (previousTargetGameObjectId >= 0 && selectedTargetGameObjectId < 0) {
			scriptManager_->QueueActionEvent(actionTargetGameObjectId, selectorComponent->targetSelectorLostActionName);
		}
		else {
			scriptManager_->QueueActionEvent(actionTargetGameObjectId, selectorComponent->targetSelectorChangedActionName);
		}
	}
}

void EditorTargetingManager::UpdateTargetSteering(float deltaTime) {
	if (editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& steeringGameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* steeringComponent = EditorComponentUtility::FindComponent(
			steeringGameObject,
			EditorComponentType::TargetSteering);

		if (!steeringGameObject.isActive || steeringComponent == nullptr || !steeringComponent->isActive) {
			continue;
		}

		float& elapsedSeconds = steeringElapsedSeconds_[steeringGameObject.id];
		elapsedSeconds += deltaTime;

		if (elapsedSeconds < steeringComponent->targetSteeringStartDelay) {
			continue;
		}

		int32_t targetGameObjectId = steeringComponent->targetSteeringTargetGameObjectId;

		if (targetGameObjectId < 0) {
			const int32_t selectorGameObjectId = steeringComponent->targetSteeringSelectorGameObjectId >= 0
				? steeringComponent->targetSteeringSelectorGameObjectId
				: steeringGameObject.id;
			GetCurrentTarget(selectorGameObjectId, targetGameObjectId);
		}

		const EditorGameObject* targetGameObject = editorScene_->FindGameObject(targetGameObjectId);

		if (targetGameObject == nullptr || !targetGameObject->isActive) {
			continue;
		}

		Vector3 ownerScale{};
		Vector3 ownerRotation{};
		Vector3 ownerPosition{};
		editorScene_->GetWorldTransform(
			steeringGameObject.id,
			ownerScale,
			ownerRotation,
			ownerPosition);
		Vector3 targetPosition = GetTargetPosition(*editorScene_, targetGameObjectId);
		const EditorComponent* targetRigidBody = FindInheritedComponent(
			*editorScene_,
			targetGameObjectId,
			EditorComponentType::RigidBody);

		if (targetRigidBody != nullptr) {
			targetPosition.x += targetRigidBody->velocity.x * steeringComponent->targetSteeringPredictionSeconds;
			targetPosition.y += targetRigidBody->velocity.y * steeringComponent->targetSteeringPredictionSeconds;
			targetPosition.z += targetRigidBody->velocity.z * steeringComponent->targetSteeringPredictionSeconds;
		}

		// Runtime側の現在Modeを解決する。Scriptが SetSteeringMoveMode で切り替えた値を優先し、
		// 未初期化ならScene設定値から始める。
		auto activeModeIterator = steeringActiveMoveModes_.find(steeringGameObject.id);
		if (activeModeIterator == steeringActiveMoveModes_.end()) {
			activeModeIterator = steeringActiveMoveModes_.emplace(
				steeringGameObject.id,
				(std::clamp)(steeringComponent->targetSteeringMoveMode, 0, 6)).first;
			steeringModeElapsedSeconds_[steeringGameObject.id] = 0.0f;
			steeringModeCompletionNotified_[steeringGameObject.id] = false;
		}

		const int32_t activeMoveMode = activeModeIterator->second;
		float& modeElapsedSeconds = steeringModeElapsedSeconds_[steeringGameObject.id];
		bool& hasNotifiedCompletion = steeringModeCompletionNotified_[steeringGameObject.id];
		modeElapsedSeconds += deltaTime;

		// Targetのworld姿勢から、右・前の基準軸を作る。Playerがレールを進んでも
		// 相対位置を維持できるよう、毎Frame Targetの向きから軸を取り直す。
		Vector3 targetScale{};
		Vector3 targetRotation{};
		Vector3 targetOrigin{};
		editorScene_->GetWorldTransform(targetGameObjectId, targetScale, targetRotation, targetOrigin);
		(void)targetScale;
		const Vector3 targetForward = GetForwardVector(targetRotation);
		const Vector3 horizontalForward = NormalizeVector3({targetForward.x, 0.0f, targetForward.z});
		const Vector3 targetRight{horizontalForward.z, 0.0f, -horizontalForward.x};

		// 各Modeが「どこへ行きたいか」を desiredPosition として決め、
		// 実際の移動は共通処理(旋回 + 前進、または相対位置追従)で行う。
		Vector3 desiredPosition = targetPosition;
		bool usesPositionFollow = false;
		bool hasReachedGoal = false;

		const auto makeRelativePosition = [&](float right, float up, float forwardOffset) {
			return Vector3{
				targetOrigin.x + targetRight.x * right + horizontalForward.x * forwardOffset,
				targetOrigin.y + up,
				targetOrigin.z + targetRight.z * right + horizontalForward.z * forwardOffset};
		};

		switch (activeMoveMode) {
			case 2: {
				// Parallel: Targetの左右へ一定距離を保って並走する。
				desiredPosition = makeRelativePosition(
					steeringComponent->targetSteeringSideOffset,
					steeringComponent->targetSteeringVerticalOffset,
					steeringComponent->targetSteeringForwardOffset);
				usesPositionFollow = true;
				break;
			}
			case 3: {
				// Chase: Target後方から追跡し、目標距離まで詰めたら完了扱いにする。
				const float chaseDistance = (std::max)(steeringComponent->targetSteeringTargetDistance, 0.0f);
				desiredPosition = makeRelativePosition(0.0f, steeringComponent->targetSteeringVerticalOffset, -chaseDistance);
				usesPositionFollow = true;
				hasReachedGoal =
					GetVectorLength(SubtractVector3(targetPosition, ownerPosition)) <= chaseDistance + kRayEpsilon;
				break;
			}
			case 4: {
				// KeepDistance: 近すぎれば離れ、遠すぎれば近づき、適正距離ならほぼ並走する。
				const float keepDistance = (std::max)(steeringComponent->targetSteeringTargetDistance, 0.0f);
				const float margin = (std::max)(steeringComponent->targetSteeringDistanceMargin, 0.0f);
				const Vector3 toOwner = SubtractVector3(ownerPosition, targetOrigin);
				const float currentDistance = GetVectorLength(toOwner);
				const Vector3 awayDirection = currentDistance > kRayEpsilon
					? MultiplyVector3(1.0f / currentDistance, toOwner)
					: MultiplyVector3(-1.0f, horizontalForward);
				// 適正距離のときは現在方位を保ったまま横Offsetぶんだけ流す。
				const float desiredRadius = currentDistance < keepDistance - margin
					? keepDistance
					: currentDistance > keepDistance + margin
						? keepDistance
						: currentDistance;
				desiredPosition = {
					targetOrigin.x + awayDirection.x * desiredRadius + targetRight.x * steeringComponent->targetSteeringSideOffset,
					targetOrigin.y + steeringComponent->targetSteeringVerticalOffset,
					targetOrigin.z + awayDirection.z * desiredRadius + targetRight.z * steeringComponent->targetSteeringSideOffset};
				usesPositionFollow = true;
				break;
			}
			case 5: {
				// PlayerRelativeMove: Target相対の開始Offsetから終了Offsetへ移動する。
				// 横切り、正面高速通過、横方向への離脱をScene設定だけで作るための汎用Mode。
				const float duration = (std::max)(steeringComponent->targetSteeringDuration, kRayEpsilon);
				const float normalizedTime = (std::clamp)(modeElapsedSeconds / duration, 0.0f, 1.0f);
				const Vector3& startOffset = steeringComponent->targetSteeringStartOffset;
				const Vector3& endOffset = steeringComponent->targetSteeringEndOffset;
				desiredPosition = makeRelativePosition(
					startOffset.x + (endOffset.x - startOffset.x) * normalizedTime,
					startOffset.y + (endOffset.y - startOffset.y) * normalizedTime,
					startOffset.z + (endOffset.z - startOffset.z) * normalizedTime);
				usesPositionFollow = true;
				hasReachedGoal = normalizedTime >= 1.0f;
				break;
			}
			case 6: {
				// Retreat: Target相対Offset方向へ離れ続ける。攻撃後の離脱に使う。
				desiredPosition = makeRelativePosition(
					steeringComponent->targetSteeringSideOffset,
					steeringComponent->targetSteeringVerticalOffset,
					steeringComponent->targetSteeringForwardOffset);
				usesPositionFollow = true;
				break;
			}
			case 0:
			case 1:
			default:
				// Direct / ArcApproach はどちらもTarget自身を目指す。
				// 差は旋回速度の設定値で表現する(小さいほど大きく弧を描く)。
				desiredPosition = targetPosition;
				usesPositionFollow = false;
				break;
		}

		// 進みたい方向。相対位置Modeでは目的地へのベクトル、直進系ではTargetへのベクトル。
		const Vector3 moveOffset = SubtractVector3(desiredPosition, ownerPosition);
		const float moveDistance = GetVectorLength(moveOffset);

		if (moveDistance > kRayEpsilon) {
			const float horizontalDistance = std::sqrt(moveOffset.x * moveOffset.x + moveOffset.z * moveOffset.z);
			const float desiredYaw = std::atan2(moveOffset.x, moveOffset.z);
			const float desiredPitch = -std::atan2(moveOffset.y, (std::max)(horizontalDistance, kRayEpsilon));
			const float maximumTurn = steeringComponent->targetSteeringTurnSpeed * kPi / 180.0f * deltaTime;
			// 船首を徐々に目標方向へ向け、その船首方向へ前進させる(横滑りさせない)。
			steeringGameObject.rotate.y = MoveTowardsAngle(steeringGameObject.rotate.y, desiredYaw, maximumTurn);
			steeringGameObject.rotate.x = MoveTowardsAngle(steeringGameObject.rotate.x, desiredPitch, maximumTurn);
			const Vector3 forward = GetForwardVector(steeringGameObject.rotate);

			if (steeringComponent->targetSteeringMode == 1 && physicsManager_ != nullptr) {
				physicsManager_->AddForce(
					steeringGameObject.id,
					{
						forward.x * steeringComponent->targetSteeringAcceleration,
						forward.y * steeringComponent->targetSteeringAcceleration,
						forward.z * steeringComponent->targetSteeringAcceleration});
			}
			else {
				float& currentSpeed = steeringSpeeds_[steeringGameObject.id];
				currentSpeed = MoveTowards(
					currentSpeed,
					steeringComponent->targetSteeringMaximumSpeed,
					steeringComponent->targetSteeringAcceleration * deltaTime);
				// 相対位置Modeは目的地を追い越さないよう、残り距離で1Frameの移動量を制限する。
				float frameDistance = currentSpeed * deltaTime;

				if (usesPositionFollow) {
					const float followSpeed = steeringComponent->targetSteeringPositionLerpSpeed > 0.0f
						? steeringComponent->targetSteeringPositionLerpSpeed
						: currentSpeed;
					frameDistance = (std::min)(followSpeed * deltaTime, moveDistance);
				}

				steeringGameObject.translate.x += forward.x * frameDistance;
				steeringGameObject.translate.y += forward.y * frameDistance;
				steeringGameObject.translate.z += forward.z * frameDistance;
			}
		}

		// Duration経過またはMode固有の到達条件でNextMoveModeへ遷移し、完了Actionを通知する。
		const bool hasDurationElapsed =
			steeringComponent->targetSteeringDuration > 0.0f &&
			modeElapsedSeconds >= steeringComponent->targetSteeringDuration;

		// 現在のMode instanceにつき一度だけ通知する。遷移先が無い場合(Scriptに次の行動を
		// 任せる場合)でも、毎Frame同じActionを投げ続けないようにする。
		if ((hasDurationElapsed || hasReachedGoal) && !hasNotifiedCompletion) {
			hasNotifiedCompletion = true;

			if (scriptManager_ != nullptr && !steeringComponent->targetSteeringCompletedActionName.empty()) {
				const int32_t actionTargetGameObjectId =
					steeringComponent->targetSteeringActionTargetGameObjectId >= 0
						? steeringComponent->targetSteeringActionTargetGameObjectId
						: steeringGameObject.id;
				EditorScriptActionPayload payload{};
				payload.type = EditorScriptActionPayloadTypeGameObject;
				payload.gameObjectId = steeringGameObject.id;
				scriptManager_->QueueActionPayload(
					actionTargetGameObjectId,
					steeringComponent->targetSteeringCompletedActionName,
					payload);
			}

			const int32_t nextMoveMode = steeringComponent->targetSteeringNextMoveMode;

			if (nextMoveMode >= 0 && nextMoveMode != activeMoveMode) {
				// Scene設定だけで完結する敵はここで次のModeへ進み、新しいModeで再び通知できるようにする。
				activeModeIterator->second = (std::clamp)(nextMoveMode, 0, 6);
				modeElapsedSeconds = 0.0f;
				hasNotifiedCompletion = false;
			}
		}
	}
}

bool EditorTargetingManager::SetSteeringMoveMode(int32_t steeringGameObjectId, int32_t moveMode) {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* steeringGameObject = editorScene_->FindGameObject(steeringGameObjectId);

	if (steeringGameObject == nullptr) {
		return false;
	}

	const EditorComponent* steeringComponent = EditorComponentUtility::FindComponent(
		*steeringGameObject,
		EditorComponentType::TargetSteering);

	if (steeringComponent == nullptr) {
		return false;
	}

	steeringActiveMoveModes_[steeringGameObjectId] = (std::clamp)(moveMode, 0, 6);
	steeringModeElapsedSeconds_[steeringGameObjectId] = 0.0f;
	steeringModeCompletionNotified_[steeringGameObjectId] = false;
	return true;
}

bool EditorTargetingManager::GetSteeringMoveMode(int32_t steeringGameObjectId, int32_t& moveMode) const {
	const auto activeModeIterator = steeringActiveMoveModes_.find(steeringGameObjectId);

	if (activeModeIterator != steeringActiveMoveModes_.end()) {
		moveMode = activeModeIterator->second;
		return true;
	}

	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* steeringGameObject = editorScene_->FindGameObject(steeringGameObjectId);

	if (steeringGameObject == nullptr) {
		return false;
	}

	const EditorComponent* steeringComponent = EditorComponentUtility::FindComponent(
		*steeringGameObject,
		EditorComponentType::TargetSteering);

	if (steeringComponent == nullptr) {
		return false;
	}

	moveMode = (std::clamp)(steeringComponent->targetSteeringMoveMode, 0, 6);
	return true;
}

void EditorTargetingManager::UpdateReticleUi(const EditorComponent& screenAimComponent) {
	if (editorScene_ == nullptr || screenAimComponent.screenAimReticleGameObjectId < 0) {
		return;
	}

	EditorGameObject* reticleGameObject = editorScene_->FindGameObject(
		screenAimComponent.screenAimReticleGameObjectId);

	if (reticleGameObject == nullptr) {
		static uint32_t debugCounter = 0u;
		if ((debugCounter++ % 60u) == 0u) {
			OutputDebugStringA("[ReticleUI] reticleGameObject is null\n");
		}
		return;
	}

	EditorComponent* rectTransform = EditorComponentUtility::FindComponent(
		*reticleGameObject,
		EditorComponentType::RectTransform);

	if (rectTransform == nullptr || !rectTransform->isActive) {
		static uint32_t debugCounter = 0u;
		if ((debugCounter++ % 60u) == 0u) {
			OutputDebugStringA("[ReticleUI] rectTransform is null or inactive\n");
		}
		return;
	}

	// 描画時の buttonPosition × uniformScale を相殺するため、
	// buttonPosition = normalized × 実サイズ ÷ uniformScale とする
	float referenceGameWidth = kUiReferenceWidth;
	float referenceGameHeight = kUiReferenceHeight;
	float widthHeightMatch = 0.5f;

	if (editorScene_ != nullptr) {
		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const EditorComponent* canvasScaler = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::CanvasScaler);

			if (canvasScaler != nullptr && canvasScaler->isActive) {
				referenceGameWidth = (std::max)(canvasScaler->buttonSize.x, 1.0f);
				referenceGameHeight = (std::max)(canvasScaler->buttonSize.y, 1.0f);
				widthHeightMatch = (std::clamp)(canvasScaler->sliderValue, 0.0f, 1.0f);
				break;
			}
		}
	}

	const float widthScale = g_editorGameWidth / referenceGameWidth;
	const float heightScale = g_editorGameHeight / referenceGameHeight;
	const float uniformScale = std::exp(
		std::log((std::max)(widthScale, 0.0001f)) * (1.0f - widthHeightMatch) +
		std::log((std::max)(heightScale, 0.0001f)) * widthHeightMatch);
	const float inverseUniformScale = 1.0f / (std::max)(uniformScale, 0.0001f);

	// レティクルの表示サイズを求める。
	// テキストの場合、グリフは rect の左上に描画されるため、
	// rect サイズではなく実際のテキストサイズでセンタリングする。
	EditorScriptVector2 visualSize = rectTransform->buttonSize;
	float visualCenterY = visualSize.y * 0.5f;
	const EditorComponent* reticleText = EditorComponentUtility::FindComponent(
		*reticleGameObject,
		EditorComponentType::TextMeshProUGUI);
	if (reticleText == nullptr) {
		reticleText = EditorComponentUtility::FindComponent(
			*reticleGameObject,
			EditorComponentType::Text);
	}
	if (reticleText != nullptr && ImGui::GetCurrentContext() != nullptr) {
		// 描画側と同じ fontSize（EditorGameViewManager.cpp の fontSize 計算と同一）
		const float fontSize = (std::clamp)(
			rectTransform->buttonSize.y * uniformScale,
			8.0f,
			512.0f);
		const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
			fontSize,
			FLT_MAX,
			0.0f,
			reticleText->buttonLabel.c_str());
		visualSize = {
			textSize.x * inverseUniformScale,
			textSize.y * inverseUniformScale};
		visualCenterY = visualSize.y * 0.5f;

		// AddText は行の上端ではなくフォントのグリフ基準位置から描画する。
		// 行高の半分では「+」の実際の中心と一致しないため、グリフの Y0/Y1 を使う。
		if (!reticleText->buttonLabel.empty()) {
			ImFont* font = ImGui::GetFont();
			ImFontBaked* bakedFont = font->GetFontBaked(fontSize);
			const ImFontGlyph* glyph = bakedFont != nullptr
				? bakedFont->FindGlyph(
					static_cast<ImWchar>(static_cast<unsigned char>(reticleText->buttonLabel.front())))
				: nullptr;
			if (glyph != nullptr && bakedFont->Size > 0.0f) {
				const float glyphScale = fontSize / bakedFont->Size;
				const float glyphCenterY =
					(glyph->Y0 + glyph->Y1) * 0.5f * glyphScale * inverseUniformScale;
				const float glyphHeight =
					(glyph->Y1 - glyph->Y0) * glyphScale * inverseUniformScale;
				// AddText の基準位置との差分を含め、見た目の中心をマウスへ合わせる。
				visualCenterY = glyphCenterY + glyphHeight * 0.5f;
			}
		}
	}

	rectTransform->buttonPosition = {
		screenAimComponent.screenAimNormalizedPosition.x * g_editorGameWidth * inverseUniformScale -
			visualSize.x * 0.5f,
		screenAimComponent.screenAimNormalizedPosition.y * g_editorGameHeight * inverseUniformScale -
			visualCenterY};

	static uint32_t debugCounter = 0u;
	if ((debugCounter++ % 60u) == 0u) {
		char debugBuffer[256];
		snprintf(
			debugBuffer,
			sizeof(debugBuffer),
			"[ReticleUI] pos=(%.1f,%.1f) size=(%.1f,%.1f) normalized=(%.2f,%.2f) scale=%.3f\n",
			rectTransform->buttonPosition.x, rectTransform->buttonPosition.y,
			rectTransform->buttonSize.x, rectTransform->buttonSize.y,
			screenAimComponent.screenAimNormalizedPosition.x,
			screenAimComponent.screenAimNormalizedPosition.y,
			uniformScale);
		OutputDebugStringA(debugBuffer);
	}
}
