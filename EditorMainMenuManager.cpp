#pragma warning(disable : 4189 4514)

#include "EditorMainMenuManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorMainMenuManager::Initialize() {
}

void EditorMainMenuManager::Update() {
}

void EditorMainMenuManager::Draw() {
#ifdef USE_IMGUI
	//================================================================
	// 上部メニューの ImGui 描画
	//================================================================

	// Console への操作ログ、Runtime 初期化済みフラグ、選択解除用 ID を MainMenuBar に渡す。
	g_editorMainMenuBar.Draw(
		g_editorConsoleMessages,
		g_isEditorRuntimeInitialized,
		g_selectedPlacedSceneObjectIndex,
		g_previousSelectedEditorGameObjectId);
#endif
}
