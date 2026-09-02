#pragma warning(disable : 4189 4514)

#include "EditorSceneViewManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "EditorTeamCollaborationManager.h"
#include "Log.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

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

	bool DrawPhysicsDebugSettingsButton(const ImVec2& position) {
		EditorPhysicsSettings& physicsSettings = g_editorScene.GetPhysicsSettings();
		ImGui::SetCursorScreenPos(position);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.17f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.58f, 1.0f, 1.0f));

		if (ImGui::Button("物理", ImVec2(52.0f, 30.0f))) {
			ImGui::OpenPopup("物理デバッグ表示");
		}
		bool isHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Scene View の物理表示を種類別に変更します");
		}

		ImGui::PopStyleColor(3);

		if (ImGui::BeginPopup("物理デバッグ表示")) {
			ImGui::TextUnformatted("表示する種類");
			ImGui::Separator();
			ImGui::Checkbox("当たり判定の形", &physicsSettings.drawColliderDebug);
			ImGui::Checkbox("速度 / 角速度", &physicsSettings.drawVelocityDebug);
			ImGui::Checkbox("力 / 場の向き", &physicsSettings.drawForceDirectionDebug);
			ImGui::Checkbox("影響範囲 / 流体領域", &physicsSettings.drawFieldVolumeDebug);
			ImGui::Checkbox("ばね / Joint 接続", &physicsSettings.drawConnectionDebug);
			ImGui::Checkbox("接触点 / 法線", &physicsSettings.drawContactDebug);
			ImGui::Checkbox("Ray / ShapeCast", &physicsSettings.drawCastDebug);
			ImGui::Separator();
			ImGui::Checkbox("選択中だけ表示", &physicsSettings.drawSelectedOnlyDebug);
			ImGui::DragFloat("ベクトル倍率", &physicsSettings.debugVectorScale, 0.01f, 0.01f, 10.0f, "%.2f");
			physicsSettings.debugVectorScale = (std::clamp)(physicsSettings.debugVectorScale, 0.01f, 10.0f);
			isHot = true;
			ImGui::EndPopup();
		}

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
		toolPosition.y += toolButtonStep;
		isToolBarHot |= DrawPhysicsDebugSettingsButton(toolPosition);

		return isToolBarHot;
	}

	bool IsPhysicsDebugColliderType(EditorComponentType componentType) {
		return
			componentType == EditorComponentType::AutoConvexCollision ||
			componentType == EditorComponentType::BoxCollider ||
			componentType == EditorComponentType::SphereCollider ||
			componentType == EditorComponentType::CapsuleCollider ||
			componentType == EditorComponentType::MeshCollider ||
			componentType == EditorComponentType::TerrainCollider ||
			componentType == EditorComponentType::WheelCollider ||
			componentType == EditorComponentType::CharacterController;
	}

	bool IsPhysicsDebugJointType(EditorComponentType componentType) {
		return
			componentType == EditorComponentType::HingeJoint ||
			componentType == EditorComponentType::FixedJoint ||
			componentType == EditorComponentType::SpringJoint ||
			componentType == EditorComponentType::ConfigurableJoint ||
			componentType == EditorComponentType::CharacterJoint;
	}

	const EditorComponent* FindActiveComponent(
		const EditorGameObject& gameObject,
		EditorComponentType componentType) {
		const EditorComponent* component = EditorComponentUtility::FindComponent(gameObject, componentType);
		if (component == nullptr || !component->isActive) {
			return nullptr;
		}

		return component;
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
		const Matrix4x4 worldMatrix = g_editorScene.GetWorldMatrix(gameObject.id);
		return Transform(localPoint, worldMatrix);
	}

	Vector3 GetGameObjectWorldPosition(const EditorGameObject& gameObject) {
		const Matrix4x4 worldMatrix = g_editorScene.GetWorldMatrix(gameObject.id);
		return Vector3{
			worldMatrix.matrix[3][0],
			worldMatrix.matrix[3][1],
			worldMatrix.matrix[3][2]};
	}

	int32_t ResolveRailPointIndex(int32_t pointIndex, int32_t pointCount, bool isLooping) {
		if (isLooping) {
			const int32_t wrappedIndex = pointIndex % pointCount;
			return wrappedIndex < 0 ? wrappedIndex + pointCount : wrappedIndex;
		}

		return (std::clamp)(pointIndex, 0, pointCount - 1);
	}

	Vector3 EvaluateRailCatmullRom(
		const Vector3& firstPoint,
		const Vector3& secondPoint,
		const Vector3& thirdPoint,
		const Vector3& fourthPoint,
		float normalizedTime) {
		const float squaredTime = normalizedTime * normalizedTime;
		const float cubedTime = squaredTime * normalizedTime;
		return {
			0.5f * (
				2.0f * secondPoint.x +
				(-firstPoint.x + thirdPoint.x) * normalizedTime +
				(2.0f * firstPoint.x - 5.0f * secondPoint.x + 4.0f * thirdPoint.x - fourthPoint.x) * squaredTime +
				(-firstPoint.x + 3.0f * secondPoint.x - 3.0f * thirdPoint.x + fourthPoint.x) * cubedTime),
			0.5f * (
				2.0f * secondPoint.y +
				(-firstPoint.y + thirdPoint.y) * normalizedTime +
				(2.0f * firstPoint.y - 5.0f * secondPoint.y + 4.0f * thirdPoint.y - fourthPoint.y) * squaredTime +
				(-firstPoint.y + 3.0f * secondPoint.y - 3.0f * thirdPoint.y + fourthPoint.y) * cubedTime),
			0.5f * (
				2.0f * secondPoint.z +
				(-firstPoint.z + thirdPoint.z) * normalizedTime +
				(2.0f * firstPoint.z - 5.0f * secondPoint.z + 4.0f * thirdPoint.z - fourthPoint.z) * squaredTime +
				(-firstPoint.z + 3.0f * secondPoint.z - 3.0f * thirdPoint.z + fourthPoint.z) * cubedTime)};
	}

	const EditorComponent* FindRailMovementForPath(int32_t railPathGameObjectId) {
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* railMovement = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::RailMovement);

			if (railMovement != nullptr && railMovement->railPathGameObjectId == railPathGameObjectId) {
				return railMovement;
			}
		}

		return nullptr;
	}

	bool IsRailPathObject(const EditorGameObject& gameObject) {
		if (gameObject.children.size() < 2u) {
			return false;
		}

		if (FindRailMovementForPath(gameObject.id) != nullptr) {
			return true;
		}

		int32_t namedPointCount = 0;

		for (const int32_t childGameObjectId : gameObject.children) {
			const EditorGameObject* childGameObject = g_editorScene.FindGameObject(childGameObjectId);

			if (childGameObject != nullptr && childGameObject->name.rfind("Point", 0u) == 0u) {
				namedPointCount++;
			}
		}

		return namedPointCount >= 2;
	}

	int32_t ResolveSelectedRailPathGameObjectId() {
		const EditorGameObject* selectedGameObject =
			g_editorScene.FindGameObject(g_selectedEditorGameObjectId);

		if (selectedGameObject == nullptr) {
			return -1;
		}

		const EditorComponent* railMovement = EditorComponentUtility::FindComponent(
			*selectedGameObject,
			EditorComponentType::RailMovement);

		if (railMovement != nullptr && railMovement->railPathGameObjectId >= 0) {
			return railMovement->railPathGameObjectId;
		}

		if (IsRailPathObject(*selectedGameObject)) {
			return selectedGameObject->id;
		}

		const EditorGameObject* parentGameObject =
			g_editorScene.FindGameObject(selectedGameObject->parentId);
		return parentGameObject != nullptr && IsRailPathObject(*parentGameObject) ?
			parentGameObject->id : -1;
	}

	bool BuildRailPreviewSamples(
		const EditorGameObject& railPathGameObject,
		const EditorComponent* railMovement,
		std::vector<Vector3>& controlPointPositions,
		std::vector<Vector3>& railSamples) {
		controlPointPositions.clear();
		railSamples.clear();

		for (const int32_t childGameObjectId : railPathGameObject.children) {
			const EditorGameObject* controlPointGameObject =
				g_editorScene.FindGameObject(childGameObjectId);

			if (controlPointGameObject != nullptr && controlPointGameObject->isActive) {
				controlPointPositions.push_back(GetGameObjectWorldPosition(*controlPointGameObject));
			}
		}

		if (controlPointPositions.size() < 2u) {
			return false;
		}

		const bool isLooping = railMovement != nullptr && railMovement->railLoop;
		const bool usesSmoothCurve = railMovement == nullptr || railMovement->railUseSmoothCurve;
		const int32_t controlPointCount = static_cast<int32_t>(controlPointPositions.size());
		const int32_t segmentCount = isLooping ? controlPointCount : controlPointCount - 1;
		const int32_t samplesPerSegment = usesSmoothCurve ? 16 : 1;
		railSamples.push_back(controlPointPositions.front());

		for (int32_t segmentIndex = 0; segmentIndex < segmentCount; segmentIndex++) {
			const int32_t firstIndex = ResolveRailPointIndex(segmentIndex - 1, controlPointCount, isLooping);
			const int32_t secondIndex = ResolveRailPointIndex(segmentIndex, controlPointCount, isLooping);
			const int32_t thirdIndex = ResolveRailPointIndex(segmentIndex + 1, controlPointCount, isLooping);
			const int32_t fourthIndex = ResolveRailPointIndex(segmentIndex + 2, controlPointCount, isLooping);

			for (int32_t sampleIndex = 1; sampleIndex <= samplesPerSegment; sampleIndex++) {
				const float normalizedTime =
					static_cast<float>(sampleIndex) / static_cast<float>(samplesPerSegment);
				railSamples.push_back(usesSmoothCurve ?
					EvaluateRailCatmullRom(
						controlPointPositions[static_cast<size_t>(firstIndex)],
						controlPointPositions[static_cast<size_t>(secondIndex)],
						controlPointPositions[static_cast<size_t>(thirdIndex)],
						controlPointPositions[static_cast<size_t>(fourthIndex)],
						normalizedTime) :
					controlPointPositions[static_cast<size_t>(thirdIndex)]);
			}
		}

		return railSamples.size() >= 2u;
	}

	void DrawWorldProjectedLine(
		ImDrawList* sceneDrawList,
		const Vector3& worldStart,
		const Vector3& worldEnd,
		ImU32 color,
		float thickness) {
		ProjectedScenePoint projectedStart{};
		ProjectedScenePoint projectedEnd{};
		bool hasScreenStart = TryProjectWorldPosition(worldStart, projectedStart);
		bool hasScreenEnd = TryProjectWorldPosition(worldEnd, projectedEnd);

		if (!hasScreenStart || !hasScreenEnd) {
			return;
		}

		if (std::fabs(projectedStart.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedStart.ndcY) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcY) > kProjectedLineNdcLimit) {
			return;
		}

		sceneDrawList->AddLine(projectedStart.screenPosition, projectedEnd.screenPosition, color, thickness);
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
		DrawWorldProjectedLine(sceneDrawList, worldStart, worldEnd, color, thickness);
	}

	void DrawWorldArrowBetween(
		ImDrawList* sceneDrawList,
		const Vector3& worldStart,
		const Vector3& worldEnd,
		ImU32 color,
		const char* label) {
		ProjectedScenePoint projectedStart{};
		ProjectedScenePoint projectedEnd{};
		if (!TryProjectWorldPosition(worldStart, projectedStart) ||
			!TryProjectWorldPosition(worldEnd, projectedEnd)) {
			return;
		}

		if (std::fabs(projectedStart.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedStart.ndcY) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcX) > kProjectedLineNdcLimit ||
			std::fabs(projectedEnd.ndcY) > kProjectedLineNdcLimit) {
			return;
		}

		const float screenVectorX = projectedEnd.screenPosition.x - projectedStart.screenPosition.x;
		const float screenVectorY = projectedEnd.screenPosition.y - projectedStart.screenPosition.y;
		const float screenLength = std::sqrt((screenVectorX * screenVectorX) + (screenVectorY * screenVectorY));
		if (screenLength <= 1.0f) {
			return;
		}

		const float normalizedX = screenVectorX / screenLength;
		const float normalizedY = screenVectorY / screenLength;
		constexpr float kArrowHeadLength = 9.0f;
		constexpr float kArrowHeadWidth = 4.0f;
		const ImVec2 arrowBase{
			projectedEnd.screenPosition.x - (normalizedX * kArrowHeadLength),
			projectedEnd.screenPosition.y - (normalizedY * kArrowHeadLength)};
		const ImVec2 arrowLeft{
			arrowBase.x - (normalizedY * kArrowHeadWidth),
			arrowBase.y + (normalizedX * kArrowHeadWidth)};
		const ImVec2 arrowRight{
			arrowBase.x + (normalizedY * kArrowHeadWidth),
			arrowBase.y - (normalizedX * kArrowHeadWidth)};

		sceneDrawList->AddLine(projectedStart.screenPosition, projectedEnd.screenPosition, color, 2.0f);
		sceneDrawList->AddTriangleFilled(projectedEnd.screenPosition, arrowLeft, arrowRight, color);
		if (label != nullptr && label[0] != '\0') {
			sceneDrawList->AddText(
				ImVec2(projectedEnd.screenPosition.x + 5.0f, projectedEnd.screenPosition.y + 3.0f),
				color,
				label);
		}
	}

	void DrawWorldVectorArrow(
		ImDrawList* sceneDrawList,
		const Vector3& worldStart,
		const Vector3& vector,
		float displayScale,
		ImU32 color,
		const char* label) {
		const float vectorLength = Length(vector);
		if (vectorLength <= kProjectionEpsilon) {
			return;
		}

		const float displayLength = (std::clamp)(vectorLength * displayScale, 0.3f, 20.0f);
		const Vector3 worldEnd = Add(worldStart, Multiply(displayLength, Normalize(vector)));
		DrawWorldArrowBetween(sceneDrawList, worldStart, worldEnd, color, label);
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

	void DrawWorldCircle(
		ImDrawList* sceneDrawList,
		const Vector3& center,
		const Vector3& axisA,
		const Vector3& axisB,
		float radius,
		ImU32 color) {
		constexpr int32_t kSegmentCount = 24;
		Vector3 previousPoint{};

		for (int32_t segmentIndex = 0; segmentIndex <= kSegmentCount; ++segmentIndex) {
			const float angle = std::numbers::pi_v<float> * 2.0f *
				static_cast<float>(segmentIndex) / static_cast<float>(kSegmentCount);
			const Vector3 currentPoint{
				center.x + (axisA.x * std::cos(angle) + axisB.x * std::sin(angle)) * radius,
				center.y + (axisA.y * std::cos(angle) + axisB.y * std::sin(angle)) * radius,
				center.z + (axisA.z * std::cos(angle) + axisB.z * std::sin(angle)) * radius};

			if (segmentIndex > 0) {
				DrawWorldProjectedLine(sceneDrawList, previousPoint, currentPoint, color, 1.25f);
			}
			previousPoint = currentPoint;
		}
	}

	void DrawWorldSphere(ImDrawList* sceneDrawList, const Vector3& center, float radius, ImU32 color) {
		if (radius <= 0.0f) {
			return;
		}

		DrawWorldCircle(sceneDrawList, center, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, radius, color);
		DrawWorldCircle(sceneDrawList, center, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, radius, color);
		DrawWorldCircle(sceneDrawList, center, Vector3{0.0f, 1.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}, radius, color);
	}

	void DrawWorldMarker(ImDrawList* sceneDrawList, const Vector3& worldPosition, ImU32 color) {
		ProjectedScenePoint projectedPoint{};
		if (!TryProjectWorldPosition(worldPosition, projectedPoint)) {
			return;
		}

		sceneDrawList->AddCircleFilled(projectedPoint.screenPosition, 4.0f, color);
		sceneDrawList->AddCircle(projectedPoint.screenPosition, 7.0f, color, 12, 1.5f);
	}

	void DrawRailPathDebug(ImDrawList* sceneDrawList) {
		const int32_t selectedRailPathGameObjectId = ResolveSelectedRailPathGameObjectId();
		std::vector<int32_t> railPathGameObjectIds;

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* railMovement = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::RailMovement);

			if (railMovement == nullptr || railMovement->railPathGameObjectId < 0 ||
				std::find(
					railPathGameObjectIds.begin(),
					railPathGameObjectIds.end(),
					railMovement->railPathGameObjectId) != railPathGameObjectIds.end()) {
				continue;
			}

			railPathGameObjectIds.push_back(railMovement->railPathGameObjectId);
		}

		if (selectedRailPathGameObjectId >= 0 &&
			std::find(
				railPathGameObjectIds.begin(),
				railPathGameObjectIds.end(),
				selectedRailPathGameObjectId) == railPathGameObjectIds.end()) {
			railPathGameObjectIds.push_back(selectedRailPathGameObjectId);
		}

		for (const int32_t railPathGameObjectId : railPathGameObjectIds) {
			const EditorGameObject* railPathGameObject =
				g_editorScene.FindGameObject(railPathGameObjectId);
			const EditorComponent* railMovement = FindRailMovementForPath(railPathGameObjectId);

			if (railPathGameObject == nullptr) {
				continue;
			}

			std::vector<Vector3> controlPointPositions;
			std::vector<Vector3> railSamples;

			if (!BuildRailPreviewSamples(
					*railPathGameObject,
					railMovement,
					controlPointPositions,
					railSamples)) {
				continue;
			}

			const bool isSelectedPath = railPathGameObjectId == selectedRailPathGameObjectId;
			const ImU32 railColor = isSelectedPath ?
				IM_COL32(255, 190, 70, 255) : IM_COL32(55, 205, 235, 150);
			const float railThickness = isSelectedPath ? 3.5f : 1.75f;

			for (size_t sampleIndex = 1u; sampleIndex < railSamples.size(); sampleIndex++) {
				DrawWorldProjectedLine(
					sceneDrawList,
					railSamples[sampleIndex - 1u],
					railSamples[sampleIndex],
					railColor,
					railThickness);
			}

			if (!isSelectedPath) {
				continue;
			}

			for (size_t controlPointIndex = 0u;
				controlPointIndex < controlPointPositions.size();
				controlPointIndex++) {
				const Vector3& controlPointPosition = controlPointPositions[controlPointIndex];
				DrawWorldMarker(sceneDrawList, controlPointPosition, IM_COL32(255, 225, 105, 255));
				ProjectedScenePoint projectedPoint{};

				if (TryProjectWorldPosition(controlPointPosition, projectedPoint)) {
					const std::string pointLabel = "P" + std::to_string(controlPointIndex);
					sceneDrawList->AddText(
						ImVec2(projectedPoint.screenPosition.x + 9.0f, projectedPoint.screenPosition.y - 14.0f),
						IM_COL32(255, 235, 150, 255),
						pointLabel.c_str());
				}
			}

			constexpr size_t kGuideSampleInterval = 16u;

			for (size_t sampleIndex = 0u;
				sampleIndex < railSamples.size();
				sampleIndex += kGuideSampleInterval) {
				const size_t previousSampleIndex = sampleIndex > 0u ? sampleIndex - 1u : sampleIndex;
				const size_t nextSampleIndex = (std::min)(sampleIndex + 1u, railSamples.size() - 1u);
				const Vector3 direction = Subtract(
					railSamples[nextSampleIndex],
					railSamples[previousSampleIndex]);

				if (Length(direction) <= kProjectionEpsilon) {
					continue;
				}

				const Vector3 forward = Normalize(direction);
				Vector3 referenceUp{0.0f, 1.0f, 0.0f};

				if (std::fabs(Dot(forward, referenceUp)) >= 0.98f) {
					referenceUp = {0.0f, 0.0f, 1.0f};
				}

				const Vector3 right = Normalize(Cross(referenceUp, forward));
				const Vector3 up = Normalize(Cross(forward, right));
				const Vector3& samplePosition = railSamples[sampleIndex];
				DrawWorldArrowBetween(
					sceneDrawList,
					samplePosition,
					Add(samplePosition, Multiply(1.4f, forward)),
					IM_COL32(255, 220, 90, 230),
					nullptr);

				if (railMovement == nullptr) {
					continue;
				}

				const float horizontalRange = (std::max)(railMovement->railMovementRange.x, 0.0f);
				const float verticalRange = (std::max)(railMovement->railMovementRange.y, 0.0f);
				DrawWorldProjectedLine(
					sceneDrawList,
					Add(samplePosition, Multiply(-horizontalRange, right)),
					Add(samplePosition, Multiply(horizontalRange, right)),
					IM_COL32(80, 220, 255, 190),
					1.5f);
				DrawWorldProjectedLine(
					sceneDrawList,
					Add(samplePosition, Multiply(-verticalRange, up)),
					Add(samplePosition, Multiply(verticalRange, up)),
					IM_COL32(120, 255, 150, 190),
					1.5f);
			}
		}
	}

	void DrawTrajectoryPreviewDebug(ImDrawList* sceneDrawList) {
		for (const EditorGameObject& rendererObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* trajectoryRenderer = EditorComponentUtility::FindComponent(
				rendererObject,
				EditorComponentType::TrajectoryRenderer);

			if (!rendererObject.isActive || trajectoryRenderer == nullptr ||
				!trajectoryRenderer->isActive || !trajectoryRenderer->trajectoryShowInSceneView) {
				continue;
			}

			const int32_t predictionGameObjectId = trajectoryRenderer->trajectoryPredictionGameObjectId >= 0
				? trajectoryRenderer->trajectoryPredictionGameObjectId
				: rendererObject.id;
			const EditorGameObject* predictionObject = g_editorScene.FindGameObject(predictionGameObjectId);
			const EditorComponent* prediction = predictionObject != nullptr
				? EditorComponentUtility::FindComponent(*predictionObject, EditorComponentType::BallisticPrediction)
				: nullptr;

			if (prediction == nullptr || !prediction->isActive || !prediction->ballisticValid ||
				prediction->ballisticTrajectoryPoints.size() < 2u) {
				continue;
			}

			const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
				(std::clamp)(trajectoryRenderer->trajectoryColor.x, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryColor.y, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryColor.z, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryAlpha, 0.0f, 1.0f)));
			const int32_t pointCount = (std::min)(
				static_cast<int32_t>(prediction->ballisticTrajectoryPoints.size()),
				(std::clamp)(trajectoryRenderer->trajectoryMaximumPoints, 2, 2048));

			for (int32_t pointIndex = 1; pointIndex < pointCount; pointIndex++) {
				DrawWorldProjectedLine(
					sceneDrawList,
					prediction->ballisticTrajectoryPoints[static_cast<size_t>(pointIndex - 1)],
					prediction->ballisticTrajectoryPoints[static_cast<size_t>(pointIndex)],
					lineColor,
					(std::max)(trajectoryRenderer->trajectoryThickness, 0.5f));
			}

			if (trajectoryRenderer->trajectoryShowImpactPoint) {
				ProjectedScenePoint projectedImpact{};

				if (TryProjectWorldPosition(prediction->ballisticImpactPosition, projectedImpact)) {
					sceneDrawList->AddCircle(
						projectedImpact.screenPosition,
						6.0f,
						lineColor,
						16,
						(std::max)(trajectoryRenderer->trajectoryThickness, 1.0f));
				}
			}
		}
	}

	void DrawLocalBoxDebug(
		ImDrawList* sceneDrawList,
		const EditorGameObject& gameObject,
		const Vector3& center,
		const Vector3& size,
		ImU32 color) {
		const Vector3 halfSize{
			size.x * 0.5f,
			size.y * 0.5f,
			size.z * 0.5f};
		const Vector3 corners[8] = {
			MakeLocalPoint(center, -halfSize.x, -halfSize.y, -halfSize.z),
			MakeLocalPoint(center, halfSize.x, -halfSize.y, -halfSize.z),
			MakeLocalPoint(center, halfSize.x, -halfSize.y, halfSize.z),
			MakeLocalPoint(center, -halfSize.x, -halfSize.y, halfSize.z),
			MakeLocalPoint(center, -halfSize.x, halfSize.y, -halfSize.z),
			MakeLocalPoint(center, halfSize.x, halfSize.y, -halfSize.z),
			MakeLocalPoint(center, halfSize.x, halfSize.y, halfSize.z),
			MakeLocalPoint(center, -halfSize.x, halfSize.y, halfSize.z)};
		const int32_t edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0},
			{4, 5}, {5, 6}, {6, 7}, {7, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}};

		for (const int32_t(&edge)[2] : edges) {
			DrawProjectedLine(sceneDrawList, gameObject, corners[edge[0]], corners[edge[1]], color, 1.5f);
		}
	}

	void DrawBoxColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject, const EditorComponent& collider, ImU32 color) {
		DrawLocalBoxDebug(sceneDrawList, gameObject, collider.colliderCenter, collider.colliderSize, color);
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

	void DrawWheelColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject, const EditorComponent& collider, ImU32 color) {
		const float radius = (std::max)(collider.colliderRadius, 0.01f);
		const float halfWidth = (std::max)(collider.colliderSize.x * 0.5f, 0.01f);
		DrawLocalCircle(
			sceneDrawList,
			gameObject,
			collider.colliderCenter,
			Vector3{0.0f, 1.0f, 0.0f},
			Vector3{0.0f, 0.0f, 1.0f},
			radius,
			color);
		DrawProjectedLine(
			sceneDrawList,
			gameObject,
			MakeLocalPoint(collider.colliderCenter, -halfWidth, 0.0f, 0.0f),
			MakeLocalPoint(collider.colliderCenter, halfWidth, 0.0f, 0.0f),
			color,
			1.5f);
	}

	void DrawColliderDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		for (const EditorComponent& collider : gameObject.components) {
			if (!collider.isActive || !IsPhysicsDebugColliderType(collider.type)) {
				continue;
			}

			const ImU32 debugColor = GetPhysicsDebugColor(gameObject, collider);
			if (collider.type == EditorComponentType::SphereCollider) {
				DrawSphereColliderDebug(sceneDrawList, gameObject, collider, debugColor);
			}
			else if (collider.type == EditorComponentType::CapsuleCollider ||
			         collider.type == EditorComponentType::CharacterController) {
				DrawCapsuleColliderDebug(sceneDrawList, gameObject, collider, debugColor);
			}
			else if (collider.type == EditorComponentType::WheelCollider) {
				DrawWheelColliderDebug(sceneDrawList, gameObject, collider, debugColor);
			}
			else {
				// 複雑形状 Collider は生成元メッシュの外枠を Scene View の目安として表示する。
				DrawBoxColliderDebug(sceneDrawList, gameObject, collider, debugColor);
			}
		}
	}

	void DrawVelocityDebug(
		ImDrawList* sceneDrawList,
		const EditorGameObject& gameObject,
		const EditorPhysicsSettings& physicsSettings) {
		const EditorComponent* rigidBody = FindActiveComponent(gameObject, EditorComponentType::RigidBody);
		if (rigidBody == nullptr) {
			return;
		}

		const bool shouldDrawLabel = IsGameObjectSelected(gameObject.id);
		DrawWorldVectorArrow(
			sceneDrawList,
			GetGameObjectWorldPosition(gameObject),
			rigidBody->velocity,
			physicsSettings.debugVectorScale,
			IM_COL32(70, 225, 255, 255),
			shouldDrawLabel ? "速度" : nullptr);
		DrawWorldVectorArrow(
			sceneDrawList,
			Add(GetGameObjectWorldPosition(gameObject), Vector3{0.0f, 0.2f, 0.0f}),
			rigidBody->angularVelocity,
			physicsSettings.debugVectorScale,
			IM_COL32(225, 105, 255, 255),
			shouldDrawLabel ? "角速度" : nullptr);
	}

	void DrawRadialFieldArrows(
		ImDrawList* sceneDrawList,
		const Vector3& center,
		float radius,
		bool isInward,
		ImU32 color) {
		const Vector3 directions[6] = {
			Vector3{1.0f, 0.0f, 0.0f},
			Vector3{-1.0f, 0.0f, 0.0f},
			Vector3{0.0f, 1.0f, 0.0f},
			Vector3{0.0f, -1.0f, 0.0f},
			Vector3{0.0f, 0.0f, 1.0f},
			Vector3{0.0f, 0.0f, -1.0f}};
		const float previewRadius = radius > 0.0f ? (std::min)(radius, 4.0f) : 2.0f;

		for (const Vector3& direction : directions) {
			const Vector3 outerPoint = Add(center, Multiply(previewRadius, direction));
			const Vector3 innerPoint = Add(center, Multiply(previewRadius * 0.35f, direction));
			if (isInward) {
				DrawWorldArrowBetween(sceneDrawList, outerPoint, innerPoint, color, nullptr);
			}
			else {
				DrawWorldArrowBetween(sceneDrawList, innerPoint, outerPoint, color, nullptr);
			}
		}
	}

	void DrawForceDirectionDebug(
		ImDrawList* sceneDrawList,
		const EditorGameObject& gameObject,
		const EditorPhysicsSettings& physicsSettings) {
		const bool shouldDrawLabel = IsGameObjectSelected(gameObject.id);
		const EditorComponent* rigidBody = FindActiveComponent(gameObject, EditorComponentType::RigidBody);
		if (rigidBody != nullptr && rigidBody->useGravity) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				physicsSettings.gravity,
				physicsSettings.debugVectorScale,
				IM_COL32(255, 95, 80, 255),
				shouldDrawLabel ? "重力" : nullptr);
		}

		const EditorComponent* constantForce = FindActiveComponent(gameObject, EditorComponentType::ConstantForce);
		if (constantForce != nullptr) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				constantForce->velocity,
				physicsSettings.debugVectorScale,
				IM_COL32(255, 170, 70, 255),
				shouldDrawLabel ? "常時力" : nullptr);
		}

		const EditorComponent* thruster = FindActiveComponent(gameObject, EditorComponentType::Thruster);
		if (thruster != nullptr) {
			const Vector3 applicationPoint = TransformColliderPoint(
				gameObject,
				thruster->thrusterLocalApplicationPoint);
			Vector3 thrustDirection = thruster->thrusterDirection;

			if (thruster->thrusterUseLocalDirection) {
				const Vector3 localDirectionPoint = Add(
					thruster->thrusterLocalApplicationPoint,
					thruster->thrusterDirection);
				thrustDirection = Subtract(
					TransformColliderPoint(gameObject, localDirectionPoint),
					applicationPoint);
			}

			if (Length(thrustDirection) > 0.0001f) {
				const Vector3 thrustForce = Multiply(
					thruster->thrusterForce * (std::clamp)(thruster->thrusterThrottle, 0.0f, 1.0f),
					Normalize(thrustDirection));
				DrawWorldVectorArrow(
					sceneDrawList,
					applicationPoint,
					thrustForce,
					physicsSettings.debugVectorScale,
					IM_COL32(255, 135, 45, 255),
					shouldDrawLabel ? "推進力" : nullptr);
			}
		}

		const EditorComponent* torsionSpring = FindActiveComponent(
			gameObject,
			EditorComponentType::TorsionSpring);

		if (torsionSpring != nullptr) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				torsionSpring->torsionRestRotation,
				physicsSettings.debugVectorScale,
				IM_COL32(220, 130, 255, 255),
				shouldDrawLabel ? "目標回転" : nullptr);
		}

		const EditorComponent* vortexField = FindActiveComponent(
			gameObject,
			EditorComponentType::VortexField);

		if (vortexField != nullptr) {
			const Vector3 fieldCenter = GetGameObjectWorldPosition(gameObject);
			const Vector3 worldAxis = Subtract(
				TransformColliderPoint(gameObject, vortexField->vortexAxis),
				fieldCenter);

			if (Length(worldAxis) > 0.0001f) {
				DrawWorldVectorArrow(
					sceneDrawList,
					fieldCenter,
					Normalize(worldAxis),
					physicsSettings.debugVectorScale,
					IM_COL32(70, 220, 255, 255),
					shouldDrawLabel ? "渦軸" : nullptr);
			}
		}

		const EditorComponent* pressureField = FindActiveComponent(
			gameObject,
			EditorComponentType::PressureField);

		if (pressureField != nullptr) {
			DrawRadialFieldArrows(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				pressureField->pressureFieldRadius,
				pressureField->pressureFieldPressure < 0.0f,
				IM_COL32(255, 125, 70, 255));
		}

		const EditorComponent* uprightStabilizer = FindActiveComponent(
			gameObject,
			EditorComponentType::UprightStabilizer);

		if (uprightStabilizer != nullptr) {
			const Vector3 worldPosition = GetGameObjectWorldPosition(gameObject);
			const Vector3 currentWorldUp = Subtract(
				TransformColliderPoint(gameObject, uprightStabilizer->uprightLocalUpAxis),
				worldPosition);
			DrawWorldVectorArrow(
				sceneDrawList,
				worldPosition,
				currentWorldUp,
				physicsSettings.debugVectorScale,
				IM_COL32(255, 190, 70, 255),
				shouldDrawLabel ? "現在の上" : nullptr);
			DrawWorldVectorArrow(
				sceneDrawList,
				worldPosition,
				uprightStabilizer->uprightTargetWorldUp,
				physicsSettings.debugVectorScale,
				IM_COL32(80, 255, 145, 255),
				shouldDrawLabel ? "目標の上" : nullptr);
		}

		const EditorComponent* aerodynamics = FindActiveComponent(gameObject, EditorComponentType::Aerodynamics);
		if (aerodynamics != nullptr) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				aerodynamics->aerodynamicAmbientWindVelocity,
				physicsSettings.debugVectorScale,
				IM_COL32(90, 255, 145, 255),
				shouldDrawLabel ? "基礎風" : nullptr);
		}

		const EditorComponent* windZone = FindActiveComponent(gameObject, EditorComponentType::WindZone);
		if (windZone != nullptr) {
			if (windZone->windZoneMode == 0) {
				const Vector3 windVelocity = Multiply(windZone->windZoneSpeed, Normalize(windZone->windZoneDirection));
				DrawWorldVectorArrow(
					sceneDrawList,
					GetGameObjectWorldPosition(gameObject),
					windVelocity,
					physicsSettings.debugVectorScale,
					IM_COL32(90, 255, 145, 255),
					shouldDrawLabel ? "風" : nullptr);
			}
			else {
				DrawRadialFieldArrows(
					sceneDrawList,
					GetGameObjectWorldPosition(gameObject),
					windZone->windZoneRadius,
					false,
					IM_COL32(90, 255, 145, 255));
			}
		}

		const EditorComponent* gravityField = FindActiveComponent(gameObject, EditorComponentType::GravityField);
		if (gravityField != nullptr) {
			DrawRadialFieldArrows(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				gravityField->gravityFieldInfluenceRadius,
				true,
				IM_COL32(255, 95, 80, 255));
		}

		const EditorComponent* rotatingFrame = FindActiveComponent(gameObject, EditorComponentType::RotatingFrame);
		if (rotatingFrame != nullptr) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				rotatingFrame->rotatingFrameAngularVelocity,
				physicsSettings.debugVectorScale,
				IM_COL32(225, 105, 255, 255),
				shouldDrawLabel ? "回転軸" : nullptr);
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				rotatingFrame->rotatingFrameLinearVelocity,
				physicsSettings.debugVectorScale,
				IM_COL32(70, 225, 255, 255),
				shouldDrawLabel ? "中心速度" : nullptr);
		}

		const EditorComponent* fluidVolume = FindActiveComponent(gameObject, EditorComponentType::FluidVolume);
		if (fluidVolume != nullptr) {
			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				fluidVolume->fluidFlowVelocity,
				physicsSettings.debugVectorScale,
				IM_COL32(60, 235, 180, 255),
				shouldDrawLabel ? "流れ" : nullptr);
		}

		const EditorComponent* electromagneticField = FindActiveComponent(gameObject, EditorComponentType::ElectromagneticField);
		if (electromagneticField != nullptr) {
			if (electromagneticField->electromagneticFieldMode == 0) {
				DrawWorldVectorArrow(
					sceneDrawList,
					GetGameObjectWorldPosition(gameObject),
					electromagneticField->electromagneticElectricField,
					physicsSettings.debugVectorScale,
					IM_COL32(255, 225, 70, 255),
					shouldDrawLabel ? "電場" : nullptr);
			}
			else {
				const bool isInward = electromagneticField->electromagneticSourceCharge < 0.0f;
				DrawRadialFieldArrows(
					sceneDrawList,
					GetGameObjectWorldPosition(gameObject),
					electromagneticField->electromagneticInfluenceRadius,
					isInward,
					IM_COL32(255, 225, 70, 255));
			}

			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				electromagneticField->electromagneticMagneticField,
				physicsSettings.debugVectorScale,
				IM_COL32(210, 100, 255, 255),
				shouldDrawLabel ? "磁場" : nullptr);
		}

		for (const EditorComponent& component : gameObject.components) {
			if (!component.isActive || !IsPhysicsDebugJointType(component.type)) {
				continue;
			}

			DrawWorldVectorArrow(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				component.jointAxis,
				physicsSettings.debugVectorScale,
				IM_COL32(255, 205, 90, 255),
				shouldDrawLabel ? "Joint軸" : nullptr);
		}
	}

	void DrawFieldVolumeDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		constexpr ImU32 fieldColor = IM_COL32(90, 255, 160, 170);
		const EditorComponent* windZone = FindActiveComponent(gameObject, EditorComponentType::WindZone);
		if (windZone != nullptr && windZone->windZoneRadius > 0.0f) {
			DrawWorldSphere(sceneDrawList, GetGameObjectWorldPosition(gameObject), windZone->windZoneRadius, fieldColor);
		}

		const EditorComponent* gravityField = FindActiveComponent(gameObject, EditorComponentType::GravityField);
		if (gravityField != nullptr && gravityField->gravityFieldInfluenceRadius > 0.0f) {
			DrawWorldSphere(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				gravityField->gravityFieldInfluenceRadius,
				IM_COL32(255, 95, 80, 160));
		}

		const EditorComponent* rotatingFrame = FindActiveComponent(gameObject, EditorComponentType::RotatingFrame);
		if (rotatingFrame != nullptr && rotatingFrame->rotatingFrameRadius > 0.0f) {
			DrawWorldSphere(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				rotatingFrame->rotatingFrameRadius,
				IM_COL32(225, 105, 255, 160));
		}

		const EditorComponent* fluidVolume = FindActiveComponent(gameObject, EditorComponentType::FluidVolume);
		if (fluidVolume != nullptr) {
			DrawLocalBoxDebug(
				sceneDrawList,
				gameObject,
				Vector3{0.0f, 0.0f, 0.0f},
				fluidVolume->fluidVolumeSize,
				IM_COL32(60, 235, 180, 210));
		}

		const EditorComponent* buoyancy = FindActiveComponent(gameObject, EditorComponentType::Buoyancy);
		if (buoyancy != nullptr) {
			DrawLocalBoxDebug(
				sceneDrawList,
				gameObject,
				buoyancy->buoyancyCenterOffset,
				buoyancy->buoyancyHullSize,
				IM_COL32(70, 170, 255, 210));
		}

		const EditorComponent* aerodynamics = FindActiveComponent(gameObject, EditorComponentType::Aerodynamics);
		if (aerodynamics != nullptr) {
			DrawWorldMarker(
				sceneDrawList,
				TransformColliderPoint(gameObject, aerodynamics->aerodynamicCenterOfPressure),
				IM_COL32(255, 170, 70, 255));
		}

		const EditorComponent* electromagneticField = FindActiveComponent(gameObject, EditorComponentType::ElectromagneticField);
		if (electromagneticField != nullptr && electromagneticField->electromagneticInfluenceRadius > 0.0f) {
			DrawWorldSphere(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				electromagneticField->electromagneticInfluenceRadius,
				IM_COL32(255, 225, 70, 160));
		}

		const EditorComponent* vortexField = FindActiveComponent(
			gameObject,
			EditorComponentType::VortexField);

		if (vortexField != nullptr && vortexField->vortexRadius > 0.0f) {
			DrawWorldSphere(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				vortexField->vortexRadius,
				IM_COL32(70, 220, 255, 160));
		}

		const EditorComponent* pressureField = FindActiveComponent(
			gameObject,
			EditorComponentType::PressureField);

		if (pressureField != nullptr && pressureField->pressureFieldRadius > 0.0f) {
			DrawWorldSphere(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				pressureField->pressureFieldRadius,
				IM_COL32(255, 125, 70, 160));
		}
	}

	void DrawConnectionDebug(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		const EditorComponent* springForce = FindActiveComponent(gameObject, EditorComponentType::SpringForce);
		if (springForce != nullptr) {
			const Vector3 ownerAnchor = TransformColliderPoint(gameObject, springForce->springForceLocalAnchor);
			Vector3 targetAnchor = springForce->springForceWorldAnchor;
			const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(springForce->springForceTargetGameObjectId);
			if (targetGameObject != nullptr) {
				targetAnchor = TransformColliderPoint(*targetGameObject, springForce->springForceTargetLocalAnchor);
			}

			DrawWorldProjectedLine(
				sceneDrawList,
				ownerAnchor,
				targetAnchor,
				IM_COL32(255, 185, 80, 255),
				2.0f);
			DrawWorldMarker(sceneDrawList, ownerAnchor, IM_COL32(255, 210, 110, 255));
			DrawWorldMarker(sceneDrawList, targetAnchor, IM_COL32(255, 210, 110, 255));
		}

		const EditorComponent* ropeConstraint = FindActiveComponent(
			gameObject,
			EditorComponentType::RopeConstraint);

		if (ropeConstraint != nullptr) {
			const Vector3 ownerAnchor = TransformColliderPoint(gameObject, ropeConstraint->ropeLocalAnchor);
			Vector3 targetAnchor = ropeConstraint->ropeWorldAnchor;
			const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(
				ropeConstraint->ropeTargetGameObjectId);

			if (targetGameObject != nullptr) {
				targetAnchor = TransformColliderPoint(*targetGameObject, ropeConstraint->ropeTargetLocalAnchor);
			}

			const ImU32 ropeColor = ropeConstraint->ropeIsBroken
				? IM_COL32(255, 70, 70, 255)
				: IM_COL32(80, 220, 255, 255);
			DrawWorldProjectedLine(sceneDrawList, ownerAnchor, targetAnchor, ropeColor, 2.5f);
			DrawWorldMarker(sceneDrawList, ownerAnchor, ropeColor);
			DrawWorldMarker(sceneDrawList, targetAnchor, ropeColor);
		}

		const EditorComponent* suspension = FindActiveComponent(
			gameObject,
			EditorComponentType::Suspension);

		if (suspension != nullptr) {
			const Vector3 worldAnchor = TransformColliderPoint(
				gameObject,
				suspension->suspensionLocalAnchor);
			const Vector3 worldDirectionPoint = TransformColliderPoint(
				gameObject,
				Add(suspension->suspensionLocalAnchor, suspension->suspensionLocalDirection));
			const Vector3 rawWorldDirection = Subtract(worldDirectionPoint, worldAnchor);
			const float directionLength = Length(rawWorldDirection);

			if (directionLength > kProjectionEpsilon) {
				const Vector3 worldDirection = Multiply(1.0f / directionLength, rawWorldDirection);
				const float displayLength = suspension->suspensionIsGrounded
					? suspension->suspensionCurrentLength
					: suspension->suspensionMaximumLength;
				const Vector3 wheelCenter = Add(
					worldAnchor,
					Multiply((std::max)(displayLength, 0.0f), worldDirection));
				const ImU32 suspensionColor = suspension->suspensionIsGrounded
					? IM_COL32(80, 255, 145, 255)
					: IM_COL32(150, 170, 190, 255);
				DrawWorldProjectedLine(
					sceneDrawList,
					worldAnchor,
					wheelCenter,
					suspensionColor,
					2.5f);
				DrawWorldMarker(sceneDrawList, worldAnchor, suspensionColor);
				DrawWorldSphere(
					sceneDrawList,
					wheelCenter,
					(std::max)(suspension->suspensionWheelRadius, 0.0f),
					suspensionColor);
			}
		}

		const EditorComponent* torsionSpring = FindActiveComponent(
			gameObject,
			EditorComponentType::TorsionSpring);

		if (torsionSpring != nullptr) {
			const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(
				torsionSpring->torsionTargetGameObjectId);

			if (targetGameObject != nullptr) {
				DrawWorldProjectedLine(
					sceneDrawList,
					GetGameObjectWorldPosition(gameObject),
					GetGameObjectWorldPosition(*targetGameObject),
					IM_COL32(220, 130, 255, 255),
					2.0f);
			}
		}

		const EditorComponent* pulleyConstraint = FindActiveComponent(
			gameObject,
			EditorComponentType::PulleyConstraint);

		if (pulleyConstraint != nullptr) {
			const Vector3 ownerAnchor = TransformColliderPoint(
				gameObject,
				pulleyConstraint->pulleyOwnerLocalAnchor);
			const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(
				pulleyConstraint->pulleyTargetGameObjectId);
			const ImU32 pulleyColor = pulleyConstraint->pulleyIsBroken
				? IM_COL32(255, 70, 70, 255)
				: IM_COL32(75, 235, 220, 255);

			DrawWorldProjectedLine(
				sceneDrawList,
				ownerAnchor,
				pulleyConstraint->pulleyOwnerWorldSupport,
				pulleyColor,
				2.5f);
			DrawWorldProjectedLine(
				sceneDrawList,
				pulleyConstraint->pulleyOwnerWorldSupport,
				pulleyConstraint->pulleyTargetWorldSupport,
				pulleyColor,
				1.5f);
			DrawWorldMarker(sceneDrawList, ownerAnchor, pulleyColor);
			DrawWorldMarker(sceneDrawList, pulleyConstraint->pulleyOwnerWorldSupport, pulleyColor);
			DrawWorldMarker(sceneDrawList, pulleyConstraint->pulleyTargetWorldSupport, pulleyColor);

			if (targetGameObject != nullptr) {
				const Vector3 targetAnchor = TransformColliderPoint(
					*targetGameObject,
					pulleyConstraint->pulleyTargetLocalAnchor);
				DrawWorldProjectedLine(
					sceneDrawList,
					pulleyConstraint->pulleyTargetWorldSupport,
					targetAnchor,
					pulleyColor,
					2.5f);
				DrawWorldMarker(sceneDrawList, targetAnchor, pulleyColor);
			}
		}

		const EditorComponent* physicsServo = FindActiveComponent(
			gameObject,
			EditorComponentType::PhysicsServo);

		if (physicsServo != nullptr) {
			Vector3 desiredPosition = physicsServo->servoTargetPosition;
			const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(
				physicsServo->servoTargetGameObjectId);

			if (targetGameObject != nullptr) {
				desiredPosition = Add(
					GetGameObjectWorldPosition(*targetGameObject),
					physicsServo->servoTargetPosition);
			}

			DrawWorldProjectedLine(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				desiredPosition,
				IM_COL32(120, 175, 255, 255),
				2.0f);
			DrawWorldMarker(sceneDrawList, desiredPosition, IM_COL32(120, 175, 255, 255));
		}

		for (const EditorComponent& component : gameObject.components) {
			if (!component.isActive || !IsPhysicsDebugJointType(component.type)) {
				continue;
			}

			const EditorGameObject* connectedGameObject = g_editorScene.FindGameObject(component.connectedGameObjectId);
			if (connectedGameObject == nullptr) {
				continue;
			}

			DrawWorldProjectedLine(
				sceneDrawList,
				GetGameObjectWorldPosition(gameObject),
				GetGameObjectWorldPosition(*connectedGameObject),
				IM_COL32(255, 205, 90, 255),
				2.0f);
			DrawWorldMarker(
				sceneDrawList,
				GetGameObjectWorldPosition(*connectedGameObject),
				IM_COL32(255, 205, 90, 255));
		}
	}

	void DrawContactDebug(
		ImDrawList* sceneDrawList,
		const EditorPhysicsSettings& physicsSettings) {
		if (!g_editorRuntimeManager.IsPlaying()) {
			return;
		}

		const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& contactEvents =
			g_editorRuntimeManager.GetPhysicsManager().GetContactDebugEvents();
		for (const EditorJoltPhysicsManager::PhysicsEvent& contactEvent : contactEvents) {
			if (contactEvent.type == EditorJoltPhysicsManager::PhysicsEventType::CollisionExit ||
				contactEvent.type == EditorJoltPhysicsManager::PhysicsEventType::TriggerExit) {
				continue;
			}

			const EditorJoltPhysicsManager::CollisionInfo& collision = contactEvent.collision;
			if (collision.selfGameObjectId > collision.otherGameObjectId) {
				continue;  // 同じ接触は両側分のEventがあるため、ID順で1本へまとめる。
			}

			if (physicsSettings.drawSelectedOnlyDebug &&
				!IsGameObjectSelected(collision.selfGameObjectId) &&
				!IsGameObjectSelected(collision.otherGameObjectId)) {
				continue;
			}

			const ImU32 contactColor = collision.isTrigger
				? IM_COL32(80, 255, 150, 255)
				: IM_COL32(255, 145, 70, 255);
			DrawWorldMarker(sceneDrawList, collision.point, contactColor);
			DrawWorldArrowBetween(
				sceneDrawList,
				collision.point,
				Add(collision.point, Multiply(0.75f, Normalize(collision.normal))),
				contactColor,
				nullptr);
		}
	}

	void DrawCastDebug(ImDrawList* sceneDrawList) {
		if (!g_editorRuntimeManager.IsPlaying()) {
			return;
		}

		const std::vector<EditorPhysicsManager::PhysicsDebugCast>& debugCasts =
			g_editorRuntimeManager.GetPhysicsManager().GetFrameDebugCasts();
		for (const EditorPhysicsManager::PhysicsDebugCast& debugCast : debugCasts) {
			const Vector3 castDirection = Normalize(debugCast.direction);
			if (Length(castDirection) <= kProjectionEpsilon) {
				continue;
			}

			Vector3 castEnd = Add(debugCast.origin, Multiply(debugCast.distance, castDirection));
			if (debugCast.hasHit) {
				castEnd = debugCast.hit.point;
			}

			ImU32 castColor = IM_COL32(255, 225, 80, 230);
			if (debugCast.type == EditorPhysicsManager::PhysicsDebugCastType::Sphere) {
				castColor = IM_COL32(70, 225, 255, 230);
			}
			else if (debugCast.type == EditorPhysicsManager::PhysicsDebugCastType::Capsule) {
				castColor = IM_COL32(225, 105, 255, 230);
			}

			DrawWorldArrowBetween(sceneDrawList, debugCast.origin, castEnd, castColor, nullptr);
			if (debugCast.radius > 0.0f) {
				DrawWorldSphere(sceneDrawList, debugCast.origin, debugCast.radius, castColor);
				DrawWorldSphere(sceneDrawList, castEnd, debugCast.radius, castColor);
			}

			if (debugCast.hasHit) {
				DrawWorldMarker(sceneDrawList, debugCast.hit.point, IM_COL32(255, 95, 80, 255));
				DrawWorldArrowBetween(
					sceneDrawList,
					debugCast.hit.point,
					Add(debugCast.hit.point, Multiply(0.75f, Normalize(debugCast.hit.normal))),
					IM_COL32(255, 95, 80, 255),
					nullptr);
			}
		}
	}

	// Hookは「選択点」と「力を伝えるRigidbody」が別Objectになるため、
	// Hierarchyの数値だけでは接続先の取り違えに気付けない。SceneView上で線として見せる。
	void DrawHookWireDebug(ImDrawList* sceneDrawList) {
		if (!g_isHookWireSceneGizmoVisible) {
			return;
		}

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const EditorComponent* hookComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::WireConnectable);
			if (hookComponent == nullptr || !hookComponent->isActive) {
				continue;
			}

			Vector3 hookWorldScale{};
			Vector3 hookWorldRotation{};
			Vector3 hookWorldPosition{};
			if (!g_editorScene.GetWorldTransform(
					gameObject.id,
					hookWorldScale,
					hookWorldRotation,
					hookWorldPosition)) {
				continue;
			}

			// AnchorはHookのローカル座標なので、World行列を通した実位置を描く。
			const Vector3 anchorWorldPosition = Transform(
				hookComponent->wireConnectableLocalAnchor,
				g_editorScene.GetWorldMatrix(gameObject.id));

			ProjectedScenePoint anchorPoint{};
			if (!TryProjectWorldPosition(anchorWorldPosition, anchorPoint)) {
				continue;
			}

			const int32_t physicsBodyGameObjectId =
				hookComponent->wireConnectablePhysicsBodyGameObjectId >= 0
					? hookComponent->wireConnectablePhysicsBodyGameObjectId
					: gameObject.id;
			const EditorGameObject* physicsBodyGameObject =
				g_editorScene.FindGameObject(physicsBodyGameObjectId);
			const EditorComponent* physicsBodyRigidBody = physicsBodyGameObject != nullptr
				? EditorComponentUtility::FindComponent(*physicsBodyGameObject, EditorComponentType::RigidBody)
				: nullptr;
			// 力の伝わらない構成（参照切れ、Rigidbodyなし）は警告色にして配置中に気付けるようにする。
			const bool isPhysicsBodyValid = physicsBodyGameObject != nullptr && physicsBodyRigidBody != nullptr;
			const ImU32 hookColor = isPhysicsBodyValid
				? IM_COL32(120, 220, 255, 235)
				: IM_COL32(255, 140, 60, 235);

			sceneDrawList->AddCircleFilled(anchorPoint.screenPosition, 4.0f, hookColor);
			sceneDrawList->AddCircle(anchorPoint.screenPosition, 8.0f, hookColor, 0, 1.5f);

			if (physicsBodyGameObject == nullptr || physicsBodyGameObjectId == gameObject.id) {
				continue;
			}

			Vector3 bodyWorldScale{};
			Vector3 bodyWorldRotation{};
			Vector3 bodyWorldPosition{};
			if (!g_editorScene.GetWorldTransform(
					physicsBodyGameObjectId,
					bodyWorldScale,
					bodyWorldRotation,
					bodyWorldPosition)) {
				continue;
			}

			ProjectedScenePoint bodyPoint{};
			if (!TryProjectWorldPosition(bodyWorldPosition, bodyPoint)) {
				continue;
			}

			const bool isLineSafe =
				std::fabs(anchorPoint.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(anchorPoint.ndcY) <= kProjectedLineNdcLimit &&
				std::fabs(bodyPoint.ndcX) <= kProjectedLineNdcLimit &&
				std::fabs(bodyPoint.ndcY) <= kProjectedLineNdcLimit;
			if (!isLineSafe) {
				continue;
			}

			sceneDrawList->AddLine(
				anchorPoint.screenPosition,
				bodyPoint.screenPosition,
				hookColor,
				1.5f);
			sceneDrawList->AddCircle(bodyPoint.screenPosition, 5.0f, hookColor, 0, 1.5f);
		}
	}

	void DrawPhysicsDebug(ImDrawList* sceneDrawList) {
		const EditorPhysicsSettings& physicsSettings = g_editorScene.GetPhysicsSettings();
		if (!physicsSettings.drawColliderDebug &&
			!physicsSettings.drawVelocityDebug &&
			!physicsSettings.drawForceDirectionDebug &&
			!physicsSettings.drawFieldVolumeDebug &&
			!physicsSettings.drawConnectionDebug &&
			!physicsSettings.drawContactDebug &&
			!physicsSettings.drawCastDebug) {
			return;
		}

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive ||
				(physicsSettings.drawSelectedOnlyDebug && !IsGameObjectSelected(gameObject.id))) {
				continue;
			}

			if (physicsSettings.drawColliderDebug) {
				DrawColliderDebug(sceneDrawList, gameObject);
			}

			if (physicsSettings.drawVelocityDebug) {
				DrawVelocityDebug(sceneDrawList, gameObject, physicsSettings);
			}

			if (physicsSettings.drawForceDirectionDebug) {
				DrawForceDirectionDebug(sceneDrawList, gameObject, physicsSettings);
			}

			if (physicsSettings.drawFieldVolumeDebug) {
				DrawFieldVolumeDebug(sceneDrawList, gameObject);
			}

			if (physicsSettings.drawConnectionDebug) {
				DrawConnectionDebug(sceneDrawList, gameObject);
			}
		}

		if (physicsSettings.drawContactDebug) {
			DrawContactDebug(sceneDrawList, physicsSettings);
		}

		if (physicsSettings.drawCastDebug) {
			DrawCastDebug(sceneDrawList);
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

			Vector3 worldScale{};
			Vector3 worldRotation{};
			Vector3 worldPosition{};
			if (!g_editorScene.GetWorldTransform(
					gameObjectId,
					worldScale,
					worldRotation,
					worldPosition)) {
				continue;
			}

			pivotTransform.translate = Add(pivotTransform.translate, worldPosition);
			pivotTransform.rotate = Add(pivotTransform.rotate, worldRotation);
			pivotTransform.scale = Add(pivotTransform.scale, worldScale);
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

			Vector3 worldScale{};
			Vector3 worldRotation{};
			Vector3 worldPosition{};
			if (!g_editorScene.GetWorldTransform(
					gameObjectId,
					worldScale,
					worldRotation,
					worldPosition)) {
				continue;
			}

			Vector3 localOffset = Subtract(worldPosition, beforeTransform.translate);
			localOffset.x *= scaleRatio.x;
			localOffset.y *= scaleRatio.y;
			localOffset.z *= scaleRatio.z;
			localOffset = Transform(localOffset, deltaRotationMatrix);

			const Vector3 nextWorldPosition = Add(afterTransform.translate, localOffset);
			const Vector3 nextWorldRotation = Add(worldRotation, rotationDelta);
			const Vector3 nextWorldScale{
				(std::max)(0.01f, worldScale.x * scaleRatio.x),
				(std::max)(0.01f, worldScale.y * scaleRatio.y),
				(std::max)(0.01f, worldScale.z * scaleRatio.z)};
			g_editorScene.SetWorldTransform(
				gameObjectId,
				nextWorldScale,
				nextWorldRotation,
				nextWorldPosition);
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
		if (!TryProjectWorldPosition(GetGameObjectWorldPosition(gameObject), projectedPoint) ||
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
		if (!TryProjectWorldPosition(GetGameObjectWorldPosition(gameObject), projectedPoint) ||
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

		const Matrix4x4 worldMatrix = g_editorScene.GetWorldMatrix(gameObject.id);
		Vector3 forwardDirection{
			worldMatrix.matrix[2][0],
			worldMatrix.matrix[2][1],
			worldMatrix.matrix[2][2]};
		forwardDirection = Length(forwardDirection) > 0.0001f
			? Normalize(forwardDirection)
			: GetSceneIconForwardDirection(gameObject.rotate);
		Vector3 forwardEnd = Add(GetGameObjectWorldPosition(gameObject), Multiply(1.2f, forwardDirection));
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

	void DrawSceneTargetPointGizmo(ImDrawList* sceneDrawList, const EditorGameObject& gameObject) {
		const EditorComponent* targetPoint = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::TargetPoint);

		if (targetPoint == nullptr || !targetPoint->isActive) {
			return;
		}

		const Matrix4x4 worldMatrix = g_editorScene.GetWorldMatrix(gameObject.id);
		const Vector3 targetPosition = Transform(targetPoint->targetPointAimOffset, worldMatrix);
		const ImU32 targetColor = IsGameObjectSelected(gameObject.id)
			? IM_COL32(255, 235, 80, 255)
			: IM_COL32(255, 150, 60, 210);
		DrawWorldSphere(
			sceneDrawList,
			targetPosition,
			(std::max)(targetPoint->targetPointRadius, 0.01f),
			targetColor);
		DrawWorldMarker(sceneDrawList, targetPosition, targetColor);

		ProjectedScenePoint projectedPoint{};

		if (TryProjectWorldPosition(targetPosition, projectedPoint)) {
			sceneDrawList->AddText(
				ImVec2(projectedPoint.screenPosition.x + 9.0f, projectedPoint.screenPosition.y - 9.0f),
				targetColor,
				gameObject.name.c_str());
		}
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
			DrawSceneTargetPointGizmo(sceneDrawList, gameObject);
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

	const std::string sceneDisplayName = g_currentScenePath.empty()
		? "未保存シーン"
		: std::filesystem::path(g_currentScenePath).filename().generic_string();
	const std::string sceneWindowTitle = sceneDisplayName + " - シーン###SceneView";
	ImGui::Begin(sceneWindowTitle.c_str(), nullptr, sceneWindowFlags);  // ###SceneView は固定し、Docking を維持したまま現在 Scene 名を表示する。

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

				// 起動時の固定テクスチャ一覧にない画像も Sprite として配置できるよう、拡張子で判定する。
				bool isSpriteAsset =
					EditorAssetUtility::HasExtension(droppedAsset, ".png") ||
					EditorAssetUtility::HasExtension(droppedAsset, ".jpg") ||
					EditorAssetUtility::HasExtension(droppedAsset, ".jpeg");

				if (EditorAssetUtility::HasExtension(droppedAsset, ".prefab")) {
					g_editorScene.PushUndo();
					const int32_t prefabRootId = g_editorScene.InstantiatePrefab(droppedAsset);
					EditorGameObject* prefabRoot = g_editorScene.FindGameObject(prefabRootId);

					if (prefabRoot != nullptr) {
						prefabRoot->translate = GetModelDropPosition();
						g_selectedEditorGameObjectId = prefabRootId;
						SetSingleSelectedGameObject(prefabRootId);
						g_previousSelectedEditorGameObjectId = -1;
						g_editorSceneSynchronizer.Update(
							g_editorTextureFilePaths,
							g_selectedPlacedSceneObjectIndex);
					}
				}
				else if (isSpriteAsset) {
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

	char sceneFpsText[192]{};
	const float sceneFrameRate = ImGui::GetIO().Framerate;
	const float sceneFrameTimeMilliseconds = sceneFrameRate > 0.0f ? 1000.0f / sceneFrameRate : 0.0f;
	constexpr double bytesPerMegabyte = 1024.0 * 1024.0;
	const double localVideoMemoryUsageMegabytes =
		static_cast<double>(g_renderProfile.localVideoMemoryUsage) / bytesPerMegabyte;
	const double localVideoMemoryBudgetMegabytes =
		static_cast<double>(g_renderProfile.localVideoMemoryBudget) / bytesPerMegabyte;
	std::snprintf(
		sceneFpsText,
		_countof(sceneFpsText),
		"%.1f FPS  CPU %.2f ms  GPU %.2f ms\nVRAM %.0f / %.0f MB  Obj %u  Inst %u",
		sceneFrameRate,
		sceneFrameTimeMilliseconds,
		g_renderProfile.gpuFrameMilliseconds,
		localVideoMemoryUsageMegabytes,
		localVideoMemoryBudgetMegabytes,
		g_renderProfile.sceneObjectCount,
		g_renderProfile.instanceCount);
	const ImVec2 sceneFpsTextSize = ImGui::CalcTextSize(sceneFpsText);
	const ImVec2 sceneFpsTextPosition{
		g_editorSceneX + 54.0f,
		g_editorSceneY + g_editorSceneHeight - sceneFpsTextSize.y - 14.0f};
	const ImVec2 sceneFpsBackgroundMin{
		sceneFpsTextPosition.x - 8.0f,
		sceneFpsTextPosition.y - 5.0f};
	const ImVec2 sceneFpsBackgroundMax{
		sceneFpsTextPosition.x + sceneFpsTextSize.x + 8.0f,
		sceneFpsTextPosition.y + sceneFpsTextSize.y + 5.0f};
	sceneDrawList->AddRectFilled(
		sceneFpsBackgroundMin,
		sceneFpsBackgroundMax,
		IM_COL32(12, 18, 24, 205),
		5.0f);
	sceneDrawList->AddText(
		sceneFpsTextPosition,
		IM_COL32(210, 245, 210, 255),
		sceneFpsText);

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

		DrawRailPathDebug(sceneDrawList);
		DrawTrajectoryPreviewDebug(sceneDrawList);
		DrawPhysicsDebug(sceneDrawList);
		DrawHookWireDebug(sceneDrawList);

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
			g_editorScene.GetWorldTransform(
				selectedGameObjectGizmo->id,
				selectedGameObjectGizmoTransform.scale,
				selectedGameObjectGizmoTransform.rotate,
				selectedGameObjectGizmoTransform.translate);
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

		bool isSelectionLockedByAnotherUser =
			IsEditorTeamGameObjectLockedByAnotherUser(g_selectedEditorGameObjectId);

		for (const int32_t selectedGameObjectId : g_selectedEditorGameObjectIds) {
			isSelectionLockedByAnotherUser = isSelectionLockedByAnotherUser ||
				IsEditorTeamGameObjectLockedByAnotherUser(selectedGameObjectId);
		}

		if (hasSelectedGizmoTransform && selectedGizmoTransform != nullptr &&
			!isSelectionLockedByAnotherUser) {
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
					// ギズモのワールドSRTを親空間へ戻し、階層を壊さず保存する。
					g_editorScene.SetWorldTransform(
						selectedGameObjectGizmo->id,
						selectedGizmoTransform->scale,
						selectedGizmoTransform->rotate,
						selectedGizmoTransform->translate);
				}
				else {
					g_editorSelectionManager.SyncSelectedPlacedObjectToGameObject(g_selectedPlacedSceneObjectIndex);  // SceneObject を動かした結果を、対応する GameObject の Transform Component へ同期する。
				}
			}

			// ギズモが反応しない不具合の再現時にVS出力から状態を追えるようにする。
			// 毎フレーム出すと埋もれるので、SceneView内で左クリックした瞬間だけ出す。
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				const ImVec2 clickMousePosition = ImGui::GetMousePos();
				// カメラや行列がNaN/Infになっていないかもここで確認する。
				// NaNになった編集用カメラでギズモを計算すると、比較が常にfalseになり
				// 「出るのに一切反応しない」症状に一致する。
				const bool isCameraTransformFinite =
					std::isfinite(g_cameraTransform.translate.x) &&
					std::isfinite(g_cameraTransform.translate.y) &&
					std::isfinite(g_cameraTransform.translate.z) &&
					std::isfinite(g_cameraTransform.rotate.x) &&
					std::isfinite(g_cameraTransform.rotate.y) &&
					std::isfinite(g_cameraTransform.rotate.z);
				const bool isViewMatrixFinite = std::isfinite(g_viewMatrix.matrix[3][0]) &&
					std::isfinite(g_viewMatrix.matrix[3][1]) &&
					std::isfinite(g_viewMatrix.matrix[3][2]);

				char gizmoDiagnosticBuffer[512];
				std::snprintf(
					gizmoDiagnosticBuffer,
					sizeof(gizmoDiagnosticBuffer),
					"[GIZMO-DIAG] click=(%.1f,%.1f) sceneRect=(%.1f,%.1f,%.1f,%.1f) "
					"hasSelectedGizmoTransform=%d isSelectionLockedByAnotherUser=%d "
					"isGizmoHovered=%d isGizmoActive=%d selectedGameObjectId=%d "
					"multiSelectCount=%zu wantCaptureMouse=%d "
					"cameraTransformFinite=%d viewMatrixFinite=%d cameraPos=(%.2f,%.2f,%.2f)\n",
					static_cast<double>(clickMousePosition.x),
					static_cast<double>(clickMousePosition.y),
					static_cast<double>(g_editorSceneX),
					static_cast<double>(g_editorSceneY),
					static_cast<double>(g_editorSceneWidth),
					static_cast<double>(g_editorSceneHeight),
					hasSelectedGizmoTransform ? 1 : 0,
					isSelectionLockedByAnotherUser ? 1 : 0,
					isGizmoHovered ? 1 : 0,
					isGizmoActive ? 1 : 0,
					g_selectedEditorGameObjectId,
					g_selectedEditorGameObjectIds.size(),
					ImGui::GetIO().WantCaptureMouse ? 1 : 0,
					isCameraTransformFinite ? 1 : 0,
					isViewMatrixFinite ? 1 : 0,
					static_cast<double>(g_cameraTransform.translate.x),
					static_cast<double>(g_cameraTransform.translate.y),
					static_cast<double>(g_cameraTransform.translate.z));
				// このタグはVS出力で意図的に必ず見せたいため、抑制済みのLog()ではなく直接出す。
				OutputDebugStringA(gizmoDiagnosticBuffer);
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
					if (!TryProjectWorldPosition(GetGameObjectWorldPosition(gameObject), projectedPoint)) {
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
