#pragma warning(disable : 4189 4514)

#include "EditorPvShootWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

using namespace EditorSharedState;

namespace {
	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 MultiplyVector3(float scalar, const Vector3& value) {
		return {value.x * scalar, value.y * scalar, value.z * scalar};
	}

	float LengthVector3(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 ResolveWorldPosition(const EditorGameObject& gameObject) {
		Vector3 scale = gameObject.scale;
		Vector3 rotate = gameObject.rotate;
		Vector3 translate = gameObject.translate;
		g_editorScene.GetWorldTransform(gameObject.id, scale, rotate, translate);
		return translate;
	}

	// マウスが実際にGameView（###StandaloneGameView）の上に乗っているかを判定する。
	// 単純な矩形判定だとPV撮影パネル自身の上でも真になってしまい、
	// パネル操作中にCameraが暴れるため、Hover中のWindow自体を比較する。
	bool IsPointerOverGameView() {
		ImGuiContext* context = ImGui::GetCurrentContext();
		ImGuiWindow* gameWindow = ImGui::FindWindowByName("###StandaloneGameView");
		return context != nullptr && gameWindow != nullptr && context->HoveredWindow == gameWindow;
	}
	// PV撮影で動かす対象Cameraを、GameViewと同じPriority優先度で探す。
	EditorComponent* FindActivePvCameraComponent(EditorGameObject** outGameObject) {
		EditorGameObject* selectedGameObject = nullptr;
		EditorComponent* selectedComponent = nullptr;
		int32_t selectedPriority = INT32_MIN;

		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			EditorComponent* cameraComponent =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
			if (cameraComponent == nullptr || !cameraComponent->isActive) {
				EditorComponent* cinemachineCameraComponent =
					EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CinemachineCamera);
				if (cinemachineCameraComponent != nullptr && cinemachineCameraComponent->isActive) {
					cameraComponent = cinemachineCameraComponent;
				}
				else {
					cameraComponent = nullptr;
				}
			}

			if (cameraComponent == nullptr || cameraComponent->cameraPriority <= selectedPriority) {
				continue;
			}

			selectedGameObject = &gameObject;
			selectedComponent = cameraComponent;
			selectedPriority = cameraComponent->cameraPriority;
		}

		if (outGameObject != nullptr) {
			*outGameObject = selectedGameObject;
		}
		return selectedComponent;
	}

	// Exposure / Bloom を持つ PostProcess Component をScene内から探す。通常はScene内に1つ。
	EditorComponent* FindActivePostProcessComponent() {
		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			EditorComponent* postProcessComponent =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::PostProcess);
			if (postProcessComponent != nullptr && postProcessComponent->isActive) {
				return postProcessComponent;
			}
		}
		return nullptr;
	}
}  // namespace

void EditorPvShootWindowManager::Initialize() {
	pvCameraController_.Initialize();
}

void EditorPvShootWindowManager::Update() {
	g_pvShootManualTimeScale = g_isPvShootModeActive ? manualTimeScale_ : 1.0f;
}

void EditorPvShootWindowManager::Draw() {
#ifdef USE_IMGUI
	if (g_isStandaloneGame) {
		return;
	}

	DrawToggleWindow();

	if (g_isPvShootModeActive) {
		DrawControlPanel();
	}
#endif
}

bool EditorPvShootWindowManager::IsActive() const {
	return g_isPvShootModeActive;
}

void EditorPvShootWindowManager::DrawToggleWindow() {
#ifdef USE_IMGUI
	constexpr ImGuiWindowFlags toggleWindowFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("PV撮影###PvShootToggle", nullptr, toggleWindowFlags)) {
		ImGui::Checkbox("PV撮影モード", &g_isPvShootModeActive);
		ImGui::TextDisabled("ONの間はGameViewだけ全画面表示します。");
	}
	ImGui::End();
#endif
}

