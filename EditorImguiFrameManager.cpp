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
