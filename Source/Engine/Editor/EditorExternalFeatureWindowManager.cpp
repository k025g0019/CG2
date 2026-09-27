#include "EditorExternalFeatureWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "Source/Engine/Haptics/HapticSystem.h"
#include "Source/Engine/Online/OnlineService.h"
#include "Source/Engine/Speech/SpeechSystem.h"
#include "Source/Engine/Vision/VisionSystem.h"

#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

#include <algorithm>
#include <string>
#include <vector>

using namespace EditorSharedState;

namespace {
	constexpr int32_t kPreviewBlockWidth = 64;   // Preview を描く横方向のブロック数。
	constexpr int32_t kPreviewBlockHeight = 48;  // 縦方向のブロック数。

	// D3D12 テクスチャを増やさずに Camera 映像を確認できるよう、
	// 縮小したブロックを ImDrawList で塗って Preview にする。
	void DrawFramePreview(const ImageFrame& frame, float blockSize) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float previewWidth = static_cast<float>(kPreviewBlockWidth) * blockSize;
		const float previewHeight = static_cast<float>(kPreviewBlockHeight) * blockSize;

		for (int32_t blockY = 0; blockY < kPreviewBlockHeight; ++blockY) {
			for (int32_t blockX = 0; blockX < kPreviewBlockWidth; ++blockX) {
				const int32_t pixelX = (std::min)(
					frame.width - 1,
					blockX * frame.width / kPreviewBlockWidth);
				const int32_t pixelY = (std::min)(
					frame.height - 1,
					blockY * frame.height / kPreviewBlockHeight);
				const size_t pixelOffset =
					(static_cast<size_t>(pixelY) * static_cast<size_t>(frame.width) +
					 static_cast<size_t>(pixelX)) * 4u;
				const ImU32 color = IM_COL32(
					frame.pixels[pixelOffset + 2u],
					frame.pixels[pixelOffset + 1u],
					frame.pixels[pixelOffset + 0u],
					255);

				const ImVec2 blockMinimum(
					origin.x + static_cast<float>(blockX) * blockSize,
					origin.y + static_cast<float>(blockY) * blockSize);
				const ImVec2 blockMaximum(blockMinimum.x + blockSize, blockMinimum.y + blockSize);
				drawList->AddRectFilled(blockMinimum, blockMaximum, color);
			}
		}

