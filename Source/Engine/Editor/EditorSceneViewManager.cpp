#pragma warning(disable : 4189 4514)

#include "EditorSceneViewManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace EditorSharedState;

namespace {
	constexpr float kEditorSceneHeaderHeight = 24.0f;  // kEditorSceneHeaderHeight はタブ見出し分の高さ。SceneView の初回サイズに足す。
	constexpr float kProjectionEpsilon = 0.0001f;  // kProjectionEpsilon は w が 0 に近い点やカメラ後方の点を描画対象から外すためのしきい値。
	constexpr float kProjectedLineNdcLimit = 4.0f;  // kProjectedLineNdcLimit は補助線が画面外へ飛びすぎる時に DrawList へ流さないための安全域。

	struct ProjectedScenePoint {
		ImVec2 screenPosition;  // screenPosition は ImGui DrawList に渡せる 2D 座標。
		float ndcX;  // ndcX/Y はクリップ後の正規化座標。線の安全判定に使う。
		float ndcY;
	};

	bool TryProjectWorldPosition(const Vector3& worldPosition, ProjectedScenePoint& projectedPoint) {
		Matrix4x4 viewProjectionMatrix = Multiply(g_viewMatrix, g_projectionMatrix);  // viewProjectionMatrix は 3D ワールド座標をクリップ空間へ変換するための合成行列。
		float clipX = worldPosition.x * viewProjectionMatrix.matrix[0][0] +
			worldPosition.y * viewProjectionMatrix.matrix[1][0] +
			worldPosition.z * viewProjectionMatrix.matrix[2][0] +
			viewProjectionMatrix.matrix[3][0];  // clipX は透視除算前のクリップ座標 X。
		float clipY = worldPosition.x * viewProjectionMatrix.matrix[0][1] +
			worldPosition.y * viewProjectionMatrix.matrix[1][1] +
			worldPosition.z * viewProjectionMatrix.matrix[2][1] +
			viewProjectionMatrix.matrix[3][1];  // clipY は透視除算前のクリップ座標 Y。
		float clipW = worldPosition.x * viewProjectionMatrix.matrix[0][3] +
			worldPosition.y * viewProjectionMatrix.matrix[1][3] +
			worldPosition.z * viewProjectionMatrix.matrix[2][3] +
			viewProjectionMatrix.matrix[3][3];  // clipW は透視除算に使う同次座標 w。

		// clipW が 0 付近、または負ならカメラの後ろ側へ回っているので 2D 線を引かない。
		if (clipW <= kProjectionEpsilon) {
			return false;
		}

		float ndcX = clipX / clipW;  // ndcX/Y は SceneView へ貼り付ける前の -1.0f から 1.0f の正規化座標。
		float ndcY = clipY / clipW;
		projectedPoint.ndcX = ndcX;
		projectedPoint.ndcY = ndcY;
		projectedPoint.screenPosition = ImVec2(
			g_editorSceneX + (ndcX + 1.0f) * 0.5f * g_editorSceneWidth,
			g_editorSceneY + (1.0f - ndcY) * 0.5f * g_editorSceneHeight);
		return true;
	}

	ImVec2 ProjectWorldPosition(const Vector3& worldPosition) {
		ProjectedScenePoint projectedPoint{};  // projectedPoint は画面座標と NDC をまとめて受ける一時変数。
		if (!TryProjectWorldPosition(worldPosition, projectedPoint)) {
			return ImVec2(-10000.0f, -10000.0f);  // 後方点は通常描画から外したいが、既存の選択処理互換のため固定の遠方座標を返す。
		}

		return projectedPoint.screenPosition;
	}

	Vector3 GetModelDropPosition() {
		ImVec2 mousePosition = ImGui::GetIO().MousePos;  // mousePosition は画面全体基準の ImGui マウス座標。
		float sceneRateX = (mousePosition.x - g_editorSceneX) / g_editorSceneWidth;  // sceneRateX/Y は SceneView 内での 0.0f から 1.0f の相対位置。
		float sceneRateY = (mousePosition.y - g_editorSceneY) / g_editorSceneHeight;

		// モデルは Scene 中央を原点にした見やすい仮配置範囲へ落とす。
		return Vector3{
			(sceneRateX - 0.5f) * 4.0f,
			(0.5f - sceneRateY) * 3.0f,
			0.0f
		};
	}

	Vector3 GetSpriteDropPosition() {
		ImVec2 mousePosition = ImGui::GetIO().MousePos;  // mousePosition は Project から SceneView へドロップした瞬間の画面座標。
		float sceneRateX = (mousePosition.x - g_editorSceneX) / g_editorSceneWidth;  // sceneRateX/Y は SceneView 左上からの割合。Sprite 座標へ戻す倍率に使う。
		float sceneRateY = (mousePosition.y - g_editorSceneY) / g_editorSceneHeight;

		// Sprite は 2D 描画座標なので、ウィンドウ幅・高さ基準の座標に変換する。
		return Vector3{
			sceneRateX * g_editorWindowWidth,
			sceneRateY * g_editorWindowHeight,
			0.0f
		};
	}

	const char* GetActiveEditorToolName() {
		// g_activeEditorTool は Inspector のツール選択番号。表示文字列に変換する。
		if (g_activeEditorTool == 1) {
			return "移動";
		}

		if (g_activeEditorTool == 2) {
			return "回転";
		}

		if (g_activeEditorTool == 3) {
			return "拡縮";
		}

		return "統合";
	}

	ImGuizmo::OPERATION GetActiveGizmoOperation() {
		// g_activeEditorTool を ImGuizmo が要求する操作 enum に変換する。
		if (g_activeEditorTool == 2) {
			return ImGuizmo::ROTATE;
		}

		if (g_activeEditorTool == 3) {
			return ImGuizmo::SCALE;
		}

		if (g_activeEditorTool == 4) {
			return ImGuizmo::UNIVERSAL;
		}

		return ImGuizmo::TRANSLATE;
	}

	bool IsToolShortcutTriggered(int32_t dikCode) {
		return
			dikCode >= 0 &&
			dikCode < 256 &&
			g_key[dikCode] != 0 &&
			g_preKey[dikCode] == 0;
	}

	void UpdateActiveToolFromShortcut(bool canUseShortcut) {
		if (!canUseShortcut) {
			return;
		}

		// Unity と同じ W/E/R/T で、SceneView 上から直接ギズモ種別を切り替える。
		if (IsToolShortcutTriggered(DIK_W)) {
			g_activeEditorTool = 1;
		}
		else if (IsToolShortcutTriggered(DIK_E)) {
			g_activeEditorTool = 2;
		}
		else if (IsToolShortcutTriggered(DIK_R)) {
			g_activeEditorTool = 3;
		}
		else if (IsToolShortcutTriggered(DIK_T)) {
			g_activeEditorTool = 4;
		}
	}

	bool DrawSceneToolButton(
		const char* visibleLabel,
		const char* tooltipText,
		int32_t toolIndex,
		const ImVec2& position) {
		ImGui::SetCursorScreenPos(position);
		ImGui::PushID(toolIndex);

		bool isCurrentTool = g_activeEditorTool == toolIndex;  // 現在選択中のツールだけ明るくして、切替状態を見えるようにする。
		ImVec4 buttonColor = isCurrentTool ? ImVec4(0.20f, 0.42f, 0.78f, 1.0f) : ImVec4(0.12f, 0.14f, 0.17f, 0.95f);
		ImVec4 hoveredColor = isCurrentTool ? ImVec4(0.25f, 0.50f, 0.90f, 1.0f) : ImVec4(0.20f, 0.23f, 0.28f, 1.0f);
		ImVec4 activeColor = ImVec4(0.30f, 0.58f, 1.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);

		bool isClicked = ImGui::Button(visibleLabel, ImVec2(32.0f, 30.0f));
		bool isHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if (isClicked) {
			g_activeEditorTool = toolIndex;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", tooltipText);
		}

		ImGui::PopStyleColor(3);
		ImGui::PopID();

		return isHot;
	}

