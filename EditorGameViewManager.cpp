#pragma warning(disable : 4189 4514)

#include "EditorGameViewManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

using namespace EditorSharedState;

namespace {
	Transforms GetGameCameraTransform() {
		g_isGameViewUsingSceneCamera = true;  // Camera Component が見つからない場合は Scene カメラを使う。

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* cameraComponent =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
			if (cameraComponent == nullptr || !cameraComponent->isActive) {
				continue;
			}

			g_isGameViewUsingSceneCamera = false;
			return Transforms{
				gameObject.scale,
				gameObject.rotate,
				gameObject.translate
			};
		}

		return g_cameraTransform;
	}

	void UpdateGameCameraMatrices() {
		Transforms gameCameraTransform = GetGameCameraTransform();  // GameView は Camera Component を優先し、なければ Scene カメラを使う。
		g_gameCameraMatrix = MakeAffineMatrix(
			gameCameraTransform.scale,
			gameCameraTransform.rotate,
			gameCameraTransform.translate);
		g_gameViewMatrix = Inverse(g_gameCameraMatrix);
		g_gameProjectionMatrix = MakePerspectiveFovMatrix(
			0.45f,
			g_editorGameWidth / g_editorGameHeight,
			0.1f,
			100.0f);
	}
}

void EditorGameViewManager::Initialize() {
}

void EditorGameViewManager::Update() {
}

void EditorGameViewManager::Draw() {
#ifdef USE_IMGUI
	g_isGameViewVisible = false;  // Draw 中に有効な矩形を取れたフレームだけ true にする。

	constexpr ImGuiWindowFlags gameWindowFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowSize(ImVec2(640.0f, 360.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("ゲーム###GameView", nullptr, gameWindowFlags)) {
		ImGui::End();
		return;
	}

	ImVec2 gameContentPosition = ImGui::GetCursorScreenPos();  // GameView 内で DirectX が描画する左上座標。
	ImVec2 gameContentSize = ImGui::GetContentRegionAvail();  // Docking 後の GameView 描画可能サイズ。

	g_editorGameX = gameContentPosition.x;
	g_editorGameY = gameContentPosition.y;
	g_editorGameWidth = (std::max)(gameContentSize.x, 240.0f);
	g_editorGameHeight = (std::max)(gameContentSize.y, 135.0f);
	g_isGameViewVisible = true;

	UpdateGameCameraMatrices();

	ImDrawList* gameDrawList = ImGui::GetWindowDrawList();  // GameView 上へ枠と状態表示だけ重ねる。
	ImVec2 gameMin{g_editorGameX, g_editorGameY};
	ImVec2 gameMax{g_editorGameX + g_editorGameWidth, g_editorGameY + g_editorGameHeight};
	gameDrawList->AddRect(gameMin, gameMax, IM_COL32(75, 95, 120, 255));

	const char* playStateText = g_editorRuntimeManager.IsPlaying() ? "Play中" : "停止中";
	const char* cameraText = g_isGameViewUsingSceneCamera ? "Camera: Scene カメラ代用" : "Camera: Camera Component";
	gameDrawList->AddRectFilled(
		ImVec2(g_editorGameX + 10.0f, g_editorGameY + 10.0f),
		ImVec2(g_editorGameX + 270.0f, g_editorGameY + 58.0f),
		IM_COL32(16, 22, 30, 185),
		6.0f);
	gameDrawList->AddText(
		ImVec2(g_editorGameX + 20.0f, g_editorGameY + 18.0f),
		IM_COL32(235, 240, 245, 255),
		playStateText);
	gameDrawList->AddText(
		ImVec2(g_editorGameX + 20.0f, g_editorGameY + 38.0f),
		g_isGameViewUsingSceneCamera ? IM_COL32(255, 210, 130, 255) : IM_COL32(170, 215, 255, 255),
		cameraText);

	ImGui::Dummy(ImVec2(g_editorGameWidth, g_editorGameHeight));  // ウィンドウの内容領域を GameView の描画領域として確保する。
	ImGui::End();
#endif
}
