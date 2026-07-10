#pragma once

#pragma warning(disable : 5045)

#include "EditorRuntimeManager.h"
#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorMainMenuBar {
public:
	void Initialize(EditorScene* editorScene, EditorRuntimeManager* runtimeManager);  // Play / Stop 操作に使う Scene と RuntimeManager を受け取る
	void Update();  // 現時点では自動更新なし
	// メインメニューと Play ボタンを描画する
	void Draw(
		std::vector<std::string>& consoleMessages,
		bool& isRuntimeInitialized,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& previousSelectedGameObjectId);

private:
	EditorScene* editorScene_ = nullptr;  // Play 対象の Scene
	EditorRuntimeManager* runtimeManager_ = nullptr;  // Play / Stop を切り替える RuntimeManager
};

#pragma warning(pop)