void EditorPvShootWindowManager::DrawPlaybackControls() {
#ifdef USE_IMGUI
	// EditorDiagnosticsWindowManager::DrawReplay と同じEditorReplayManagerを使う。
	// PV撮影モード中はDiagnostics Windowが隠れるため、ここへ同じ操作を出す。
	constexpr const char* pvReplayPath = "runtime_cache/replays/pv_shoot.cgreplay";
	EditorReplayManager& replayManager = g_editorRuntimeManager.GetReplayManager();
	const EditorReplayMode replayMode = replayManager.GetMode();
	const char* replayModeName = "停止";

	if (replayMode == EditorReplayMode::Recording) {
		replayModeName = "記録中";
	}
	else if (replayMode == EditorReplayMode::Playback) {
		replayModeName = "再生中";
	}

	ImGui::Text("状態: %s", replayModeName);
	ImGui::Text(
		"Frame: %zu / %zu",
		replayManager.GetPlaybackFrameIndex(),
		replayManager.GetFrameCount());

	if (replayMode != EditorReplayMode::Recording && ImGui::Button("1. Scene先頭からPlayerを記録")) {
		if (g_editorRuntimeManager.IsPlaying()) {
			g_editorRuntimeManager.TogglePlay();
		}

		replayManager.StartRecording();
		g_editorRuntimeManager.TogglePlay();
	}

	if (replayMode == EditorReplayMode::Recording) {
		ImGui::SameLine();

		if (ImGui::Button("記録停止・保存")) {
			replayManager.Stop();
			replayManager.Save(pvReplayPath);
		}
	}

	if (replayMode != EditorReplayMode::Playback && replayManager.GetFrameCount() > 0u) {
		if (ImGui::Button("2. 記録した動きを再生しながら撮影")) {
			if (g_editorRuntimeManager.IsPlaying()) {
				g_editorRuntimeManager.TogglePlay();
			}

			replayManager.StartPlayback();
			g_editorRuntimeManager.TogglePlay();
		}
	}

	if (replayMode == EditorReplayMode::Playback) {
		ImGui::SameLine();

		if (ImGui::Button("再生停止")) {
			replayManager.Stop();
		}
	}

	if (ImGui::Button("保存済みの記録を読込")) {
		replayManager.Load(pvReplayPath);
	}
#endif
}

