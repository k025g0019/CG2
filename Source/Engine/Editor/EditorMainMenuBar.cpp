#include "EditorMainMenuBar.h"

#pragma warning(disable : 5045)

#include "EditorAssetUtility.h"
#include "EditorSharedState.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

using namespace EditorSharedState;

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // 作成テキストアセットは UTF-8 BOM 付きで保存する

	std::string MakeDefaultPlayerInputActionsText() {
		return
			"# CG2 PlayerInput Actions\r\n"
			"# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n"
			"Action|Player|Move|Vector2|2DVector|W|S|A|D\r\n"
			"Action|Player|Jump|Button|Key|Space\r\n"
			"Action|Player|Fire|Button|Mouse|LeftButton\r\n";
	}

	bool WriteUtf8BomTextFile(const std::string& filePath, const std::string& fileText) {
		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}

		file.write(reinterpret_cast<const char*>(kUtf8Bom), static_cast<std::streamsize>(sizeof(kUtf8Bom)));
		file.write(fileText.data(), static_cast<std::streamsize>(fileText.size()));
		return file.good();
	}

	std::string MakeUniqueInputActionsAssetPath(const std::string& directoryPath) {
		const std::filesystem::path baseDirectoryPath(directoryPath);
		const std::string baseName = "InGameInputAction";

		for (int32_t fileIndex = 0; fileIndex < 1000; ++fileIndex) {
			std::string candidateName = baseName;
			if (fileIndex > 0) {
				candidateName += std::to_string(fileIndex);
			}

			const std::filesystem::path candidatePath =
				baseDirectoryPath / (candidateName + ".inputactions");
			if (!std::filesystem::exists(candidatePath)) {
				return candidatePath.generic_string();
			}
		}

		return (baseDirectoryPath / "InGameInputAction.inputactions").generic_string();
	}

	bool IsSceneAssetPath(const std::string& assetPath) {
		return EditorAssetUtility::HasExtension(assetPath, ".scene");
	}

	std::string GetProjectAssetCreateDirectory(const std::string& selectedAssetPath) {
		// Project で Assets 配下のフォルダやファイルを選択している時は、その場所へ保存候補を寄せる
		if (!selectedAssetPath.empty() &&
			selectedAssetPath.rfind("Assets/", 0) == 0) {
			std::filesystem::path selectedPath(selectedAssetPath);
			if (std::filesystem::is_directory(selectedPath)) {
				return selectedPath.generic_string();
			}

			const std::filesystem::path parentPath = selectedPath.parent_path();
			if (!parentPath.empty()) {
				return parentPath.generic_string();
			}
		}

		return "Assets";
	}

	std::string BuildDefaultScenePath() {
		const std::filesystem::path baseDirectoryPath(GetProjectAssetCreateDirectory(g_selectedAssetPath));
		std::filesystem::create_directories(baseDirectoryPath);
		return (baseDirectoryPath / "NewScene.scene").generic_string();
	}

	std::string NormalizeScenePath(const std::string& scenePathText) {
		std::filesystem::path scenePath(scenePathText);

		if (scenePath.empty()) {
			scenePath = std::filesystem::path(BuildDefaultScenePath());
		}

		if (scenePath.extension().empty()) {
			scenePath += ".scene";
		}

		const std::string genericScenePath = scenePath.generic_string();
		const bool isProjectRelativePath =
			genericScenePath.rfind("Assets/", 0) == 0 ||
			genericScenePath.rfind("resources/", 0) == 0;

		if (scenePath.is_relative() && !isProjectRelativePath) {
			scenePath = std::filesystem::path("Assets") / scenePath;
		}

		return scenePath.lexically_normal().generic_string();
	}

	std::vector<std::string> CollectSceneAssetPaths() {
		std::vector<std::string> scenePaths;
		const std::vector<std::filesystem::path> rootPaths = {
			std::filesystem::path("Assets"),
			std::filesystem::path("resources"),
		};
		std::error_code fileError;

		for (const std::filesystem::path& rootPath : rootPaths) {
			fileError.clear();
			if (!std::filesystem::exists(rootPath, fileError)) {
				continue;
			}

			for (const std::filesystem::directory_entry& entry :
			     std::filesystem::recursive_directory_iterator(
				     rootPath,
				     std::filesystem::directory_options::skip_permission_denied,
				     fileError)) {
				if (fileError) {
					break;
				}

				if (!entry.is_regular_file(fileError)) {
					continue;
				}

				const std::string assetPath = entry.path().generic_string();
				if (IsSceneAssetPath(assetPath)) {
					scenePaths.push_back(assetPath);
				}
			}
		}

		std::sort(scenePaths.begin(), scenePaths.end());
		scenePaths.erase(std::unique(scenePaths.begin(), scenePaths.end()), scenePaths.end());
		return scenePaths;
	}

	void SyncSelectionToScene() {
		g_editorSelectionManager.SyncLegacySelection(
			g_selectedEditorGameObjectId,
			g_selectedSceneObject,
			g_selectedPlacedSceneObjectIndex);
	}

	void RefreshSceneObjects() {
		g_editorSceneSynchronizer.Update(g_editorTextureFilePaths, g_selectedPlacedSceneObjectIndex);
		SyncSelectionToScene();
	}

	void SelectGameObject(int32_t gameObjectId) {
		SetSingleSelectedGameObject(gameObjectId);
		g_selectedPlacedSceneObjectIndex = -1;
		SyncSelectionToScene();
	}

	void OpenProjectSettings(std::vector<std::string>& consoleMessages) {
		// Inspector は GameObject 未選択時に、環境 / 物理 / モデル設定などの Project 設定を表示する。
		ClearSelectedGameObjects();
		g_isLegacyPreviewVisible = false;
		g_selectedAssetPath.clear();
		consoleMessages.push_back("Edit: 設定を表示");
	}

	void SelectFirstGameObjectOrClear() {
		if (g_editorScene.GetGameObjects().empty()) {
			ClearSelectedGameObjects();
			return;
		}

		SelectGameObject(g_editorScene.GetGameObjects()[0].id);
	}

	bool SaveSceneToPath(
		EditorScene* editorScene,
		const std::string& scenePath,
		std::vector<std::string>& consoleMessages) {
		if (editorScene == nullptr) {
			return false;
		}

		const std::string normalizedScenePath = NormalizeScenePath(scenePath);
		const std::filesystem::path parentPath = std::filesystem::path(normalizedScenePath).parent_path();
		if (!parentPath.empty()) {
			std::filesystem::create_directories(parentPath);
		}

		if (!editorScene->SaveScene(normalizedScenePath)) {
			consoleMessages.push_back("File: シーン保存に失敗");
			return false;
		}

		g_currentScenePath = normalizedScenePath;
		g_selectedAssetPath = normalizedScenePath;
		consoleMessages.push_back("File: シーン保存 " + normalizedScenePath);
		return true;
	}

	bool LoadSceneFromPath(
		EditorScene* editorScene,
		const std::string& scenePath,
		std::vector<std::string>& consoleMessages) {
		if (editorScene == nullptr) {
			return false;
		}

		const std::string normalizedScenePath = NormalizeScenePath(scenePath);
		if (!editorScene->LoadScene(normalizedScenePath)) {
			consoleMessages.push_back("File: シーン読込に失敗");
			return false;
		}

		g_currentScenePath = normalizedScenePath;
		g_selectedAssetPath = normalizedScenePath;
		SelectFirstGameObjectOrClear();
		RefreshSceneObjects();
		consoleMessages.push_back("File: シーン読込 " + normalizedScenePath);
		return true;
	}

	void CreateEmptyGameObject(std::vector<std::string>& consoleMessages) {
		g_editorScene.PushUndo();  // GameObject 生成を Undo 対象にする
		const int32_t gameObjectId = g_editorScene.CreateGameObject("GameObject");
		SelectGameObject(gameObjectId);
		consoleMessages.push_back("Scene: 空のGameObjectを作成");
	}

	void CreateLightGameObject(std::vector<std::string>& consoleMessages) {
		g_editorScene.PushUndo();  // ライト生成を Undo 対象にする
		const int32_t gameObjectId = g_editorScene.CreateGameObject("Light");
		g_editorScene.AddComponent(gameObjectId, EditorComponentType::Light);
		EditorGameObject* lightGameObject = g_editorScene.FindGameObject(gameObjectId);
		if (lightGameObject != nullptr) {
			// Scene 中央より少し上と手前に出し、生成直後からアイコンとギズモで掴める位置に置く。
			lightGameObject->translate = {0.0f, 3.0f, -2.0f};
			lightGameObject->rotate = {1.1f, 0.0f, 0.0f};
		}
		SelectGameObject(gameObjectId);
		g_selectedSceneObject = 2;
		consoleMessages.push_back("Scene: ライトを作成");
	}

	void CreateCameraGameObject(std::vector<std::string>& consoleMessages) {
		g_editorScene.PushUndo();  // カメラ生成を Undo 対象にする
		const int32_t gameObjectId = g_editorScene.CreateGameObject("Camera");
		g_editorScene.AddComponent(gameObjectId, EditorComponentType::Camera);
		EditorGameObject* cameraGameObject = g_editorScene.FindGameObject(gameObjectId);
		if (cameraGameObject != nullptr) {
			// 原点を見やすい距離から見る初期位置にして、生成直後からScene上で見失わないようにする。
			cameraGameObject->translate = {0.0f, 2.0f, -6.0f};
			cameraGameObject->rotate = {0.25f, 0.0f, 0.0f};
		}
		SelectGameObject(gameObjectId);
		g_selectedSceneObject = 3;
		consoleMessages.push_back("Scene: カメラを作成");
	}

	void CreatePrimitiveGameObject(const char* assetPath) {
		if (assetPath == nullptr) {
			return;
		}

		g_editorAssetFactory.CreateModelGameObject(
			assetPath,
			Vector3{0.0f, 1.5f, 0.0f},
			g_selectedEditorGameObjectId,
			g_selectedPlacedSceneObjectIndex,
			g_selectedSceneObject);
		g_previousSelectedEditorGameObjectId = -1;
		SyncSelectionToScene();
	}

	void CreateInputActionsAsset(std::vector<std::string>& consoleMessages) {
		const std::filesystem::path createDirectoryPath("Assets");
		std::filesystem::create_directories(createDirectoryPath);
		const std::string filePath = MakeUniqueInputActionsAssetPath(createDirectoryPath.generic_string());

		if (WriteUtf8BomTextFile(filePath, MakeDefaultPlayerInputActionsText())) {
			g_selectedAssetPath = filePath;
			consoleMessages.push_back("Asset: Input Actions を作成 " + filePath);
			return;
		}

		consoleMessages.push_back("Asset: Input Actions の作成に失敗");
	}

	void AddComponentToSelectedGameObject(
		EditorComponentType componentType,
		const char* componentName,
		std::vector<std::string>& consoleMessages) {
		if (g_selectedEditorGameObjectId < 0 || componentName == nullptr) {
			return;
		}

		g_editorScene.PushUndo();  // Component 追加を Undo 対象にする
		if (!g_editorScene.AddComponent(g_selectedEditorGameObjectId, componentType)) {
			consoleMessages.push_back("Component: 追加できませんでした " + std::string(componentName));
			return;
		}

		RefreshSceneObjects();
		consoleMessages.push_back("Component: 追加 " + std::string(componentName));
	}
}

