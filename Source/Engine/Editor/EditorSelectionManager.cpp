#include "EditorSelectionManager.h"

#include <cstddef>

#pragma warning(disable : 5045)

void EditorSelectionManager::Initialize(EditorScene* editorScene, std::vector<EditorSceneObject>* sceneObjects) {
	editorScene_ = editorScene;  // Scene と描画用配列を相互同期するため保持する
	sceneObjects_ = sceneObjects;
}

void EditorSelectionManager::Update() {
}

void EditorSelectionManager::Draw() {
}

void EditorSelectionManager::SyncLegacySelection(
	int32_t selectedGameObjectId,
	int32_t& selectedSceneObject,
	int32_t& selectedPlacedSceneObjectIndex) const {
	if (editorScene_ == nullptr || sceneObjects_ == nullptr) {
		return;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(selectedGameObjectId);  // 選択中 GameObject が存在しなければ SceneView 側の選択を解除する
	if (gameObject == nullptr) {
		selectedPlacedSceneObjectIndex = -1;
		return;
	}

	selectedPlacedSceneObjectIndex = -1;
	for (int32_t sceneObjectIndex = 0;
	     sceneObjectIndex < static_cast<int32_t>(sceneObjects_->size());
	     sceneObjectIndex++) {
		const EditorSceneObject& sceneObject = (*sceneObjects_)[static_cast<size_t>(sceneObjectIndex)];  // GameObject ID が一致する描画用 SceneObject を探す
		if (sceneObject.gameObjectId == selectedGameObjectId) {
			selectedPlacedSceneObjectIndex = sceneObjectIndex;
			selectedSceneObject = sceneObject.type == EditorSceneObjectType::Model ? 0 : 1;  // 旧 UI の分類番号。Model は 0、Sprite は 1
			return;
		}
	}

	// 描画用 SceneObject がない場合も、Component 種類から Inspector の表示分類を決める
	if (editorScene_->HasComponent(gameObject->id, EditorComponentType::ModelRenderer)) {
		selectedSceneObject = 0;
	}
	else if (editorScene_->HasComponent(gameObject->id, EditorComponentType::SpriteRenderer)) {
		selectedSceneObject = 1;
	}
	else if (editorScene_->HasComponent(gameObject->id, EditorComponentType::Light)) {
		selectedSceneObject = 2;
	}
	else if (editorScene_->HasComponent(gameObject->id, EditorComponentType::Camera)) {
		selectedSceneObject = 3;
	}
}

void EditorSelectionManager::SyncSelectedPlacedObjectToGameObject(int32_t selectedPlacedSceneObjectIndex) const {
	if (editorScene_ == nullptr || sceneObjects_ == nullptr) {
		return;
	}

	// 無効な SceneObject 番号は同期しない
	if (selectedPlacedSceneObjectIndex < 0 ||
		selectedPlacedSceneObjectIndex >= static_cast<int32_t>(sceneObjects_->size())) {
		return;
	}

	const EditorSceneObject& sceneObject =
		(*sceneObjects_)[static_cast<size_t>(selectedPlacedSceneObjectIndex)];

	EditorGameObject* gameObject = editorScene_->FindGameObject(sceneObject.gameObjectId);  // SceneObject が紐づく GameObject を探す
	if (gameObject == nullptr) {
		return;
	}

	gameObject->translate = sceneObject.transform.translate;  // ギズモ結果の Transform を GameObject 側に反映する
	gameObject->rotate = sceneObject.transform.rotate;
	gameObject->scale = sceneObject.transform.scale;
}