	bool DrawSceneToolBar() {
		bool isToolBarHot = false;  // true なら Scene 範囲選択やカメラ操作を開始しない。
		ImVec2 toolPosition{g_editorSceneX + 6.0f, g_editorSceneY + 12.0f};
		constexpr float toolButtonStep = 36.0f;

		isToolBarHot |= DrawSceneToolButton("W", "移動ギズモ", 1, toolPosition);
		toolPosition.y += toolButtonStep;
		isToolBarHot |= DrawSceneToolButton("E", "回転ギズモ", 2, toolPosition);
		toolPosition.y += toolButtonStep;
		isToolBarHot |= DrawSceneToolButton("R", "拡縮ギズモ", 3, toolPosition);
		toolPosition.y += toolButtonStep;
		isToolBarHot |= DrawSceneToolButton("T", "統合ギズモ", 4, toolPosition);

		return isToolBarHot;
	}

	const EditorComponent* FindDebugCollider(const EditorGameObject& gameObject) {
		const EditorComponentType colliderTypes[] = {
			EditorComponentType::BoxCollider,
			EditorComponentType::SphereCollider,
			EditorComponentType::CapsuleCollider,
			EditorComponentType::MeshCollider,
			EditorComponentType::TerrainCollider,
			EditorComponentType::CharacterController};

		for (EditorComponentType colliderType : colliderTypes) {
			const EditorComponent* collider = EditorComponentUtility::FindComponent(gameObject, colliderType);
			if (collider != nullptr && collider->isActive) {
				return collider;
			}
		}

		return nullptr;
	}

	ImU32 GetPhysicsDebugColor(const EditorGameObject& gameObject, const EditorComponent& collider) {
		if (IsGameObjectSelected(gameObject.id)) {
			return IM_COL32(255, 215, 90, 255);
		}

		// Trigger は押し返さない接触判定なので、通常 Collider と色を分ける。
		return collider.isTrigger
			       ? IM_COL32(80, 255, 150, 230)
			       : IM_COL32(85, 190, 255, 230);
	}

	Vector3 MakeLocalPoint(const Vector3& center, float x, float y, float z) {
		return Vector3{
			center.x + x,
			center.y + y,
			center.z + z};
	}

	Vector3 TransformColliderPoint(const EditorGameObject& gameObject, const Vector3& localPoint) {
		Matrix4x4 worldMatrix = MakeAffineMatrix(gameObject.scale, gameObject.rotate, gameObject.translate);
		return Transform(localPoint, worldMatrix);
	}

	void DrawProjectedLine(
		ImDrawList* sceneDrawList,
		const EditorGameObject& gameObject,
		const Vector3& localStart,
		const Vector3& localEnd,
		ImU32 color,
		float thickness) {
		Vector3 worldStart = TransformColliderPoint(gameObject, localStart);
		Vector3 worldEnd = TransformColliderPoint(gameObject, localEnd);
		ProjectedScenePoint projectedStart{};
		ProjectedScenePoint projectedEnd{};
		bool hasScreenStart = TryProjectWorldPosition(worldStart, projectedStart);  // hasScreenStart/End はカメラ前方の点だけ DrawList に流すための判定。
		bool hasScreenEnd = TryProjectWorldPosition(worldEnd, projectedEnd);

		if (!hasScreenStart || !hasScreenEnd) {
			return;
		}

		// 画面外へ大きく発散した補助線は SceneView を埋めるだけなので描かない。
		if (std::fabs(projectedStart.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedStart.ndcY) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcY) > kProjectedLineNdcLimit) {
			return;
		}

