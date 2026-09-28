#pragma warning(disable : 4189 4514 5045)

#include "EditorDiagnosticsWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorProfilerManager.h"
#include "EditorSharedState.h"
#include "Source/Engine/Asset/AssetRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

using namespace EditorSharedState;

namespace {
	constexpr int32_t kValidationIntervalFrames = 60;

	// SceneSerializationSmokeTest専用の許容誤差比較。Engine全体で使う汎用Math Utilityは
	// 既存コードに見当たらないため、ここでだけ小さなepsilonを使う。
	bool IsNearlyEqualForSmokeTest(float actual, float expected) {
		constexpr float kEpsilon = 0.0001f;
		return std::fabs(actual - expected) <= kEpsilon;
	}

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

	float GetAverageMilliseconds(const EditorProfilerSample& sample) {
		return sample.sampleCount > 0u
			? sample.totalMilliseconds / static_cast<float>(sample.sampleCount)
			: 0.0f;
	}

	std::string EscapeProfilerCsvField(const std::string& fieldText) {
		const bool needsQuoting =
			fieldText.find(',') != std::string::npos ||
			fieldText.find('"') != std::string::npos ||
			fieldText.find('\n') != std::string::npos;

		if (!needsQuoting) {
			return fieldText;
		}

		std::string escapedText = "\"";
		for (const char character : fieldText) {
			if (character == '"') {
				escapedText += "\"\"";
			}
			else {
				escapedText += character;
			}
		}
		escapedText += "\"";
		return escapedText;
	}

	// 呼出回数(回数が多くて重い)と合計/最大ms(1回自体が重い)を分けて共有できるよう、表と同じ列でCSV化する。
	std::string BuildProfilerExportText(const std::vector<EditorProfilerSample>& samples) {
		std::string exportText =
			"イベント名,処理元,GameObject,Thread,呼出回数,合計ms,平均ms,Self ms,最大ms,DrawCall,Dispatch,Alloc回数,Alloc KB\n";

		for (const EditorProfilerSample& sample : samples) {
			std::string gameObjectText = "-";
			if (sample.gameObjectId >= 0) {
				const EditorGameObject* gameObject = g_editorScene.FindGameObject(sample.gameObjectId);
				gameObjectText =
					(gameObject != nullptr ? gameObject->name : std::string("削除済み")) +
					" (" + std::to_string(sample.gameObjectId) + ")";
			}

			char numberBuffer[256]{};
			std::snprintf(
				numberBuffer,
				_countof(numberBuffer),
				"%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%.2f",
				static_cast<unsigned long long>(sample.sampleCount),
				sample.totalMilliseconds,
				GetAverageMilliseconds(sample),
				sample.selfMilliseconds,
				sample.peakMilliseconds,
				static_cast<unsigned long long>(sample.drawCallCount),
				static_cast<unsigned long long>(sample.dispatchCount),
				static_cast<unsigned long long>(sample.allocationCount),
				static_cast<double>(sample.allocatedBytes) / 1024.0);

			exportText += EscapeProfilerCsvField(sample.name) + ",";
			exportText += EscapeProfilerCsvField(sample.source) + ",";
			exportText += EscapeProfilerCsvField(gameObjectText) + ",";
			exportText += EscapeProfilerCsvField(sample.threadName) + ",";
			exportText += numberBuffer;
			exportText += "\n";
		}

		return exportText;
	}

	void SaveProfilerExportToFile(const std::string& exportText) {
		wchar_t fileBuffer[MAX_PATH] = L"ProfilerResult.csv";
		OPENFILENAMEW ofn{};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = L"CSVファイル (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
		ofn.lpstrFile = fileBuffer;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrDefExt = L"csv";
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

		if (GetSaveFileNameW(&ofn) == 0) {
			return;  // 保存先選択をキャンセルした場合は何もしない
		}

		std::filesystem::path savePath(fileBuffer);
		if (savePath.extension().empty()) {
			savePath += L".csv";
		}

		std::ofstream outputFile(savePath, std::ios::trunc | std::ios::binary);
		if (!outputFile.is_open()) {
			return;
		}

		static constexpr unsigned char kProfilerExportUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};
		outputFile.write(reinterpret_cast<const char*>(kProfilerExportUtf8Bom), sizeof(kProfilerExportUtf8Bom));
		outputFile << exportText;
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