void EditorMainMenuBar::Initialize(EditorScene* editorScene, EditorRuntimeManager* runtimeManager) {
	editorScene_ = editorScene;  // Draw の Play ボタンで使う参照を保持する
	runtimeManager_ = runtimeManager;
}

void EditorMainMenuBar::Update() {
}

void EditorMainMenuBar::Draw(
	std::vector<std::string>& consoleMessages,
	bool& isRuntimeInitialized,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& previousSelectedGameObjectId) {
	if (editorScene_ == nullptr || runtimeManager_ == nullptr) {
		return;
	}

	static bool shouldOpenSceneSaveAsPopup = false;  // 保存先入力モーダルを次フレームで開く要求
	static bool shouldOpenSceneLoadPopup = false;  // 読込候補一覧モーダルを次フレームで開く要求
	static char sceneSavePathBuffer[260] = {};  // 名前を付けて保存の入力欄
	static char sceneLoadPathBuffer[260] = {};  // 読込候補一覧での直接入力欄

	// MainMenuBar が開けないフレームはメニュー描画を行わない
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (ImGui::BeginMenu("ファイル")) {
		if (ImGui::MenuItem("保存", "Ctrl+S")) {
			if (g_currentScenePath.empty()) {
				const std::string defaultScenePath = BuildDefaultScenePath();
				strncpy_s(sceneSavePathBuffer, sizeof(sceneSavePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
				shouldOpenSceneSaveAsPopup = true;
			}
			else {
				SaveSceneToPath(editorScene_, g_currentScenePath, consoleMessages);
			}
		}

		if (ImGui::MenuItem("名前を付けて保存")) {
			const std::string defaultScenePath =
				g_currentScenePath.empty() ? BuildDefaultScenePath() : g_currentScenePath;
			strncpy_s(sceneSavePathBuffer, sizeof(sceneSavePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
			shouldOpenSceneSaveAsPopup = true;
		}

		if (ImGui::MenuItem("読み込み")) {
			sceneLoadPathBuffer[0] = '\0';
			shouldOpenSceneLoadPopup = true;
		}

		ImGui::Separator();
		ImGui::TextDisabled(
			"現在: %s",
			g_currentScenePath.empty() ? "未保存シーン" : g_currentScenePath.c_str());

		ImGui::Separator();

		if (ImGui::MenuItem("終了")) {
			PostQuitMessage(0);  // 上部メニューからも通常の終了導線へ流す
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("編集")) {
		if (ImGui::MenuItem("元に戻す", "Ctrl+Z")) {
			if (editorScene_->Undo()) {
				RefreshSceneObjects();
				consoleMessages.push_back("Edit: 元に戻す");
			}
		}

		if (ImGui::MenuItem("やり直し", "Ctrl+Y")) {
			if (editorScene_->Redo()) {
				RefreshSceneObjects();
				consoleMessages.push_back("Edit: やり直し");
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("設定を開く")) {
			OpenProjectSettings(consoleMessages);
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("アセット")) {
		if (ImGui::MenuItem("Input Actions 作成")) {
			CreateInputActionsAsset(consoleMessages);
		}

		if (ImGui::MenuItem("選択アセット解除")) {
			g_selectedAssetPath.clear();
			consoleMessages.push_back("Asset: 選択解除");
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("ゲームオブジェクト")) {
		if (ImGui::MenuItem("空のGameObject")) {
			CreateEmptyGameObject(consoleMessages);
		}

		if (ImGui::BeginMenu("3D Object")) {
			if (ImGui::MenuItem("Cube")) {
				CreatePrimitiveGameObject("resources/UVCube.fbx");
			}
			if (ImGui::MenuItem("Box")) {
				CreatePrimitiveGameObject("resources/box.fbx");
			}
			if (ImGui::MenuItem("Cylinder")) {
				CreatePrimitiveGameObject("resources/cylinder.fbx");
			}
			if (ImGui::MenuItem("Cone")) {
				CreatePrimitiveGameObject("resources/cone.fbx");
			}
			if (ImGui::MenuItem("Torus")) {
				CreatePrimitiveGameObject("resources/Torus.fbx");
			}
			if (ImGui::MenuItem("Ico")) {
				CreatePrimitiveGameObject("resources/ICOCube.fbx");
			}
			if (ImGui::MenuItem("Sphere")) {
				CreatePrimitiveGameObject("resources/sphere.fbx");
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("ライト")) {
			CreateLightGameObject(consoleMessages);
		}

		if (ImGui::MenuItem("カメラ")) {
			CreateCameraGameObject(consoleMessages);
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("コンポーネント")) {
		const bool canAddComponent = g_selectedEditorGameObjectId >= 0;

		if (ImGui::MenuItem("メッシュフィルター", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::MeshFilter, "メッシュフィルター", consoleMessages);
		}

		if (ImGui::MenuItem("メッシュレンダラー", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::ModelRenderer, "メッシュレンダラー", consoleMessages);
		}

		if (ImGui::MenuItem("リジッドボディ", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::RigidBody, "リジッドボディ", consoleMessages);
		}

		if (ImGui::MenuItem("箱の当たり判定", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::BoxCollider, "箱の当たり判定", consoleMessages);
		}

		if (ImGui::MenuItem("球の当たり判定", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::SphereCollider, "球の当たり判定", consoleMessages);
		}

		if (ImGui::MenuItem("プレイヤー入力", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::PlayerInput, "プレイヤー入力", consoleMessages);
		}

		if (ImGui::MenuItem("C++ スクリプト", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::Script, "C++ スクリプト", consoleMessages);
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("ウィンドウ")) {
		ImGui::MenuItem("アニメーション", nullptr, &g_isAnimationWindowVisible);

		if (ImGui::MenuItem("Console 表示")) {
			g_isConsoleCleared = false;
		}

		if (ImGui::MenuItem("選択解除")) {
			ClearSelectedGameObjects();
		}

		if (ImGui::MenuItem("レイアウト再構築")) {
			g_isDockLayoutInitialized = false;  // 次回起動時に既定 Dock を組み直せるようフラグを戻す
			consoleMessages.push_back("Window: レイアウト再構築は次回起動時に反映されます");
		}

		ImGui::EndMenu();
	}

	if (shouldOpenSceneSaveAsPopup) {
		ImGui::OpenPopup("SceneSaveAsPopup");
		shouldOpenSceneSaveAsPopup = false;
	}

	if (ImGui::BeginPopupModal("SceneSaveAsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("シーンの保存先を入力");
		ImGui::InputText("保存先", sceneSavePathBuffer, sizeof(sceneSavePathBuffer));
		ImGui::TextDisabled("例: Assets/Scenes/Sample.scene");

		if (ImGui::Button("保存する", ImVec2(160.0f, 0.0f))) {
			if (SaveSceneToPath(editorScene_, sceneSavePathBuffer, consoleMessages)) {
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (shouldOpenSceneLoadPopup) {
		ImGui::OpenPopup("SceneLoadPopup");
		shouldOpenSceneLoadPopup = false;
	}

	if (ImGui::BeginPopupModal("SceneLoadPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("読み込むシーンを選択");
		const std::vector<std::string> scenePaths = CollectSceneAssetPaths();

		ImGui::BeginChild("SceneLoadList", ImVec2(520.0f, 220.0f), true);
		for (const std::string& scenePath : scenePaths) {
			if (ImGui::Selectable(scenePath.c_str(), false)) {
				if (LoadSceneFromPath(editorScene_, scenePath, consoleMessages)) {
					ImGui::CloseCurrentPopup();
				}
			}
		}

		if (scenePaths.empty()) {
			ImGui::TextDisabled("読み込める .scene がありません");
		}
		ImGui::EndChild();

		ImGui::Separator();
		ImGui::Text("直接パス入力");
		ImGui::InputText("パス", sceneLoadPathBuffer, sizeof(sceneLoadPathBuffer));

		if (ImGui::Button("このパスを読込", ImVec2(160.0f, 0.0f))) {
			if (sceneLoadPathBuffer[0] != '\0' &&
				LoadSceneFromPath(editorScene_, sceneLoadPathBuffer, consoleMessages)) {
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::Separator();
	if (ImGui::Button(runtimeManager_->IsPlaying() ? "Stop" : "Play")) {
		// Runtime 未初期化なら Play 前に Scene 参照を渡す
		if (!isRuntimeInitialized) {
			runtimeManager_->Initialize(editorScene_, &consoleMessages);
			isRuntimeInitialized = true;
		}

		runtimeManager_->TogglePlay();  // Play 中なら Stop、停止中なら Play に切り替える
		consoleMessages.push_back(runtimeManager_->IsPlaying() ? "Play: Started" : "Play: Stopped");
		selectedPlacedSceneObjectIndex = -1;  // Play 切替後は SceneView の描画オブジェクト選択を解除する
		previousSelectedGameObjectId = -1;
	}

	ImGui::EndMainMenuBar();
}
