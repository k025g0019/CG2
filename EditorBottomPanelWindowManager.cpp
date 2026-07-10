#pragma warning(disable : 4189 4514)

#include "EditorBottomPanelWindowManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorBottomPanelWindowManager::Initialize() {
}

void EditorBottomPanelWindowManager::Update() {
}

void EditorBottomPanelWindowManager::Draw() {
#ifdef USE_IMGUI
	//================================================================
	// BottomPanel ウィンドウの ImGui 描画
	//================================================================

	constexpr ImGuiWindowFlags dockableWindowFlags = ImGuiWindowFlags_NoCollapse;  // dockableWindowFlags は Docking 先で折りたたまれて Project が見えなくなるのを防ぐ。
	float bottomPanelWidth = g_editorWindowWidth - g_editorRightWidth;  // bottomPanelWidth は右 Inspector を除いた幅。初回配置の目安として BottomPanel に渡す。

	// Draw 内で Project 検索、アセット選択、Console クリア、Play 状態表示を処理する。
	g_editorBottomPanel.Draw(
		g_editorWindowHeight,
		g_editorBottomHeight,
		bottomPanelWidth,
		dockableWindowFlags,
		g_assetFilter,
		_countof(g_assetFilter),
		g_selectedAssetPath,
		g_editorTextureFilePaths,
		g_textureSrvHandlesGPU,
		_countof(g_textureFilePaths),
		g_isConsoleCleared,
		g_editorConsoleMessages,
		g_editorSceneWidth,
		g_editorSceneHeight,
		g_editorRuntimeManager.IsPlaying());
#endif
}
