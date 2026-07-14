#pragma warning(disable : 4189 4514)

#include "EditorGameViewManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui_internal.h"

#include <cmath>

using namespace EditorSharedState;

namespace {
	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	bool IsNearlyZeroVector3(const Vector3& value) {
		return
			std::fabs(value.x) <= 0.001f &&
			std::fabs(value.y) <= 0.001f &&
			std::fabs(value.z) <= 0.001f;
	}

	Transforms ResolveWorldTransform(const EditorGameObject& gameObject) {
		Transforms worldTransform{
			gameObject.scale,
			gameObject.rotate,
			gameObject.translate};

		const EditorGameObject* currentParent = g_editorScene.FindGameObject(gameObject.parentId);
		while (currentParent != nullptr) {
			worldTransform.translate = AddVector3(currentParent->translate, worldTransform.translate);
			worldTransform.rotate = AddVector3(currentParent->rotate, worldTransform.rotate);
			worldTransform.scale = {
				worldTransform.scale.x * currentParent->scale.x,
				worldTransform.scale.y * currentParent->scale.y,
				worldTransform.scale.z * currentParent->scale.z};
			currentParent = g_editorScene.FindGameObject(currentParent->parentId);
		}

		return worldTransform;
	}

	const EditorComponent* FindRuntimeCameraComponent(const EditorGameObject& gameObject) {
		const EditorComponent* cameraComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
		if (cameraComponent != nullptr && cameraComponent->isActive) {
			return cameraComponent;
		}

		const EditorComponent* cinemachineCameraComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CinemachineCamera);
		if (cinemachineCameraComponent != nullptr && cinemachineCameraComponent->isActive) {
			return cinemachineCameraComponent;
		}

		return nullptr;
	}

	Transforms BuildFollowCameraTransform(
		const EditorGameObject& cameraGameObject,
		const EditorComponent& cameraComponent) {
		Transforms gameCameraTransform = ResolveWorldTransform(cameraGameObject);
		if (cameraComponent.connectedGameObjectId < 0 ||
			cameraComponent.connectedGameObjectId == cameraGameObject.id) {
			return gameCameraTransform;
		}

		const EditorGameObject* targetGameObject =
			g_editorScene.FindGameObject(cameraComponent.connectedGameObjectId);
		if (targetGameObject == nullptr || !targetGameObject->isActive) {
			return gameCameraTransform;
		}

		constexpr Vector3 defaultFollowOffset = {0.0f, 2.0f, -6.0f};
		Vector3 followOffset = cameraGameObject.translate;
		if (IsNearlyZeroVector3(followOffset)) {
			followOffset = defaultFollowOffset;
		}

		const Transforms targetWorldTransform = ResolveWorldTransform(*targetGameObject);
		gameCameraTransform.translate = AddVector3(targetWorldTransform.translate, followOffset);
		return gameCameraTransform;
	}

	Transforms GetGameCameraTransform() {
		g_isGameViewUsingSceneCamera = true;  // Camera Component が見つからない場合は Scene カメラを使う。

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* cameraComponent = FindRuntimeCameraComponent(gameObject);
			if (cameraComponent == nullptr) {
				continue;
			}

			g_isGameViewUsingSceneCamera = false;
			return BuildFollowCameraTransform(gameObject, *cameraComponent);
		}

		return g_cameraTransform;
	}

	void UpdateGameCameraMatrices() {
		Transforms gameCameraTransform = GetGameCameraTransform();  // GameView は Camera Component を優先し、なければ Scene カメラを使う。
		g_gameCameraPosition = gameCameraTransform.translate;  // PixelShader の視線方向計算で使う GameView カメラ位置。
		g_gameCameraMatrix = MakeAffineMatrix(
			gameCameraTransform.scale,
			gameCameraTransform.rotate,
			gameCameraTransform.translate);
		g_gameViewMatrix = Inverse(g_gameCameraMatrix);

		// 既定の投影パラメータ（Camera Component がなければこの値を使う）
		float fovY = 0.45f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;
		int32_t projectionMode = 0;

		// Camera Component を検索して投影パラメータを上書き
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}
			const EditorComponent* cameraComponent = FindRuntimeCameraComponent(gameObject);
			if (cameraComponent == nullptr) {
				continue;
			}
			fovY = cameraComponent->cameraFieldOfView * (std::numbers::pi_v<float> / 180.0f);
			nearZ = cameraComponent->cameraNearClip;
			farZ = cameraComponent->cameraFarClip;
			projectionMode = cameraComponent->cameraProjectionMode;
			break;
		}

		if (projectionMode == 1) {
			const float orthoHeight = 10.0f;
			const float aspect = g_editorGameWidth / g_editorGameHeight;
			const float orthoWidth = orthoHeight * aspect;
			g_gameProjectionMatrix = MakeOrthographicMatrix(
				-orthoWidth * 0.5f, -orthoHeight * 0.5f,
				orthoWidth * 0.5f, orthoHeight * 0.5f,
				nearZ, farZ);
		} else {
			g_gameProjectionMatrix = MakePerspectiveFovMatrix(
				fovY,
				g_editorGameWidth / g_editorGameHeight,
				nearZ,
				farZ);
		}
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

	ImGuiWindow* gameWindow = ImGui::GetCurrentWindowRead();  // gameWindow は Docking タブの表示状態込みで GameView が本当に見えているかを判定する。
	bool isGameDockTabVisible =
		gameWindow != nullptr &&
		!gameWindow->Hidden &&
		!gameWindow->SkipItems &&
		(!gameWindow->DockIsActive || gameWindow->DockTabIsVisible);  // Dock 中は選択中タブだけ描画対象にし、隠れタブの残像描画を防ぐ。
	if (!isGameDockTabVisible) {
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
