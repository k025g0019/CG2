#pragma once

#include "EditorScene.h"
#include "EditorSceneObject.h"

#include <cstdint>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSelectionManager {
public:
	void Initialize(EditorScene* editorScene, std::vector<EditorSceneObject>* sceneObjects);  // 選択同期に使う Scene と描画用 SceneObject 配列を受け取る
	void Update();  // 現時点では自動更新なし
	void Draw();  // 現時点では直接描画なし

	// GameObject 選択を旧 selectedSceneObject / selectedPlacedSceneObjectIndex に同期する
	void SyncLegacySelection(
		int32_t selectedGameObjectId,
		int32_t& selectedSceneObject,
		int32_t& selectedPlacedSceneObjectIndex) const;
	void SyncSelectedPlacedObjectToGameObject(int32_t selectedPlacedSceneObjectIndex) const;  // ギズモで動かした描画用 SceneObject の Transform を GameObject に戻す

private:
	EditorScene* editorScene_ = nullptr;  // 選択対象の GameObject を持つ Scene
	std::vector<EditorSceneObject>* sceneObjects_ = nullptr;  // SceneView で選択される描画用オブジェクト配列
};

#pragma warning(pop)
