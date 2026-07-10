#include "EditorHierarchyPanel.h"

#include "EditorAssetUtility.h"
#include "EditorSharedState.h"

#pragma warning(push, 0)
#include "externals/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

using namespace EditorSharedState;

namespace {
	void FocusSceneCameraOnGameObject(const EditorGameObject& gameObject) {
		// 現在のカメラ向きを保ったまま、選択 GameObject が SceneView の中央へ来る位置まで寄せる。
		const float cameraPitch = g_cameraTransform.rotate.x;
		const float cameraYaw = g_cameraTransform.rotate.y;

		Vector3 forward{};
		forward.x = std::sin(cameraYaw) * std::cos(cameraPitch);
		forward.y = -std::sin(cameraPitch);
		forward.z = std::cos(cameraYaw) * std::cos(cameraPitch);

		const float focusDistance = 6.0f;
		g_cameraTransform.translate.x = gameObject.translate.x - forward.x * focusDistance;
		g_cameraTransform.translate.y = gameObject.translate.y - forward.y * focusDistance;
		g_cameraTransform.translate.z = gameObject.translate.z - forward.z * focusDistance;
	}

	void SelectFirstGameObjectOrClear(
		EditorScene* editorScene,
		EditorSelectionManager* selectionManager,
		int32_t& selectedGameObjectId,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& previousSelectedGameObjectId,
		int32_t& selectedSceneObject) {
		// 削除後に残りオブジェクトがあれば先頭を選び、空なら全選択を解除する。
		if (editorScene == nullptr || selectionManager == nullptr) {
			return;
		}

		if (editorScene->GetGameObjects().empty()) {
			selectedGameObjectId = -1;
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 0;
			return;
		}

		selectedGameObjectId = editorScene->GetGameObjects()[0].id;
		selectedPlacedSceneObjectIndex = -1;
		previousSelectedGameObjectId = -1;
		selectionManager->SyncLegacySelection(
			selectedGameObjectId,
			selectedSceneObject,
			selectedPlacedSceneObjectIndex);
	}
}

void EditorHierarchyPanel::Initialize(
	EditorScene* editorScene,
	EditorSelectionManager* selectionManager,
	EditorAssetFactory* assetFactory,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Draw で使う外部データを保持する
	selectionManager_ = selectionManager;
	assetFactory_ = assetFactory;
	consoleMessages_ = consoleMessages;
}

void EditorHierarchyPanel::Update() {
}

void EditorHierarchyPanel::Draw(
	char* hierarchyFilter,
	size_t hierarchyFilterSize,
	int32_t& selectedGameObjectId,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& previousSelectedGameObjectId,
	int32_t& selectedSceneObject) {
	if (editorScene_ == nullptr || selectionManager_ == nullptr || assetFactory_ == nullptr) {
		return;
	}

	const bool isHierarchyWindowFocused =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);  // Hierarchy 全体がアクティブな時だけ Delete を受け付ける。

	ImGui::InputText("検索", hierarchyFilter, hierarchyFilterSize);  // Hierarchy 上部の GameObject 名検索

	auto createPrimitiveGameObject = [&](const char* assetPath) {
		// 基本形 FBX 名から内部プリミティブメッシュを作り、Renderer / Rigidbody / Collider 付きで Scene に置く
		assetFactory_->CreateModelGameObject(
			assetPath,
			Vector3{0.0f, 1.5f, 0.0f},
			selectedGameObjectId,
			selectedPlacedSceneObjectIndex,
			selectedSceneObject);
		previousSelectedGameObjectId = -1;
	};

	if (ImGui::Button("作成")) {
		ImGui::OpenPopup("HierarchyCreatePopup");
	}

	ImGui::SameLine();

	if (ImGui::Button("親解除")) {
		editorScene_->PushUndo();  // 選択中 GameObject の parentId を無効 ID に戻す
		editorScene_->SetParent(selectedGameObjectId, -1);
	}

	if (ImGui::BeginPopup("HierarchyCreatePopup")) {
		if (ImGui::MenuItem("空のGameObject")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject("GameObject");
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			if (consoleMessages_ != nullptr) {
				consoleMessages_->push_back("Scene: 空のGameObjectを作成");
			}
		}

		if (ImGui::BeginMenu("3D Object")) {
			if (ImGui::MenuItem("Cube")) {
				createPrimitiveGameObject("resources/UVCube.fbx");
			}
			if (ImGui::MenuItem("Box")) {
				createPrimitiveGameObject("resources/box.fbx");
			}
			if (ImGui::MenuItem("Cylinder")) {
				createPrimitiveGameObject("resources/cylinder.fbx");
			}
			if (ImGui::MenuItem("Cone")) {
				createPrimitiveGameObject("resources/cone.fbx");
			}
			if (ImGui::MenuItem("Torus")) {
				createPrimitiveGameObject("resources/to-tasu.fbx");
			}
			if (ImGui::MenuItem("Ico")) {
				createPrimitiveGameObject("resources/ICOCube.fbx");
			}
			if (ImGui::MenuItem("Sphere")) {
				createPrimitiveGameObject("resources/sphere.fbx");
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("ライト")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject("Light");
			editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::Light);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 2;
		}

		if (ImGui::MenuItem("カメラ")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject("Camera");
			editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::Camera);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 3;
		}

		ImGui::EndPopup();
	}

	ImGui::Separator();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		// 親なし GameObject だけをルートノードとして描画する
		if (gameObject.parentId == -1) {
			DrawGameObjectNode(
				gameObject.id,
				0,
				hierarchyFilter,
				selectedGameObjectId,
				selectedPlacedSceneObjectIndex,
				selectedSceneObject);
		}
	}

	if (isHierarchyWindowFocused &&
		!ImGui::IsAnyItemActive() &&
		!ImGui::GetIO().WantTextInput &&
		selectedGameObjectId >= 0 &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		EditorGameObject* deletingGameObject = editorScene_->FindGameObject(selectedGameObjectId);
		const std::string deletingName =
			deletingGameObject != nullptr ? deletingGameObject->name : "GameObject";

		editorScene_->PushUndo();  // Delete も Undo で戻せるように、削除前の Scene を退避する。
		if (editorScene_->DeleteGameObject(selectedGameObjectId)) {
			SelectFirstGameObjectOrClear(
				editorScene_,
				selectionManager_,
				selectedGameObjectId,
				selectedPlacedSceneObjectIndex,
				previousSelectedGameObjectId,
				selectedSceneObject);
			if (consoleMessages_ != nullptr) {
				consoleMessages_->push_back("Hierarchy: 削除 " + deletingName);
			}
		}
	}
}

