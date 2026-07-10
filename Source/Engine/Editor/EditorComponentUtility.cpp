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
