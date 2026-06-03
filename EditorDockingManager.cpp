#pragma warning(disable : 4189 4514)

#include "EditorDockingManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorDockingManager::Initialize() {
}

void EditorDockingManager::Update() {
}

void EditorDockingManager::Draw() {
#ifdef USE_IMGUI
	//================================================================
	// メイン Viewport 全体を覆う DockSpace ホスト
	//================================================================

	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();  // mainViewport はアプリの作業領域。DockSpace の位置とサイズの基準にする。
	constexpr ImGuiDockNodeFlags mainDockSpaceFlags = ImGuiDockNodeFlags_None;  // mainDockSpaceFlags は DockSpace 自体の挙動。今は標準動作のままにする。

	// dockSpaceHostWindowFlags は DockSpace 専用の透明ウィンドウにするためのフラグ。
	constexpr ImGuiWindowFlags dockSpaceHostWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::SetNextWindowPos(mainViewport->WorkPos);  // DockSpace ホストをメイン Viewport に完全一致させる。
	ImGui::SetNextWindowSize(mainViewport->WorkSize);
	ImGui::SetNextWindowViewport(mainViewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);  // 余白や角丸があると SceneView の描画範囲と Dock 範囲がずれるため 0 にする。
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("MainDockSpaceHost", nullptr, dockSpaceHostWindowFlags);
	ImGui::PopStyleVar(3);

	ImGuiID mainDockSpaceId = ImGui::GetID("MainDockSpace");  // mainDockSpaceId は ImGui 内で DockNode を識別する固定 ID。

	// 初回だけ標準レイアウトを作る。既存 DockNode がある時はユーザー配置を壊さない。
	bool shouldBuildDefaultDockLayout =
		!g_isDockLayoutInitialized && ImGui::DockBuilderGetNode(mainDockSpaceId) == nullptr;

	//================================================================
	// 初回の Unity 風 Dock 分割
	//================================================================

	if (shouldBuildDefaultDockLayout) {
		ImGuiID centerDockId = mainDockSpaceId;  // centerDockId は分割後も SceneView を置く中央領域として残す。
		ImGuiID leftDockId = 0;  // left/right/bottom は DockBuilderSplitNode が書き込む分割先 ID。
		ImGuiID rightDockId = 0;
		ImGuiID bottomDockId = 0;
		ImGuiID gameDockId = 0;

		ImGui::DockBuilderRemoveNode(mainDockSpaceId);  // 古いノードを消してから DockSpace 用ノードを作り直す。
		ImGui::DockBuilderAddNode(
			mainDockSpaceId,
			ImGuiDockNodeFlags_DockSpace | mainDockSpaceFlags);

		ImGui::DockBuilderSetNodePos(mainDockSpaceId, mainViewport->WorkPos);  // 初期 DockNode の矩形をメイン Viewport と同じサイズにする。
		ImGui::DockBuilderSetNodeSize(mainDockSpaceId, mainViewport->WorkSize);

		ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Left, 0.18f, &leftDockId, &centerDockId);  // 左 18%、右 24%、下 28% を切り出し、残りを SceneView の中央領域にする。
		ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Right, 0.24f, &rightDockId, &centerDockId);
		ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Down, 0.28f, &bottomDockId, &centerDockId);
		ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Right, 0.38f, &gameDockId, &centerDockId);  // 中央領域を SceneView と GameView に分ける。

		ImGui::DockBuilderDockWindow("シーン###SceneView", centerDockId);  // ### 以降の ID で、表示名を日本語にしても DockBuilder が同じ窓を認識できる。
		ImGui::DockBuilderDockWindow("ゲーム###GameView", gameDockId);
		ImGui::DockBuilderDockWindow("ヒエラルキー###Hierarchy", leftDockId);
		ImGui::DockBuilderDockWindow("インスペクター###Inspector", rightDockId);
		ImGui::DockBuilderDockWindow("下部パネル###BottomPanel", bottomDockId);

		ImGui::DockBuilderFinish(mainDockSpaceId);  // DockBuilder の編集内容を確定し、以後はユーザーのドラッグ配置を優先する。
	}

	ImGui::DockSpace(mainDockSpaceId, ImVec2(0.0f, 0.0f), mainDockSpaceFlags);  // 実際の DockSpace を表示し、子ウィンドウをドラッグ移動・ドッキング可能にする。
	ImGui::End();

	g_isDockLayoutInitialized = true;  // 初期配置済みフラグ。次フレーム以降は標準配置で上書きしない。
	UpdateEditorLayout();  // Docking 後の SceneView 座標を使って DirectX の viewport / scissor を合わせる。
#endif
}
