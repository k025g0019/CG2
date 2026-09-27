#pragma warning(disable : 4189 4514)

#include "EditorImguiFrameManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorImguiFrameManager::Initialize() {
}

void EditorImguiFrameManager::Update() {
	// ImGui バックエンドは DirectX / HWND に依存するため、初期化前と終了後は呼ばない。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

#ifdef USE_IMGUI
	//================================================================
	// ImGui と ImGuizmo のフレーム開始
	//================================================================

	ImGui_ImplDX12_NewFrame();  // DX12 バックエンドに、今フレーム用の Descriptor / CommandList 状態を準備させる。
	ImGui_ImplWin32_NewFrame();  // Win32 バックエンドに、マウス座標やキーボード入力を ImGuiIO へ反映させる。
	ImGui::NewFrame();  // この呼び出し以降、Begin / Button / DragFloat などで UI を組み立てられる。
	// ImGui の画面座標の原点を控える。ViewportsEnable 中はデスクトップ基準になるため、
	// D3D12 の viewport / scissor へ渡す時にこの分を引いて back buffer 座標へ戻す。
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	g_editorRenderOriginX = mainViewport != nullptr ? mainViewport->Pos.x : 0.0f;
	g_editorRenderOriginY = mainViewport != nullptr ? mainViewport->Pos.y : 0.0f;

	ImGuizmo::BeginFrame();  // ImGuizmo は ImGui の DrawList 上にギズモを描くため、ImGui フレーム開始後に呼ぶ。
#endif
}

void EditorImguiFrameManager::Draw() {
	// ImGui 描画データを確定できない状態では、Renderer に描画要求を渡さない。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

#ifdef USE_IMGUI
	//================================================================
	// ImGui DrawData の確定
	//================================================================

	ImGui::Render();  // ここで ImGui::GetDrawData() が Renderer から使える状態になる。
#endif

	g_isDrawRequested = true;  // RendererManager はこのフラグが true のフレームだけ GPU コマンドを発行する。
}

void EditorImguiFrameManager::RenderPlatformWindows() {
	// 分離 Window はメイン ImGui DrawData を描画した後に処理する。
	// ImGui の Win32 / DX12 backend が各 Window 用 HWND・SwapChain・CommandList を管理する。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

#ifdef USE_IMGUI
	const ImGuiIO& io = ImGui::GetIO();
	if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0) {
		return;
	}

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
#endif
}