		sceneDrawList->AddLine(projectedStart.screenPosition, projectedEnd.screenPosition, color, thickness);
	}

	void DrawLocalCircle(
		ImDrawList* sceneDrawList,
		const EditorGameObject& gameObject,
		const Vector3& center,
		const Vector3& axisA,
		const Vector3& axisB,
		float radius,
		ImU32 color) {
		constexpr int32_t kSegmentCount = 32;  // 円を SceneView 上で十分滑らかに見せる分割数
		Vector3 previousPoint{};

		for (int32_t segmentIndex = 0; segmentIndex <= kSegmentCount; ++segmentIndex) {
			float angle = std::numbers::pi_v<float> * 2.0f * static_cast<float>(segmentIndex) / static_cast<float>(kSegmentCount);
			Vector3 currentPoint{
				center.x + (axisA.x * std::cos(angle) + axisB.x * std::sin(angle)) * radius,
				center.y + (axisA.y * std::cos(angle) + axisB.y * std::sin(angle)) * radius,
				center.z + (axisA.z * std::cos(angle) + axisB.z * std::sin(angle)) * radius};

			if (segmentIndex > 0) {
				DrawProjectedLine(sceneDrawList, gameObject, previousPoint, currentPoint, color, 1.5f);
			}
			previousPoint = currentPoint;
		}
	}

	void DrawBoxColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject, const EditorComponent& collider, ImU32 color) {
		Vector3 halfSize{
			collider.colliderSize.x * 0.5f,
			collider.colliderSize.y * 0.5f,
			collider.colliderSize.z * 0.5f};
		Vector3 corners[8] = {
			MakeLocalPoint(collider.colliderCenter, -halfSize.x, -halfSize.y, -halfSize.z),
			MakeLocalPoint(collider.colliderCenter, halfSize.x, -halfSize.y, -halfSize.z),
			MakeLocalPoint(collider.colliderCenter, halfSize.x, -halfSize.y, halfSize.z),
			MakeLocalPoint(collider.colliderCenter, -halfSize.x, -halfSize.y, halfSize.z),
			MakeLocalPoint(collider.colliderCenter, -halfSize.x, halfSize.y, -halfSize.z),
			MakeLocalPoint(collider.colliderCenter, halfSize.x, halfSize.y, -halfSize.z),
			MakeLocalPoint(collider.colliderCenter, halfSize.x, halfSize.y, halfSize.z),
			MakeLocalPoint(collider.colliderCenter, -halfSize.x, halfSize.y, halfSize.z)};
		const int32_t edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0},
			{4, 5}, {5, 6}, {6, 7}, {7, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}};

		for (const int32_t(&edge)[2] : edges) {
			DrawProjectedLine(sceneDrawList, gameObject, corners[edge[0]], corners[edge[1]], color, 1.5f);
		}
	}

	void DrawSphereColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject, const EditorComponent& collider, ImU32 color) {
		DrawLocalCircle(sceneDrawList, gameObject, collider.colliderCenter, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, collider.colliderRadius, color);
		DrawLocalCircle(sceneDrawList, gameObject, collider.colliderCenter, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, collider.colliderRadius, color);
		DrawLocalCircle(sceneDrawList, gameObject, collider.colliderCenter, Vector3{0.0f, 1.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, collider.colliderRadius, color);
	}

	void DrawCapsuleColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject, const EditorComponent& collider, ImU32 color) {
		float radius = (std::max)(collider.colliderRadius, 0.01f);
		float halfCylinderHeight = (std::max)((collider.colliderSize.y * 0.5f) - radius, 0.0f);
		Vector3 topCenter = MakeLocalPoint(collider.colliderCenter, 0.0f, halfCylinderHeight, 0.0f);
		Vector3 bottomCenter = MakeLocalPoint(collider.colliderCenter, 0.0f, -halfCylinderHeight, 0.0f);
		Vector3 sideOffsets[4] = {
			Vector3{radius, 0.0f, 0.0f},
			Vector3{-radius, 0.0f, 0.0f},
			Vector3{0.0f, 0.0f, radius},
			Vector3{0.0f, 0.0f, -radius}};

		DrawLocalCircle(sceneDrawList, gameObject, topCenter, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, radius, color);
		DrawLocalCircle(sceneDrawList, gameObject, bottomCenter, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, radius, color);

		for (const Vector3& sideOffset : sideOffsets) {
			Vector3 sideTop = MakeLocalPoint(topCenter, sideOffset.x, sideOffset.y, sideOffset.z);
			Vector3 sideBottom = MakeLocalPoint(bottomCenter, sideOffset.x, sideOffset.y, sideOffset.z);
			DrawProjectedLine(sceneDrawList, gameObject, sideTop, sideBottom, color, 1.5f);
		}
	}

	void DrawPhysicsDebug(ImDrawList* sceneDrawList) {
		const EditorPhysicsSettings& physicsSettings = g_editorScene.GetPhysicsSettings();
		if (!physicsSettings.drawColliderDebug) {
			return;
		}

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* collider = FindDebugCollider(gameObject);
			if (collider == nullptr) {
				continue;
			}

			ImU32 debugColor = GetPhysicsDebugColor(gameObject, *collider);
			if (collider->type == EditorComponentType::SphereCollider) {
				DrawSphereColliderDebug(sceneDrawList, gameObject, *collider, debugColor);
			}
			else if (collider->type == EditorComponentType::CapsuleCollider ||
			         collider->type == EditorComponentType::CharacterController) {
				DrawCapsuleColliderDebug(sceneDrawList, gameObject, *collider, debugColor);
			}
			else {
				// MeshCollider / TerrainCollider は Jolt 側も Box 近似で初期表示するため、SceneView でも外枠を表示する。
				DrawBoxColliderDebug(sceneDrawList, gameObject, *collider, debugColor);
			}
		}
	}

	bool IsProjectedPointInsideScene(const ImVec2& screenPosition, float margin) {
		return
			screenPosition.x >= g_editorSceneX - margin &&
			screenPosition.x <= g_editorSceneX + g_editorSceneWidth + margin &&
			screenPosition.y >= g_editorSceneY - margin &&
			screenPosition.y <= g_editorSceneY + g_editorSceneHeight + margin;
	}

	bool HasActiveSceneComponent(const EditorGameObject& gameObject, EditorComponentType componentType) {
		const EditorComponent* component = EditorComponentUtility::FindComponent(gameObject, componentType);
		return component != nullptr && component->isActive;
	}

	Transforms BuildSelectionPivotTransform(const std::vector<int32_t>& selectedGameObjectIds) {
		Transforms pivotTransform{};
		pivotTransform.scale = {1.0f, 1.0f, 1.0f};

		if (selectedGameObjectIds.empty()) {
			return pivotTransform;
		}

		float selectedObjectCount = 0.0f;
		for (int32_t gameObjectId : selectedGameObjectIds) {
			EditorGameObject* gameObject = g_editorScene.FindGameObject(gameObjectId);
			if (gameObject == nullptr) {
				continue;
			}

			pivotTransform.translate = Add(pivotTransform.translate, gameObject->translate);
			pivotTransform.rotate = Add(pivotTransform.rotate, gameObject->rotate);
			pivotTransform.scale = Add(pivotTransform.scale, gameObject->scale);
			selectedObjectCount += 1.0f;
		}

		if (selectedObjectCount <= 0.0f) {
			pivotTransform.scale = {1.0f, 1.0f, 1.0f};
			return pivotTransform;
		}

		float inverseSelectedObjectCount = 1.0f / selectedObjectCount;
		pivotTransform.translate = Multiply(inverseSelectedObjectCount, pivotTransform.translate);
		pivotTransform.rotate = Multiply(inverseSelectedObjectCount, pivotTransform.rotate);
		pivotTransform.scale = Multiply(inverseSelectedObjectCount, pivotTransform.scale);
		return pivotTransform;
	}

	void ApplyTransformDeltaToSelectedGameObjects(
		const std::vector<int32_t>& selectedGameObjectIds,
		const Transforms& beforeTransform,
		const Transforms& afterTransform) {
		const Vector3 translationDelta = Subtract(afterTransform.translate, beforeTransform.translate);
		const Vector3 rotationDelta = Subtract(afterTransform.rotate, beforeTransform.rotate);
		Vector3 scaleRatio{1.0f, 1.0f, 1.0f};
		scaleRatio.x = std::fabs(beforeTransform.scale.x) > 0.0001f ? afterTransform.scale.x / beforeTransform.scale.x : 1.0f;
		scaleRatio.y = std::fabs(beforeTransform.scale.y) > 0.0001f ? afterTransform.scale.y / beforeTransform.scale.y : 1.0f;
		scaleRatio.z = std::fabs(beforeTransform.scale.z) > 0.0001f ? afterTransform.scale.z / beforeTransform.scale.z : 1.0f;
		const Matrix4x4 deltaRotationMatrix = MakeAffineMatrix(
			Vector3{1.0f, 1.0f, 1.0f},
			rotationDelta,
			Vector3{0.0f, 0.0f, 0.0f});

		for (int32_t gameObjectId : selectedGameObjectIds) {
			EditorGameObject* gameObject = g_editorScene.FindGameObject(gameObjectId);
			if (gameObject == nullptr) {
				continue;
			}

			Vector3 localOffset = Subtract(gameObject->translate, beforeTransform.translate);
			localOffset.x *= scaleRatio.x;
			localOffset.y *= scaleRatio.y;
			localOffset.z *= scaleRatio.z;
			localOffset = Transform(localOffset, deltaRotationMatrix);

			gameObject->translate = Add(afterTransform.translate, localOffset);
			gameObject->rotate = Add(gameObject->rotate, rotationDelta);
			gameObject->scale = {
				(std::max)(0.01f, gameObject->scale.x * scaleRatio.x),
				(std::max)(0.01f, gameObject->scale.y * scaleRatio.y),
				(std::max)(0.01f, gameObject->scale.z * scaleRatio.z)};
		}
	}

	bool HasAnyActiveSceneComponent(EditorComponentType componentType) {
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (gameObject.isActive && HasActiveSceneComponent(gameObject, componentType)) {
				return true;
			}
		}

		return false;
	}

	void SelectGameObjectFromSceneIcon(int32_t gameObjectId) {
		// Scene 上の Light / Camera アイコンを押した時も、Hierarchy と同じ GameObject 選択へ揃える。
		SetSingleSelectedGameObject(gameObjectId);
		g_selectedPlacedSceneObjectIndex = -1;
		g_selectedAssetPath.clear();
		g_editorSelectionManager.SyncLegacySelection(
			g_selectedEditorGameObjectId,
			g_selectedSceneObject,
			g_selectedPlacedSceneObjectIndex);
	}

	Vector3 GetSceneIconForwardDirection(const Vector3& rotation) {
		const float cosPitch = std::cos(rotation.x);
		Vector3 forwardDirection{};
		forwardDirection.x = std::sin(rotation.y) * cosPitch;
		forwardDirection.y = -std::sin(rotation.x);
		forwardDirection.z = std::cos(rotation.y) * cosPitch;

		if (Length(forwardDirection) <= 0.0001f) {
			return {0.0f, 0.0f, 1.0f};
		}

		return Normalize(forwardDirection);
	}

	bool DrawSceneLightGameObjectIcon(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		if (!g_isLightGizmoVisible || !HasActiveSceneComponent(gameObject, EditorComponentType::Light)) {
			return false;
		}

		ProjectedScenePoint projectedPoint{};
		if (!TryProjectWorldPosition(gameObject.translate, projectedPoint) ||
			!IsProjectedPointInsideScene(projectedPoint.screenPosition, 42.0f)) {
			return false;
		}

		const bool isSelected = IsGameObjectSelected(gameObject.id);
		ImU32 iconColor = isSelected ? IM_COL32(255, 245, 120, 255) : IM_COL32(255, 220, 80, 255);
		sceneDrawList->AddCircleFilled(projectedPoint.screenPosition, 7.0f, iconColor);
		sceneDrawList->AddCircle(projectedPoint.screenPosition, 12.0f, iconColor, 16, 2.0f);

		for (int32_t rayIndex = 0; rayIndex < 8; ++rayIndex) {
			float angle = std::numbers::pi_v<float> * 2.0f * static_cast<float>(rayIndex) / 8.0f;
			ImVec2 rayStart{
				projectedPoint.screenPosition.x + std::cos(angle) * 14.0f,
				projectedPoint.screenPosition.y + std::sin(angle) * 14.0f
			};
			ImVec2 rayEnd{
				projectedPoint.screenPosition.x + std::cos(angle) * 20.0f,
				projectedPoint.screenPosition.y + std::sin(angle) * 20.0f
			};
			sceneDrawList->AddLine(rayStart, rayEnd, iconColor, 2.0f);
		}

		sceneDrawList->AddText(
			ImVec2(projectedPoint.screenPosition.x + 16.0f, projectedPoint.screenPosition.y - 8.0f),
			IM_COL32(255, 245, 180, 255),
			gameObject.name.c_str());

		ImGui::SetCursorScreenPos(
			ImVec2(projectedPoint.screenPosition.x - 24.0f, projectedPoint.screenPosition.y - 24.0f));
		ImGui::PushID(gameObject.id);
		ImGui::InvisibleButton("SceneLightGameObjectIcon", ImVec2(96.0f, 48.0f));
		bool isIconHot = ImGui::IsItemHovered() || ImGui::IsItemActive();

		if (ImGui::IsItemClicked()) {
			SelectGameObjectFromSceneIcon(gameObject.id);
		}

		ImGui::PopID();
		return isIconHot;
	}

	bool DrawSceneCameraGameObjectIcon(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		if (!g_isCameraGizmoVisible || !HasActiveSceneComponent(gameObject, EditorComponentType::Camera)) {
			return false;
		}

		ProjectedScenePoint projectedPoint{};
		if (!TryProjectWorldPosition(gameObject.translate, projectedPoint) ||
			!IsProjectedPointInsideScene(projectedPoint.screenPosition, 48.0f)) {
			return false;
		}

		const bool isSelected = IsGameObjectSelected(gameObject.id);
		ImU32 iconColor = isSelected ? IM_COL32(110, 230, 255, 255) : IM_COL32(180, 220, 255, 255);
		ImVec2 iconMin{projectedPoint.screenPosition.x - 16.0f, projectedPoint.screenPosition.y - 10.0f};
		ImVec2 iconMax{projectedPoint.screenPosition.x + 14.0f, projectedPoint.screenPosition.y + 10.0f};
		sceneDrawList->AddRect(iconMin, iconMax, iconColor, 2.0f, 0, 2.0f);
		sceneDrawList->AddTriangleFilled(
			ImVec2(iconMax.x, projectedPoint.screenPosition.y - 6.0f),
			ImVec2(iconMax.x + 14.0f, projectedPoint.screenPosition.y - 12.0f),
			ImVec2(iconMax.x + 14.0f, projectedPoint.screenPosition.y + 12.0f),
			IM_COL32(180, 220, 255, 210));

		Vector3 forwardDirection = GetSceneIconForwardDirection(gameObject.rotate);
		Vector3 forwardEnd = Add(gameObject.translate, Multiply(1.2f, forwardDirection));
		ProjectedScenePoint projectedForwardEnd{};
		if (TryProjectWorldPosition(forwardEnd, projectedForwardEnd)) {
			sceneDrawList->AddLine(projectedPoint.screenPosition, projectedForwardEnd.screenPosition, iconColor, 2.0f);
		}

		sceneDrawList->AddText(
			ImVec2(projectedPoint.screenPosition.x - 16.0f, projectedPoint.screenPosition.y + 18.0f),
			IM_COL32(180, 220, 255, 255),
			gameObject.name.c_str());

		ImGui::SetCursorScreenPos(
			ImVec2(projectedPoint.screenPosition.x - 24.0f, projectedPoint.screenPosition.y - 24.0f));
		ImGui::PushID(gameObject.id);
		ImGui::InvisibleButton("SceneCameraGameObjectIcon", ImVec2(96.0f, 56.0f));
		bool isIconHot = ImGui::IsItemHovered() || ImGui::IsItemActive();

		if (ImGui::IsItemClicked()) {
			SelectGameObjectFromSceneIcon(gameObject.id);
		}

		ImGui::PopID();
		return isIconHot;
	}

	bool DrawSceneLightAndCameraGameObjectIcons(ImDrawList* sceneDrawList) {
		if (!g_isSceneGizmoVisible) {
			return false;
		}

		bool isAnyIconHot = false;
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			isAnyIconHot = DrawSceneLightGameObjectIcon(sceneDrawList, gameObject) || isAnyIconHot;
			isAnyIconHot = DrawSceneCameraGameObjectIcon(sceneDrawList, gameObject) || isAnyIconHot;
		}

		return isAnyIconHot;
	}
}