void EditorPvShootWindowManager::DrawControlPanel() {
#ifdef USE_IMGUI
	ImGui::SetNextWindowPos(ImVec2(8.0f, 96.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("PV撮影パネル###PvShootPanel")) {
		ImGui::End();
		return;
	}

	ImGui::SeparatorText("Playプレイバック");
	ImGui::TextDisabled("先にPlayerの動きを記録し、その再生を流しながらCameraだけ自由に動かせます。");
	DrawPlaybackControls();

	EditorGameObject* cameraGameObject = nullptr;
	EditorComponent* cameraComponent = FindActivePvCameraComponent(&cameraGameObject);

	ImGui::SeparatorText("Camera");
	if (cameraGameObject != nullptr && cameraComponent != nullptr) {
		ImGui::TextDisabled("GameView上で右/中ドラッグ・ホイール・WASD/矢印/QEでSceneViewと同様に操作できます。");

		const bool isPointerOverGameView = IsPointerOverGameView();
		Transforms cameraRuntimeTransform{
			cameraGameObject->scale,
			cameraGameObject->rotate,
			cameraGameObject->translate};

		pvCameraController_.UpdateMouse(
			isPointerOverGameView,
			isMiddleCameraDragging_,
			isRightCameraDragging_,
			cameraRuntimeTransform,
			g_editorCameraRotateSpeed,
			g_editorCameraPanSpeed,
			g_editorCameraWheelMoveSpeed);

		Transforms unusedUvTransform{};
		pvCameraController_.UpdateKeyboard(
			g_key,
			g_preKey,
			false,  // PV撮影中は常に操作できるようにする（SceneViewはPlay中を弾くがここでは弾かない）。
			cameraRuntimeTransform,
			unusedUvTransform,
			g_editorCameraMoveSpeed,
			g_editorCameraRotateSpeed,
			g_editorCameraFastRate);

		cameraGameObject->translate = cameraRuntimeTransform.translate;
		cameraGameObject->rotate = cameraRuntimeTransform.rotate;

		ImGui::DragFloat3("位置", &cameraGameObject->translate.x, 0.05f);

		constexpr float radianToDegree = 180.0f / std::numbers::pi_v<float>;
		constexpr float degreeToRadian = std::numbers::pi_v<float> / 180.0f;
		float rotationDegrees[3] = {
			cameraGameObject->rotate.x * radianToDegree,
			cameraGameObject->rotate.y * radianToDegree,
			cameraGameObject->rotate.z * radianToDegree};
		if (ImGui::DragFloat3("回転", rotationDegrees, 0.5f)) {
			cameraGameObject->rotate.x = rotationDegrees[0] * degreeToRadian;
			cameraGameObject->rotate.y = rotationDegrees[1] * degreeToRadian;
			cameraGameObject->rotate.z = rotationDegrees[2] * degreeToRadian;
		}

		ImGui::SliderFloat("FOV", &cameraComponent->cameraFieldOfView, 1.0f, 179.0f);

		ImGui::SeparatorText("注視");
		ImGui::Checkbox("対象を常に注視", &lookAtPlayerEnabled_);

		if (lookAtPlayerEnabled_) {
			const EditorGameObject* currentTarget = g_editorScene.FindGameObject(lookAtTargetGameObjectId_);

			if (currentTarget == nullptr) {
				// 未選択なら名前に"Player"を含む最初のGameObjectを自動選択する。
				for (const EditorGameObject& candidate : g_editorScene.GetGameObjects()) {
					if (candidate.name.find("Player") != std::string::npos) {
						lookAtTargetGameObjectId_ = candidate.id;
						currentTarget = &candidate;
						break;
					}
				}
			}

			const char* previewName = currentTarget != nullptr ? currentTarget->name.c_str() : "(未選択)";
			if (ImGui::BeginCombo("注視対象", previewName)) {
				for (const EditorGameObject& candidate : g_editorScene.GetGameObjects()) {
					const bool isSelected = candidate.id == lookAtTargetGameObjectId_;
					if (ImGui::Selectable(candidate.name.c_str(), isSelected)) {
						lookAtTargetGameObjectId_ = candidate.id;
					}
				}
				ImGui::EndCombo();
			}

			const EditorGameObject* lookAtTarget = g_editorScene.FindGameObject(lookAtTargetGameObjectId_);
			if (lookAtTarget != nullptr) {
				const Vector3 targetWorldPosition = ResolveWorldPosition(*lookAtTarget);
				const Vector3 lookDirection = SubtractVector3(targetWorldPosition, cameraGameObject->translate);
				const float lookDistance = LengthVector3(lookDirection);

				if (lookDistance > 0.0001f) {
					const Vector3 normalizedDirection = MultiplyVector3(1.0f / lookDistance, lookDirection);
					cameraGameObject->rotate.x = -std::asin((std::clamp)(normalizedDirection.y, -1.0f, 1.0f));
					cameraGameObject->rotate.y = std::atan2(normalizedDirection.x, normalizedDirection.z);
				}
			}
			else {
				ImGui::TextDisabled("注視対象が見つかりません。");
			}
		}
	}
	else {
		ImGui::TextDisabled("Camera Component が見つかりません。");
	}

	EditorComponent* postProcessComponent = FindActivePostProcessComponent();

	ImGui::SeparatorText("PostProcess");
	if (postProcessComponent != nullptr) {
		ImGui::SliderFloat("露出", &postProcessComponent->compositeExposure, 0.0f, 8.0f);
		ImGui::SliderFloat("Bloom強度", &postProcessComponent->bloomIntensity, 0.0f, 5.0f);
	}
	else {
		ImGui::TextDisabled("PostProcess Component が見つかりません。");
	}

	ImGui::SeparatorText("再生速度");
	ImGui::SliderFloat("TimeScale", &manualTimeScale_, 0.0f, 2.0f);
	if (ImGui::Button("1.0にリセット")) {
		manualTimeScale_ = 1.0f;
	}

	ImGui::End();
#endif
}