		ImGui::Dummy(ImVec2(previewWidth, previewHeight));
	}

	// Preview 矩形へ認識結果を重ねる(仕様書 32 項)。
	void DrawResultOverlay(const VisionResult& result, const ImVec2& previewOrigin, float previewWidth, float previewHeight) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (const ObjectDetectionResult& object : result.objects) {
			const ImVec2 boxMinimum(
				previewOrigin.x + object.x * previewWidth,
				previewOrigin.y + object.y * previewHeight);
			const ImVec2 boxMaximum(
				boxMinimum.x + object.width * previewWidth,
				boxMinimum.y + object.height * previewHeight);
			drawList->AddRect(boxMinimum, boxMaximum, IM_COL32(80, 220, 120, 255), 0.0f, 0, 2.0f);
			const std::string labelText = object.label + " " + std::to_string(static_cast<int32_t>(object.confidence * 100.0f)) + "%";
			drawList->AddText(
				ImVec2(boxMinimum.x + 2.0f, boxMinimum.y - 14.0f),
				IM_COL32(80, 220, 120, 255),
				labelText.c_str());
		}

		for (const FaceDetectionResult& face : result.faces) {
			const ImVec2 boxMinimum(
				previewOrigin.x + face.x * previewWidth,
				previewOrigin.y + face.y * previewHeight);
			const ImVec2 boxMaximum(
				boxMinimum.x + face.width * previewWidth,
				boxMinimum.y + face.height * previewHeight);
			drawList->AddRect(boxMinimum, boxMaximum, IM_COL32(240, 200, 80, 255), 0.0f, 0, 2.0f);
		}

		for (const FaceLandmarkPoint& landmark : result.faceLandmarks) {
			drawList->AddCircleFilled(
				ImVec2(previewOrigin.x + landmark.x * previewWidth, previewOrigin.y + landmark.y * previewHeight),
				2.0f,
				IM_COL32(255, 120, 200, 255));
		}

		if (result.colorTracking.isDetected) {
			const ImVec2 boxMinimum(
				previewOrigin.x + result.colorTracking.boundsX * previewWidth,
				previewOrigin.y + result.colorTracking.boundsY * previewHeight);
			const ImVec2 boxMaximum(
				boxMinimum.x + result.colorTracking.boundsWidth * previewWidth,
				boxMinimum.y + result.colorTracking.boundsHeight * previewHeight);
			drawList->AddRect(boxMinimum, boxMaximum, IM_COL32(120, 180, 255, 255), 0.0f, 0, 2.0f);
			drawList->AddCircle(
				ImVec2(
					previewOrigin.x + result.colorTracking.centerX * previewWidth,
					previewOrigin.y + result.colorTracking.centerY * previewHeight),
				5.0f,
				IM_COL32(120, 180, 255, 255));
		}

		if (result.motion.motion) {
			drawList->AddCircle(
				ImVec2(
					previewOrigin.x + result.motion.centerX * previewWidth,
					previewOrigin.y + result.motion.centerY * previewHeight),
				8.0f + result.motion.motionMagnitude * 24.0f,
				IM_COL32(255, 140, 80, 255),
				0,
				2.0f);
		}
	}
}

void EditorExternalFeatureWindowManager::Initialize() {
	// 外部機能側の状態は各 System が持つため、ここでは表示状態だけを扱う。
	selectedCameraGameObjectId_ = -1;
}

void EditorExternalFeatureWindowManager::Update() {
	// Play 中は EditorExternalFeatureManager が各 System を進めるため、
	// ここでは Play していない間の Editor Preview だけを進める。
	if (!g_editorRuntimeManager.IsPlaying()) {
		const float deltaTime = ImGui::GetIO().DeltaTime;
		HapticSystem::Get().Update(deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f);
		ExternalFeatureLog::Flush();
	}
}

