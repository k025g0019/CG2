#include "EditorMainMenuBar.h"

#pragma warning(disable : 5045)

#include "EditorAssetUtility.h"
#include "EditorGameBuildManager.h"
#include "EditorSharedState.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

using namespace EditorSharedState;

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // 作成テキストアセットは UTF-8 BOM 付きで保存する
	constexpr char kDefaultSceneDirectory[] = "Assets/Scenes";  // Project 内で Scene Asset をまとめる標準フォルダー。
	constexpr char kEditorSettingsPath[] = "ProjectSettings/EditorSettings.cg2";
	constexpr char kAutoSaveDirectory[] = "Library/AutoSave";
	constexpr char kUnsavedSceneAutoSavePath[] = "Library/AutoSave/UnsavedScene.scene";

	enum class AutoSaveResult {
		NoChanges,
		Saved,
		Failed,
	};

	std::string MakeDefaultPlayerInputActionsText() {
		return
			"# CG2 PlayerInput Actions\r\n"
			"# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n"
			"Action|Player|Move|Vector2|2DVector|W|S|A|D\r\n"
			"Action|Player|Jump|Button|Key|Space\r\n"
			"Action|Player|Fire|Button|Mouse|LeftButton\r\n";
	}

	bool WriteUtf8BomTextFile(const std::string& filePath, const std::string& fileText) {
		const std::filesystem::path parentPath = std::filesystem::path(filePath).parent_path();

		if (!parentPath.empty()) {
			std::error_code directoryError;
			std::filesystem::create_directories(parentPath, directoryError);
		}

		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}

		file.write(reinterpret_cast<const char*>(kUtf8Bom), static_cast<std::streamsize>(sizeof(kUtf8Bom)));
		file.write(fileText.data(), static_cast<std::streamsize>(fileText.size()));
		return file.good();
	}

	bool AreFilesEqual(
		const std::filesystem::path& firstPath,
		const std::filesystem::path& secondPath) {
		std::error_code fileError;

		if (!std::filesystem::exists(firstPath, fileError) || fileError) {
			return false;
		}

		fileError.clear();
		if (!std::filesystem::exists(secondPath, fileError) || fileError) {
			return false;
		}

		fileError.clear();
		const uintmax_t firstSize = std::filesystem::file_size(firstPath, fileError);

		if (fileError) {
			return false;
		}

		fileError.clear();
		const uintmax_t secondSize = std::filesystem::file_size(secondPath, fileError);

		if (fileError || firstSize != secondSize) {
			return false;
		}

		std::ifstream firstFile(firstPath, std::ios::binary);
		std::ifstream secondFile(secondPath, std::ios::binary);

		if (!firstFile.is_open() || !secondFile.is_open()) {
			return false;
		}

		std::array<char, 65536> firstBuffer{};
		std::array<char, 65536> secondBuffer{};

		while (firstFile.good() && secondFile.good()) {
			firstFile.read(firstBuffer.data(), static_cast<std::streamsize>(firstBuffer.size()));
			secondFile.read(secondBuffer.data(), static_cast<std::streamsize>(secondBuffer.size()));
			const std::streamsize firstReadSize = firstFile.gcount();
			const std::streamsize secondReadSize = secondFile.gcount();

			if (firstReadSize != secondReadSize ||
				std::memcmp(firstBuffer.data(), secondBuffer.data(), static_cast<size_t>(firstReadSize)) != 0) {
				return false;
			}
		}

		return true;
	}

	uint64_t MakeStablePathHash(const std::string& pathText) {
		uint64_t pathHash = 1469598103934665603ull;

		for (const unsigned char character : pathText) {
			pathHash ^= static_cast<uint64_t>(character);
			pathHash *= 1099511628211ull;
		}

		return pathHash;
	}

	std::filesystem::path BuildAutoSaveBackupPath(const std::string& scenePath) {
		const std::filesystem::path normalizedPath =
			std::filesystem::path(scenePath).lexically_normal();
		std::ostringstream fileName;
		fileName << normalizedPath.stem().generic_string() << "_"
			     << std::hex << std::setw(16) << std::setfill('0')
			     << MakeStablePathHash(normalizedPath.generic_string())
			     << ".previous.scene";
		return std::filesystem::path(kAutoSaveDirectory) / fileName.str();
	}

	bool ReplaceFileFromTemporary(
		const std::filesystem::path& temporaryPath,
		const std::filesystem::path& destinationPath) {
		const std::filesystem::path absoluteTemporaryPath = std::filesystem::absolute(temporaryPath);
		const std::filesystem::path absoluteDestinationPath = std::filesystem::absolute(destinationPath);
		return MoveFileExW(
			absoluteTemporaryPath.c_str(),
			absoluteDestinationPath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	AutoSaveResult SaveSceneAutomatically(
		EditorScene* editorScene,
		const std::string& scenePath,
		std::string& savedPath) {
		if (editorScene == nullptr) {
			return AutoSaveResult::Failed;
		}

		const bool savesUnsavedScene = scenePath.empty();
		const std::filesystem::path destinationPath = savesUnsavedScene
			? std::filesystem::path(kUnsavedSceneAutoSavePath)
			: std::filesystem::path(scenePath).lexically_normal();
		const std::filesystem::path parentPath = destinationPath.parent_path();
		std::error_code fileError;

		if (!parentPath.empty()) {
			std::filesystem::create_directories(parentPath, fileError);

			if (fileError) {
				return AutoSaveResult::Failed;
			}
		}

		const std::filesystem::path temporaryPath = destinationPath.string() + ".autosave.tmp";

		if (!editorScene->SaveScene(temporaryPath.generic_string())) {
			return AutoSaveResult::Failed;
		}

		if (AreFilesEqual(temporaryPath, destinationPath)) {
			std::filesystem::remove(temporaryPath, fileError);
			return AutoSaveResult::NoChanges;
		}

		if (!savesUnsavedScene && std::filesystem::exists(destinationPath, fileError) && !fileError) {
			const std::filesystem::path backupPath = BuildAutoSaveBackupPath(scenePath);
			std::filesystem::create_directories(backupPath.parent_path(), fileError);

			if (!fileError) {
				std::filesystem::copy_file(
					destinationPath,
					backupPath,
					std::filesystem::copy_options::overwrite_existing,
					fileError);
			}
		}

		if (!ReplaceFileFromTemporary(temporaryPath, destinationPath)) {
			savedPath = temporaryPath.generic_string();
			return AutoSaveResult::Failed;
		}

		savedPath = destinationPath.generic_string();
		return AutoSaveResult::Saved;
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

	std::string GetSceneAssetCreateDirectory(const std::string& selectedAssetPath) {
		// Scene は Assets/Scenes 配下へ集約し、選択中の Scene サブフォルダーだけ維持する。
		if (!selectedAssetPath.empty() &&
			(selectedAssetPath == kDefaultSceneDirectory ||
			 selectedAssetPath.rfind(std::string(kDefaultSceneDirectory) + "/", 0) == 0)) {
			std::filesystem::path selectedPath(selectedAssetPath);

			if (std::filesystem::is_directory(selectedPath)) {
				return selectedPath.generic_string();
			}

			const std::filesystem::path parentPath = selectedPath.parent_path();
			if (!parentPath.empty()) {
				return parentPath.generic_string();
			}
		}

		return kDefaultSceneDirectory;
	}

	std::string BuildDefaultScenePath() {
		const std::filesystem::path baseDirectoryPath(GetSceneAssetCreateDirectory(g_selectedAssetPath));
		std::filesystem::create_directories(baseDirectoryPath);

		for (int32_t sceneIndex = 0; sceneIndex < 1000; sceneIndex++) {
			std::string sceneName = "NewScene";

			if (sceneIndex > 0) {
				sceneName += std::to_string(sceneIndex);
			}

			const std::filesystem::path candidatePath = baseDirectoryPath / (sceneName + ".scene");

			if (!std::filesystem::exists(candidatePath)) {
				return candidatePath.generic_string();
			}
		}

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
			scenePath = std::filesystem::path(kDefaultSceneDirectory) / scenePath;
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

	bool ContainsScenePath(
		const std::vector<std::string>& scenePaths,
		const std::string& scenePath) {
		return std::find(
			scenePaths.begin(),
			scenePaths.end(),
			scenePath) != scenePaths.end();
	}

	void AddUniqueScenePath(
		std::vector<std::string>& scenePaths,
		const std::string& scenePath) {
		if (!scenePath.empty() && !ContainsScenePath(scenePaths, scenePath)) {
			scenePaths.push_back(scenePath);
		}
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

	void CreateNewEditingScene(
		EditorScene* editorScene,
		EditorRuntimeManager* runtimeManager,
		std::vector<std::string>& consoleMessages) {
		if (editorScene == nullptr || runtimeManager == nullptr) {
			return;
		}

		if (runtimeManager->IsPlaying()) {
			runtimeManager->TogglePlay();
		}

		editorScene->InitializeDefaultScene();
		const std::string newScenePath = BuildDefaultScenePath();

		if (!SaveSceneToPath(editorScene, newScenePath, consoleMessages)) {
			g_currentScenePath.clear();
			g_selectedAssetPath.clear();
		}

		SelectFirstGameObjectOrClear();
		RefreshSceneObjects();
		consoleMessages.push_back("File: 新規 Scene を開く " + newScenePath);
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

	void CreateOceanGameObject(std::vector<std::string>& consoleMessages) {
		g_editorScene.PushUndo();
		const int32_t gameObjectId = g_editorScene.CreateGameObject("Ocean");
		g_editorScene.AddComponent(gameObjectId, EditorComponentType::Ocean);
		SelectGameObject(gameObjectId);
		RefreshSceneObjects();
		consoleMessages.push_back("Scene: Ocean を作成");
	}

	bool IsInsideCanvas(int32_t gameObjectId) {
		int32_t currentGameObjectId = gameObjectId;

		while (currentGameObjectId >= 0) {
			const EditorGameObject* currentGameObject = g_editorScene.FindGameObject(currentGameObjectId);

			if (currentGameObject == nullptr) {
				return false;
			}

			if (g_editorScene.HasComponent(currentGameObjectId, EditorComponentType::Canvas)) {
				return true;
			}

			currentGameObjectId = currentGameObject->parentId;
		}

		return false;
	}

	int32_t FindUiParentGameObjectId() {
		if (g_selectedEditorGameObjectId >= 0 && IsInsideCanvas(g_selectedEditorGameObjectId)) {
			return g_selectedEditorGameObjectId;
		}

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (g_editorScene.HasComponent(gameObject.id, EditorComponentType::Canvas)) {
				return gameObject.id;
			}
		}

		return -1;
	}

	int32_t CreateCanvasGameObject(bool pushesUndo) {
		if (pushesUndo) {
			g_editorScene.PushUndo();
		}

		const int32_t canvasGameObjectId = g_editorScene.CreateGameObject("Canvas");
		g_editorScene.AddComponent(canvasGameObjectId, EditorComponentType::Canvas);
		g_editorScene.AddComponent(canvasGameObjectId, EditorComponentType::CanvasScaler);
		g_editorScene.AddComponent(canvasGameObjectId, EditorComponentType::GraphicRaycaster);
		return canvasGameObjectId;
	}

	void CreateUiElementGameObject(
		const char* gameObjectName,
		EditorComponentType visualType,
		std::vector<std::string>& consoleMessages) {
		g_editorScene.PushUndo();
		int32_t parentGameObjectId = FindUiParentGameObjectId();

		if (parentGameObjectId < 0) {
			parentGameObjectId = CreateCanvasGameObject(false);
		}

		const int32_t uiGameObjectId = g_editorScene.CreateGameObject(gameObjectName);
		g_editorScene.SetParent(uiGameObjectId, parentGameObjectId);
		g_editorScene.AddComponent(uiGameObjectId, visualType);
		EditorGameObject* uiGameObject = g_editorScene.FindGameObject(uiGameObjectId);
		EditorComponent* visualComponent = nullptr;

		if (uiGameObject != nullptr) {
			for (EditorComponent& component : uiGameObject->components) {
				if (component.type == visualType) {
					visualComponent = &component;
					break;
				}
			}
		}

		if (visualComponent != nullptr) {
			visualComponent->buttonPosition = {40.0f, 40.0f};
			visualComponent->buttonSize = visualType == EditorComponentType::Slider
				? EditorScriptVector2{240.0f, 28.0f}
				: EditorScriptVector2{320.0f, 32.0f};
			visualComponent->buttonLabel = gameObjectName;
		}

		SelectGameObject(uiGameObjectId);
		consoleMessages.push_back(std::string("Scene UI: ") + gameObjectName + " を作成");
	}

	EditorComponent* FindComponent(
		EditorGameObject* gameObject,
		EditorComponentType componentType) {
		if (gameObject == nullptr) {
			return nullptr;
		}

		for (EditorComponent& component : gameObject->components) {
			if (component.type == componentType) {
				return &component;
			}
		}

		return nullptr;
	}

	EditorGameObject* CreateStressModel(
		EditorScene* editorScene,
		const std::string& objectName,
		const std::string& assetPath,
		const Vector3& position,
		const Vector3& scale) {
		if (editorScene == nullptr) {
			return nullptr;
		}

		const int32_t gameObjectId = editorScene->CreateGameObject(objectName);
		editorScene->AddComponent(gameObjectId, EditorComponentType::MeshFilter);
		editorScene->AddComponent(gameObjectId, EditorComponentType::ModelRenderer);
		EditorGameObject* gameObject = editorScene->FindGameObject(gameObjectId);

		if (gameObject == nullptr) {
			return nullptr;
		}

		gameObject->translate = position;
		gameObject->scale = scale;
		EditorComponent* meshFilter = FindComponent(gameObject, EditorComponentType::MeshFilter);
		EditorComponent* modelRenderer = FindComponent(gameObject, EditorComponentType::ModelRenderer);

		if (meshFilter != nullptr) {
			meshFilter->assetPath = assetPath;
		}

		if (modelRenderer != nullptr) {
			modelRenderer->assetPath = assetPath;
			modelRenderer->useImportedMaterialTextures = true;
			modelRenderer->lightingMode = 3;
		}

		return gameObject;
	}

	void CreateGameplayFoundationValidationScene(
		EditorScene* editorScene,
		EditorRuntimeManager* runtimeManager,
		std::vector<std::string>& consoleMessages) {
		if (editorScene == nullptr || runtimeManager == nullptr) {
			return;
		}

		if (runtimeManager->IsPlaying()) {
			runtimeManager->TogglePlay();
		}

		//============================================================
		// Scene と Prefab Asset
		//============================================================

		std::filesystem::create_directories("Assets/Scenes");
		std::filesystem::create_directories("Assets/Prefabs");
		const std::string validationInputPath =
			"Assets/GameplayFoundationValidation.inputactions";
		WriteUtf8BomTextFile(validationInputPath, MakeDefaultPlayerInputActionsText());

		EditorScene additiveScene;
		const int32_t additiveMarkerId = additiveScene.CreateGameObject("Additive Scene Marker");
		additiveScene.AddComponent(additiveMarkerId, EditorComponentType::Saveable);
		additiveScene.SaveScene("Assets/Scenes/GameplayFoundationAdditive.scene");

		editorScene->InitializeDefaultScene();

		for (EditorGameObject& gameObject : editorScene->GetGameObjects()) {
			if (gameObject.name == "Main Camera") {
				gameObject.translate = {0.0f, 8.0f, -20.0f};
				gameObject.rotate = {0.12f, 0.0f, 0.0f};
			}
		}

		const int32_t prefabRootId = editorScene->CreateGameObject("Validation Prefab Root");
		editorScene->AddComponent(prefabRootId, EditorComponentType::MeshFilter);
		editorScene->AddComponent(prefabRootId, EditorComponentType::ModelRenderer);
		EditorGameObject* prefabRoot = editorScene->FindGameObject(prefabRootId);

		if (prefabRoot != nullptr) {
			prefabRoot->translate = {-5.0f, 1.0f, 6.0f};
			EditorComponent* meshFilter = FindComponent(prefabRoot, EditorComponentType::MeshFilter);
			EditorComponent* modelRenderer = FindComponent(prefabRoot, EditorComponentType::ModelRenderer);

			if (meshFilter != nullptr) {
				meshFilter->assetPath = "resources/editorDefault/box.fbx";
			}

			if (modelRenderer != nullptr) {
				modelRenderer->assetPath = "resources/editorDefault/box.fbx";
				modelRenderer->color = {0.15f, 0.65f, 0.95f};
			}
		}

		const int32_t prefabChildId = editorScene->CreateGameObject("Validation Prefab Child");
		editorScene->SetParent(prefabChildId, prefabRootId);
		EditorGameObject* prefabChild = editorScene->FindGameObject(prefabChildId);

		if (prefabChild != nullptr) {
			prefabChild->translate = {0.0f, 2.0f, 0.0f};
		}

		editorScene->SavePrefab(prefabRootId, "Assets/Prefabs/ValidationHierarchy.prefab");
		const int32_t prefabInstanceId =
			editorScene->InstantiatePrefab("Assets/Prefabs/ValidationHierarchy.prefab");
		EditorGameObject* prefabInstance = editorScene->FindGameObject(prefabInstanceId);

		if (prefabInstance != nullptr) {
			prefabInstance->translate = {5.0f, 1.0f, 6.0f};
		}

		//============================================================
		// Rail / Branch / Wave
		//============================================================

		const int32_t railPathId = editorScene->CreateGameObject("Validation Rail Path A");
		const Vector3 railPoints[] = {
			{-8.0f, 2.0f, -4.0f},
			{-4.0f, 3.0f, 5.0f},
			{4.0f, 4.0f, 13.0f},
			{10.0f, 2.0f, 22.0f},
		};

		for (int32_t pointIndex = 0; pointIndex < 4; pointIndex++) {
			const int32_t pointId = editorScene->CreateGameObject(
				"Rail A Point " + std::to_string(pointIndex));
			editorScene->SetParent(pointId, railPathId);
			EditorGameObject* point = editorScene->FindGameObject(pointId);

			if (point != nullptr) {
				point->translate = railPoints[static_cast<size_t>(pointIndex)];
			}
		}

		const int32_t railPathBId = editorScene->CreateGameObject("Validation Rail Path B");
		for (int32_t pointIndex = 0; pointIndex < 3; pointIndex++) {
			const int32_t pointId = editorScene->CreateGameObject(
				"Rail B Point " + std::to_string(pointIndex));
			editorScene->SetParent(pointId, railPathBId);
			EditorGameObject* point = editorScene->FindGameObject(pointId);

			if (point != nullptr) {
				point->translate = {
					-8.0f + static_cast<float>(pointIndex) * 8.0f,
					5.0f,
					10.0f + static_cast<float>(pointIndex) * 8.0f};
			}
		}

		const int32_t followerId = editorScene->CreateGameObject("Validation Rail Follower");
		editorScene->AddComponent(followerId, EditorComponentType::RailMovement);
		editorScene->AddComponent(followerId, EditorComponentType::RailBranch);
		EditorGameObject* follower = editorScene->FindGameObject(followerId);

		if (follower != nullptr) {
			EditorComponent* railMovement = FindComponent(follower, EditorComponentType::RailMovement);
			EditorComponent* railBranch = FindComponent(follower, EditorComponentType::RailBranch);

			if (railMovement != nullptr) {
				railMovement->railPathGameObjectId = railPathId;
				railMovement->railSpeed = 5.0f;
				railMovement->railAcceleration = 3.0f;
				railMovement->railDeceleration = 4.0f;
			}

			if (railBranch != nullptr) {
				railBranch->railBranchFollowerGameObjectId = followerId;
				railBranch->railBranchTargetPathGameObjectId = railPathBId;
				railBranch->railBranchTriggerNormalized = 0.55f;
			}
		}

		const int32_t waveTemplateId = editorScene->CreateGameObject("Validation Wave Template");
		editorScene->AddComponent(waveTemplateId, EditorComponentType::Health);
		const int32_t wavePoolId = editorScene->CreateGameObject("Validation Wave Pool");
		editorScene->AddComponent(wavePoolId, EditorComponentType::ObjectPool);
		EditorComponent* wavePool = FindComponent(
			editorScene->FindGameObject(wavePoolId),
			EditorComponentType::ObjectPool);

		if (wavePool != nullptr) {
			wavePool->objectPoolTemplateGameObjectId = waveTemplateId;
			wavePool->objectPoolInitialSize = 3;
			wavePool->objectPoolAllowExpand = false;
		}

		const int32_t waveId = editorScene->CreateGameObject("Validation Wave Spawner");
		editorScene->AddComponent(waveId, EditorComponentType::WaveSpawner);
		EditorComponent* waveSpawner = FindComponent(
			editorScene->FindGameObject(waveId),
			EditorComponentType::WaveSpawner);

		if (waveSpawner != nullptr) {
			waveSpawner->waveSpawnSourceMode = 0;
			waveSpawner->wavePoolGameObjectId = wavePoolId;
			waveSpawner->waveSpawnPointGameObjectId = waveId;
			waveSpawner->waveSpawnCount = 3;
			waveSpawner->waveFormationPattern = 2;
			waveSpawner->waveFormationSpacing = 3.0f;
			waveSpawner->waveTriggerMode = 1;
			waveSpawner->waveTriggerSourceGameObjectId = followerId;
			waveSpawner->waveTriggerValue = 0.25f;
			waveSpawner->waveSpawnInterval = 0.5f;
		}

		//============================================================
		// Aim / Weapon / Pool / Damage / Save
		//============================================================

		const int32_t damageTargetId = editorScene->CreateGameObject("Validation Damage Target");
		editorScene->AddComponent(damageTargetId, EditorComponentType::MeshFilter);
		editorScene->AddComponent(damageTargetId, EditorComponentType::ModelRenderer);
		editorScene->AddComponent(damageTargetId, EditorComponentType::BoxCollider);
		editorScene->AddComponent(damageTargetId, EditorComponentType::Health);
		editorScene->AddComponent(damageTargetId, EditorComponentType::DamageReceiver);
		editorScene->AddComponent(damageTargetId, EditorComponentType::Saveable);
		EditorGameObject* damageTarget = editorScene->FindGameObject(damageTargetId);

		if (damageTarget != nullptr) {
			damageTarget->translate = {0.0f, 3.0f, 18.0f};
			damageTarget->scale = {2.0f, 2.0f, 2.0f};
			damageTarget->isActive = false;
			EditorComponent* targetMesh = FindComponent(damageTarget, EditorComponentType::MeshFilter);
			EditorComponent* targetRenderer = FindComponent(damageTarget, EditorComponentType::ModelRenderer);
			EditorComponent* saveable = FindComponent(damageTarget, EditorComponentType::Saveable);

			if (targetMesh != nullptr) {
				targetMesh->assetPath = "resources/editorDefault/ICOCube.fbx";
			}

			if (targetRenderer != nullptr) {
				targetRenderer->assetPath = "resources/editorDefault/ICOCube.fbx";
				targetRenderer->color = {0.95f, 0.25f, 0.1f};
			}

			if (saveable != nullptr) {
				saveable->saveableKey = "validation_target";
			}
		}

		const int32_t projectileTemplateId = editorScene->CreateGameObject("Validation Projectile Template");
		editorScene->AddComponent(projectileTemplateId, EditorComponentType::MeshFilter);
		editorScene->AddComponent(projectileTemplateId, EditorComponentType::ModelRenderer);
		editorScene->AddComponent(projectileTemplateId, EditorComponentType::SphereCollider);
		EditorGameObject* projectileTemplate = editorScene->FindGameObject(projectileTemplateId);

		if (projectileTemplate != nullptr) {
			projectileTemplate->scale = {0.2f, 0.2f, 0.2f};
			projectileTemplate->isActive = false;
			EditorComponent* projectileMesh = FindComponent(projectileTemplate, EditorComponentType::MeshFilter);
			EditorComponent* projectileRenderer = FindComponent(projectileTemplate, EditorComponentType::ModelRenderer);

			if (projectileMesh != nullptr) {
				projectileMesh->assetPath = "resources/editorDefault/en.fbx";
			}

			if (projectileRenderer != nullptr) {
				projectileRenderer->assetPath = "resources/editorDefault/en.fbx";
				projectileRenderer->color = {1.0f, 0.65f, 0.05f};
				projectileRenderer->emissionStrength = 2.0f;
				projectileRenderer->emissionColor = {1.0f, 0.35f, 0.0f};
			}
		}

		const int32_t poolId = editorScene->CreateGameObject("Validation Projectile Pool");
		editorScene->AddComponent(poolId, EditorComponentType::ObjectPool);
		EditorComponent* objectPool = FindComponent(
			editorScene->FindGameObject(poolId),
			EditorComponentType::ObjectPool);

		if (objectPool != nullptr) {
			objectPool->objectPoolTemplateGameObjectId = projectileTemplateId;
			objectPool->objectPoolInitialSize = 16;
			objectPool->objectPoolAllowExpand = true;
		}

		const int32_t weaponId = editorScene->CreateGameObject("Validation Aim And Weapons");
		editorScene->AddComponent(weaponId, EditorComponentType::PlayerInput);
		editorScene->AddComponent(weaponId, EditorComponentType::ScreenAim);
		editorScene->AddComponent(weaponId, EditorComponentType::HitscanWeapon);
		editorScene->AddComponent(weaponId, EditorComponentType::ProjectileEmitter);
		EditorGameObject* weapon = editorScene->FindGameObject(weaponId);

		if (weapon != nullptr) {
			EditorComponent* playerInput = FindComponent(weapon, EditorComponentType::PlayerInput);
			EditorComponent* hitscan = FindComponent(weapon, EditorComponentType::HitscanWeapon);
			EditorComponent* projectile = FindComponent(weapon, EditorComponentType::ProjectileEmitter);

			if (playerInput != nullptr) {
				playerInput->assetPath = validationInputPath;
				playerInput->inputActionMapName = "Player";
			}

			if (hitscan != nullptr) {
				hitscan->hitscanAimGameObjectId = weaponId;
			}

			if (projectile != nullptr) {
				projectile->projectileAimGameObjectId = weaponId;
				projectile->projectilePoolGameObjectId = poolId;
			}
		}

		const int32_t spawnerId = editorScene->CreateGameObject("Validation Prefab Spawner");
		editorScene->AddComponent(spawnerId, EditorComponentType::PrefabSpawner);
		EditorComponent* prefabSpawner = FindComponent(
			editorScene->FindGameObject(spawnerId),
			EditorComponentType::PrefabSpawner);

		if (prefabSpawner != nullptr) {
			prefabSpawner->prefabSpawnerPoolGameObjectId = poolId;
			prefabSpawner->prefabSpawnerMode = 0;
		}

		//============================================================
		// Generic Sequence / Async Additive Scene / Checkpoint
		//============================================================

		const int32_t sequenceId = editorScene->CreateGameObject("Validation Action Sequence");
		editorScene->AddComponent(sequenceId, EditorComponentType::ActionSequence);
		EditorComponent* sequence = FindComponent(
			editorScene->FindGameObject(sequenceId),
			EditorComponentType::ActionSequence);

		if (sequence != nullptr) {
			sequence->actionSequencePlayOnStart = true;
		}

		const int32_t waitStepId = editorScene->CreateGameObject("Step 0 Wait");
		editorScene->SetParent(waitStepId, sequenceId);
		editorScene->AddComponent(waitStepId, EditorComponentType::ActionSequenceStep);
		EditorComponent* waitStep = FindComponent(
			editorScene->FindGameObject(waitStepId),
			EditorComponentType::ActionSequenceStep);

		if (waitStep != nullptr) {
			waitStep->actionSequenceStepType = 1;
			waitStep->actionSequenceWaitSeconds = 0.5f;
		}

		const int32_t activeStepId = editorScene->CreateGameObject("Step 1 Activate Target");
		editorScene->SetParent(activeStepId, sequenceId);
		editorScene->AddComponent(activeStepId, EditorComponentType::ActionSequenceStep);
		EditorComponent* activeStep = FindComponent(
			editorScene->FindGameObject(activeStepId),
			EditorComponentType::ActionSequenceStep);

		if (activeStep != nullptr) {
			activeStep->actionSequenceStepType = 2;
			activeStep->actionSequenceTargetGameObjectId = damageTargetId;
			activeStep->actionSequenceActiveValue = true;
		}

		const int32_t sceneStepId = editorScene->CreateGameObject("Step 2 Load Additive Scene");
		editorScene->SetParent(sceneStepId, sequenceId);
		editorScene->AddComponent(sceneStepId, EditorComponentType::ActionSequenceStep);
		EditorComponent* sceneStep = FindComponent(
			editorScene->FindGameObject(sceneStepId),
			EditorComponentType::ActionSequenceStep);

		if (sceneStep != nullptr) {
			sceneStep->actionSequenceStepType = 3;
			sceneStep->actionSequenceScenePath = "Assets/Scenes/GameplayFoundationAdditive.scene";
			sceneStep->actionSequenceSceneAdditive = true;
		}

		const int32_t checkpointId = editorScene->CreateGameObject("Validation Checkpoint");
		editorScene->AddComponent(checkpointId, EditorComponentType::Checkpoint);
		EditorComponent* checkpoint = FindComponent(
			editorScene->FindGameObject(checkpointId),
			EditorComponentType::Checkpoint);

		if (checkpoint != nullptr) {
			checkpoint->checkpointSlotName = "gameplay_foundation_validation";
			checkpoint->checkpointSaveOnStart = true;
		}

		g_currentScenePath.clear();
		g_selectedAssetPath.clear();
		SelectFirstGameObjectOrClear();
		RefreshSceneObjects();
		SaveSceneToPath(
			editorScene,
			"Assets/Scenes/GameplayFoundationValidation.scene",
			consoleMessages);
		consoleMessages.push_back(
			"Validation: Prefab / Rail / Wave / Aim / Weapon / Pool / Sequence / Async Scene / Saveを配置");
	}

	void CreateRenderStressScene(
		EditorScene* editorScene,
		EditorRuntimeManager* runtimeManager,
		std::vector<std::string>& consoleMessages) {
		if (editorScene == nullptr || runtimeManager == nullptr) {
			return;
		}

		if (runtimeManager->IsPlaying()) {
			runtimeManager->TogglePlay();
		}

		//============================================================
		// 基本環境
		//============================================================

		editorScene->InitializeDefaultScene();
		for (EditorGameObject& gameObject : editorScene->GetGameObjects()) {
			if (gameObject.name == "Main Camera") {
				gameObject.translate = {0.0f, 18.0f, -42.0f};
				gameObject.rotate = {0.22f, 0.0f, 0.0f};
			}
			else if (gameObject.name == "Point Light") {
				gameObject.translate = {18.0f, 28.0f, -12.0f};
			}
		}

		const int32_t postProcessGameObjectId = editorScene->CreateGameObject("Stress Post Process");
		editorScene->AddComponent(postProcessGameObjectId, EditorComponentType::PostProcess);
		EditorComponent* postProcess = FindComponent(
			editorScene->FindGameObject(postProcessGameObjectId),
			EditorComponentType::PostProcess);

		if (postProcess != nullptr) {
			postProcess->aaMode = 3;
			postProcess->ssrEnabled = true;
			postProcess->bloomIntensity = 0.65f;
		}

		//============================================================
		// Ocean / Terrain / Foliage
		//============================================================

		const int32_t oceanGameObjectId = editorScene->CreateGameObject("Stress Ocean");
		editorScene->AddComponent(oceanGameObjectId, EditorComponentType::Ocean);
		EditorComponent* ocean = FindComponent(
			editorScene->FindGameObject(oceanGameObjectId),
			EditorComponentType::Ocean);

		if (ocean != nullptr) {
			ocean->oceanGridResolution = 2048;
			ocean->oceanSize = 320.0f;
		}

		const int32_t terrainGameObjectId = editorScene->CreateGameObject("Stress Terrain");
		editorScene->AddComponent(terrainGameObjectId, EditorComponentType::Terrain);
		EditorGameObject* terrainGameObject = editorScene->FindGameObject(terrainGameObjectId);
		EditorComponent* terrain = FindComponent(terrainGameObject, EditorComponentType::Terrain);

		if (terrainGameObject != nullptr) {
			terrainGameObject->translate = {0.0f, -6.0f, 70.0f};
		}

		if (terrain != nullptr) {
			terrain->assetPath = "resources/model/huzisann.png";
			terrain->colliderSize = {260.0f, 28.0f, 260.0f};
			terrain->oceanGridResolution = 256;
		}

		EditorGameObject* foliageGameObject = CreateStressModel(
			editorScene,
			"Stress Foliage",
			"resources/editorDefault/cone.fbx",
			{0.0f, -1.0f, 28.0f},
			{0.22f, 1.8f, 0.22f});

		if (foliageGameObject != nullptr) {
			editorScene->AddComponent(foliageGameObject->id, EditorComponentType::Foliage);
			EditorComponent* foliage = FindComponent(foliageGameObject, EditorComponentType::Foliage);
			EditorComponent* foliageRenderer = FindComponent(
				foliageGameObject,
				EditorComponentType::ModelRenderer);

			if (foliage != nullptr) {
				foliage->assetPath = "resources/editorDefault/sibahu.png";
				foliage->colliderSize = {180.0f, 1.0f, 180.0f};
				foliage->intensity = 0.82f;
				foliage->particleMaxCount = 8192;
				foliage->colliderRadius = 170.0f;
			}

			if (foliageRenderer != nullptr) {
				foliageRenderer->textureAssetPath = "resources/editorDefault/sibahu.png";
				foliageRenderer->doubleSided = true;
			}
		}

		//============================================================
		// Opaque / OIT / Refractive
		//============================================================

		for (int32_t modelIndex = 0; modelIndex < 48; ++modelIndex) {
			const int32_t columnIndex = modelIndex % 12;
			const int32_t rowIndex = modelIndex / 12;
			const float x = (static_cast<float>(columnIndex) - 5.5f) * 4.5f;
			const float z = 8.0f + static_cast<float>(rowIndex) * 5.0f;
			EditorGameObject* modelGameObject = CreateStressModel(
				editorScene,
				"Stress Opaque " + std::to_string(modelIndex),
				"resources/editorDefault/ICOCube.fbx",
				{x, 2.0f, z},
				{1.4f, 1.4f, 1.4f});

			if (modelGameObject != nullptr) {
				modelGameObject->rotate.y = static_cast<float>(modelIndex) * 0.21f;
			}
		}

		for (int32_t transparentIndex = 0; transparentIndex < 32; ++transparentIndex) {
			const int32_t columnIndex = transparentIndex % 8;
			const int32_t rowIndex = transparentIndex / 8;
			const float x = (static_cast<float>(columnIndex) - 3.5f) * 5.5f;
			const float z = 34.0f + static_cast<float>(rowIndex) * 4.0f;
			EditorGameObject* transparentGameObject = CreateStressModel(
				editorScene,
				"Stress OIT " + std::to_string(transparentIndex),
				"resources/editorDefault/box.fbx",
				{x, 3.0f, z},
				{1.8f, 3.6f, 1.8f});
			EditorComponent* transparentRenderer = FindComponent(
				transparentGameObject,
				EditorComponentType::ModelRenderer);

			if (transparentRenderer != nullptr) {
				transparentRenderer->alphaMode = 2;
				transparentRenderer->alpha = 0.34f;
				transparentRenderer->color = {0.15f, 0.55f, 0.95f};
				transparentRenderer->doubleSided = true;
			}
		}

		for (int32_t glassIndex = 0; glassIndex < 12; ++glassIndex) {
			const float x = (static_cast<float>(glassIndex) - 5.5f) * 4.8f;
			EditorGameObject* glassGameObject = CreateStressModel(
				editorScene,
				"Stress Refractive " + std::to_string(glassIndex),
				"resources/editorDefault/ICOCube.fbx",
				{x, 5.0f, 54.0f},
				{2.2f, 2.2f, 2.2f});
			EditorComponent* glassRenderer = FindComponent(
				glassGameObject,
				EditorComponentType::ModelRenderer);

			if (glassRenderer != nullptr) {
				glassRenderer->alphaMode = 2;
				glassRenderer->alpha = 0.28f;
				glassRenderer->transmission = 0.92f;
				glassRenderer->ior = 1.52f;
				glassRenderer->roughness = 0.06f;
				glassRenderer->color = {0.72f, 0.92f, 1.0f};
			}
		}

		//============================================================
		// Skinned Motion Vector / Particle Collision
		//============================================================

		for (int32_t skinnedIndex = 0; skinnedIndex < 4; ++skinnedIndex) {
			const int32_t gameObjectId = editorScene->CreateGameObject(
				"Stress Skinned " + std::to_string(skinnedIndex));
			editorScene->AddComponent(gameObjectId, EditorComponentType::MeshFilter);
			editorScene->AddComponent(gameObjectId, EditorComponentType::SkinnedMeshRenderer);
			editorScene->AddComponent(gameObjectId, EditorComponentType::Animation);
			editorScene->AddComponent(gameObjectId, EditorComponentType::Animator);
			EditorGameObject* skinnedGameObject = editorScene->FindGameObject(gameObjectId);

			if (skinnedGameObject != nullptr) {
				skinnedGameObject->translate = {
					(static_cast<float>(skinnedIndex) - 1.5f) * 7.0f,
					2.0f,
					64.0f};
				skinnedGameObject->scale = {1.5f, 1.5f, 1.5f};
				EditorComponent* meshFilter = FindComponent(
					skinnedGameObject,
					EditorComponentType::MeshFilter);
				EditorComponent* skinnedRenderer = FindComponent(
					skinnedGameObject,
					EditorComponentType::SkinnedMeshRenderer);

				if (meshFilter != nullptr) {
					meshFilter->assetPath = "Assets/ai.fbx";
				}

				if (skinnedRenderer != nullptr) {
					skinnedRenderer->assetPath = "Assets/ai.fbx";
					skinnedRenderer->useImportedMaterialTextures = true;
				}
			}
		}

		for (int32_t particleIndex = 0; particleIndex < 2; ++particleIndex) {
			const int32_t gameObjectId = editorScene->CreateGameObject(
				particleIndex == 0 ? "Stress Depth Particles" : "Stress SDF Particles");
			editorScene->AddComponent(gameObjectId, EditorComponentType::ParticleSystem);
			EditorGameObject* particleGameObject = editorScene->FindGameObject(gameObjectId);
			EditorComponent* particleSystem = FindComponent(
				particleGameObject,
				EditorComponentType::ParticleSystem);

			if (particleGameObject != nullptr) {
				particleGameObject->translate = {
					particleIndex == 0 ? -10.0f : 10.0f,
					10.0f,
					20.0f};
			}

			if (particleSystem != nullptr) {
				particleSystem->particleMaxCount = 8192;
				particleSystem->particleRate = 1800.0f;
				particleSystem->particleLifetime = 4.0f;
				particleSystem->particleShape = 2;
				particleSystem->particleShapeRadius = 4.0f;
				particleSystem->particleCollision = true;
				particleSystem->collisionDetectionMode = particleIndex;
				particleSystem->particleNoiseStrength = 2.5f;
				particleSystem->particleEndColor = {0.15f, 0.55f, 1.0f};
			}
		}

		g_currentScenePath.clear();
		g_selectedAssetPath.clear();
		SelectFirstGameObjectOrClear();
		RefreshSceneObjects();
		SaveSceneToPath(
			editorScene,
			"Assets/Scenes/RenderStress.scene",
			consoleMessages);
		consoleMessages.push_back(
			"Profile: Ocean / Terrain / Foliage / OIT / Refraction / Skinning / Particle 負荷Sceneを作成");
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

	void CreateGameplayDataAsset(std::vector<std::string>& consoleMessages) {
		const std::filesystem::path createDirectoryPath("Assets/Data");
		std::filesystem::create_directories(createDirectoryPath);
		std::filesystem::path filePath = createDirectoryPath / "NewGameplayData.gdata";

		for (int32_t fileIndex = 1; std::filesystem::exists(filePath) && fileIndex < 1000; ++fileIndex) {
			filePath = createDirectoryPath / ("NewGameplayData" + std::to_string(fileIndex) + ".gdata");
		}

		const std::string defaultText =
			"# CG2 Gameplay Data\r\n"
			"# Entry|Key|Type(0=String,1=Int,2=Float,3=Bool,4=AssetPath)|Value\r\n"
			"Entry|DisplayName|0|New Data\r\n";

		if (WriteUtf8BomTextFile(filePath.generic_string(), defaultText)) {
			g_selectedAssetPath = filePath.generic_string();
			consoleMessages.push_back("Asset: Gameplay Data を作成 " + filePath.generic_string());
			return;
		}

		consoleMessages.push_back("Asset: Gameplay Data の作成に失敗");
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
	autoSaveElapsedSeconds_ = 0.0f;
	observedScenePath_ = g_currentScenePath;
	lastAutoSaveStatus_ = "待機中";
	LoadAutoSaveSettings();
}

void EditorMainMenuBar::Update() {
}

void EditorMainMenuBar::LoadAutoSaveSettings() {
	isAutoSaveEnabled_ = true;
	autoSaveIntervalSeconds_ = 120.0f;
	std::ifstream settingsFile(kEditorSettingsPath, std::ios::binary);

	if (!settingsFile.is_open()) {
		return;
	}

	std::string line;
	bool isFirstLine = true;

	while (std::getline(settingsFile, line)) {
		if (isFirstLine && line.size() >= sizeof(kUtf8Bom) &&
			static_cast<unsigned char>(line[0]) == kUtf8Bom[0] &&
			static_cast<unsigned char>(line[1]) == kUtf8Bom[1] &&
			static_cast<unsigned char>(line[2]) == kUtf8Bom[2]) {
			line.erase(0u, sizeof(kUtf8Bom));
		}

		isFirstLine = false;
		const size_t delimiterPosition = line.find('|');

		if (delimiterPosition == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0u, delimiterPosition);
		const std::string value = line.substr(delimiterPosition + 1u);

		if (key == "AutoSaveEnabled") {
			isAutoSaveEnabled_ = value == "1";
		}
		else if (key == "AutoSaveIntervalSeconds") {
			std::istringstream valueStream(value);
			float loadedInterval = autoSaveIntervalSeconds_;
			valueStream >> loadedInterval;

			if (!valueStream.fail()) {
				autoSaveIntervalSeconds_ = (std::clamp)(loadedInterval, 30.0f, 1800.0f);
			}
		}
	}
}

void EditorMainMenuBar::SaveAutoSaveSettings() const {
	std::ostringstream settingsText;
	settingsText << "CG2EditorSettings|1\r\n"
	             << "AutoSaveEnabled|" << (isAutoSaveEnabled_ ? 1 : 0) << "\r\n"
	             << "AutoSaveIntervalSeconds|" << autoSaveIntervalSeconds_ << "\r\n";
	WriteUtf8BomTextFile(kEditorSettingsPath, settingsText.str());
}

void EditorMainMenuBar::UpdateAutoSave(std::vector<std::string>& consoleMessages) {
	if (observedScenePath_ != g_currentScenePath) {
		observedScenePath_ = g_currentScenePath;
		autoSaveElapsedSeconds_ = 0.0f;
		lastAutoSaveStatus_ = "Scene切替後の待機中";
	}

	if (!isAutoSaveEnabled_ || runtimeManager_ == nullptr || runtimeManager_->IsPlaying()) {
		return;
	}

	const float frameDeltaTime = (std::max)(ImGui::GetIO().DeltaTime, 0.0f);
	autoSaveElapsedSeconds_ += frameDeltaTime;

	if (autoSaveElapsedSeconds_ < autoSaveIntervalSeconds_) {
		return;
	}

	autoSaveElapsedSeconds_ = 0.0f;
	std::string savedPath;
	const AutoSaveResult saveResult = SaveSceneAutomatically(
		editorScene_,
		g_currentScenePath,
		savedPath);

	if (saveResult == AutoSaveResult::Saved) {
		lastAutoSaveStatus_ = "保存: " + savedPath;
		consoleMessages.push_back("File: 自動保存 " + savedPath);
	}
	else if (saveResult == AutoSaveResult::NoChanges) {
		lastAutoSaveStatus_ = "変更なし";
	}
	else {
		lastAutoSaveStatus_ = savedPath.empty()
			? "保存失敗"
			: "保存失敗（一時ファイル: " + savedPath + "）";
		consoleMessages.push_back("File: 自動保存に失敗 " + savedPath);
	}
}

void EditorMainMenuBar::Draw(
	std::vector<std::string>& consoleMessages,
	bool& isRuntimeInitialized,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& previousSelectedGameObjectId) {
	if (editorScene_ == nullptr || runtimeManager_ == nullptr) {
		return;
	}

	UpdateAutoSave(consoleMessages);

	static bool shouldOpenSceneSaveAsPopup = false;  // 保存先入力モーダルを次フレームで開く要求
	static bool shouldOpenSceneLoadPopup = false;  // 読込候補一覧モーダルを次フレームで開く要求
	static bool shouldOpenNewScenePopup = false;  // 編集中 Scene を新規 Scene へ置き換える確認要求
	static bool shouldOpenRenderStressPopup = false;  // 現在 Scene を負荷検証用 Scene へ置き換える確認要求
	static bool shouldOpenGameplayValidationPopup = false;  // 汎用ゲーム基盤の検証Scene作成確認
	static bool shouldOpenGameBuildPopup = false;  // ゲーム書き出し設定を次フレームで開く要求
	static char sceneSavePathBuffer[260] = {};  // 名前を付けて保存の入力欄
	static char sceneLoadPathBuffer[260] = {};  // 読込候補一覧での直接入力欄
	static char productNameBuffer[128] = "CG2Game";  // 書き出す exe の名前
	static char outputDirectoryBuffer[260] = "Builds/CG2Game";  // Player の出力先
	static EditorGameBuildSettings gameBuildSettings{};  // Build Settings モーダルの編集状態

	// MainMenuBar が開けないフレームはメニュー描画を行わない
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (ImGui::BeginMenu("ファイル")) {
		if (ImGui::MenuItem("新規 Scene", "Ctrl+N")) {
			shouldOpenNewScenePopup = true;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("保存", "Ctrl+S")) {
			if (runtimeManager_->IsPlaying()) {
				consoleMessages.push_back("File: Play中はシーンを保存できません（Stopしてから保存してください）");
			}
			else if (g_currentScenePath.empty()) {
				const std::string defaultScenePath = BuildDefaultScenePath();
				strncpy_s(sceneSavePathBuffer, sizeof(sceneSavePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
				shouldOpenSceneSaveAsPopup = true;
			}
			else {
				if (SaveSceneToPath(editorScene_, g_currentScenePath, consoleMessages)) {
					autoSaveElapsedSeconds_ = 0.0f;
					lastAutoSaveStatus_ = "手動保存済み";
				}
			}
		}

		if (ImGui::MenuItem("名前を付けて保存")) {
			const std::string defaultScenePath =
				g_currentScenePath.empty() ? BuildDefaultScenePath() : g_currentScenePath;
			strncpy_s(sceneSavePathBuffer, sizeof(sceneSavePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
			shouldOpenSceneSaveAsPopup = true;
		}

		ImGui::Separator();

		bool autoSaveEnabled = isAutoSaveEnabled_;

		if (ImGui::MenuItem("自動保存", nullptr, &autoSaveEnabled)) {
			isAutoSaveEnabled_ = autoSaveEnabled;
			autoSaveElapsedSeconds_ = 0.0f;
			lastAutoSaveStatus_ = isAutoSaveEnabled_ ? "有効" : "無効";
			SaveAutoSaveSettings();
		}

		if (ImGui::BeginMenu("自動保存間隔", isAutoSaveEnabled_)) {
			struct AutoSaveIntervalOption {
				const char* label;
				float seconds;
			};

			constexpr AutoSaveIntervalOption intervalOptions[] = {
				{"30秒", 30.0f},
				{"1分", 60.0f},
				{"2分", 120.0f},
				{"5分", 300.0f},
				{"10分", 600.0f},
			};

			for (const AutoSaveIntervalOption& option : intervalOptions) {
				const bool isSelected = autoSaveIntervalSeconds_ == option.seconds;

				if (ImGui::MenuItem(option.label, nullptr, isSelected)) {
					autoSaveIntervalSeconds_ = option.seconds;
					autoSaveElapsedSeconds_ = 0.0f;
					SaveAutoSaveSettings();
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("今すぐ自動保存", nullptr, false, isAutoSaveEnabled_)) {
			autoSaveElapsedSeconds_ = autoSaveIntervalSeconds_;
			UpdateAutoSave(consoleMessages);
		}

		if (isAutoSaveEnabled_) {
			const float remainingSeconds = (std::max)(
				autoSaveIntervalSeconds_ - autoSaveElapsedSeconds_,
				0.0f);
			ImGui::TextDisabled("次回まで: %.0f秒", remainingSeconds);
			ImGui::TextDisabled("状態: %s", lastAutoSaveStatus_.c_str());
		}

		if (ImGui::MenuItem("読み込み")) {
			sceneLoadPathBuffer[0] = '\0';
			shouldOpenSceneLoadPopup = true;
		}

		if (ImGui::MenuItem("ゲームをビルド...")) {
			if (!EditorGameBuildManager::LoadProjectSettings(gameBuildSettings)) {
				gameBuildSettings = EditorGameBuildSettings{};
			}

			const std::vector<std::string> availableScenePaths = CollectSceneAssetPaths();
			if (gameBuildSettings.scenePaths.empty()) {
				gameBuildSettings.scenePaths = availableScenePaths;
			}

			AddUniqueScenePath(gameBuildSettings.scenePaths, g_currentScenePath);

			if (!ContainsScenePath(
					gameBuildSettings.scenePaths,
					gameBuildSettings.startupScenePath)) {
				gameBuildSettings.startupScenePath = gameBuildSettings.scenePaths.empty()
					? std::string{}
					: gameBuildSettings.scenePaths[0];
			}

			strncpy_s(
				productNameBuffer,
				sizeof(productNameBuffer),
				gameBuildSettings.productName.c_str(),
				_TRUNCATE);
			strncpy_s(
				outputDirectoryBuffer,
				sizeof(outputDirectoryBuffer),
				gameBuildSettings.outputDirectory.c_str(),
				_TRUNCATE);
			shouldOpenGameBuildPopup = true;
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

		if (ImGui::MenuItem("Gameplay Data 作成")) {
			CreateGameplayDataAsset(consoleMessages);
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
				CreatePrimitiveGameObject("resources/editorDefault/UVCube.fbx");
			}
			if (ImGui::MenuItem("Box")) {
				CreatePrimitiveGameObject("resources/editorDefault/box.fbx");
			}
			if (ImGui::MenuItem("Cylinder")) {
				CreatePrimitiveGameObject("resources/cylinder.fbx");
			}
			if (ImGui::MenuItem("Cone")) {
				CreatePrimitiveGameObject("resources/editorDefault/cone.fbx");
			}
			if (ImGui::MenuItem("Torus")) {
				CreatePrimitiveGameObject("resources/Torus.fbx");
			}
			if (ImGui::MenuItem("Ico")) {
				CreatePrimitiveGameObject("resources/editorDefault/ICOCube.fbx");
			}
			if (ImGui::MenuItem("Sphere")) {
				CreatePrimitiveGameObject("resources/sphere.fbx");
			}
			if (ImGui::MenuItem("Ocean")) {
				CreateOceanGameObject(consoleMessages);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("UI")) {
			if (ImGui::MenuItem("Canvas")) {
				const int32_t canvasGameObjectId = CreateCanvasGameObject(true);
				SelectGameObject(canvasGameObjectId);
				consoleMessages.push_back("Scene UI: Canvas を作成");
			}

			if (ImGui::MenuItem("Text - TextMeshPro")) {
				CreateUiElementGameObject("Text", EditorComponentType::TextMeshProUGUI, consoleMessages);
			}

			if (ImGui::MenuItem("Image")) {
				CreateUiElementGameObject("Image", EditorComponentType::Image, consoleMessages);
			}

			if (ImGui::MenuItem("Button")) {
				CreateUiElementGameObject("Button", EditorComponentType::Button, consoleMessages);
			}

			if (ImGui::MenuItem("Slider")) {
				CreateUiElementGameObject("Slider", EditorComponentType::Slider, consoleMessages);
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

		if (ImGui::MenuItem("Auto Convex Collision", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::AutoConvexCollision, "Auto Convex Collision", consoleMessages);
		}

		if (ImGui::MenuItem("Ocean", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::Ocean, "Ocean", consoleMessages);
		}

		if (ImGui::MenuItem("Buoyancy", nullptr, false, canAddComponent)) {
			AddComponentToSelectedGameObject(EditorComponentType::Buoyancy, "Buoyancy", consoleMessages);
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
		ImGui::MenuItem("Spline Editor", nullptr, &g_isSplineEditorVisible);
		ImGui::MenuItem("Event Timeline", nullptr, &g_isGameplayTimelineWindowVisible);
		ImGui::MenuItem("State Graph", nullptr, &g_isStateGraphWindowVisible);
		ImGui::MenuItem("診断・Profiler", nullptr, &g_isDiagnosticsWindowVisible);

		if (ImGui::MenuItem("描画負荷テスト Scene を作成")) {
			shouldOpenRenderStressPopup = true;
		}

		if (ImGui::MenuItem("ゲーム基盤検証 Scene を作成")) {
			shouldOpenGameplayValidationPopup = true;
		}

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

	if (shouldOpenNewScenePopup) {
		ImGui::OpenPopup("NewScenePopup");
		shouldOpenNewScenePopup = false;
	}

	if (ImGui::BeginPopupModal("NewScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("新しい Scene を開きます。");
		ImGui::TextDisabled("現在の未保存変更は破棄されます。");

		if (ImGui::Button("新規 Scene を開く", ImVec2(180.0f, 0.0f))) {
			CreateNewEditingScene(editorScene_, runtimeManager_, consoleMessages);
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (shouldOpenRenderStressPopup) {
		ImGui::OpenPopup("RenderStressScenePopup");
		shouldOpenRenderStressPopup = false;
	}

	if (ImGui::BeginPopupModal(
			"RenderStressScenePopup",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("描画負荷テスト Scene を作成します。");
		ImGui::TextDisabled("現在の未保存変更は破棄され、Assets/Scenes/RenderStress.scene へ保存されます。");

		if (ImGui::Button("作成する", ImVec2(160.0f, 0.0f))) {
			CreateRenderStressScene(editorScene_, runtimeManager_, consoleMessages);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (shouldOpenGameplayValidationPopup) {
		ImGui::OpenPopup("GameplayFoundationValidationPopup");
		shouldOpenGameplayValidationPopup = false;
	}

	if (ImGui::BeginPopupModal(
			"GameplayFoundationValidationPopup",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("汎用ゲーム基盤の検証 Scene を作成します。");
		ImGui::TextDisabled("現在の未保存変更は破棄され、Assets/Scenes/GameplayFoundationValidation.scene へ保存されます。");

		if (ImGui::Button("作成する", ImVec2(160.0f, 0.0f))) {
			CreateGameplayFoundationValidationScene(editorScene_, runtimeManager_, consoleMessages);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
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
			if (runtimeManager_->IsPlaying()) {
				consoleMessages.push_back("File: Play中はシーンを保存できません（Stopしてから保存してください）");
			}
			else if (SaveSceneToPath(editorScene_, sceneSavePathBuffer, consoleMessages)) {
				autoSaveElapsedSeconds_ = 0.0f;
				observedScenePath_ = g_currentScenePath;
				lastAutoSaveStatus_ = "手動保存済み";
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

	if (shouldOpenGameBuildPopup) {
		ImGui::OpenPopup("GameBuildSettingsPopup");
		shouldOpenGameBuildPopup = false;
	}

	if (ImGui::BeginPopupModal(
			"GameBuildSettingsPopup",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("ゲーム書き出し設定");
		ImGui::InputText("ゲーム名", productNameBuffer, sizeof(productNameBuffer));
		ImGui::InputText("出力先", outputDirectoryBuffer, sizeof(outputDirectoryBuffer));
		ImGui::TextDisabled("Release ビルド済みの実行ファイルと最新 Assets を出力します");
		ImGui::Checkbox("参照されるAssetだけを出力", &gameBuildSettings.includeOnlyReferencedAssets);
		ImGui::TextDisabled("共通Shader、ThirdParty、既定Fallbackは常に含まれます");
		ImGui::Separator();
		ImGui::Text("ビルド対象シーン");

		const std::vector<std::string> availableScenePaths = CollectSceneAssetPaths();
		ImGui::BeginChild("GameBuildSceneList", ImVec2(620.0f, 260.0f), true);

		for (const std::string& scenePath : availableScenePaths) {
			bool isIncluded = ContainsScenePath(gameBuildSettings.scenePaths, scenePath);
			const std::string checkboxLabel = scenePath + "##BuildScene";

			if (ImGui::Checkbox(checkboxLabel.c_str(), &isIncluded)) {
				if (isIncluded) {
					AddUniqueScenePath(gameBuildSettings.scenePaths, scenePath);
				}
				else {
					gameBuildSettings.scenePaths.erase(
						std::remove(
							gameBuildSettings.scenePaths.begin(),
							gameBuildSettings.scenePaths.end(),
							scenePath),
						gameBuildSettings.scenePaths.end());

					if (gameBuildSettings.startupScenePath == scenePath) {
						gameBuildSettings.startupScenePath.clear();
					}
				}
			}

			if (isIncluded) {
				ImGui::SameLine(480.0f);
				const bool isStartupScene =
					gameBuildSettings.startupScenePath == scenePath;
				const std::string startupLabel = "起動##" + scenePath;

				if (ImGui::RadioButton(startupLabel.c_str(), isStartupScene)) {
					gameBuildSettings.startupScenePath = scenePath;
				}
			}
		}

		if (availableScenePaths.empty()) {
			ImGui::TextDisabled("Assets または resources に .scene がありません");
		}

		ImGui::EndChild();
		ImGui::TextDisabled(
			"C++ 遷移: Input::GetKeyDown(KeyCode::Space) / SceneManager::LoadScene(\"Assets/Scenes/Stage.scene\")");

		if (ImGui::Button("ゲームを書き出す", ImVec2(180.0f, 0.0f))) {
			if (!g_currentScenePath.empty() && !runtimeManager_->IsPlaying()) {
				SaveSceneToPath(editorScene_, g_currentScenePath, consoleMessages);
			}
			else if (runtimeManager_->IsPlaying()) {
				consoleMessages.push_back("File: Play中はシーンを保存できません（Stopしてから書き出してください）");
			}

			gameBuildSettings.productName = productNameBuffer;
			gameBuildSettings.outputDirectory = outputDirectoryBuffer;

			if (gameBuildSettings.startupScenePath.empty() &&
				!gameBuildSettings.scenePaths.empty()) {
				gameBuildSettings.startupScenePath = gameBuildSettings.scenePaths[0];
			}

			std::string buildMessage;
			const bool wasSettingsSaved =
				EditorGameBuildManager::SaveProjectSettings(gameBuildSettings);

			if (wasSettingsSaved) {
				g_gameBuildScenePaths = gameBuildSettings.scenePaths;
			}

			const bool wasGameExported = wasSettingsSaved &&
				EditorGameBuildManager::ExportReleaseGame(
					gameBuildSettings,
					buildMessage);

			if (!wasSettingsSaved) {
				buildMessage = "Build: ProjectSettings を保存できません";
			}

			consoleMessages.push_back(buildMessage);

			if (wasGameExported) {
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