		if (ImGui::BeginTabItem("Physics")) {
			DrawPhysics();
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

		// DirectXの失敗は以前assertだけで見ていたため、Release構成では痕跡が残らなかった。
		// 件数をタブ名に出し、開かなくても異常に気付けるようにする。
		const std::size_t apiFailureCount = EditorGetHrFailureCount();
		const std::string apiFailureTabLabel = apiFailureCount > 0u
			? std::string("DirectX失敗 (") + std::to_string(apiFailureCount) + ")###ApiFailures"
			: std::string("DirectX失敗###ApiFailures");

		if (ImGui::BeginTabItem(apiFailureTabLabel.c_str())) {
			DrawApiFailures();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
#endif
}

void EditorDiagnosticsWindowManager::DrawPhysics() {
#ifdef USE_IMGUI
	const EditorPhysicsSettings& settings = g_editorScene.GetPhysicsSettings();
	const EditorPhysicsManager& physics = g_editorRuntimeManager.GetPhysicsManager();
	ImGui::Text("接触 %d 件 / Cast %d 件", static_cast<int>(physics.GetContactDebugEvents().size()), static_cast<int>(physics.GetFrameDebugCasts().size()));
	ImGui::TextDisabled("UI (6) と Ignore Raycast (7) は物理衝突・Queryから除外されます。false の行列要素は Contact を拒否します。");
	if (ImGui::CollapsingHeader("Layer Collision Matrix", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (int32_t row = 0; row < 8; ++row) {
			for (int32_t column = 0; column < 8; ++column) {
				ImGui::PushID(row * 8 + column);
				const bool enabled = settings.layerCollisionMatrix[row][column];
				ImGui::TextColored(enabled ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f) : ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%d-%d:%s", row, column, enabled ? "Pass" : "Ignore");
				ImGui::PopID();
				if (column != 7) ImGui::SameLine();
			}
		}
	}
	if (ImGui::CollapsingHeader("Contacts", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (const EditorJoltPhysicsManager::PhysicsEvent& event : physics.GetContactDebugEvents()) {
			const auto& c = event.collision;
			ImGui::Text("%d <-> %d  point(%.2f, %.2f, %.2f)  %s", c.selfGameObjectId, c.otherGameObjectId, c.point.x, c.point.y, c.point.z, c.isTrigger ? "Trigger" : "Collision");
		}
	}
	if (ImGui::CollapsingHeader("Ray / Shape Cast", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (const EditorPhysicsManager::PhysicsDebugCast& cast : physics.GetFrameDebugCasts()) {
			const char* type = cast.type == EditorPhysicsManager::PhysicsDebugCastType::Ray ? "Ray" : cast.type == EditorPhysicsManager::PhysicsDebugCastType::Sphere ? "Sphere" : "Capsule";
			ImGui::Text("%s distance %.2f  %s", type, cast.distance, cast.hasHit ? "Hit" : "No Hit");
			if (cast.hasHit) ImGui::SameLine(), ImGui::Text(" object %d", cast.hit.gameObjectId);
		}
	}
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

// Scene保存→読込の往復でTransformと代表Component(Light)の値が保持されることを確認する
// 最初の回帰テスト。g_editorScene(現在編集中のScene)には一切触れず、独立したEditorScene
// インスタンス上でだけ検証するため、実行してもユーザーが開いているSceneを壊さない。
void EditorDiagnosticsWindowManager::RunSceneSerializationSmokeTest() {
	constexpr const char* kTestObjectName = "SerializationSmokeTestObject";
	constexpr const char* kTempSceneDirectory = "runtime_cache/validation";
	constexpr const char* kTempScenePath = "runtime_cache/validation/scene_serialization_smoke_test.scene";

	// 0 や 1 などの既定値と衝突しない、識別しやすい非既定値。
	const Vector3 expectedPosition = {12.25f, -3.5f, 87.125f};
	const Vector3 expectedRotation = {0.261799f, 0.645772f, -0.383972f};  // 15° / 37° / -22° をラジアンへ変換した値(rotateはラジアン管理のため)
	const Vector3 expectedScale = {1.5f, 0.75f, 2.25f};
	const float expectedLightIntensity = 41.75f;
	const float expectedLightRange = 63.5f;  // colliderRadius(Light の到達距離)
	const Vector3 expectedLightColor = {0.15f, 0.85f, 0.35f};

	bool allPassed = true;

	auto reportFloatMismatch = [&allPassed](const std::string& fieldName, float expected, float actual) {
		if (IsNearlyEqualForSmokeTest(actual, expected)) {
			return;
		}
		allPassed = false;
		g_editorConsoleMessages.push_back(
			"SceneSerializationSmokeTest FAILED " + fieldName +
			" Expected=" + std::to_string(expected) +
			" Actual=" + std::to_string(actual));
	};

	std::error_code directoryError;
	std::filesystem::create_directories(kTempSceneDirectory, directoryError);

	// 1〜5: 空のテストSceneへGameObjectを1個生成し、Transformと代表Component(Light)へ非既定値を設定する。
	EditorScene sourceScene;
	const int32_t gameObjectId = sourceScene.CreateGameObject(kTestObjectName);
	EditorGameObject* sourceGameObject = sourceScene.FindGameObject(gameObjectId);

	if (sourceGameObject == nullptr) {
		g_editorConsoleMessages.push_back("SceneSerializationSmokeTest FAILED GameObjectの生成に失敗しました。");
		return;
	}

	sourceGameObject->translate = expectedPosition;
	sourceGameObject->rotate = expectedRotation;
	sourceGameObject->scale = expectedScale;

	sourceScene.AddComponent(gameObjectId, EditorComponentType::Light);
	EditorGameObject* sourceGameObjectAfterAddComponent = sourceScene.FindGameObject(gameObjectId);
	EditorComponent* sourceLight = sourceGameObjectAfterAddComponent != nullptr ?
		EditorComponentUtility::FindComponent(*sourceGameObjectAfterAddComponent, EditorComponentType::Light) :
		nullptr;

	if (sourceLight == nullptr) {
		g_editorConsoleMessages.push_back("SceneSerializationSmokeTest FAILED Light Componentの追加に失敗しました。");
		return;
	}

	sourceLight->intensity = expectedLightIntensity;
	sourceLight->colliderRadius = expectedLightRange;
	sourceLight->color = expectedLightColor;

	// 6: 一時Sceneファイルへ実際に保存する。
	if (!sourceScene.SaveScene(kTempScenePath)) {
		g_editorConsoleMessages.push_back(
			"SceneSerializationSmokeTest FAILED Sceneの保存に失敗しました。 Path=" + std::string(kTempScenePath));
		return;
	}

	// 7〜8: メモリ上のSceneとは別の新しいEditorSceneインスタンスへ読み直す。保存直後の値を
	// 使い回さないことで、実際にファイルへ書かれた内容だけを検証する。
	EditorScene reloadedScene;

	if (!reloadedScene.LoadScene(kTempScenePath)) {
		g_editorConsoleMessages.push_back(
			"SceneSerializationSmokeTest FAILED Sceneの再読込に失敗しました。 Path=" + std::string(kTempScenePath));
		return;
	}

	// 9: GameObjectを名前で取得する。
	const EditorGameObject* reloadedGameObject = nullptr;

	for (const EditorGameObject& gameObject : reloadedScene.GetGameObjects()) {
		if (gameObject.name == kTestObjectName) {
			reloadedGameObject = &gameObject;
			break;
		}
	}

	if (reloadedGameObject == nullptr) {
		allPassed = false;
		g_editorConsoleMessages.push_back(
			"SceneSerializationSmokeTest FAILED GameObject Expected=" + std::string(kTestObjectName) + " Actual=見つからない");
	}
	else {
		// 10: Transformを比較する。
		reportFloatMismatch("Transform.Position.x", expectedPosition.x, reloadedGameObject->translate.x);
		reportFloatMismatch("Transform.Position.y", expectedPosition.y, reloadedGameObject->translate.y);
		reportFloatMismatch("Transform.Position.z", expectedPosition.z, reloadedGameObject->translate.z);
		reportFloatMismatch("Transform.Rotation.x", expectedRotation.x, reloadedGameObject->rotate.x);
		reportFloatMismatch("Transform.Rotation.y", expectedRotation.y, reloadedGameObject->rotate.y);
		reportFloatMismatch("Transform.Rotation.z", expectedRotation.z, reloadedGameObject->rotate.z);
		reportFloatMismatch("Transform.Scale.x", expectedScale.x, reloadedGameObject->scale.x);
		reportFloatMismatch("Transform.Scale.y", expectedScale.y, reloadedGameObject->scale.y);
		reportFloatMismatch("Transform.Scale.z", expectedScale.z, reloadedGameObject->scale.z);

		// 11〜12: Componentとそのフィールドを取得して比較する。
		const EditorComponent* reloadedLight =
			EditorComponentUtility::FindComponent(*reloadedGameObject, EditorComponentType::Light);

		if (reloadedLight == nullptr) {
			allPassed = false;
			g_editorConsoleMessages.push_back(
				"SceneSerializationSmokeTest FAILED Light Component Expected=存在する Actual=見つからない");
		}
		else {
			reportFloatMismatch("Light.Intensity", expectedLightIntensity, reloadedLight->intensity);
			reportFloatMismatch("Light.Range", expectedLightRange, reloadedLight->colliderRadius);
			reportFloatMismatch("Light.Color.r", expectedLightColor.x, reloadedLight->color.x);
			reportFloatMismatch("Light.Color.g", expectedLightColor.y, reloadedLight->color.y);
			reportFloatMismatch("Light.Color.b", expectedLightColor.z, reloadedLight->color.z);
		}
	}

	// テスト専用の一時Sceneなので、確認が終わったら削除する。
	std::error_code removeError;
	std::filesystem::remove(kTempScenePath, removeError);

	if (allPassed) {
		g_editorConsoleMessages.push_back("SceneSerializationSmokeTest PASSED");
	}
	else {
		g_editorConsoleMessages.push_back("SceneSerializationSmokeTest FAILED");
	}
}

void EditorDiagnosticsWindowManager::DrawProfiler() {
#ifdef USE_IMGUI
	EditorProfilerManager& profilerManager = g_editorRuntimeManager.GetProfilerManager();
	std::vector<EditorProfilerSample> samples = profilerManager.GetSortedSamples();
	const bool isProfilerEnabled = profilerManager.IsEnabled();
	float measurementDurationSeconds = profilerManager.GetMeasurementDurationSeconds();
	static bool showCallHierarchy = false;
	static float heavyThresholdMilliseconds = 1.0f;  // 平均msがこの値以上の行を「単体で重い」として強調表示する。
	static int32_t profilerSortColumnIndex = 4;
	static bool isProfilerSortAscending = false;

	//================================================================
	// Frame History(いつ重くなったかを時系列で見る)
	//================================================================

	static std::vector<float> cpuFrameHistory;
	static std::vector<float> gpuFrameHistory;
	profilerManager.GetFrameHistory(cpuFrameHistory, gpuFrameHistory);

	const float lastCpuMilliseconds = profilerManager.GetLastCpuFrameMilliseconds();
	const float averageCpuMilliseconds = profilerManager.GetAverageCpuFrameMilliseconds();
	ImGui::Text(
		"CPU Frame: %.2f ms (平均 %.2f ms / %.0f FPS)",
		lastCpuMilliseconds,
		averageCpuMilliseconds,
		averageCpuMilliseconds > 0.001f ? 1000.0f / averageCpuMilliseconds : 0.0f);
	ImGui::SameLine();
	ImGui::Text("GPU Frame: %.2f ms", g_renderProfile.gpuFrameMilliseconds);

	if (!cpuFrameHistory.empty()) {
		// 上限を直近の最大値に合わせ、スパイクが潰れないようにする。
		const float maximumCpuMilliseconds =
			(std::max)(*std::max_element(cpuFrameHistory.begin(), cpuFrameHistory.end()), 1.0f);
		ImGui::PlotLines(
			"CPU ms",
			cpuFrameHistory.data(),
			static_cast<int32_t>(cpuFrameHistory.size()),
			0,
			nullptr,
			0.0f,
			maximumCpuMilliseconds * 1.1f,
			ImVec2(-1.0f, 60.0f));

		if (!gpuFrameHistory.empty()) {
			const float maximumGpuMilliseconds =
				(std::max)(*std::max_element(gpuFrameHistory.begin(), gpuFrameHistory.end()), 1.0f);
			ImGui::PlotLines(
				"GPU ms",
				gpuFrameHistory.data(),
				static_cast<int32_t>(gpuFrameHistory.size()),
				0,
				nullptr,
				0.0f,
				maximumGpuMilliseconds * 1.1f,
				ImVec2(-1.0f, 60.0f));
		}
	}

	//================================================================
	// Runtime Stats(増え続けるリソースを1画面で見つける)
	//================================================================

	if (ImGui::CollapsingHeader("Runtime Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Objects: %u  Instances: %u", g_renderProfile.sceneObjectCount, g_renderProfile.instanceCount);

		const std::uint64_t usedVideoMemoryMegabytes = g_renderProfile.localVideoMemoryUsage / (1024u * 1024u);
		const std::uint64_t budgetVideoMemoryMegabytes = g_renderProfile.localVideoMemoryBudget / (1024u * 1024u);
		ImGui::Text("VRAM: %llu MB / %llu MB", usedVideoMemoryMegabytes, budgetVideoMemoryMegabytes);

		// Play 中だけ実値が取れるものは、停止中はその旨を出して誤解させない。
		if (g_editorRuntimeManager.IsPlaying()) {
			const EditorAudioManager& audioManager = g_editorRuntimeManager.GetAudioManager();
			ImGui::Text(
				"Audio Voice: %d / %d  (読込済Clip %d)",
				audioManager.GetActiveVoiceCount(),
				audioManager.GetMaxGlobalVoiceCount(),
				audioManager.GetLoadedClipCount());
			ImGui::Text(
				"Physics Body: %d",
				g_editorRuntimeManager.GetPhysicsManager().GetPhysicsBodyCount());

			const EditorVfxManager::DebugStats vfxStats = g_editorRuntimeManager.GetVfxManager().GetDebugStats();
			ImGui::Text(
				"VFX: Effect %d / Emitter %d / Particle %d",
				vfxStats.activeEffectCount,
				vfxStats.activeEmitterCount,
				vfxStats.activeParticleCount);
		}
		else {
			ImGui::TextDisabled("Audio Voice / Physics Body / VFX は Play 中のみ計測されます。");
		}

		ImGui::Text("Asset Registry 登録数: %zu", AssetRegistry::Get().GetAllRecords().size());

		// Draw Call / Dispatch は Profiler サンプルの合計から出す。
		std::uint64_t totalDrawCallCount = 0u;
		std::uint64_t totalDispatchCount = 0u;
		std::uint64_t totalAllocatedBytes = 0u;

		for (const EditorProfilerSample& sample : samples) {
			totalDrawCallCount += sample.drawCallCount;
			totalDispatchCount += sample.dispatchCount;
			totalAllocatedBytes += sample.allocatedBytes;
		}

		ImGui::Text(
			"計測中の Draw Call: %llu  Dispatch: %llu  Alloc: %llu KB",
			totalDrawCallCount,
			totalDispatchCount,
			totalAllocatedBytes / 1024u);

		if (!isProfilerEnabled && samples.empty()) {
			ImGui::TextDisabled("Draw Call / Alloc は下の計測を開始すると集計されます。");
		}

		//================================================================
		// Runtime Error(気づかないまま進めてしまう失敗を前に出す)
		//================================================================
		if (g_lastPhysicsBodyFailure != "-" && !g_lastPhysicsBodyFailure.empty()) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
				"物理Body生成の直近失敗: %s",
				g_lastPhysicsBodyFailure.c_str());
		}
	}

	//================================================================
	// 手動計測
	//================================================================

	if (isProfilerEnabled) {
		const float elapsedSeconds = profilerManager.GetElapsedMeasurementSeconds();
		const float progressRatio = (std::min)(
			elapsedSeconds / (std::max)(measurementDurationSeconds, 0.001f),
			1.0f);
		ImGui::Text("計測中: %.2f / %.2f 秒", elapsedSeconds, measurementDurationSeconds);
		ImGui::ProgressBar(progressRatio, ImVec2(-1.0f, 0.0f));

		if (ImGui::Button("計測を停止")) {
			profilerManager.SetEnabled(false);
		}
	}
	else {
		ImGui::SetNextItemWidth(140.0f);

		if (ImGui::InputFloat("計測時間（秒）", &measurementDurationSeconds, 0.5f, 1.0f, "%.1f")) {
			profilerManager.SetMeasurementDurationSeconds(measurementDurationSeconds);
		}

		if (ImGui::Button("Editor / Play負荷計測を開始")) {
			profilerManager.SetEnabled(true);
		}

		if (!samples.empty()) {
			ImGui::SameLine();

			if (ImGui::Button("結果を消去")) {
				profilerManager.Reset();
			}
		}
	}

	if (!samples.empty()) {
		if (ImGui::Button("結果をコピー")) {
			const std::string exportText = BuildProfilerExportText(samples);
			ImGui::SetClipboardText(exportText.c_str());
		}

		ImGui::SameLine();

		if (ImGui::Button("ファイルに保存…")) {
			const std::string exportText = BuildProfilerExportText(samples);
			SaveProfilerExportToFile(exportText);
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(チームへの共有用にCSVで書き出します)");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Engineと有効なNative Script DLLを同じ期間で集計し、合計時間順に表示します。");
	ImGui::Checkbox("呼出階層で表示", &showCallHierarchy);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::InputFloat("重い判定(平均ms)", &heavyThresholdMilliseconds, 0.1f, 1.0f, "%.2f");
	heavyThresholdMilliseconds = (std::max)(heavyThresholdMilliseconds, 0.0f);
	ImGui::SameLine();
	ImGui::TextDisabled("(平均msがこの値以上の行を赤く強調)");

	if (showCallHierarchy) {
		std::sort(
			samples.begin(),
			samples.end(),
			[](const EditorProfilerSample& firstSample, const EditorProfilerSample& secondSample) {
				if (firstSample.threadId != secondSample.threadId) {
					return firstSample.threadId < secondSample.threadId;
				}

				if (firstSample.gameObjectId != secondSample.gameObjectId) {
					return firstSample.gameObjectId < secondSample.gameObjectId;
				}

				return firstSample.callPath < secondSample.callPath;
			});
	}
	else {
		ImGui::SameLine();
		ImGui::TextDisabled("(列見出しをクリックすると並び替えできます。既定は呼出回数の多い順)");
	}

	ImGuiTableFlags tableFlags =
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX;

	if (!showCallHierarchy) {
		// 呼出階層表示中はThread/GameObject/CallPath順を保ちたいため、列クリックでの並び替えは通常表示のときだけ許可する。
		tableFlags |= ImGuiTableFlags_Sortable;
	}

	if (ImGui::BeginTable("ProfilerSamples", 13, tableFlags)) {
		ImGui::TableSetupColumn("イベント名", ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn("処理元", ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn("GameObject", ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_NoSort);
		ImGui::TableSetupColumn(
			"呼出回数",
			ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("合計 ms", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("平均 ms", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("最大 ms", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("DrawCall", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("Dispatch", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("Alloc回数", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableSetupColumn("Alloc KB", ImGuiTableColumnFlags_PreferSortDescending);
		ImGui::TableHeadersRow();

		if (!showCallHierarchy) {
			if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
					const ImGuiTableColumnSortSpecs& sortSpec = sortSpecs->Specs[0];
					profilerSortColumnIndex = sortSpec.ColumnIndex;
					isProfilerSortAscending = sortSpec.SortDirection == ImGuiSortDirection_Ascending;
				}

				// samplesは毎フレーム取得し直されるため、選択中の並び順も毎フレーム適用する。
				std::stable_sort(
					samples.begin(),
					samples.end(),
					[&](const EditorProfilerSample& firstSample, const EditorProfilerSample& secondSample) {
						int comparisonResult = 0;

						switch (profilerSortColumnIndex) {
							case 4:
								comparisonResult = (firstSample.sampleCount > secondSample.sampleCount) -
									(firstSample.sampleCount < secondSample.sampleCount);
								break;
							case 5:
								comparisonResult = (firstSample.totalMilliseconds > secondSample.totalMilliseconds) -
									(firstSample.totalMilliseconds < secondSample.totalMilliseconds);
								break;
							case 6: {
								const float firstAverage = GetAverageMilliseconds(firstSample);
								const float secondAverage = GetAverageMilliseconds(secondSample);
								comparisonResult = (firstAverage > secondAverage) - (firstAverage < secondAverage);
								break;
							}
							case 7:
								comparisonResult = (firstSample.selfMilliseconds > secondSample.selfMilliseconds) -
									(firstSample.selfMilliseconds < secondSample.selfMilliseconds);
								break;
							case 8:
								comparisonResult = (firstSample.peakMilliseconds > secondSample.peakMilliseconds) -
									(firstSample.peakMilliseconds < secondSample.peakMilliseconds);
								break;
							case 9:
								comparisonResult = (firstSample.drawCallCount > secondSample.drawCallCount) -
									(firstSample.drawCallCount < secondSample.drawCallCount);
								break;
							case 10:
								comparisonResult = (firstSample.dispatchCount > secondSample.dispatchCount) -
									(firstSample.dispatchCount < secondSample.dispatchCount);
								break;
							case 11:
								comparisonResult = (firstSample.allocationCount > secondSample.allocationCount) -
									(firstSample.allocationCount < secondSample.allocationCount);
								break;
							case 12:
								comparisonResult = (firstSample.allocatedBytes > secondSample.allocatedBytes) -
									(firstSample.allocatedBytes < secondSample.allocatedBytes);
								break;
							default:
								break;
							}

						if (comparisonResult == 0) {
							return false;
						}

						return isProfilerSortAscending ? comparisonResult < 0 : comparisonResult > 0;
					});

				sortSpecs->SpecsDirty = false;
			}
		}

		for (const EditorProfilerSample& sample : samples) {
			ImGui::TableNextRow();
			const float averageMilliseconds = GetAverageMilliseconds(sample);

			if (averageMilliseconds >= heavyThresholdMilliseconds) {
				// 呼出回数の多寡とは別に、1回自体が重い行だと一目で分かるよう行全体を赤系に強調する。
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 45, 40, 140));
			}

			ImGui::TableSetColumnIndex(0);
			const float indentationWidth = showCallHierarchy
				? static_cast<float>(sample.callDepth) * 14.0f
				: 0.0f;
			ImGui::Indent(indentationWidth);
			ImGui::TextUnformatted(sample.name.c_str());
			ImGui::Unindent(indentationWidth);

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", sample.callPath.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(sample.source.c_str());
			ImGui::TableSetColumnIndex(2);
			if (sample.gameObjectId >= 0) {
				const EditorGameObject* gameObject = g_editorScene.FindGameObject(sample.gameObjectId);
				ImGui::Text(
					"%s (%d)",
					gameObject != nullptr ? gameObject->name.c_str() : "削除済み",
					sample.gameObjectId);
			}
			else {
				ImGui::TextUnformatted("-");
			}
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(sample.threadName.c_str());
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%llu", static_cast<unsigned long long>(sample.sampleCount));
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.3f", sample.totalMilliseconds);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%.3f", averageMilliseconds);
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("%.3f", sample.selfMilliseconds);
			ImGui::TableSetColumnIndex(8);
			ImGui::Text("%.3f", sample.peakMilliseconds);
			ImGui::TableSetColumnIndex(9);
			ImGui::Text("%llu", static_cast<unsigned long long>(sample.drawCallCount));
			ImGui::TableSetColumnIndex(10);
			ImGui::Text("%llu", static_cast<unsigned long long>(sample.dispatchCount));
			ImGui::TableSetColumnIndex(11);
			ImGui::Text("%llu", static_cast<unsigned long long>(sample.allocationCount));
			ImGui::TableSetColumnIndex(12);
			ImGui::Text("%.2f", static_cast<double>(sample.allocatedBytes) / 1024.0);
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

	if (ImGui::Button("Scene Serialization Smoke Test")) {
		RunSceneSerializationSmokeTest();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(結果はConsoleへ出力されます。現在編集中のSceneには影響しません)");

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

void EditorDiagnosticsWindowManager::DrawApiFailures() {
#ifdef USE_IMGUI
	const std::size_t hrFailureCount = EditorGetHrFailureCount();
	const std::vector<std::string>& hrFailures = EditorGetHrFailureLog();

	if (hrFailureCount == 0u && g_shaderCompilationFailures.empty()) {
		ImGui::TextDisabled("DirectX APIの失敗とShader compile失敗は記録されていません。");
		ImGui::TextDisabled("ここに行が出た場合、同じ内容が logs/<日時>.Log にも残っています。");
		return;
	}

	if (hrFailureCount > 0u) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "HRESULT 失敗 %zu 件", hrFailureCount);
		ImGui::SameLine();

		if (ImGui::SmallButton("一覧をクリア")) {
			EditorClearHrFailureLog();
		}

		// 記録は上限つきなので、打ち切られている場合はそれが分かるようにする。
		if (hrFailureCount > hrFailures.size()) {
			ImGui::TextDisabled(
				"(表示は先頭 %zu 件。以降は件数のみ数えています)", hrFailures.size());
		}

		ImGui::Separator();

		for (const std::string& hrFailure : hrFailures) {
			ImGui::TextWrapped("%s", hrFailure.c_str());
		}
	}

	if (!g_shaderCompilationFailures.empty()) {
		ImGui::Separator();
		ImGui::TextColored(
			ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
			"Shader compile 失敗 %zu 件",
			g_shaderCompilationFailures.size());

		for (const std::string& shaderFailure : g_shaderCompilationFailures) {
			ImGui::TextWrapped("%s", shaderFailure.c_str());
		}
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
