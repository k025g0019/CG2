#pragma warning(disable : 4189 4514 5045)

#include "EditorDiagnosticsWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorProfilerManager.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_set>

using namespace EditorSharedState;

namespace {
	constexpr int32_t kValidationIntervalFrames = 60;

	const char* GetSeverityName(EditorValidationSeverity severity) {
		switch (severity) {
		case EditorValidationSeverity::Error:
			return "Error";
		case EditorValidationSeverity::Warning:
			return "Warning";
		case EditorValidationSeverity::Information:
		default:
			return "Info";
		}
	}
}

void EditorDiagnosticsWindowManager::Initialize() {
	validationIssues_.clear();
	validationFrameTimer_ = 0;
}

void EditorDiagnosticsWindowManager::Update() {
	if (!g_isDiagnosticsWindowVisible || !shouldAutoValidate_) {
		return;
	}

	if (validationFrameTimer_ > 0) {
		validationFrameTimer_--;
		return;
	}

	ValidateScene();
	validationFrameTimer_ = kValidationIntervalFrames;
}

void EditorDiagnosticsWindowManager::Draw() {
#ifdef USE_IMGUI
	if (!g_isDiagnosticsWindowVisible) {
		return;
	}

	if (!ImGui::Begin(
			"診断・Profiler###DiagnosticsWindow",
			&g_isDiagnosticsWindowVisible,
			ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("DiagnosticsTabs")) {
		if (ImGui::BeginTabItem("Profiler")) {
			DrawProfiler();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("VFX")) {
			DrawVfx();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Scene Validator")) {
			DrawSceneValidation();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Replay")) {
			DrawReplay();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("描画バッファ")) {
			DrawRenderTargets();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Before / After")) {
			DrawImageComparison();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Material Preview")) {
			DrawMaterialPreview();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Scopes")) {
			DrawScopes();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Render Graph")) {
			DrawRenderGraph();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
#endif
}

void EditorDiagnosticsWindowManager::ValidateScene() {
	validationIssues_.clear();
	const std::vector<EditorGameObject>& gameObjects = g_editorScene.GetGameObjects();
	std::unordered_set<int32_t> gameObjectIds;
	gameObjectIds.reserve(gameObjects.size());

	for (const EditorGameObject& gameObject : gameObjects) {
		if (!gameObjectIds.insert(gameObject.id).second) {
			AddIssue(EditorValidationSeverity::Error, gameObject.id, "GameObject IDが重複しています。");
		}
	}

	int32_t activeCameraCount = 0;
	int32_t activeLightCount = 0;
	int32_t particleCapacity = 0;
	int32_t waveSpawnCount = 0;

	for (const EditorGameObject& gameObject : gameObjects) {
		if (gameObject.parentId >= 0 && gameObjectIds.find(gameObject.parentId) == gameObjectIds.end()) {
			AddIssue(EditorValidationSeverity::Error, gameObject.id, "親GameObjectがScene内に存在しません。");
		}

		for (const int32_t childGameObjectId : gameObject.children) {
			const EditorGameObject* childGameObject = g_editorScene.FindGameObject(childGameObjectId);

			if (childGameObject == nullptr || childGameObject->parentId != gameObject.id) {
				AddIssue(EditorValidationSeverity::Error, gameObject.id, "子一覧と親IDが一致していません。");
			}
		}

		const EditorComponent* rigidBody = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RigidBody);
		const EditorComponent* meshCollider = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::MeshCollider);

		if (rigidBody != nullptr && rigidBody->isActive && !rigidBody->isKinematic &&
			meshCollider != nullptr && meshCollider->isActive) {
			AddIssue(
				EditorValidationSeverity::Warning,
				gameObject.id,
				"Dynamic RigidbodyがMesh Colliderを使用しています。Auto Convexまたは単純Colliderを検討してください。");
		}

		for (const EditorComponent& component : gameObject.components) {
			if (!gameObject.isActive || !component.isActive) {
				continue;
			}

			if (component.type == EditorComponentType::Camera ||
				component.type == EditorComponentType::CinemachineCamera) {
				activeCameraCount++;
			}

			if (component.type == EditorComponentType::Light) {
				activeLightCount++;
			}

			if (component.type == EditorComponentType::ParticleSystem) {
				particleCapacity += (std::max)(component.particleMaxCount, 0);
			}

			if (component.type == EditorComponentType::WaveSpawner) {
				waveSpawnCount += (std::max)(component.waveSpawnCount, 0);

				if (component.waveSpawnCount > 32 && component.waveSpawnSourceMode != 0) {
					AddIssue(
						EditorValidationSeverity::Warning,
						gameObject.id,
						"大量WaveがObjectPool方式ではありません。生成負荷とHierarchy常駐数を確認してください。");
				}
			}
		}
	}

	if (activeCameraCount == 0) {
		AddIssue(EditorValidationSeverity::Error, -1, "ActiveなCameraがありません。Game ViewはScene Cameraへフォールバックします。");
	}

	if (activeCameraCount > 4) {
		AddIssue(EditorValidationSeverity::Warning, -1, "Active Cameraが4台を超えています。Priorityと不要Cameraを確認してください。");
	}

	if (activeLightCount > 8) {
		AddIssue(EditorValidationSeverity::Warning, -1, "Active Lightが8個を超えています。影と動的Lightの範囲を確認してください。");
	}

	if (particleCapacity > 100000) {
		AddIssue(EditorValidationSeverity::Warning, -1, "Particle最大数合計が100000を超えています。距離LODと上限を確認してください。");
	}

	if (waveSpawnCount > 500) {
		AddIssue(EditorValidationSeverity::Warning, -1, "Wave生成予定数が500体を超えています。距離Trigger、Pool、生成Frame上限を確認してください。");
	}

	if (validationIssues_.empty()) {
		AddIssue(EditorValidationSeverity::Information, -1, "現在の静的検査では問題を検出しませんでした。");
	}
}

void EditorDiagnosticsWindowManager::DrawProfiler() {
#ifdef USE_IMGUI
	EditorProfilerManager& profilerManager = g_editorRuntimeManager.GetProfilerManager();
	const std::vector<EditorProfilerSample> samples = profilerManager.GetSortedSamples();

	ImGui::Text("GPU Frame: %.2f ms", g_renderProfile.gpuFrameMilliseconds);
	ImGui::SameLine();
	ImGui::Text("Objects: %u  Instances: %u", g_renderProfile.sceneObjectCount, g_renderProfile.instanceCount);

	if (ImGui::Button("Peakと平均をリセット")) {
		profilerManager.Reset();
	}

	ImGui::Separator();

	if (ImGui::BeginTable(
			"ProfilerSamples",
			4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("処理");
		ImGui::TableSetupColumn("Latest ms");
		ImGui::TableSetupColumn("Average ms");
		ImGui::TableSetupColumn("Peak ms");
		ImGui::TableHeadersRow();

		for (const EditorProfilerSample& sample : samples) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(sample.name.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.3f", sample.latestMilliseconds);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.3f", sample.averageMilliseconds);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.3f", sample.peakMilliseconds);
		}

		ImGui::EndTable();
	}
#endif
}

void EditorDiagnosticsWindowManager::DrawVfx() {
#ifdef USE_IMGUI
	using namespace EditorSharedState;

	if (!g_editorRuntimeManager.IsPlaying()) {
		ImGui::TextDisabled("Play中のみStage1 VFX(Billboard/Flipbook/Ribbon/Ring)の状態を表示します。");
		return;
	}

	const EditorVfxManager::DebugStats stats = g_editorRuntimeManager.GetVfxManager().GetDebugStats();
	ImGui::Text("Active Effect数: %d", stats.activeEffectCount);
	ImGui::Text("Active Emitter(Node)数: %d", stats.activeEmitterCount);
	ImGui::Text("Particle数(Billboard粒子+Ribbon履歴点+Ring): %d", stats.activeParticleCount);
	ImGui::Text("Effect Pool使用数: %d / %d", stats.poolUsedCount, stats.poolCapacity);
	ImGui::Separator();

	if (ImGui::BeginTable(
			"VfxEffectTable",
			5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Effect名");
		ImGui::TableSetupColumn("Node数");
		ImGui::TableSetupColumn("Particle数");
		ImGui::TableSetupColumn("描画方式");
		ImGui::TableSetupColumn("LOD Spawn倍率 / 追従");
		ImGui::TableHeadersRow();

		for (const EditorVfxManager::DebugEffectEntry& entry : stats.effects) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(entry.effectId.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", entry.nodeCount);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", entry.particleCount);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(entry.drawModeSummary.c_str());
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.2f / %s", entry.lodSpawnMultiplier, entry.isFollowing ? "追従" : "固定");
		}

		ImGui::EndTable();
	}
#endif
}

void EditorDiagnosticsWindowManager::DrawSceneValidation() {
#ifdef USE_IMGUI
	if (ImGui::Button("今すぐ検査")) {
		ValidateScene();
	}

	ImGui::SameLine();
	ImGui::Checkbox("自動検査", &shouldAutoValidate_);
	ImGui::Separator();

	for (const EditorValidationIssue& issue : validationIssues_) {
		ImVec4 color = ImVec4(0.65f, 0.78f, 0.92f, 1.0f);

		if (issue.severity == EditorValidationSeverity::Warning) {
			color = ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
		}
		else if (issue.severity == EditorValidationSeverity::Error) {
			color = ImVec4(1.0f, 0.35f, 0.30f, 1.0f);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::Text("[%s]", GetSeverityName(issue.severity));
		ImGui::PopStyleColor();
		ImGui::SameLine();

		if (issue.gameObjectId >= 0) {
			const EditorGameObject* gameObject = g_editorScene.FindGameObject(issue.gameObjectId);
			const char* objectName = gameObject != nullptr ? gameObject->name.c_str() : "Missing Object";

			if (ImGui::SmallButton((std::string(objectName) + "##" + std::to_string(issue.gameObjectId) + issue.message).c_str())) {
				g_selectedEditorGameObjectId = issue.gameObjectId;
			}

			ImGui::SameLine();
		}

		ImGui::TextWrapped("%s", issue.message.c_str());
	}
#endif
}

void EditorDiagnosticsWindowManager::DrawReplay() {
#ifdef USE_IMGUI
	constexpr const char* replayPath = "runtime_cache/replays/last.cgreplay";
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

	if (replayMode != EditorReplayMode::Recording && ImGui::Button("Scene先頭から記録")) {
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
			replayManager.Save(replayPath);
		}
	}

	if (replayMode != EditorReplayMode::Playback && replayManager.GetFrameCount() > 0u) {
		if (ImGui::Button("Scene先頭から再生")) {
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

	if (ImGui::Button("前回Replayを読込")) {
		replayManager.Load(replayPath);
	}

	ImGui::TextDisabled("Keyboard 256キーと各FrameのdeltaTimeを記録します。");
#endif
}

void EditorDiagnosticsWindowManager::DrawRenderTargets() {
#ifdef USE_IMGUI
	struct RenderTargetPreview {
		const char* name;
		D3D12_GPU_DESCRIPTOR_HANDLE textureHandle;
		const char* description;
	};

	const std::array<RenderTargetPreview, 5u> renderTargets = {{
		{"Albedo", g_gBufferManager.GetAlbedoSrvHandle(), "Base ColorとAlpha"},
		{"Normal", g_gBufferManager.GetNormalSrvHandle(), "World Normalを0..1へ変換した値"},
		{"Material", g_gBufferManager.GetMaterialSrvHandle(), "R=Roughness G=Metallic B=AO A=F0"},
		{"Emission", g_gBufferManager.GetEmissionSrvHandle(), "EmissionとTransmission"},
		{"Motion Vector", g_gBufferManager.GetMotionVectorSrvHandle(), "画面空間Velocity。RG成分を表示"},
	}};
	selectedRenderTargetIndex_ = (std::clamp)(
		selectedRenderTargetIndex_,
		0,
		static_cast<int32_t>(renderTargets.size()) - 1);
	const char* selectedName = renderTargets[static_cast<size_t>(selectedRenderTargetIndex_)].name;

	if (ImGui::BeginCombo("表示", selectedName)) {
		for (size_t renderTargetIndex = 0u; renderTargetIndex < renderTargets.size(); renderTargetIndex++) {
			const bool isSelected = static_cast<int32_t>(renderTargetIndex) == selectedRenderTargetIndex_;

			if (ImGui::Selectable(renderTargets[renderTargetIndex].name, isSelected)) {
				selectedRenderTargetIndex_ = static_cast<int32_t>(renderTargetIndex);
			}

			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	const RenderTargetPreview& selectedTarget =
		renderTargets[static_cast<size_t>(selectedRenderTargetIndex_)];
	ImGui::TextDisabled("%s", selectedTarget.description);

	if (!g_gBufferManager.IsReady() || selectedTarget.textureHandle.ptr == 0u) {
		ImGui::TextDisabled("GBufferはまだ生成されていません。");
		return;
	}

	const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
	const float renderWidth = static_cast<float>((std::max)(g_renderWidth, 1u));
	const float renderHeight = static_cast<float>((std::max)(g_renderHeight, 1u));
	const float previewHeight = availableWidth * renderHeight / renderWidth;
	ImGui::Image(
		ImTextureRef(selectedTarget.textureHandle.ptr),
		ImVec2(availableWidth, previewHeight));
#endif
}

void EditorDiagnosticsWindowManager::DrawImageComparison() {
#ifdef USE_IMGUI
	const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, 128.0f);
	const float columnWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	const float aspectRatio = static_cast<float>((std::max)(g_renderHeight, 1u)) /
		static_cast<float>((std::max)(g_renderWidth, 1u));

	if (ImGui::BeginTable("ImageComparison", 2, ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("HDR入力");
		ImGui::TableSetupColumn("最終合成");
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		if (g_hdrSrvHandleGPU.ptr != 0u) {
			ImGui::Image(ImTextureRef(g_hdrSrvHandleGPU.ptr), ImVec2(columnWidth, columnWidth * aspectRatio));
		}

		ImGui::TableSetColumnIndex(1);

		if (g_postProcessSrvHandleGPU.ptr != 0u) {
			ImGui::Image(
				ImTextureRef(g_postProcessSrvHandleGPU.ptr),
				ImVec2(columnWidth, columnWidth * aspectRatio));
		}

		ImGui::EndTable();
	}
#endif
}

void EditorDiagnosticsWindowManager::DrawMaterialPreview() {
#ifdef USE_IMGUI
	if (!g_gBufferManager.IsReady()) {
		ImGui::TextDisabled("GBufferはまだ生成されていません。");
		return;
	}

	const std::array<std::pair<const char*, D3D12_GPU_DESCRIPTOR_HANDLE>, 4u> channels = {{
		{"Base Color", g_gBufferManager.GetAlbedoSrvHandle()},
		{"World Normal", g_gBufferManager.GetNormalSrvHandle()},
		{"Roughness / Metallic / AO", g_gBufferManager.GetMaterialSrvHandle()},
		{"Emission / Transmission", g_gBufferManager.GetEmissionSrvHandle()},
	}};
	const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, 128.0f);
	const float previewWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	const float aspectRatio = static_cast<float>((std::max)(g_renderHeight, 1u)) /
		static_cast<float>((std::max)(g_renderWidth, 1u));

	if (ImGui::BeginTable("MaterialChannels", 2, ImGuiTableFlags_BordersInnerV)) {
		for (size_t channelIndex = 0u; channelIndex < channels.size(); channelIndex++) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(channels[channelIndex].first);
			ImGui::Image(
				ImTextureRef(channels[channelIndex].second.ptr),
				ImVec2(previewWidth, previewWidth * aspectRatio));
		}

		ImGui::EndTable();
	}
#endif
}

void EditorDiagnosticsWindowManager::DrawScopes() {
#ifdef USE_IMGUI
	const EditorPostProcessQualityManager& postProcessManager = g_postProcessQualityManager;

	if (!postProcessManager.HasHistogramData()) {
		ImGui::TextDisabled("Auto Exposure実行後に輝度Histogramを表示します。");
		return;
	}

	const std::array<float, 256u>& histogram = postProcessManager.GetHistogramNormalized();
	ImGui::TextUnformatted("Log Luminance Histogram (-12 EV .. +8 EV)");
	ImGui::PlotHistogram(
		"##LuminanceHistogram",
		histogram.data(),
		static_cast<int32_t>(histogram.size()),
		0,
		nullptr,
		0.0f,
		1.0f,
		ImVec2(ImGui::GetContentRegionAvail().x, 180.0f));
#endif
}

void EditorDiagnosticsWindowManager::DrawRenderGraph() {
#ifdef USE_IMGUI
	struct RenderPassState {
		const char* passName;
		const char* inputName;
		const char* outputName;
		bool isReady;
	};
	const std::array<RenderPassState, 9u> renderPasses = {{
		{"GBuffer", "Scene Mesh", "Albedo / Normal / Material / Emission", g_gBufferManager.IsReady()},
		{"GTAO", "Depth + Normal", "AO", g_ssaoPipelineState != nullptr},
		{"SSGI", "Depth + PBR GBuffer", "HDR Additive Light", g_ssgiPipelineState != nullptr},
		{"SSR", "Depth Pyramid + Motion", "Reflection", g_isInitialized},
		{"Ocean", "FFT Displacement", "HDR Water Surface", g_waterSurfacePipelineState != nullptr},
		{"Volumetric Cloud", "Sky Ray + Sun", "HDR Sky", g_skyboxPipelineState != nullptr},
		{"Bloom / Glare", "HDR", "Bloom Chain", g_bloomExtractPipelineState != nullptr},
		{"Final Composite", "HDR + AO + LUT", "LDR", g_finalCompositePipelineState != nullptr},
		{"AA / Filter", "LDR", "Back Buffer", g_fxaaPipelineState != nullptr},
	}};

	if (ImGui::BeginTable(
		"RenderGraphPasses",
		4,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("Pass");
		ImGui::TableSetupColumn("Input");
		ImGui::TableSetupColumn("Output");
		ImGui::TableSetupColumn("State");
		ImGui::TableHeadersRow();

		for (const RenderPassState& renderPass : renderPasses) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(renderPass.passName);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(renderPass.inputName);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(renderPass.outputName);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextColored(
				renderPass.isReady ? ImVec4(0.35f, 0.85f, 0.48f, 1.0f) : ImVec4(1.0f, 0.38f, 0.30f, 1.0f),
				renderPass.isReady ? "Ready" : "Missing");
		}

		ImGui::EndTable();
	}
#endif
}

void EditorDiagnosticsWindowManager::AddIssue(
	EditorValidationSeverity severity,
	int32_t gameObjectId,
	const std::string& message) {
	validationIssues_.push_back({severity, gameObjectId, message});
}