void EditorHierarchyPanel::DrawGameObjectNode(
	int32_t gameObjectId,
	int32_t depth,
	const char* hierarchyFilter,
	int32_t& selectedGameObjectId,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& selectedSceneObject) {
	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	// ルートノードが検索に一致しない場合、子に一致があるか確認する
	if (!EditorAssetUtility::MatchesFilter(gameObject->name, hierarchyFilter) && depth == 0) {
		bool hasMatchedChild = false;
		for (int32_t childId : gameObject->children) {
			const EditorGameObject* child = editorScene_->FindGameObject(childId);
			hasMatchedChild = hasMatchedChild ||
				(child != nullptr && EditorAssetUtility::MatchesFilter(child->name, hierarchyFilter));
		}

		if (!hasMatchedChild) {
			return;
		}
	}

	ImGui::Indent(static_cast<float>(depth) * 14.0f);  // depth をインデント幅に変換して親子階層を見た目に反映する
	std::string label = gameObject->children.empty() ? "  " : "v ";  // 子がある場合は v を付けて展開可能に見せる
	label += gameObject->name;
	bool isSelected = selectedGameObjectId == gameObject->id;
	if (ImGui::Selectable(label.c_str(), isSelected)) {
		selectedGameObjectId = gameObject->id;  // Hierarchy でクリックした GameObject を選択状態にする
		selectedPlacedSceneObjectIndex = -1;
		selectionManager_->SyncLegacySelection(
			selectedGameObjectId,
			selectedSceneObject,
			selectedPlacedSceneObjectIndex);
	}

	if (ImGui::IsItemHovered() &&
		ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		selectedGameObjectId = gameObject->id;  // ダブルクリック時も対象選択を揃えてからカメラを寄せる。
		selectedPlacedSceneObjectIndex = -1;
		selectionManager_->SyncLegacySelection(
			selectedGameObjectId,
			selectedSceneObject,
			selectedPlacedSceneObjectIndex);
		FocusSceneCameraOnGameObject(*gameObject);
		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back("Hierarchy: フォーカス " + gameObject->name);
		}
	}

	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload("GAME_OBJECT_ID", &gameObject->id, sizeof(gameObject->id));  // 親子付け用に GameObject ID を DragDrop Payload として渡す
		ImGui::Text("%s", gameObject->name.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT_ID")) {
			int32_t childId = *static_cast<const int32_t*>(payload->Data);  // Drop された GameObject をこのノードの子にする
			editorScene_->PushUndo();
			editorScene_->SetParent(childId, gameObject->id);
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::Unindent(static_cast<float>(depth) * 14.0f);

	for (int32_t childId : gameObject->children) {
		// 子ノードは depth + 1 で再帰描画する
		DrawGameObjectNode(
			childId,
			depth + 1,
			hierarchyFilter,
			selectedGameObjectId,
			selectedPlacedSceneObjectIndex,
			selectedSceneObject);
	}
}
