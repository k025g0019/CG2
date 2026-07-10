#pragma once

#include "EditorAssetFactory.h"
#include "EditorScene.h"
#include "EditorSelectionManager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorHierarchyPanel {
public:
	// Hierarchy が操作する Scene / 選択 / Asset 生成の参照を受け取る
	void Initialize(
		EditorScene* editorScene,
		EditorSelectionManager* selectionManager,
		EditorAssetFactory* assetFactory,
		std::vector<std::string>* consoleMessages);
	void Update();  // 現時点では自動更新なし
	// Hierarchy の検索、生成ボタン、親子ツリーを描画する
	void Draw(
		char* hierarchyFilter,
		size_t hierarchyFilterSize,
		int32_t& selectedGameObjectId,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& previousSelectedGameObjectId,
		int32_t& selectedSceneObject);

private:
	EditorScene* editorScene_ = nullptr;  // 表示する GameObject を持つ Scene
	EditorSelectionManager* selectionManager_ = nullptr;  // Hierarchy 選択を SceneView / Inspector 選択へ同期する Manager
	EditorAssetFactory* assetFactory_ = nullptr;  // ボタンから FBX GameObject を作る Factory
	std::vector<std::string>* consoleMessages_ = nullptr;  // 生成ログを書き込む Console メッセージ配列

	// 親子階層 1 ノード分を描画する
	void DrawGameObjectNode(
		int32_t gameObjectId,
		int32_t depth,
		const char* hierarchyFilter,
		int32_t& selectedGameObjectId,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& selectedSceneObject);
};

#pragma warning(pop)
