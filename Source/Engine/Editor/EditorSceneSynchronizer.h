#pragma once

#include "EditorScene.h"
#include "EditorSceneObjectManager.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAnimationManager;

class EditorSceneSynchronizer {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorSceneObjectManager* sceneObjectManager,
		EditorAnimationManager* animationManager);  // Scene、描画 Object、再生中 Bone Pose の同期先を受け取る
	void Update(const std::vector<std::string>& textureFilePaths, int32_t& selectedPlacedSceneObjectIndex);  // GameObject と描画用 SceneObject の生成 / 削除 / Transform 同期を行う
	void Draw();  // 現時点では直接描画なし

private:
	EditorScene* editorScene_ = nullptr;  // 正とする GameObject データ
	EditorSceneObjectManager* sceneObjectManager_ = nullptr;  // DirectX 描画に使う SceneObject データ
	EditorAnimationManager* animationManager_ = nullptr;  // Play 中の Clip 番号と再生秒を Bone Pose へ反映する
};

#pragma warning(pop)
