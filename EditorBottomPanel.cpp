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
		g_selectedEditorGameObjectId = gameObjectId;
		g_previousSelectedEditorGameObjectId = -1;
		g_selectedPlacedSceneObjectIndex = -1;
		SyncSelectionToScene();
	}

	void SelectFirstGameObjectOrClear() {
		// Scene が空なら選択を解除し、残っていれば先頭 GameObject を選択する。
		if (g_editorScene.GetGameObjects().empty()) {
			g_selectedEditorGameObjectId = -1;
			g_previousSelectedEditorGameObjectId = -1;
			g_selectedPlacedSceneObjectIndex = -1;
			g_selectedSceneObject = 0;
			return;
		}

		SelectGameObject(g_editorScene.GetGameObjects()[0].id);
	}

	void AddBuiltInPrimitiveAsset(std::vector<std::string>& assetPaths, const char* assetPath) {
		// 実ファイルがなくても、内部プリミティブとして置けるアセットは Project に出しておく。
		if (assetPath == nullptr) {
			return;
		}

		const std::string builtInAssetPath = assetPath;
		if (std::find(assetPaths.begin(), assetPaths.end(), builtInAssetPath) == assetPaths.end()) {
			assetPaths.push_back(builtInAssetPath);
		}
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
			EditorAssetUtility::HasExtension(assetPath, ".json") ||
			EditorAssetUtility::HasExtension(assetPath, ".scene") ||
			EditorAssetUtility::HasExtension(assetPath, ".prefab") ||
			EditorAssetUtility::HasExtension(assetPath, ".inputactions");
	}

	std::string MakeDefaultPlayerInputActionsText() {
		return
			"# CG2 PlayerInput Actions\r\n"
			"# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n"
			"Action|Player|Move|Vector2|2DVector|W|S|A|D\r\n"
			"Action|Player|Jump|Button|Key|Space\r\n"
			"Action|Player|Fire|Button|Mouse|LeftButton\r\n";
	}

	std::string GetProjectAssetCreateDirectory(const std::string& selectedAssetPath) {
		// Project で Assets 配下のファイルを選択している時は、そのフォルダへ新規アセットを作る。
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
				if (IsProjectAssetFile(assetPath)) {
					assetPaths.push_back(assetPath);
				}
			}
		}

		// Sphere は内部メッシュで生成するので、resources に実ファイルがなくても一覧へ出す。
		AddBuiltInPrimitiveAsset(assetPaths, "resources/sphere.fbx");

		std::sort(assetPaths.begin(), assetPaths.end());
		assetPaths.erase(std::unique(assetPaths.begin(), assetPaths.end()), assetPaths.end());
		return assetPaths;
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

			ImGui::InputText("検索", assetFilter, assetFilterSize);  // Project 内アセット名検索
			ImGui::SameLine();
			if (ImGui::Button("+")) {
				ImGui::OpenPopup("ProjectCreateAssetPopup");
			}
			if (ImGui::BeginPopup("ProjectCreateAssetPopup")) {
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
				ImGui::EndPopup();
			}
			ImGui::BeginChild("Folders", ImVec2(180.0f, 0.0f), ImGuiChildFlags_Borders);  // 左側の簡易フォルダツリー
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::TreeNode("Assets")) {
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("resources(内部)")) {
				ImGui::TreeNodeEx("sound", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
				ImGui::TreePop();
			}
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("Assets", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);  // 右側のアセットグリッド
			if (ImGui::BeginTable("AssetGrid", 4, ImGuiTableFlags_SizingStretchSame)) {
				std::vector<std::string> assetPaths = CollectProjectAssetPaths();

				for (const std::string& relativePath : assetPaths) {
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
						else if (EditorAssetUtility::HasExtension(relativePath, ".inputactions")) {
							assetIcon = "INPUT";
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