void EditorExternalFeatureWindowManager::Draw() {
	if (!g_isExternalFeatureWindowVisible) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 480.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("外部認識・オンライン", &g_isExternalFeatureWindowVisible)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("ExternalFeatureTabs")) {
		if (ImGui::BeginTabItem("音声認識")) {
			DrawSpeechTab();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("画像認識")) {
			DrawVisionTab();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("オンライン")) {
			DrawOnlineTab();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Haptics")) {
			DrawHapticsTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

void EditorExternalFeatureWindowManager::DrawSpeechTab() {
	const SpeechRuntimeStatus status = SpeechSystem::Get().GetStatus();

	ImGui::Text("状態: %s", ToDisplayString(status.state));
	ImGui::Text("Backend: %s", status.backendName.empty() ? "未初期化" : status.backendName.c_str());
	ImGui::Text("マイク: %s", status.deviceName.empty() ? "未初期化" : status.deviceName.c_str());
	ImGui::Text("マイク入力: %s", status.isMicrophoneActive ? "開いている" : "停止");
	ImGui::Text("認識中: %s", status.isRecognizing ? "はい" : "いいえ");

	ImGui::Text("音量");
	ImGui::SameLine();
	ImGui::ProgressBar(status.audioLevel, ImVec2(-1.0f, 0.0f));

	ImGui::Separator();
	ImGui::Text("直近の認識文字列");
	ImGui::TextWrapped("%s", status.lastText.empty() ? "(なし)" : status.lastText.c_str());
	ImGui::Text("Confidence: %.2f", status.lastConfidence);
	ImGui::Text("直近キーワード: %s", status.lastKeyword.empty() ? "(なし)" : status.lastKeyword.c_str());
	ImGui::Text("確定回数: %d", status.recognizedCount);

	if (status.lastError.HasError()) {
		ImGui::Separator();
		ImGui::TextColored(
			ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
			"エラー(code=%d): %s",
			status.lastError.code,
			status.lastError.message.c_str());
	}

	ImGui::Separator();
	ImGui::TextDisabled("SpeechRecognizer Component を持つ GameObject");

	for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		const EditorComponent* speechComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SpeechRecognizer);

		if (speechComponent == nullptr) {
			continue;
		}

		ImGui::PushID(gameObject.id);
		const bool isRecognizing = SpeechSystem::Get().IsRecognizing(gameObject.id);
		ImGui::Text("%s (ID %d): %s", gameObject.name.c_str(), gameObject.id, isRecognizing ? "認識中" : "停止");

		if (g_editorRuntimeManager.IsPlaying()) {
			ImGui::SameLine();

			if (isRecognizing) {
				if (ImGui::SmallButton("停止")) {
					g_editorRuntimeManager.GetExternalFeatureManager().StopSpeechRecognition(gameObject.id);
				}
			}
			else if (ImGui::SmallButton("開始")) {
				g_editorRuntimeManager.GetExternalFeatureManager().StartSpeechRecognition(gameObject.id);
			}
		}

		SpeechResult lastResult{};

		if (SpeechSystem::Get().TryGetLatestResult(gameObject.id, lastResult)) {
			ImGui::TextDisabled(
				"  直近: %s (%.2f) Keyword=%s",
				lastResult.text.c_str(),
				lastResult.confidence,
				lastResult.matchedKeyword.empty() ? "-" : lastResult.matchedKeyword.c_str());
		}

		ImGui::PopID();
	}
}

