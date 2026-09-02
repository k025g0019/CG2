#include "EditorHierarchyPanel.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#pragma warning(push, 0)
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
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

		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition = gameObject.translate;
		g_editorScene.GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			worldPosition);

		// Wireで大きな力を受けた物体等がNaN/Infへ吹き飛んだ状態でダブルクリックすると、
		// 編集用カメラ(g_cameraTransform)自体がNaNになり、以後SceneView全体のギズモの
		// 当たり判定計算が壊れる(NaNとの比較は常にfalseになるため)。書き込む前に弾く。
		const bool isWorldPositionFinite =
			std::isfinite(worldPosition.x) &&
			std::isfinite(worldPosition.y) &&
			std::isfinite(worldPosition.z);
		if (!isWorldPositionFinite) {
			return;
		}

		const float focusDistance = 6.0f;
		g_cameraTransform.translate.x = worldPosition.x - forward.x * focusDistance;
		g_cameraTransform.translate.y = worldPosition.y - forward.y * focusDistance;
		g_cameraTransform.translate.z = worldPosition.z - forward.z * focusDistance;
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
			ClearSelectedGameObjects();
			selectedGameObjectId = -1;
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 0;
			return;
		}

		selectedGameObjectId = editorScene->GetGameObjects()[0].id;
		SetSingleSelectedGameObject(selectedGameObjectId);
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
		editorScene_->SetParent(selectedGameObjectId, -1, true);
	}

	// Hookは「選択中の物体の子として、見た目・選択Collider・HookPointを揃えた1セット」を作る。
	// 手作業でRenderer/Collider/HookPoint/力伝達先を毎回設定するとPhysicsBody設定漏れが起きやすいため、
	// 20個並べても事故らないようにEditor側で組み立てる。
	auto createHookOnSelectedGameObject = [&]() {
		EditorGameObject* parentGameObject = editorScene_->FindGameObject(selectedGameObjectId);
		const int32_t parentGameObjectId = parentGameObject != nullptr ? parentGameObject->id : -1;

		// Renderer付きSphereとして作り、描画用SceneObjectの登録までAssetFactoryに任せる。
		assetFactory_->CreateModelGameObject(
			"resources/sphere.fbx",
			Vector3{0.0f, 0.0f, 0.0f},
			selectedGameObjectId,
			selectedPlacedSceneObjectIndex,
			selectedSceneObject);

		const int32_t hookGameObjectId = selectedGameObjectId;
		EditorGameObject* hookGameObject = editorScene_->FindGameObject(hookGameObjectId);
		if (hookGameObject == nullptr) {
			return;
		}

		hookGameObject->name = editorScene_->MakeUniqueGameObjectName("Hook");
		editorScene_->AddComponent(hookGameObjectId, EditorComponentType::WireConnectable);

		// Raycastで狙えるかはCollider(FindMainCollider)だけで決まり、Rigidbodyの有無は無関係。
		// 子Hook（親を選択して作った場合）は力を親へ渡すだけの選択点なので、Rigidbody自体を持たせない。
		// 親を選ばず作った場合はwireConnectablePhysicsBodyGameObjectIdがHook自身になり、
		// Wireの力もHook自身のRigidbodyへ掛かるため、こちらはDynamicのRigidbodyを残す。
		if (parentGameObjectId >= 0) {
			editorScene_->RemoveComponent(hookGameObjectId, EditorComponentType::RigidBody);
		}

		// 掴む目印として分かる大きさにする。Colliderは選択用で、物体同士の衝突用ではない。
		hookGameObject->scale = {0.25f, 0.25f, 0.25f};

		if (EditorComponent* hookComponent = EditorComponentUtility::FindComponent(
			*hookGameObject,
			EditorComponentType::WireConnectable)) {
			// 親が指定されていれば力の伝達先を親へ向ける。-1のままだとHook自身を物理Bodyとして扱ってしまう。
			hookComponent->wireConnectablePhysicsBodyGameObjectId = parentGameObjectId;
			hookComponent->wireConnectableUseHitPoint = false;  // 固定フックなのでAnchorはローカル原点に固定する。
			hookComponent->wireConnectableLocalAnchor = {0.0f, 0.0f, 0.0f};
		}

		if (parentGameObjectId >= 0) {
			editorScene_->SetParent(hookGameObjectId, parentGameObjectId, false);
			hookGameObject = editorScene_->FindGameObject(hookGameObjectId);
			if (hookGameObject != nullptr) {
				// 親の中心(ローカル原点)にそのまま置くと、親自身の当たり判定の内部に
				// 埋まってしまい、狙うRayが必ず親の表面で止まってHookまで届かない
				// (FindBestHookはRaycastの命中GameObjectがHook自身と一致するかで判定するため、
				// 親に埋まっていると永遠に選択できない)。表面に出るよう手前へ少しずらす。
				hookGameObject->translate = {0.0f, 0.0f, -0.6f};
			}
		}

		SetSingleSelectedGameObject(hookGameObjectId);
		previousSelectedGameObjectId = -1;

		if (consoleMessages_ != nullptr) {
			consoleMessages_->push_back(
				parentGameObjectId >= 0
					? "Scene: Hookを子として作成し、力を伝えるRigidbodyへ親を設定"
					: "Scene: Hookを作成（親未選択のため力の伝達先はHook自身）");
		}
	};

	if (ImGui::BeginPopup("HierarchyCreatePopup")) {
		if (ImGui::MenuItem("Hook（選択物体の子）")) {
			createHookOnSelectedGameObject();
		}

		ImGui::SetItemTooltip(
			"選択中の物体の子として Renderer + 選択Collider + HookPoint を作り、\n"
			"HookPointの「力を伝えるRigidbody」に選択中の物体を設定します。");

		ImGui::Separator();

		if (ImGui::MenuItem("空のGameObject")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject(editorScene_->MakeUniqueGameObjectName("GameObject"));
			SetSingleSelectedGameObject(selectedGameObjectId);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			if (consoleMessages_ != nullptr) {
				consoleMessages_->push_back("Scene: 空のGameObjectを作成");
			}
		}

		if (ImGui::BeginMenu("3D Object")) {
			if (ImGui::MenuItem("Cube")) {
				createPrimitiveGameObject("resources/editorDefault/UVCube.fbx");
			}
			if (ImGui::MenuItem("Box")) {
				createPrimitiveGameObject("resources/editorDefault/box.fbx");
			}
			if (ImGui::MenuItem("Cylinder")) {
				createPrimitiveGameObject("resources/cylinder.fbx");
			}
			if (ImGui::MenuItem("Cone")) {
				createPrimitiveGameObject("resources/editorDefault/cone.fbx");
			}
			if (ImGui::MenuItem("Torus")) {
				createPrimitiveGameObject("resources/to-tasu.fbx");
			}
			if (ImGui::MenuItem("Ico")) {
				createPrimitiveGameObject("resources/editorDefault/ICOCube.fbx");
			}
			if (ImGui::MenuItem("Sphere")) {
				createPrimitiveGameObject("resources/sphere.fbx");
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("ライト")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject(editorScene_->MakeUniqueGameObjectName("Light"));
			editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::Light);
			EditorGameObject* lightGameObject = editorScene_->FindGameObject(selectedGameObjectId);
			if (lightGameObject != nullptr) {
				// 作成直後から Scene 上のアイコンとギズモを掴める位置に置く。
				lightGameObject->translate = {0.0f, 3.0f, -2.0f};
				lightGameObject->rotate = {1.1f, 0.0f, 0.0f};
			}
			SetSingleSelectedGameObject(selectedGameObjectId);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 2;
		}

		if (ImGui::MenuItem("カメラ")) {
			editorScene_->PushUndo();
			selectedGameObjectId = editorScene_->CreateGameObject(editorScene_->MakeUniqueGameObjectName("Camera"));
			editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::Camera);
			EditorGameObject* cameraGameObject = editorScene_->FindGameObject(selectedGameObjectId);
			if (cameraGameObject != nullptr) {
				// 原点を見やすい距離から見る初期位置にして、生成直後から見失わないようにする。
				cameraGameObject->translate = {0.0f, 2.0f, -6.0f};
				cameraGameObject->rotate = {0.25f, 0.0f, 0.0f};
			}
			SetSingleSelectedGameObject(selectedGameObjectId);
			selectedPlacedSceneObjectIndex = -1;
			previousSelectedGameObjectId = -1;
			selectedSceneObject = 3;
		}

		ImGui::EndPopup();
	}

	ImGui::Separator();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		// EffectSystem が Play 中だけ生成する描画粒子は、利用者が編集する Scene オブジェクトではない。
		if (gameObject.name.rfind("__EffectParticle_", 0u) == 0u) {
			continue;
		}

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

	// Undoはあるが、Deleteキー一発で子階層ごと即消えると事故りやすいため確認を挟む。
	static int32_t pendingHierarchyDeleteGameObjectId = -1;

	if (isHierarchyWindowFocused &&
		!ImGui::IsAnyItemActive() &&
		!ImGui::GetIO().WantTextInput &&
		selectedGameObjectId >= 0 &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		pendingHierarchyDeleteGameObjectId = selectedGameObjectId;
		ImGui::OpenPopup("Hierarchy削除確認");
	}

	if (ImGui::BeginPopupModal("Hierarchy削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		EditorGameObject* deletingGameObject = editorScene_->FindGameObject(pendingHierarchyDeleteGameObjectId);
		const std::string deletingName =
			deletingGameObject != nullptr ? deletingGameObject->name : "GameObject";

		ImGui::Text("「%s」を子階層ごと削除しますか？", deletingName.c_str());

		if (ImGui::Button("削除する", ImVec2(120.0f, 0.0f))) {
			editorScene_->PushUndo();  // Delete も Undo で戻せるように、削除前の Scene を退避する。
			if (editorScene_->DeleteGameObject(pendingHierarchyDeleteGameObjectId)) {
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
			pendingHierarchyDeleteGameObjectId = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
			pendingHierarchyDeleteGameObjectId = -1;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
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

	// 一時 Particle が親子付けされた場合も Hierarchy へ表示しない。
	if (gameObject->name.rfind("__EffectParticle_", 0u) == 0u) {
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

	(void)depth;
	const bool hasChildren = !gameObject->children.empty();
	ImGuiTreeNodeFlags treeNodeFlags =
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (IsGameObjectSelected(gameObject->id)) {
		treeNodeFlags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!hasChildren) {
		treeNodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	const bool isNodeOpen = ImGui::TreeNodeEx(
		reinterpret_cast<void*>(static_cast<intptr_t>(gameObject->id)),
		treeNodeFlags,
		"%s",
		gameObject->name.c_str());
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
		selectedGameObjectId = gameObject->id;  // Hierarchy でクリックした GameObject を選択状態にする
		SetSingleSelectedGameObject(selectedGameObjectId);
		selectedPlacedSceneObjectIndex = -1;
		selectionManager_->SyncLegacySelection(
			selectedGameObjectId,
			selectedSceneObject,
			selectedPlacedSceneObjectIndex);
	}

	if (ImGui::IsItemHovered() &&
		ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		selectedGameObjectId = gameObject->id;  // ダブルクリック時も対象選択を揃えてからカメラを寄せる。
		SetSingleSelectedGameObject(selectedGameObjectId);
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
			editorScene_->SetParent(childId, gameObject->id, true);
		}
		ImGui::EndDragDropTarget();
	}

	if (hasChildren && isNodeOpen) {
		for (int32_t childId : gameObject->children) {
			// TreeNode の開閉状態に従い、開いている時だけ子を再帰描画する。
			DrawGameObjectNode(
				childId,
				depth + 1,
				hierarchyFilter,
				selectedGameObjectId,
				selectedPlacedSceneObjectIndex,
				selectedSceneObject);
		}

		ImGui::TreePop();
	}
}
