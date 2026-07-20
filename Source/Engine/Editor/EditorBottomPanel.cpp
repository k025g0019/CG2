#pragma warning(disable : 5045)

#include "EditorBottomPanel.h"

#include "EditorAssetUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace EditorSharedState;

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // 作成アセットは UTF-8 BOM 付きで保存する。

	void SyncSelectionToScene() {
		// 現在の GameObject 選択を SceneView の旧選択番号へ同期する。
		g_editorSelectionManager.SyncLegacySelection(
			g_selectedEditorGameObjectId,
			g_selectedSceneObject,
			g_selectedPlacedSceneObjectIndex);
	}

	void RefreshSceneObjects() {
		// Scene 読み込みや削除の直後に描画用 SceneObject を作り直す。
		g_editorSceneSynchronizer.Update(g_editorTextureFilePaths, g_selectedPlacedSceneObjectIndex);
		SyncSelectionToScene();
	}

	void SelectGameObject(int32_t gameObjectId) {
		// 選択切り替え時は Inspector と SceneView の両方が同じ GameObject を指すようにする。
		SetSingleSelectedGameObject(gameObjectId);
		g_selectedPlacedSceneObjectIndex = -1;
		SyncSelectionToScene();
	}

	void SelectFirstGameObjectOrClear() {
		// Scene が空なら選択を解除し、残っていれば先頭 GameObject を選択する。
		if (g_editorScene.GetGameObjects().empty()) {
			ClearSelectedGameObjects();
			return;
		}

		SelectGameObject(g_editorScene.GetGameObjects()[0].id);
	}

	bool IsProjectAssetFile(const std::string& assetPath) {
		// Project に並べる対象は、Scene 配置や Inspector 確認に使う素材だけに絞る。
		return EditorAssetUtility::HasExtension(assetPath, ".png") ||
			EditorAssetUtility::HasExtension(assetPath, ".jpg") ||
			EditorAssetUtility::HasExtension(assetPath, ".jpeg") ||
			EditorAssetUtility::HasExtension(assetPath, ".obj") ||
			EditorAssetUtility::HasExtension(assetPath, ".mtl") ||
			EditorAssetUtility::HasExtension(assetPath, ".fbx") ||
			EditorAssetUtility::HasExtension(assetPath, ".wav") ||
			EditorAssetUtility::HasExtension(assetPath, ".dll") ||
			EditorAssetUtility::HasExtension(assetPath, ".cpp") ||
			EditorAssetUtility::HasExtension(assetPath, ".h") ||
			EditorAssetUtility::HasExtension(assetPath, ".bat") ||
			EditorAssetUtility::HasExtension(assetPath, ".py") ||
			EditorAssetUtility::HasExtension(assetPath, ".onnx") ||
			EditorAssetUtility::HasExtension(assetPath, ".xml") ||
			EditorAssetUtility::HasExtension(assetPath, ".yaml") ||
			EditorAssetUtility::HasExtension(assetPath, ".yml") ||
			EditorAssetUtility::HasExtension(assetPath, ".json") ||
			EditorAssetUtility::HasExtension(assetPath, ".scene") ||
			EditorAssetUtility::HasExtension(assetPath, ".prefab") ||
			EditorAssetUtility::HasExtension(assetPath, ".inputactions") ||
			EditorAssetUtility::HasExtension(assetPath, ".animgraph") ||
			EditorAssetUtility::HasExtension(assetPath, ".animclip") ||
			EditorAssetUtility::HasExtension(assetPath, ".effect");
	}

	std::string MakeDefaultPlayerInputActionsText() {
		return
			"# CG2 PlayerInput Actions\r\n"
			"# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n"
			"Action|Player|Move|Vector2|2DVector|W|S|A|D\r\n"
			"Action|Player|Jump|Button|Key|Space\r\n"
			"Action|Player|Fire|Button|Mouse|LeftButton\r\n";
	}

	std::string MakeDefaultAnimationGraphText() {
		// FBX の Clip 番号はモデルごとに異なるため、作成直後は Clip 0 だけで安全に再生できる構成にする。
		return
			"{\r\n"
			"  \"entryState\": 0,\r\n"
			"  \"parameters\": [\r\n"
			"    { \"name\": \"MoveX\", \"type\": \"Float\", \"float\": 0.0 },\r\n"
			"    { \"name\": \"MoveY\", \"type\": \"Float\", \"float\": 0.0 },\r\n"
			"    { \"name\": \"Speed\", \"type\": \"Float\", \"float\": 0.0 }\r\n"
			"  ],\r\n"
			"  \"states\": [\r\n"
			"    {\r\n"
			"      \"name\": \"Locomotion\",\r\n"
			"      \"blendType\": \"Blend2DDirectional\",\r\n"
			"      \"parameterX\": \"MoveX\",\r\n"
			"      \"parameterY\": \"MoveY\",\r\n"
			"      \"loop\": true,\r\n"
			"      \"samples\": [\r\n"
			"        { \"clip\": 0, \"x\": 0.0, \"y\": 0.0, \"speed\": 1.0 },\r\n"
			"        { \"clip\": 0, \"x\": 0.0, \"y\": 1.0, \"speed\": 1.0 },\r\n"
			"        { \"clip\": 0, \"x\": 0.0, \"y\": -1.0, \"speed\": 1.0 },\r\n"
			"        { \"clip\": 0, \"x\": -1.0, \"y\": 0.0, \"speed\": 1.0 },\r\n"
			"        { \"clip\": 0, \"x\": 1.0, \"y\": 0.0, \"speed\": 1.0 }\r\n"
			"      ]\r\n"
			"    }\r\n"
			"  ],\r\n"
			"  \"transitions\": [],\r\n"
			"  \"events\": []\r\n"
			"}\r\n";
	}

	std::string MakeDefaultEffectAssetText() {
		return
			"{\r\n"
			"  \"renderAsset\": \"resources/en.fbx\",\r\n"
			"  \"playOnAwake\": true,\r\n"
			"  \"looping\": true,\r\n"
			"  \"duration\": 2.0,\r\n"
			"  \"startDelay\": 0.0,\r\n"
			"  \"emissionRate\": 10.0,\r\n"
			"  \"burstCount\": 0,\r\n"
			"  \"maxCount\": 256,\r\n"
			"  \"lifetime\": 1.0,\r\n"
			"  \"speed\": 1.0,\r\n"
			"  \"startSize\": 0.2,\r\n"
			"  \"endSize\": 0.0,\r\n"
			"  \"shape\": 1,\r\n"
			"  \"shapeRadius\": 0.5,\r\n"
			"  \"shapeAngle\": 25.0,\r\n"
			"  \"simulationSpace\": 0,\r\n"
			"  \"gravity\": 0.0,\r\n"
			"  \"drag\": 0.0,\r\n"
			"  \"rotationSpeed\": 0.0,\r\n"
			"  \"speedRandomness\": 0.15,\r\n"
			"  \"lifetimeRandomness\": 0.1,\r\n"
			"  \"sizeRandomness\": 0.1,\r\n"
			"  \"startAlpha\": 1.0,\r\n"
			"  \"endAlpha\": 0.0,\r\n"
			"  \"emissionStrength\": 1.0,\r\n"
			"  \"endSpeedMultiplier\": 0.35,\r\n"
			"  \"noiseStrength\": 0.0,\r\n"
			"  \"noiseFrequency\": 1.0,\r\n"
			"  \"collision\": false,\r\n"
			"  \"collisionBounce\": 0.35,\r\n"
			"  \"collisionFriction\": 0.2,\r\n"
			"  \"prewarm\": false,\r\n"
			"  \"startColor\": { \"x\": 1.0, \"y\": 0.8, \"z\": 0.2 },\r\n"
			"  \"endColor\": { \"x\": 1.0, \"y\": 0.1, \"z\": 0.0 },\r\n"
			"  \"direction\": { \"x\": 0.0, \"y\": 1.0, \"z\": 0.0 },\r\n"
			"  \"boxSize\": { \"x\": 1.0, \"y\": 1.0, \"z\": 1.0 }\r\n"
			"}\r\n";
	}

	std::string MakeDefaultPropertyAnimationClipText() {
		// Additive の位置カーブなので、作成直後にどの GameObject へ付けても元配置を壊さず上下動を確認できる。
		return
			"{\r\n"
			"  \"name\": \"NewAnimationClip\",\r\n"
			"  \"duration\": 2.0,\r\n"
			"  \"sampleRate\": 30.0,\r\n"
			"  \"loop\": true,\r\n"
			"  \"tracks\": [\r\n"
			"    {\r\n"
			"      \"property\": \"Transform.LocalPositionY\",\r\n"
			"      \"writeMode\": \"Additive\",\r\n"
			"      \"keys\": [\r\n"
			"        { \"time\": 0.0, \"value\": 0.0, \"outTangent\": 2.0, \"interpolation\": \"CubicHermite\" },\r\n"
			"        { \"time\": 1.0, \"value\": 1.0, \"inTangent\": 0.0, \"outTangent\": 0.0, \"interpolation\": \"CubicHermite\" },\r\n"
			"        { \"time\": 2.0, \"value\": 0.0, \"inTangent\": -2.0, \"interpolation\": \"CubicHermite\" }\r\n"
			"      ]\r\n"
			"    }\r\n"
			"  ],\r\n"
			"  \"events\": []\r\n"
			"}\r\n";
	}

	std::string GetProjectAssetCreateDirectory(const std::string& selectedAssetPath) {
		// Project で Assets / resources 配下のファイルやフォルダを選択している時は、その場所へ新規アセットを作る。
		if (!selectedAssetPath.empty() &&
			(selectedAssetPath.rfind("Assets/", 0) == 0 ||
			 selectedAssetPath == "Assets" ||
			 selectedAssetPath.rfind("resources/", 0) == 0 ||
			 selectedAssetPath == "resources")) {
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

	std::string MakeUniqueTextAssetPath(
		const std::string& directoryPath,
		const std::string& baseName,
		const std::string& extension) {
		const std::filesystem::path baseDirectoryPath(directoryPath);

		for (int32_t fileIndex = 0; fileIndex < 1000; ++fileIndex) {
			std::string candidateName = baseName;

			if (fileIndex > 0) {
				candidateName += std::to_string(fileIndex);
			}

			const std::filesystem::path candidatePath = baseDirectoryPath / (candidateName + extension);

			if (!std::filesystem::exists(candidatePath)) {
				return candidatePath.generic_string();
			}
		}

		return (baseDirectoryPath / (baseName + extension)).generic_string();
	}

	std::string MakeUniqueFolderPath(const std::string& directoryPath, const std::string& baseFolderName) {
		const std::filesystem::path baseDirectoryPath(directoryPath);
		for (int32_t folderIndex = 0; folderIndex < 1000; ++folderIndex) {
			std::string candidateFolderName = baseFolderName;
			if (folderIndex > 0) {
				candidateFolderName += std::to_string(folderIndex);
			}

			const std::filesystem::path candidatePath = baseDirectoryPath / candidateFolderName;
			if (!std::filesystem::exists(candidatePath)) {
				return candidatePath.generic_string();
			}
		}

		return (baseDirectoryPath / baseFolderName).generic_string();
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

	bool LoadSceneFromAssetPath(const std::string& assetPath, std::vector<std::string>& consoleMessages) {
		// .scene のダブルクリックは外部アプリではなく、エディタ内の現在シーン読み込みとして扱う。
		if (!g_editorScene.LoadScene(assetPath)) {
			consoleMessages.push_back("Asset: シーン読込に失敗 " + assetPath);
			return false;
		}

		g_currentScenePath = assetPath;
		g_selectedAssetPath = assetPath;
		SelectFirstGameObjectOrClear();
		RefreshSceneObjects();
		consoleMessages.push_back("Asset: シーン読込 " + assetPath);
		return true;
	}

	void OpenAssetWithShell(const std::string& assetPath, std::vector<std::string>& consoleMessages) {
		// .scene はエディタ内で開き、それ以外は OS 既定アプリへ引き渡す。
		if (EditorAssetUtility::HasExtension(assetPath, ".scene")) {
			LoadSceneFromAssetPath(assetPath, consoleMessages);
			return;
		}

		if (!std::filesystem::exists(assetPath)) {
			consoleMessages.push_back("Asset: 開けません " + assetPath);
			return;
		}

		const std::string command = "start \"\" \"" + assetPath + "\"";
		const int result = std::system(command.c_str());
		if (result != 0) {
			consoleMessages.push_back("Asset: 外部アプリ起動に失敗 " + assetPath);
			return;
		}

		consoleMessages.push_back("Asset: 開く " + assetPath);
	}

	void DeleteSelectedAsset(std::string& selectedAssetPath, std::vector<std::string>& consoleMessages) {
		// Project の選択ファイルを Delete で消し、削除後は Inspector 選択も空に戻す。
		if (selectedAssetPath.empty()) {
			return;
		}

		const std::string deletingAssetPath = selectedAssetPath;
		if (!std::filesystem::exists(deletingAssetPath)) {
			consoleMessages.push_back("Asset: 削除対象が見つかりません " + deletingAssetPath);
			selectedAssetPath.clear();
			if (g_selectedAssetPath == deletingAssetPath) {
				g_selectedAssetPath.clear();
			}
			return;
		}

		std::error_code deleteError;
		const bool isRemoved = std::filesystem::remove(deletingAssetPath, deleteError);
		if (!isRemoved || deleteError) {
			consoleMessages.push_back("Asset: 削除に失敗 " + deletingAssetPath);
			return;
		}

		if (g_currentScenePath == deletingAssetPath) {
			// 開いている .scene 自体を消した時は、未保存シーン状態へ戻してパスだけ切る。
			g_currentScenePath.clear();
		}

		consoleMessages.push_back("Asset: 削除 " + deletingAssetPath);
		selectedAssetPath.clear();
		if (g_selectedAssetPath == deletingAssetPath) {
			g_selectedAssetPath.clear();
		}
	}

	std::vector<std::string> CollectProjectAssetPaths() {
		std::vector<std::string> assetPaths;  // assetPaths は Project グリッドに表示する Assets / resources 内ファイル。
		const std::vector<std::filesystem::path> rootPaths = {
			std::filesystem::path("Assets"),
			std::filesystem::path("resources"),
		};
		std::error_code fileError;

		for (const std::filesystem::path& rootPath : rootPaths) {
			fileError.clear();

			// ルートフォルダがない状態でもエディタを落とさないため、存在確認してから走査する。
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

				std::string assetPath = entry.path().generic_string();
				if (IsProjectAssetFile(assetPath) &&
					!EditorAssetUtility::IsBuiltInPrimitiveAssetPath(assetPath)) {
					assetPaths.push_back(assetPath);
				}
			}
		}

		std::sort(assetPaths.begin(), assetPaths.end());
		assetPaths.erase(std::unique(assetPaths.begin(), assetPaths.end()), assetPaths.end());
		return assetPaths;
	}

	bool IsAssetInSelectedProjectFolder(const std::string& assetPath, const std::string& selectedAssetPath) {
		// 左ツリーでフォルダーを選んでいる時だけ、その配下アセットに Project グリッドを絞る。
		if (selectedAssetPath.empty()) {
			return true;
		}

		std::error_code fileError;
		const std::filesystem::path selectedPath(selectedAssetPath);
		if (!std::filesystem::exists(selectedPath, fileError) ||
			!std::filesystem::is_directory(selectedPath, fileError)) {
			return true;
		}

		const std::string folderPrefix = selectedPath.generic_string() + "/";
		return assetPath == selectedPath.generic_string() ||
			assetPath.rfind(folderPrefix, 0) == 0;
	}

	void DrawProjectFolderNode(const std::filesystem::path& folderPath, std::string& selectedAssetPath) {
		std::error_code fileError;
		if (!std::filesystem::exists(folderPath, fileError) ||
			!std::filesystem::is_directory(folderPath, fileError)) {
			return;
		}

		const std::string folderPathText = folderPath.generic_string();
		const bool isSelected = selectedAssetPath == folderPathText;
		ImGuiTreeNodeFlags treeNodeFlags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick |
			(isSelected ? ImGuiTreeNodeFlags_Selected : 0);

		bool hasChildDirectory = false;
		for (const std::filesystem::directory_entry& childEntry :
		     std::filesystem::directory_iterator(
			     folderPath,
			     std::filesystem::directory_options::skip_permission_denied,
			     fileError)) {
			if (fileError) {
				break;
			}

			if (childEntry.is_directory(fileError)) {
				hasChildDirectory = true;
				break;
			}
		}

		if (!hasChildDirectory) {
			treeNodeFlags |= ImGuiTreeNodeFlags_Leaf;
		}

		const bool isOpened = ImGui::TreeNodeEx(folderPathText.c_str(), treeNodeFlags, "%s", folderPath.filename().generic_string().c_str());
		if (ImGui::IsItemClicked()) {
			selectedAssetPath = folderPathText;
			g_selectedAssetPath = folderPathText;
		}

		if (isOpened) {
			if (hasChildDirectory) {
				std::vector<std::filesystem::path> childDirectories;
				for (const std::filesystem::directory_entry& childEntry :
				     std::filesystem::directory_iterator(
					     folderPath,
					     std::filesystem::directory_options::skip_permission_denied,
					     fileError)) {
					if (fileError) {
						break;
					}

					if (childEntry.is_directory(fileError)) {
						childDirectories.push_back(childEntry.path());
					}
				}

				std::sort(childDirectories.begin(), childDirectories.end());
				for (const std::filesystem::path& childDirectoryPath : childDirectories) {
					DrawProjectFolderNode(childDirectoryPath, selectedAssetPath);
				}
			}

			ImGui::TreePop();
		}
	}
}

void EditorBottomPanel::Initialize() {
}

void EditorBottomPanel::Update() {
}

void EditorBottomPanel::Draw(
	float editorWindowHeight,
	float editorBottomHeight,
	float bottomPanelWidth,
	ImGuiWindowFlags dockableWindowFlags,
	char* assetFilter,
	size_t assetFilterSize,
	std::string& selectedAssetPath,
	const std::vector<std::string>& textureFilePaths,
	const D3D12_GPU_DESCRIPTOR_HANDLE* textureSrvHandlesGPU,
	size_t textureCount,
	bool& isConsoleCleared,
	std::vector<std::string>& consoleMessages,
	float editorSceneWidth,
	float editorSceneHeight,
	bool isPlaying) {
	float bottomY = editorWindowHeight - editorBottomHeight;  // 下部パネルの上端 Y。画面下から editorBottomHeight 分だけ上げる
	ImGui::SetNextWindowPos(ImVec2(0.0f, bottomY), ImGuiCond_FirstUseEver);  // Docking 初回配置前のデフォルト位置とサイズ
	ImGui::SetNextWindowSize(ImVec2(bottomPanelWidth, editorBottomHeight), ImGuiCond_FirstUseEver);
	ImGui::Begin("下部パネル###BottomPanel", nullptr, dockableWindowFlags);
	if (ImGui::BeginTabBar("BottomPanelTabs", ImGuiTabBarFlags_Reorderable)) {
		if (ImGui::BeginTabItem("Project")) {
			const bool isProjectWindowFocused =
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);  // Project タブ全体がアクティブな時だけ Delete を受け付ける。
			static char newFolderName[128] = "NewFolder";  // Project の新規フォルダー名入力欄。
			bool isOpenFolderPopupRequested = false;  // メニューを閉じた後にフォルダー作成モーダルを開く要求。

			ImGui::InputText("検索", assetFilter, assetFilterSize);  // Project 内アセット名検索
			ImGui::SameLine();
			if (ImGui::Button("+")) {
				ImGui::OpenPopup("ProjectCreateAssetPopup");
			}
			if (ImGui::BeginPopup("ProjectCreateAssetPopup")) {
				if (ImGui::MenuItem("フォルダー")) {
					std::memset(newFolderName, 0, sizeof(newFolderName));
					const char* defaultFolderName = "NewFolder";
					std::memcpy(newFolderName, defaultFolderName, std::strlen(defaultFolderName));
					isOpenFolderPopupRequested = true;
				}

				if (ImGui::MenuItem("Input Actions")) {
					const std::string createDirectoryPath = GetProjectAssetCreateDirectory(selectedAssetPath);
					std::filesystem::create_directories(createDirectoryPath);
					const std::string filePath = MakeUniqueInputActionsAssetPath(createDirectoryPath);

					if (WriteUtf8BomTextFile(filePath, MakeDefaultPlayerInputActionsText())) {
						selectedAssetPath = filePath;
						consoleMessages.push_back("Asset: Created " + filePath);
					}
					else {
						consoleMessages.push_back("Asset: Failed to create Input Actions");
					}
				}

				if (ImGui::MenuItem("Animation Graph")) {
					const std::string createDirectoryPath = GetProjectAssetCreateDirectory(selectedAssetPath);
					std::filesystem::create_directories(createDirectoryPath);
					const std::string filePath = MakeUniqueTextAssetPath(
						createDirectoryPath,
						"NewAnimationGraph",
						".animgraph");

					if (WriteUtf8BomTextFile(filePath, MakeDefaultAnimationGraphText())) {
						selectedAssetPath = filePath;
						g_selectedAssetPath = filePath;
						consoleMessages.push_back("Asset: Animation Graph を作成 " + filePath);
					}
					else {
						consoleMessages.push_back("Asset: Animation Graph の作成に失敗");
					}
				}

				if (ImGui::MenuItem("Animation Clip")) {
					const std::string createDirectoryPath = GetProjectAssetCreateDirectory(selectedAssetPath);
					std::filesystem::create_directories(createDirectoryPath);
					const std::string filePath = MakeUniqueTextAssetPath(
						createDirectoryPath,
						"NewAnimationClip",
						".animclip");

					if (WriteUtf8BomTextFile(filePath, MakeDefaultPropertyAnimationClipText())) {
						selectedAssetPath = filePath;
						g_selectedAssetPath = filePath;
						consoleMessages.push_back("Asset: Animation Clip を作成 " + filePath);
					}
					else {
						consoleMessages.push_back("Asset: Animation Clip の作成に失敗");
					}
				}

				if (ImGui::MenuItem("Effect Asset")) {
					const std::string createDirectoryPath = GetProjectAssetCreateDirectory(selectedAssetPath);
					std::filesystem::create_directories(createDirectoryPath);
					const std::string filePath = MakeUniqueTextAssetPath(
						createDirectoryPath,
						"NewEffect",
						".effect");

					if (WriteUtf8BomTextFile(filePath, MakeDefaultEffectAssetText())) {
						selectedAssetPath = filePath;
						g_selectedAssetPath = filePath;
						consoleMessages.push_back("Asset: Effect Asset を作成 " + filePath);
					}
					else {
						consoleMessages.push_back("Asset: Effect Asset の作成に失敗");
					}
				}
				ImGui::EndPopup();
			}

			if (isOpenFolderPopupRequested) {
				ImGui::OpenPopup("ProjectCreateFolderPopup");
			}

			if (ImGui::BeginPopupModal("ProjectCreateFolderPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::InputText("フォルダー名", newFolderName, sizeof(newFolderName));

				if (ImGui::Button("作成", ImVec2(120.0f, 0.0f))) {
					const std::string createDirectoryPath = GetProjectAssetCreateDirectory(selectedAssetPath);
					std::filesystem::create_directories(createDirectoryPath);

					std::string folderName = newFolderName;
					if (folderName.empty()) {
						folderName = "NewFolder";
					}

					const std::string folderPath = MakeUniqueFolderPath(createDirectoryPath, folderName);
					std::error_code fileError;
					const bool isCreated = std::filesystem::create_directories(folderPath, fileError);
					if (isCreated && !fileError) {
						selectedAssetPath = folderPath;
						g_selectedAssetPath = folderPath;
						consoleMessages.push_back("Asset: Folder created " + folderPath);
					}
					else {
						consoleMessages.push_back("Asset: Failed to create folder " + folderPath);
					}

					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();
				if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::BeginChild("Folders", ImVec2(180.0f, 0.0f), ImGuiChildFlags_Borders);  // 左側の簡易フォルダツリー
			DrawProjectFolderNode(std::filesystem::path("Assets"), selectedAssetPath);
			DrawProjectFolderNode(std::filesystem::path("resources"), selectedAssetPath);
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("Assets", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);  // 右側のアセットグリッド
			if (ImGui::BeginTable("AssetGrid", 4, ImGuiTableFlags_SizingStretchSame)) {
				std::vector<std::string> assetPaths = CollectProjectAssetPaths();

				for (const std::string& relativePath : assetPaths) {
					if (!IsAssetInSelectedProjectFolder(relativePath, selectedAssetPath)) {
						continue;
					}

					// 検索文字に一致しないアセットは表示しない
					if (!EditorAssetUtility::MatchesFilter(relativePath, assetFilter)) {
						continue;
					}

					ImGui::TableNextColumn();
					ImGui::PushID(relativePath.c_str());
					bool isPng =
						EditorAssetUtility::HasExtension(relativePath, ".png") ||
						EditorAssetUtility::HasExtension(relativePath, ".PNG");
					// Texture として登録済みなら SRV 番号を得る
					int32_t textureIndex =
						EditorAssetUtility::GetTextureIndex(textureFilePaths, relativePath);
					if (isPng &&
						textureSrvHandlesGPU != nullptr &&
						textureIndex >= 0 &&
						static_cast<size_t>(textureIndex) < textureCount) {
						ImGui::Image(
							ImTextureRef(textureSrvHandlesGPU[static_cast<size_t>(textureIndex)].ptr),
							ImVec2(48.0f, 48.0f));
						if (ImGui::IsItemClicked()) {
							selectedAssetPath = relativePath;  // クリックした Texture を Inspector の選択アセットにする
							g_selectedAssetPath = relativePath;
						}
						if (ImGui::IsItemHovered() &&
							ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							OpenAssetWithShell(relativePath, consoleMessages);
						}
						if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
							// SceneView に Drop するため AssetPath を Payload に入れる
							ImGui::SetDragDropPayload(
								"ASSET_PATH",
								relativePath.c_str(),
								relativePath.size() + 1);
							ImGui::Text("%s", relativePath.c_str());
							ImGui::EndDragDropSource();
						}
						std::string filename = EditorAssetUtility::GetFilename(relativePath);
						ImGui::TextWrapped("%s", filename.c_str());
					}
					else {
						bool isSelected = selectedAssetPath == relativePath;  // 画像以外は拡張子別のテキストアイコンで表示する
						const char* assetIcon = "FILE";
						if (EditorAssetUtility::HasExtension(relativePath, ".wav")) {
							assetIcon = "WAV";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".obj")) {
							assetIcon = "OBJ";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".fbx")) {
							assetIcon = "FBX";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".mtl")) {
							assetIcon = "MTL";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".dll")) {
							assetIcon = "DLL";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".cpp")) {
							assetIcon = "CPP";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".h")) {
							assetIcon = "HDR";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".py")) {
							assetIcon = "PY";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".onnx")) {
							assetIcon = "ONNX";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".inputactions")) {
							assetIcon = "INPUT";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".animgraph")) {
							assetIcon = "ANIM";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".animclip")) {
							assetIcon = "CLIP";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".effect")) {
							assetIcon = "FX";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".json")) {
							assetIcon = "JSON";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".scene")) {
							assetIcon = "SCENE";
						}
						else if (EditorAssetUtility::HasExtension(relativePath, ".prefab")) {
							assetIcon = "PFB";
						}

						if (ImGui::Button(assetIcon, ImVec2(58.0f, 48.0f))) {
							selectedAssetPath = relativePath;  // クリックした Asset を Inspector の選択アセットにする
							g_selectedAssetPath = relativePath;
						}
						if (ImGui::IsItemHovered() &&
							ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							OpenAssetWithShell(relativePath, consoleMessages);
						}
						if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
							// SceneView に Drop するため AssetPath を Payload に入れる
							ImGui::SetDragDropPayload(
								"ASSET_PATH",
								relativePath.c_str(),
								relativePath.size() + 1);
							ImGui::Text("%s", relativePath.c_str());
							ImGui::EndDragDropSource();
						}
						if (isSelected) {
							ImGui::TextColored(
								ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
								"%s",
								EditorAssetUtility::GetFilename(relativePath).c_str());
						}
						else {
							ImGui::TextWrapped("%s", EditorAssetUtility::GetFilename(relativePath).c_str());
						}
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (EditorAssetUtility::HasFilterText(assetFilter)) {
				ImGui::TextDisabled("検索中: %s", assetFilter);
			}

			if (isProjectWindowFocused &&
				!ImGui::IsAnyItemActive() &&
				!ImGui::GetIO().WantTextInput &&
				ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
				DeleteSelectedAsset(selectedAssetPath, consoleMessages);
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Console")) {
			if (ImGui::Button("ログ消去")) {
				consoleMessages.clear();  // Console 表示用メッセージ配列を空にする
				isConsoleCleared = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("ログ表示")) {
				isConsoleCleared = false;
			}
			ImGui::Separator();
			if (!isConsoleCleared) {
				ImGui::Text("Scene: %.0f x %.0f", editorSceneWidth, editorSceneHeight);  // SceneView の現在サイズと Play 状態を Console に出す
				ImGui::Text("Play: %s", isPlaying ? "Running" : "Stopped");
				ImGui::Separator();
				for (const std::string& consoleMessage : consoleMessages) {
					ImGui::TextWrapped("%s", consoleMessage.c_str());  // 各 Manager が追加したログを折り返し表示する
				}
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