void EditorExternalFeatureWindowManager::DrawVisionTab() {
	VisionSystem& visionSystem = VisionSystem::Get();
	const std::vector<int32_t> cameraGameObjectIds = visionSystem.GetCameraGameObjectIds();

	ImGui::SliderFloat("Preview 倍率", &previewScale_, 1.0f, 6.0f, "%.1f");

	if (cameraGameObjectIds.empty()) {
		ImGui::TextDisabled("開いている Camera がありません。");
	}

	for (const int32_t cameraGameObjectId : cameraGameObjectIds) {
		ImGui::PushID(cameraGameObjectId);
		const VisionRuntimeStatus cameraStatus = visionSystem.GetCameraStatus(cameraGameObjectId);
		ImGui::Text(
			"Camera (ID %d): %s / %s",
			cameraGameObjectId,
			ToDisplayString(cameraStatus.cameraState),
			cameraStatus.cameraDeviceName.empty() ? "未初期化" : cameraStatus.cameraDeviceName.c_str());
		ImGui::TextDisabled(
			"  解像度: %d x %d / 取得 FPS: %.1f / 累計 %d フレーム",
			cameraStatus.frameWidth,
			cameraStatus.frameHeight,
			cameraStatus.captureFps,
			cameraStatus.capturedFrameCount);

		if (cameraStatus.lastError.HasError()) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
				"  エラー: %s",
				cameraStatus.lastError.message.c_str());
		}

		const ImageFrame* frame = visionSystem.GetLatestFrame(cameraGameObjectId);

		if (frame != nullptr) {
			const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
			DrawFramePreview(*frame, previewScale_);

			// この Camera を使っている認識結果を重ねる。
			for (const int32_t recognizerGameObjectId : visionSystem.GetRecognizerGameObjectIds()) {
				VisionResult result{};

				if (!visionSystem.TryGetResult(recognizerGameObjectId, result) || !result.isValid) {
					continue;
				}

				DrawResultOverlay(
					result,
					previewOrigin,
					static_cast<float>(kPreviewBlockWidth) * previewScale_,
					static_cast<float>(kPreviewBlockHeight) * previewScale_);
			}
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::TextDisabled("ImageRecognizer Component の認識結果");

	for (const int32_t recognizerGameObjectId : visionSystem.GetRecognizerGameObjectIds()) {
		ImGui::PushID(recognizerGameObjectId);
		const VisionRuntimeStatus status = visionSystem.GetRecognizerStatus(recognizerGameObjectId);
		ImGui::Text(
			"Recognizer (ID %d): %s / Backend %s",
			recognizerGameObjectId,
			ToDisplayString(status.recognitionState),
			status.backendName.c_str());
		ImGui::TextDisabled(
			"  推論: %.1f ms / 認識フレーム %d",
			status.inferenceMilliseconds,
			status.recognizedFrameCount);

		VisionResult result{};

		if (visionSystem.TryGetResult(recognizerGameObjectId, result)) {
			ImGui::TextDisabled("  モード: %s", ToDisplayString(result.mode));

			for (const ObjectDetectionResult& object : result.objects) {
				ImGui::TextDisabled("  物体: %s (%.2f)", object.label.c_str(), object.confidence);
			}

			for (const ImageClassificationResult& classification : result.classifications) {
				ImGui::TextDisabled("  分類: %s (%.2f)", classification.label.c_str(), classification.confidence);
			}

			if (!result.faces.empty()) {
				ImGui::TextDisabled("  顔: %d 件", static_cast<int32_t>(result.faces.size()));
			}

			if (result.headPose.isValid) {
				ImGui::TextDisabled(
					"  頭部方向: yaw %.1f / pitch %.1f / roll %.1f",
					result.headPose.yaw,
					result.headPose.pitch,
					result.headPose.roll);
			}

			if (result.mode == VisionRecognitionMode::ColorTracking) {
				ImGui::TextDisabled(
					"  色: %s 中心 (%.2f, %.2f) 面積比 %.4f",
					result.colorTracking.isDetected ? "検出" : "未検出",
					result.colorTracking.centerX,
					result.colorTracking.centerY,
					result.colorTracking.areaRatio);
			}

			if (result.mode == VisionRecognitionMode::MotionDetection) {
				ImGui::TextDisabled(
					"  動き: %s 量 %.3f",
					result.motion.motion ? "検出" : "未検出",
					result.motion.motionMagnitude);
			}

			if (result.error.HasError()) {
				ImGui::TextColored(
					ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
					"  %s",
					result.error.message.c_str());
			}
		}

		ImGui::PopID();
	}
}

