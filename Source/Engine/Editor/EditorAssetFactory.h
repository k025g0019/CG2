#pragma once

#include "EditorScene.h"
#include "EditorSceneObjectManager.h"
#include "Vector.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAssetFactory {
public:
	// Asset から GameObject と SceneObject を作るために必要な参照を受け取る
	void Initialize(
		EditorScene* editorScene,
		EditorSceneObjectManager* sceneObjectManager,
		const std::vector<std::string>* textureFilePaths,
		std::vector<std::string>* consoleMessages);
	void Update();  // 現時点では自動更新なし
	void Draw();  // 現時点では直接描画なし

	// OBJ / FBX などのモデル Asset から GameObject を作る
	void CreateModelGameObject(
		const std::string& assetPath,
		const Vector3& position,
		int32_t& selectedGameObjectId,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& selectedSceneObject);
	// Texture Asset から Sprite GameObject を作る
	void CreateSpriteGameObject(
		const std::string& assetPath,
		const Vector3& position,
		int32_t& selectedGameObjectId,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& selectedSceneObject);

private:
	EditorScene* editorScene_ = nullptr;  // GameObject / Component を作る対象 Scene
	EditorSceneObjectManager* sceneObjectManager_ = nullptr;  // DirectX 描画用 SceneObject を作る Manager
	const std::vector<std::string>* textureFilePaths_ = nullptr;  // Texture Asset の登録済みパス一覧
	std::vector<std::string>* consoleMessages_ = nullptr;  // 生成ログを書き込む Console メッセージ配列
};

#pragma warning(pop)