void EditorSceneViewManager::Initialize() {
}

void EditorSceneViewManager::Update() {
}

void EditorSceneViewManager::Draw() {
#ifdef USE_IMGUI
	g_isSceneViewVisible = false;  // Draw 中に有効な矩形を取れたフレームだけ true にする。
	std::vector<EditorSceneObject>& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();  // editorSceneObjects は SceneObjectManager が持つ DirectX 描画用オブジェクト一覧。

	// SceneView は DirectX の絵を背面に出すため、ImGui の背景とスクロールを消す。
	constexpr ImGuiWindowFlags sceneWindowFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	//================================================================
	// SceneView ウィンドウとタブ
	//================================================================

	// 初回表示サイズは DirectX viewport と同じ見た目になるよう Scene 領域に合わせる。
	ImGui::SetNextWindowSize(
		ImVec2(g_editorSceneWidth, g_editorSceneHeight + kEditorSceneHeaderHeight),
		ImGuiCond_FirstUseEver);

	ImGui::Begin("シーン###SceneView", nullptr, sceneWindowFlags);  // ###SceneView は DockBuilder と同じ固定 ID。表示名だけ日本語にしている。

	ImGuiWindow* sceneWindow = ImGui::GetCurrentWindowRead();  // sceneWindow は Docking タブの表示状態まで含めて SceneView の実表示を判定するために使う。
	bool isSceneDockTabVisible =
		sceneWindow != nullptr &&
		!sceneWindow->Hidden &&
		!sceneWindow->SkipItems &&
		(!sceneWindow->DockIsActive || sceneWindow->DockTabIsVisible);  // Dock 中は選択タブだけ true にし、隠れたタブへ DirectX と DrawList を流さない。

	if (!isSceneDockTabVisible) {
		ImGui::End();
		return;
	}

	g_editorViewportTabIndex = 0;  // SceneView は編集専用。GameView は別 Dock ウィンドウで扱う。
	bool isSceneTabActive = true;  // SceneView 内では常にギズモ、範囲選択、ドロップ配置を有効にする。
	ImVec2 sceneContentPosition = ImGui::GetCursorScreenPos();  // sceneContentPosition はタブ下の描画開始位置。DirectX viewport の左上になる。
	ImVec2 sceneContentSize = ImGui::GetContentRegionAvail();  // sceneContentSize は Docking 後に残っている SceneView の描画可能サイズ。

	//================================================================
	// SceneView の描画範囲
	//================================================================

	g_editorSceneX = sceneContentPosition.x;  // DirectX と ImGui 補助描画が同じ位置に出るよう、SceneView の矩形を共有状態に保存する。
	g_editorSceneY = sceneContentPosition.y;

	g_editorSceneWidth = (std::max)(sceneContentSize.x, 240.0f);  // 幅と高さは最低値を持たせ、0 除算や極小 viewport を避ける。
	g_editorSceneHeight = (std::max)(sceneContentSize.y, 180.0f);
	g_isSceneViewVisible = true;

	g_viewport.TopLeftX = g_editorSceneX;  // viewport は DirectX12 の NDC から画面座標への変換範囲。
	g_viewport.TopLeftY = g_editorSceneY;
	g_viewport.Width = g_editorSceneWidth;
	g_viewport.Height = g_editorSceneHeight;

	g_scissorRect.left = static_cast<LONG>(g_editorSceneX);  // scissorRect は DirectX12 が SceneView の外へ描かないための切り取り矩形。
	g_scissorRect.top = static_cast<LONG>(g_editorSceneY);
	g_scissorRect.right = static_cast<LONG>(g_editorSceneX + g_editorSceneWidth);
	g_scissorRect.bottom = static_cast<LONG>(g_editorSceneY + g_editorSceneHeight);

	// 3D 用透視投影。アスペクト比は Docking 後の SceneView サイズから毎フレーム更新する。
	g_projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		g_editorSceneWidth / g_editorSceneHeight,
		0.1f,
		1000.0f);

	// Sprite 用正射影。2D 座標をウィンドウサイズ基準で扱う。
	g_spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		g_editorWindowWidth,
		g_editorWindowHeight,
		0.0f,
		100.0f);

	// cameraMatrix はエディターカメラの Transform から作るワールド行列。
	g_cameraMatrix = MakeAffineMatrix(
		g_cameraTransform.scale,
		g_cameraTransform.rotate,
		g_cameraTransform.translate);

	g_viewMatrix = Inverse(g_cameraMatrix);  // viewMatrix はカメラ行列の逆行列。ワールド座標をカメラ空間へ移す。

	// 左端 42px はツールバー領域として扱い、Scene 操作のクリック判定から外す。
	ImVec2 sceneInteractionMin{g_editorSceneX + 42.0f, g_editorSceneY};

	// sceneInteractionMax は Scene 操作を受け付ける右下座標。
	ImVec2 sceneInteractionMax{
		g_editorSceneX + g_editorSceneWidth,
		g_editorSceneY + g_editorSceneHeight
	};

	// isSceneHovered は Scene タブ上でのみカメラ移動や範囲選択を開始するための判定。
	bool isSceneHovered = isSceneTabActive && ImGui::IsMouseHoveringRect(
		sceneInteractionMin,
		sceneInteractionMax);

	auto drawSceneDropTarget = [&]() {
		ImRect sceneDropTargetRect(sceneInteractionMin, sceneInteractionMax);  // sceneDropTargetRect は Project アセットを Scene に落とせる矩形。
		ImGuiID sceneDropTargetId = ImGui::GetID("SceneDropTarget");  // sceneDropTargetId は BeginDragDropTargetCustom 用の固定 ID。

		// Scene タブが有効な時だけ、アセットドロップから GameObject を作る。
		if (isSceneTabActive && ImGui::BeginDragDropTargetCustom(sceneDropTargetRect, sceneDropTargetId)) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
				// droppedAsset は Project パネルから渡された resources 相対パス。
				std::string droppedAsset(
					static_cast<const char*>(payload->Data),
					static_cast<size_t>(payload->DataSize - 1));
				g_selectedAssetPath = droppedAsset;

				// droppedTextureIndex が 0 以上なら登録済みテクスチャなので Sprite として配置する。
				int32_t droppedTextureIndex =
					EditorAssetUtility::GetTextureIndex(g_editorTextureFilePaths, droppedAsset);

				if (droppedTextureIndex >= 0) {
					// 画像アセットは SpriteRenderer 付き GameObject として配置する。
					g_editorAssetFactory.CreateSpriteGameObject(
						droppedAsset,
						GetSpriteDropPosition(),
						g_selectedEditorGameObjectId,
						g_selectedPlacedSceneObjectIndex,
						g_selectedSceneObject);
					SetSingleSelectedGameObject(g_selectedEditorGameObjectId);
					g_previousSelectedEditorGameObjectId = -1;
				} else if (EditorAssetUtility::HasExtension(droppedAsset, ".obj") ||
				           EditorAssetUtility::HasExtension(droppedAsset, ".fbx")) {
					// obj/fbx は ModelRenderer 付き GameObject として配置する。
					g_editorAssetFactory.CreateModelGameObject(
						droppedAsset,
						GetModelDropPosition(),
						g_selectedEditorGameObjectId,
						g_selectedPlacedSceneObjectIndex,
						g_selectedSceneObject);
					SetSingleSelectedGameObject(g_selectedEditorGameObjectId);
					g_previousSelectedEditorGameObjectId = -1;
				}
			}

			ImGui::EndDragDropTarget();
		}
	};

	//================================================================
	// Scene カメラ操作
	//================================================================

	// マウスの右ドラッグ・中ドラッグ・ホイールで、エディターカメラ Transform を更新する。
	g_editorSceneCameraController.UpdateMouse(
		isSceneHovered,
		g_isSceneMiddleCameraDragging,
		g_isSceneRightCameraDragging,
		g_cameraTransform,
		g_editorCameraRotateSpeed,
		g_editorCameraPanSpeed,
		g_editorCameraWheelMoveSpeed);

	ImDrawList* sceneDrawList = ImGui::GetWindowDrawList();  // sceneDrawList は SceneView 上にガイド線・アイコン・選択矩形を重ねるための DrawList。
	bool canUseToolShortcut =
		isSceneHovered &&
		!g_isSceneMiddleCameraDragging &&
		!g_isSceneRightCameraDragging &&
		!ImGui::GetIO().WantTextInput;  // 文字入力中やカメラドラッグ中は W/E/R/T をギズモ切替に使わない。
	UpdateActiveToolFromShortcut(canUseToolShortcut);

	//================================================================
	// ガイド線とタブ説明
	//================================================================

	sceneDrawList->AddRect(
		ImVec2(g_editorSceneX, g_editorSceneY),
		ImVec2(g_editorSceneX + g_editorSceneWidth, g_editorSceneY + g_editorSceneHeight),
		IM_COL32(75, 95, 120, 255));
	sceneDrawList->AddText(
		ImVec2(g_editorSceneX + g_editorSceneWidth - 92.0f, g_editorSceneY + 6.0f),
		IM_COL32(180, 220, 255, 255),
		"Perspective");

	// Scene タブだけ床グリッドを表示する。Game / Asset Store では補助線を出さない。
	if (isSceneTabActive) {
		sceneDrawList->PushClipRect(
			ImVec2(g_editorSceneX, g_editorSceneY),
			ImVec2(g_editorSceneX + g_editorSceneWidth, g_editorSceneY + g_editorSceneHeight),
			true);

		for (int32_t gridIndex = -10; gridIndex <= 10; ++gridIndex) {
			// gridIndex == 0 は X/Z 軸なので色と太さを強くする。
			ImU32 gridColor = gridIndex == 0
				                  ? IM_COL32(120, 160, 210, 180)
				                  : IM_COL32(90, 110, 135, 95);

			// zLine は X 固定で Z 方向に伸びる線。ProjectWorldPosition で画面座標へ変換する。
			ProjectedScenePoint zLineStart{};
			ProjectedScenePoint zLineEnd{};
			bool hasZLineStart = TryProjectWorldPosition(Vector3{static_cast<float>(gridIndex), 0.0f, -10.0f}, zLineStart);
			bool hasZLineEnd = TryProjectWorldPosition(Vector3{static_cast<float>(gridIndex), 0.0f, 10.0f}, zLineEnd);

			// xLine は Z 固定で X 方向に伸びる線。
			ProjectedScenePoint xLineStart{};
			ProjectedScenePoint xLineEnd{};
			bool hasXLineStart = TryProjectWorldPosition(Vector3{-10.0f, 0.0f, static_cast<float>(gridIndex)}, xLineStart);
			bool hasXLineEnd = TryProjectWorldPosition(Vector3{10.0f, 0.0f, static_cast<float>(gridIndex)}, xLineEnd);

			bool isZLineSafe =
				std::fabs(zLineStart.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(zLineStart.ndcY) <= kProjectedLineNdcLimit &&
				std::fabs(zLineEnd.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(zLineEnd.ndcY) <= kProjectedLineNdcLimit;
			if (hasZLineStart && hasZLineEnd && isZLineSafe) {
				sceneDrawList->AddLine(zLineStart.screenPosition, zLineEnd.screenPosition, gridColor, gridIndex == 0 ? 2.0f : 1.0f);
			}

			bool isXLineSafe =
				std::fabs(xLineStart.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(xLineStart.ndcY) <= kProjectedLineNdcLimit &&
				std::fabs(xLineEnd.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(xLineEnd.ndcY) <= kProjectedLineNdcLimit;
			if (hasXLineStart && hasXLineEnd && isXLineSafe) {
				sceneDrawList->AddLine(xLineStart.screenPosition, xLineEnd.screenPosition, gridColor, gridIndex == 0 ? 2.0f : 1.0f);
			}
		}

		DrawPhysicsDebug(sceneDrawList);

		sceneDrawList->PopClipRect();
	}

	bool isGizmoHovered = false;  // isGizmoHovered はギズモ上クリックを範囲選択として扱わないためのフラグ。
	bool isGizmoActive = false;  // isGizmoActive はギズモ操作中に Scene 選択を開始しないためのフラグ。
	Transforms* selectedGizmoTransform = nullptr;  // selectedGizmoTransform は現在ギズモで動かす Transform の実体。
	EditorGameObject* selectedGameObjectGizmo = nullptr;  // Light / Camera など描画メッシュを持たない GameObject を直接動かす対象。
	Transforms selectedGameObjectGizmoTransform{};  // GameObject の translate / rotate / scale を ImGuizmo 用 Transform へ一時変換する。
	Transforms multiSelectionPivotTransform{};  // 複数選択時は、平均位置の仮想 Transform をギズモ中心として使う。
	bool hasSelectedGizmoTransform = false;  // hasSelectedGizmoTransform は nullptr と旧プレビュー選択を明確に分けるためのフラグ。
	bool isSceneToolBarHot = isSceneTabActive ? DrawSceneToolBar() : false;  // SceneView 左側の W/E/R/T ツールバーが触られているか

	//================================================================
	// ギズモ対象の決定
	//================================================================


	// 旧モデルプレビューを表示中なら、モデル Transform を直接ギズモ対象にする。
	if (g_isLegacyPreviewVisible && g_selectedSceneObject == 0) {
		selectedGizmoTransform = &g_transform;
		hasSelectedGizmoTransform = true;
	} else if (g_isLegacyPreviewVisible && g_selectedSceneObject == 1) {
		selectedGizmoTransform = &g_spriteTransform;  // 旧スプライトプレビューを表示中なら、Sprite Transform を直接ギズモ対象にする。
		hasSelectedGizmoTransform = true;
	}

	// 配置済み SceneObject が選択されている場合は、旧プレビューよりこちらを優先する。
	if (g_selectedPlacedSceneObjectIndex >= 0 &&
		g_selectedPlacedSceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size())) {
		// selectedPlacedSceneObject は DirectX 描画と Inspector が共有する配置データ。
		EditorSceneObject& selectedPlacedSceneObject =
			editorSceneObjects[static_cast<size_t>(g_selectedPlacedSceneObjectIndex)];
		selectedGizmoTransform = &selectedPlacedSceneObject.transform;
		hasSelectedGizmoTransform = true;
	}

	// Light / Camera / 空 GameObject は描画用 SceneObject を持たないので、GameObject の Transform を直接ギズモ対象にする。
	if (!hasSelectedGizmoTransform && g_selectedEditorGameObjectId >= 0) {
		selectedGameObjectGizmo = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
		if (selectedGameObjectGizmo != nullptr) {
			selectedGameObjectGizmoTransform.translate = selectedGameObjectGizmo->translate;
			selectedGameObjectGizmoTransform.rotate = selectedGameObjectGizmo->rotate;
			selectedGameObjectGizmoTransform.scale = selectedGameObjectGizmo->scale;
			selectedGizmoTransform = &selectedGameObjectGizmoTransform;
			hasSelectedGizmoTransform = true;
		}
	}

	if (g_selectedEditorGameObjectIds.size() >= 2) {
		multiSelectionPivotTransform = BuildSelectionPivotTransform(g_selectedEditorGameObjectIds);
		selectedGizmoTransform = &multiSelectionPivotTransform;
		selectedGameObjectGizmo = nullptr;
		hasSelectedGizmoTransform = true;
	}

	auto updateGizmoState = [&]() {
		isGizmoHovered = isGizmoHovered || ImGui::IsItemHovered();  // InvisibleButton の hover / active 状態も、範囲選択抑制に含める。
		isGizmoActive = isGizmoActive || ImGui::IsItemActive();
	};
	isGizmoHovered = isGizmoHovered || isSceneToolBarHot;
	isGizmoActive = isGizmoActive || isSceneToolBarHot;
	if (isSceneTabActive) {
		const bool isSceneIconHot = DrawSceneLightAndCameraGameObjectIcons(sceneDrawList);
		isGizmoHovered = isGizmoHovered || isSceneIconHot;
		isGizmoActive = isGizmoActive || isSceneIconHot;
	}

	if (isSceneTabActive) {
		//================================================================
		// Transform ギズモ
		//================================================================

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(sceneDrawList);
		ImGuizmo::SetRect(g_editorSceneX, g_editorSceneY, g_editorSceneWidth, g_editorSceneHeight);

		if (hasSelectedGizmoTransform && selectedGizmoTransform != nullptr) {
			ImGuizmo::OPERATION gizmoOperation = GetActiveGizmoOperation();  // gizmoOperation は移動・回転・拡縮・統合のどれを操作するかを表す。
			const Transforms originalGizmoTransform = *selectedGizmoTransform;  // 複数選択時は変換前との差分を各 GameObject へ配るため、編集前を保持する。

			// selectedWorldMatrix は選択 Transform を ImGuizmo が編集できる 4x4 行列にしたもの。
			Matrix4x4 selectedWorldMatrix = MakeAffineMatrix(
				selectedGizmoTransform->scale,
				selectedGizmoTransform->rotate,
				selectedGizmoTransform->translate);

			// isManipulated はギズモが行列を書き換えたフレームだけ true になる。
			bool isManipulated = ImGuizmo::Manipulate(
				&g_viewMatrix.matrix[0][0],
				&g_projectionMatrix.matrix[0][0],
				gizmoOperation,
				g_isGizmoLocalMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
				&selectedWorldMatrix.matrix[0][0],
				nullptr,
				g_isGizmoSnapEnabled ? g_gizmoSnapValues : nullptr);
			isGizmoHovered = isGizmoHovered || ImGuizmo::IsOver(gizmoOperation);
			isGizmoActive = isGizmoActive || ImGuizmo::IsUsing();

			if (isManipulated) {
				// ImGuizmo が返した行列を Transform の translate / rotate / scale に戻すための一時配列。
				float gizmoTranslation[3] = {};
				float gizmoRotationDegrees[3] = {};
				float gizmoScale[3] = {};
				ImGuizmo::DecomposeMatrixToComponents(
					&selectedWorldMatrix.matrix[0][0],
					gizmoTranslation,
					gizmoRotationDegrees,
					gizmoScale);

				constexpr float degreeToRadian = std::numbers::pi_v<float> / 180.0f;  // ImGuizmo は回転を degree で返すため、エンジン側の radian に変換する。
				selectedGizmoTransform->translate = {
					gizmoTranslation[0],
					gizmoTranslation[1],
					gizmoTranslation[2]
				};
				selectedGizmoTransform->rotate = {
					gizmoRotationDegrees[0] * degreeToRadian,
					gizmoRotationDegrees[1] * degreeToRadian,
					gizmoRotationDegrees[2] * degreeToRadian
				};

				// scale は 0 以下になると描画や逆行列で壊れるため、最小 0.01f に丸める。
				selectedGizmoTransform->scale = {
					(std::max)(0.01f, gizmoScale[0]),
					(std::max)(0.01f, gizmoScale[1]),
					(std::max)(0.01f, gizmoScale[2])
				};

				if (g_selectedEditorGameObjectIds.size() >= 2) {
					ApplyTransformDeltaToSelectedGameObjects(
						g_selectedEditorGameObjectIds,
						originalGizmoTransform,
						*selectedGizmoTransform);
					g_editorSceneSynchronizer.Update(
						g_editorTextureFilePaths,
						g_selectedPlacedSceneObjectIndex);
				}
				else if (selectedGameObjectGizmo != nullptr) {
					// SceneObject を持たない GameObject は、一時 Transform の結果をそのまま本体へ戻す。
					selectedGameObjectGizmo->translate = selectedGizmoTransform->translate;
					selectedGameObjectGizmo->rotate = selectedGizmoTransform->rotate;
					selectedGameObjectGizmo->scale = selectedGizmoTransform->scale;
				}
				else {
					g_editorSelectionManager.SyncSelectedPlacedObjectToGameObject(g_selectedPlacedSceneObjectIndex);  // SceneObject を動かした結果を、対応する GameObject の Transform Component へ同期する。
				}
			}
		}

		Matrix4x4 viewManipulateModelMatrix = MakeIdentity4x4();  // viewManipulateModelMatrix は ViewManipulate API に渡すダミー行列。
		Matrix4x4 viewManipulateViewMatrix = g_viewMatrix;  // viewManipulateViewMatrix は操作結果を受け取るカメラ View 行列。

		// 右上の小さい方位ギズモ。ドラッグでカメラ回転を変更する。
		ImGuizmo::ViewManipulate(
			&viewManipulateViewMatrix.matrix[0][0],
			&g_projectionMatrix.matrix[0][0],
			ImGuizmo::ROTATE,
			ImGuizmo::LOCAL,
			&viewManipulateModelMatrix.matrix[0][0],
			4.0f,
			ImVec2(g_editorSceneX + g_editorSceneWidth - 96.0f, g_editorSceneY + 12.0f),
			ImVec2(78.0f, 78.0f),
			IM_COL32(20, 26, 34, 190));

		isGizmoHovered = isGizmoHovered || ImGuizmo::IsViewManipulateHovered();
		isGizmoActive = isGizmoActive || ImGuizmo::IsUsingViewManipulate();

		if (ImGuizmo::IsUsingViewManipulate()) {
			Matrix4x4 viewManipulateCameraMatrix = Inverse(viewManipulateViewMatrix);  // View 行列を Camera ワールド行列に戻してから Transform に分解する。
			float cameraTranslation[3] = {};
			float cameraRotationDegrees[3] = {};
			float cameraScale[3] = {};
			ImGuizmo::DecomposeMatrixToComponents(
				&viewManipulateCameraMatrix.matrix[0][0],
				cameraTranslation,
				cameraRotationDegrees,
				cameraScale);

			constexpr float degreeToRadian = std::numbers::pi_v<float> / 180.0f;  // ViewManipulate も degree を返すため、エンジン側の radian に変換する。
			g_cameraTransform.translate = {
				cameraTranslation[0],
				cameraTranslation[1],
				cameraTranslation[2]
			};
			g_cameraTransform.rotate = {
				cameraRotationDegrees[0] * degreeToRadian,
				cameraRotationDegrees[1] * degreeToRadian,
				cameraRotationDegrees[2] * degreeToRadian
			};
			g_cameraTransform.scale = {1.0f, 1.0f, 1.0f};

			// 変更後のカメラ Transform から、SceneView 用の行列を同じフレーム内で更新する。
			g_cameraMatrix = MakeAffineMatrix(
				g_cameraTransform.scale,
				g_cameraTransform.rotate,
				g_cameraTransform.translate);
			g_viewMatrix = Inverse(g_cameraMatrix);
		}

		if (g_isSceneAssistVisible) {
			// assistMin / assistMax は Scene 操作説明の背景ボックス範囲。
			ImVec2 assistMin{g_editorSceneX + 52.0f, g_editorSceneY + 10.0f};
			ImVec2 assistMax{assistMin.x + 430.0f, assistMin.y + 66.0f};
			sceneDrawList->AddRectFilled(assistMin, assistMax, IM_COL32(16, 22, 30, 185), 6.0f);
			sceneDrawList->AddRect(assistMin, assistMax, IM_COL32(88, 105, 125, 180), 6.0f);
			sceneDrawList->AddText(
				ImVec2(assistMin.x + 10.0f, assistMin.y + 8.0f),
				IM_COL32(235, 240, 245, 255),
				"Scene操作: 右ドラッグ=回転 / 中ドラッグ=平行移動 / ホイール=前後");
			sceneDrawList->AddText(
				ImVec2(assistMin.x + 10.0f, assistMin.y + 30.0f),
				IM_COL32(170, 215, 255, 255),
				"オブジェクト: 左ドラッグ=範囲選択 / ギズモ軸=直接編集");

			// gizmoAssistText は現在のツール・座標系・スナップ状態を 1 行にまとめる。
			char gizmoAssistText[128] = {};
			std::snprintf(
				gizmoAssistText,
				sizeof(gizmoAssistText),
				"Tool: %s / Mode: %s / Snap: %s",
				GetActiveEditorToolName(),
				g_isGizmoLocalMode ? "Local" : "World",
				g_isGizmoSnapEnabled ? "On" : "Off");
			sceneDrawList->AddText(
				ImVec2(assistMin.x + 10.0f, assistMin.y + 50.0f),
				IM_COL32(200, 210, 220, 255),
				gizmoAssistText);
		}
	}

	if (isSceneTabActive &&
		g_isSceneGizmoVisible &&
		g_isLightGizmoVisible &&
		!HasAnyActiveSceneComponent(EditorComponentType::Light)) {
		//================================================================
		// ライトアイコン
		//================================================================

		ImVec2 lightIconPosition = ProjectWorldPosition(g_directionalLightIconPosition);  // lightIconPosition は DirectionalLight のワールド位置を SceneView 座標へ投影した位置。
		ImU32 lightIconColor = IM_COL32(255, 220, 80, 255);  // lightIconColor は太陽アイコン本体と光線に使う色。
		sceneDrawList->AddCircleFilled(lightIconPosition, 8.0f, lightIconColor);

		for (int32_t rayIndex = 0; rayIndex < 8; ++rayIndex) {
			// angle は 8 本の光線を円周上に均等配置するための角度。
			float angle = std::numbers::pi_v<float> * 2.0f *
				static_cast<float>(rayIndex) / 8.0f;

			// rayStart / rayEnd は太陽アイコンから外側へ伸びる短い線分。
			ImVec2 rayStart{
				lightIconPosition.x + std::cos(angle) * 12.0f,
				lightIconPosition.y + std::sin(angle) * 12.0f
			};
			ImVec2 rayEnd{
				lightIconPosition.x + std::cos(angle) * 18.0f,
				lightIconPosition.y + std::sin(angle) * 18.0f
			};
			sceneDrawList->AddLine(rayStart, rayEnd, lightIconColor, 2.0f);
		}

		sceneDrawList->AddText(
			ImVec2(lightIconPosition.x + 14.0f, lightIconPosition.y - 8.0f),
			IM_COL32(255, 245, 180, 255),
			"Light");
		ImGui::SetCursorScreenPos(ImVec2(lightIconPosition.x - 20.0f, lightIconPosition.y - 20.0f));
		ImGui::InvisibleButton("DirectionalLightIcon", ImVec2(80.0f, 40.0f));
		updateGizmoState();

		// ライトアイコンをクリックしたら、Inspector の対象を DirectionalLight に切り替える。
		if (ImGui::IsItemClicked()) {
			ClearSelectedGameObjects();
			g_selectedPlacedSceneObjectIndex = -1;
			g_selectedSceneObject = 2;
		}
	}

	if (isSceneTabActive && g_isSceneGizmoVisible && g_isCameraGizmoVisible) {
		//================================================================
		// カメラアイコン
		//================================================================

		// cameraIconPosition は SceneView 右上に固定表示する DebugCamera アイコンの左上座標。
		ImVec2 cameraIconPosition{
			g_editorSceneX + g_editorSceneWidth - 86.0f,
			g_editorSceneY + 104.0f
		};
		sceneDrawList->AddRect(
			ImVec2(cameraIconPosition.x, cameraIconPosition.y),
			ImVec2(cameraIconPosition.x + 30.0f, cameraIconPosition.y + 18.0f),
			IM_COL32(180, 220, 255, 255),
			2.0f,
			0,
			2.0f);
		sceneDrawList->AddTriangleFilled(
			ImVec2(cameraIconPosition.x + 30.0f, cameraIconPosition.y + 4.0f),
			ImVec2(cameraIconPosition.x + 42.0f, cameraIconPosition.y),
			ImVec2(cameraIconPosition.x + 42.0f, cameraIconPosition.y + 22.0f),
			IM_COL32(180, 220, 255, 210));
		sceneDrawList->AddText(
			ImVec2(cameraIconPosition.x - 6.0f, cameraIconPosition.y + 24.0f),
			IM_COL32(180, 220, 255, 255),
			"SceneCam");
		ImGui::SetCursorScreenPos(ImVec2(cameraIconPosition.x - 8.0f, cameraIconPosition.y - 8.0f));
		ImGui::InvisibleButton("DebugCameraIcon", ImVec2(68.0f, 54.0f));
		updateGizmoState();

		// カメラアイコンをクリックしたら、Inspector の対象を DebugCamera に切り替える。
		if (ImGui::IsItemClicked()) {
			ClearSelectedGameObjects();
			g_selectedPlacedSceneObjectIndex = -1;
			g_selectedSceneObject = 3;
		}
	}

	// 左クリック開始時、ギズモ上でなければ Scene 範囲選択を開始する。
	if (isSceneHovered && !isGizmoHovered && !isGizmoActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		g_isSceneRangeSelecting = true;
		g_sceneRangeStart = ImGui::GetIO().MousePos;
		g_sceneRangeEnd = g_sceneRangeStart;
	}

	if (g_isSceneRangeSelecting) {
		//================================================================
		// 範囲選択
		//================================================================

		g_sceneRangeEnd = ImGui::GetIO().MousePos;  // g_sceneRangeEnd はドラッグ中の現在マウス位置。

		// rangeMin / rangeMax はドラッグ方向に関係なく左上・右下になるよう min/max で作る。
		ImVec2 rangeMin{
			(std::min)(g_sceneRangeStart.x, g_sceneRangeEnd.x),
			(std::min)(g_sceneRangeStart.y, g_sceneRangeEnd.y)
		};
		ImVec2 rangeMax{
			(std::max)(g_sceneRangeStart.x, g_sceneRangeEnd.x),
			(std::max)(g_sceneRangeStart.y, g_sceneRangeEnd.y)
		};
		sceneDrawList->AddRectFilled(rangeMin, rangeMax, IM_COL32(80, 150, 255, 40));
		sceneDrawList->AddRect(rangeMin, rangeMax, IM_COL32(80, 150, 255, 220), 0.0f, 0, 2.0f);

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			float rangeWidth = rangeMax.x - rangeMin.x;  // rangeWidth / rangeHeight はクリックとドラッグ選択を区別するための矩形サイズ。
			float rangeHeight = rangeMax.y - rangeMin.y;

			bool hasRangeArea = rangeWidth >= 4.0f && rangeHeight >= 4.0f;  // 4px 未満は誤操作を避けるため範囲選択として扱わない。
			ImVec2 modelScreenPosition = ProjectWorldPosition(g_transform.translate);  // 旧モデルプレビューも範囲選択できるように、モデル中心を画面座標へ投影する。

			// isModelInRange は旧モデルプレビューの中心が選択矩形内にあるかの判定。
			bool isModelInRange =
				modelScreenPosition.x >= rangeMin.x &&
				modelScreenPosition.x <= rangeMax.x &&
				modelScreenPosition.y >= rangeMin.y &&
				modelScreenPosition.y <= rangeMax.y;

			if (hasRangeArea && isModelInRange) {
				ClearSelectedGameObjects();
				g_selectedPlacedSceneObjectIndex = -1;
				g_selectedSceneObject = 0;
			}

			// 旧モデルが選ばれなかった時だけ、GameObject を複数選択できるように範囲選択結果を配列へ入れる。
			if (hasRangeArea && !isModelInRange) {
				std::vector<int32_t> rangeSelectedGameObjectIds;

				for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
					if (!gameObject.isActive) {
						continue;
					}

					ProjectedScenePoint projectedPoint{};
					if (!TryProjectWorldPosition(gameObject.translate, projectedPoint)) {
						continue;
					}

					bool isGameObjectInRange =
						projectedPoint.screenPosition.x >= rangeMin.x &&
						projectedPoint.screenPosition.x <= rangeMax.x &&
						projectedPoint.screenPosition.y >= rangeMin.y &&
						projectedPoint.screenPosition.y <= rangeMax.y;
					if (isGameObjectInRange) {
						rangeSelectedGameObjectIds.push_back(gameObject.id);
					}
				}

				if (!rangeSelectedGameObjectIds.empty()) {
					SetSelectedGameObjectIds(rangeSelectedGameObjectIds);
					g_editorSelectionManager.SyncLegacySelection(
						g_selectedEditorGameObjectId,
						g_selectedSceneObject,
						g_selectedPlacedSceneObjectIndex);
				}
			}

			g_isSceneRangeSelecting = false;  // マウスを離したので範囲選択のドラッグ状態を終了する。
		}
	}

	drawSceneDropTarget();  // SceneView 最後にドロップターゲットを処理して、Project からのアセット配置を受ける。
	ImGui::End();
#endif
}