void EditorExternalFeatureWindowManager::DrawOnlineTab() {
	OnlineService& onlineService = OnlineService::Get();
	const OnlineDebugInfo debugInfo = onlineService.GetDebugInfo();

	ImGui::Text("接続状態: %s", ToDisplayString(debugInfo.state));
	ImGui::Text("環境: %s", ToDisplayString(debugInfo.environment));
	ImGui::Text("Base URL: %s", debugInfo.activeBaseUrl.empty() ? "未設定" : debugInfo.activeBaseUrl.c_str());
	ImGui::Text("Game ID: %s", onlineService.GetConfig().gameId.empty() ? "未設定" : onlineService.GetConfig().gameId.c_str());
	ImGui::Text(
		"Player: %s",
		onlineService.GetConfig().playerId.empty() ? "未設定" : onlineService.GetConfig().playerId.c_str());

	ImGui::Separator();
	ImGui::Text("最後の Request: %s", debugInfo.lastRequestSummary.empty() ? "(なし)" : debugInfo.lastRequestSummary.c_str());
	ImGui::TextWrapped("Body: %s", debugInfo.lastRequestBody.empty() ? "(なし)" : debugInfo.lastRequestBody.c_str());
	ImGui::Text("Status Code: %d", debugInfo.lastStatusCode);
	ImGui::Text("通信時間: %.1f ms", debugInfo.lastElapsedMilliseconds);
	ImGui::TextWrapped(
		"最後の Response: %s",
		debugInfo.lastResponseBody.empty() ? "(なし)" : debugInfo.lastResponseBody.c_str());

	ImGui::Separator();
	ImGui::Text("送信中: %d 件", debugInfo.inFlightCount);
	ImGui::Text("再送待ち: %d 件", debugInfo.pendingQueueCount);
	ImGui::Text("成功: %d / 失敗: %d", debugInfo.completedRequestCount, debugInfo.failedRequestCount);

	if (debugInfo.lastError.HasError()) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
			"エラー(code=%d): %s",
			debugInfo.lastError.code,
			debugInfo.lastError.message.c_str());
	}

	if (ImGui::Button("いま再送する")) {
		onlineService.RetryPendingNow();
	}

	ImGui::SameLine();

	if (ImGui::Button("再送 Queue を空にする")) {
		onlineService.ClearPendingQueue();
	}

	ImGui::SameLine();

	if (ImGui::Button("疎通確認 (/health)")) {
		OnlineRequest request{};
		request.endpoint = "/health";
		request.method = "GET";
		onlineService.RequestAsync(request, [](const OnlineResponse& response) {
			ExternalFeatureLog::Info(
				ExternalFeatureCategory::Online,
				"疎通確認: status=" + std::to_string(response.statusCode) + " body=" + response.body);
		});
	}

	ImGui::TextDisabled("設定は Inspector の「プロジェクト設定 → Online Services」で変更します。");
}

void EditorExternalFeatureWindowManager::DrawHapticsTab() {
	HapticSystem& hapticSystem = HapticSystem::Get();
	const HapticDeviceInfo deviceInfo = hapticSystem.GetDeviceInfo();

	ImGui::Text("Backend: %s", deviceInfo.backendName.c_str());
	ImGui::Text("Device: %s", deviceInfo.deviceName.empty() ? "なし" : deviceInfo.deviceName.c_str());
	ImGui::Text("Device 状態: %s", ToDisplayString(deviceInfo.state));
	ImGui::Text("System 状態: %s", ToDisplayString(hapticSystem.GetState()));
	ImGui::Text("出力強度: %.2f", hapticSystem.GetCurrentOutputIntensity());

	float masterIntensity = hapticSystem.GetMasterIntensity();

	if (ImGui::SliderFloat("全体の強さ", &masterIntensity, 0.0f, 1.0f, "%.2f")) {
		hapticSystem.SetMasterIntensity(masterIntensity);
	}

	if (ImGui::Button("Device 再検出")) {
		hapticSystem.RefreshDevice();
	}

	ImGui::SameLine();

	if (ImGui::Button("すべて停止")) {
		hapticSystem.StopAll();
	}

	const ExternalFeatureError lastError = hapticSystem.GetLastError();

	if (lastError.HasError()) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
			"エラー(code=%d): %s",
			lastError.code,
			lastError.message.c_str());
	}

	ImGui::Separator();
	std::vector<HapticPlaybackStatus> playbackStatus;
	hapticSystem.GetPlaybackStatus(playbackStatus);
	ImGui::Text("再生中: %d 本", static_cast<int32_t>(playbackStatus.size()));

	for (const HapticPlaybackStatus& status : playbackStatus) {
		const std::string remainingText = status.isLooping
			? std::string("ループ")
			: std::to_string(status.remainingSeconds) + " 秒";
		ImGui::TextDisabled(
			"  %s / 強度 %.2f / 周波数 %.1f / 残り %s / Object %d",
			status.clipName.c_str(),
			status.intensity,
			status.frequency,
			remainingText.c_str(),
			status.ownerGameObjectId);
	}

	ImGui::Separator();
	ImGui::TextDisabled("HapticSource Component は Inspector からプレビューできます。");
}
