#pragma warning(disable : 4189 4514)

#include "EditorHierarchyWindowManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorHierarchyWindowManager::Initialize() {
}

void EditorHierarchyWindowManager::Update() {
}

void EditorHierarchyWindowManager::Draw() {
#ifdef USE_IMGUI
	//================================================================
	// Hierarchy ウィンドウの ImGui 描画
	//================================================================

	constexpr float editorMenuHeight = 20.0f;  // editorMenuHeight は上部メニューの高さ。初回表示位置をメニュー下に合わせる。
	constexpr ImGuiWindowFlags dockableWindowFlags = ImGuiWindowFlags_NoCollapse;  // dockableWindowFlags は Docking 先で折りたたまれて操作不能になるのを防ぐ。
	ImGui::SetNextWindowPos(ImVec2(0.0f, editorMenuHeight), ImGuiCond_FirstUseEver);  // 初回だけ左領域に置く。以後は Docking のユーザー配置を優先する。

	// 初回サイズは左幅と、上メニュー・下部パネルを除いた高さから決める。
	ImGui::SetNextWindowSize(
		ImVec2(g_editorLeftWidth, g_editorWindowHeight - editorMenuHeight - g_editorBottomHeight),
		ImGuiCond_FirstUseEver);

	ImGui::Begin("ヒエラルキー###Hierarchy", nullptr, dockableWindowFlags);  // ###Hierarchy は DockBuilder と一致させる固定 ID。

	// Draw 内で検索文字列、選択 GameObject、旧プレビュー選択を更新する。
	g_editorHierarchyPanel.Draw(
		g_hierarchyFilter,
		_countof(g_hierarchyFilter),
		g_selectedEditorGameObjectId,
		g_selectedPlacedSceneObjectIndex,
		g_previousSelectedEditorGameObjectId,
		g_selectedSceneObject);
	ImGui::End();
#endif
}
