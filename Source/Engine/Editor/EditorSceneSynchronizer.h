#pragma once

#include "EditorScene.h"
#include "EditorSceneObjectManager.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSceneSynchronizer {
public:
	void Initialize(EditorScene* editorScene, EditorSceneObjectManager* sceneObjectManager);  // 同期元 Scene と同期先 SceneObjectManager を受け取る
	void Update(const std::vector<std::string>& textureFilePaths, int32_t& selectedPlacedSceneObjectIndex);  // GameObject と描画用 SceneObject の生成 / 削除 / Transform 同期を行う
	void Draw();  // 現時点では直接描画なし

private:
	EditorScene* editorScene_ = nullptr;  // 正とする GameObject データ
	EditorSceneObjectManager* sceneObjectManager_ = nullptr;  // DirectX 描画に使う SceneObject データ
};

#pragma warning(pop)
