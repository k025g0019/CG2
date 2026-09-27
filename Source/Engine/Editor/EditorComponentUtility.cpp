#include "EditorComponentUtility.h"

EditorComponent* EditorComponentUtility::FindComponent(EditorGameObject& gameObject, EditorComponentType type) {
	for (EditorComponent& component : gameObject.components) {
		// 目的の種類と一致する最初の Component を返す
		if (component.type == type) {
			return &component;
		}
	}

	// Component がない場合は呼び出し側で判定しやすいよう nullptr
	return nullptr;
}

const EditorComponent* EditorComponentUtility::FindComponent(
	const EditorGameObject& gameObject, EditorComponentType type) {
	for (const EditorComponent& component : gameObject.components) {
		// 目的の種類と一致する最初の Component を返す
		if (component.type == type) {
			return &component;
		}
	}

	// Component がない場合は呼び出し側で判定しやすいよう nullptr
	return nullptr;
}

const EditorComponent* EditorComponentUtility::FindInheritedComponent(
	const EditorScene& editorScene,
	int32_t gameObjectId,
	EditorComponentType type,
	int32_t* componentOwnerGameObjectId) {
	const EditorGameObject* currentGameObject = editorScene.FindGameObject(gameObjectId);

	while (currentGameObject != nullptr) {
		const EditorComponent* component = FindComponent(*currentGameObject, type);

		if (component != nullptr && component->isActive) {
			if (componentOwnerGameObjectId != nullptr) {
				*componentOwnerGameObjectId = currentGameObject->id;
			}

			return component;
		}

		currentGameObject = currentGameObject->parentId >= 0
			? editorScene.FindGameObject(currentGameObject->parentId)
			: nullptr;
	}

	return nullptr;
}

Vector3 EditorComponentUtility::ResolveInheritedRigidBodyVelocity(
	const EditorScene& editorScene,
	int32_t sourceGameObjectId,
	bool useParentRigidBody,
	const Vector3& sampleWorldPosition,
	float linearInheritance,
	float angularInheritance) {
	int32_t rigidBodyOwnerGameObjectId = sourceGameObjectId;
	const EditorGameObject* sourceGameObject = editorScene.FindGameObject(sourceGameObjectId);
	const EditorComponent* rigidBody = sourceGameObject != nullptr
		? FindComponent(*sourceGameObject, EditorComponentType::RigidBody)
		: nullptr;

	if ((rigidBody == nullptr || !rigidBody->isActive) && useParentRigidBody) {
		rigidBody = FindInheritedComponent(
			editorScene,
			sourceGameObjectId,
			EditorComponentType::RigidBody,
			&rigidBodyOwnerGameObjectId);
	}

	if (rigidBody == nullptr || !rigidBody->isActive) {
		return {0.0f, 0.0f, 0.0f};
	}

	const EditorGameObject* rigidBodyOwner = editorScene.FindGameObject(rigidBodyOwnerGameObjectId);
	Vector3 rigidBodyWorldPosition{};

	if (rigidBodyOwner != nullptr) {
		Vector3 worldScale = rigidBodyOwner->scale;
		Vector3 worldRotation = rigidBodyOwner->rotate;
		rigidBodyWorldPosition = rigidBodyOwner->translate;
		editorScene.GetWorldTransform(
			rigidBodyOwnerGameObjectId,
			worldScale,
			worldRotation,
			rigidBodyWorldPosition);
	}

	const Vector3 radius{
		sampleWorldPosition.x - rigidBodyWorldPosition.x,
		sampleWorldPosition.y - rigidBodyWorldPosition.y,
		sampleWorldPosition.z - rigidBodyWorldPosition.z};
	const Vector3 angularPointVelocity{
		rigidBody->angularVelocity.y * radius.z - rigidBody->angularVelocity.z * radius.y,
		rigidBody->angularVelocity.z * radius.x - rigidBody->angularVelocity.x * radius.z,
		rigidBody->angularVelocity.x * radius.y - rigidBody->angularVelocity.y * radius.x};

	return {
		rigidBody->velocity.x * linearInheritance + angularPointVelocity.x * angularInheritance,
		rigidBody->velocity.y * linearInheritance + angularPointVelocity.y * angularInheritance,
		rigidBody->velocity.z * linearInheritance + angularPointVelocity.z * angularInheritance};
}
