#include "EditorGameplayToolsWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef USE_IMGUI
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#endif

using namespace EditorSharedState;

namespace {
	struct CanvasBounds {
		float minimumA = -10.0f;
		float maximumA = 10.0f;
		float minimumB = -10.0f;
		float maximumB = 10.0f;
	};

	struct TimelineMarker {
		int32_t gameObjectId = -1;
		EditorComponent* component = nullptr;
		bool isWave = false;
		float normalizedPosition = 0.0f;
		std::string label;
	};

	EditorComponent* FindComponent(EditorGameObject* gameObject, EditorComponentType type) {
		return gameObject != nullptr
			? EditorComponentUtility::FindComponent(*gameObject, type)
			: nullptr;
	}

	std::vector<EditorGameObject*> CollectControlPoints(EditorGameObject* railPathGameObject) {
		std::vector<EditorGameObject*> controlPoints;

		if (railPathGameObject == nullptr) {
			return controlPoints;
		}

		controlPoints.reserve(railPathGameObject->children.size());

		for (int32_t childGameObjectId : railPathGameObject->children) {
			EditorGameObject* childGameObject = g_editorScene.FindGameObject(childGameObjectId);

			if (childGameObject != nullptr) {
				controlPoints.push_back(childGameObject);
			}
		}

		return controlPoints;
	}

	bool IsRailPathCandidate(const EditorGameObject& gameObject) {
		if (gameObject.children.size() < 2u) {
			return false;
		}

		for (const EditorGameObject& followerGameObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* railMovementComponent = EditorComponentUtility::FindComponent(
				followerGameObject,
				EditorComponentType::RailMovement);

			if (railMovementComponent != nullptr && railMovementComponent->railPathGameObjectId == gameObject.id) {
				return true;
			}
		}

		if (gameObject.name.find("Path") != std::string::npos) {
			return true;
		}

		int32_t namedPointCount = 0;
		for (int32_t childGameObjectId : gameObject.children) {
			const EditorGameObject* childGameObject = g_editorScene.FindGameObject(childGameObjectId);

			if (childGameObject != nullptr && childGameObject->name.rfind("Point", 0u) == 0u) {
				namedPointCount++;
			}
		}

		return namedPointCount >= 2;
	}

	EditorComponent* FindRailMovementForPath(int32_t railPathGameObjectId) {
		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			EditorComponent* railMovementComponent = FindComponent(
				&gameObject,
				EditorComponentType::RailMovement);

			if (railMovementComponent != nullptr &&
				railMovementComponent->railPathGameObjectId == railPathGameObjectId) {
				return railMovementComponent;
			}
		}

		return nullptr;
	}

	int32_t ResolveSplinePointIndex(int32_t pointIndex, int32_t pointCount, bool isLooping) {
		if (isLooping) {
			const int32_t wrappedIndex = pointIndex % pointCount;
			return wrappedIndex < 0 ? wrappedIndex + pointCount : wrappedIndex;
		}

		return (std::clamp)(pointIndex, 0, pointCount - 1);
	}

	Vector3 EvaluateSplineCatmullRom(
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

	std::vector<Vector3> BuildSplinePreviewPositions(
		const std::vector<EditorGameObject*>& controlPoints,
		const EditorComponent* railMovementComponent) {
		std::vector<Vector3> previewPositions;

		if (controlPoints.size() < 2u) {
			return previewPositions;
		}

		const bool isLooping = railMovementComponent != nullptr && railMovementComponent->railLoop;
		const bool usesSmoothCurve =
			railMovementComponent == nullptr || railMovementComponent->railUseSmoothCurve;
		const int32_t pointCount = static_cast<int32_t>(controlPoints.size());
		const int32_t segmentCount = isLooping ? pointCount : pointCount - 1;
		const int32_t samplesPerSegment = usesSmoothCurve ? 16 : 1;
		previewPositions.push_back(controlPoints.front()->translate);

		for (int32_t segmentIndex = 0; segmentIndex < segmentCount; segmentIndex++) {
			const int32_t firstIndex = ResolveSplinePointIndex(segmentIndex - 1, pointCount, isLooping);
			const int32_t secondIndex = ResolveSplinePointIndex(segmentIndex, pointCount, isLooping);
			const int32_t thirdIndex = ResolveSplinePointIndex(segmentIndex + 1, pointCount, isLooping);
			const int32_t fourthIndex = ResolveSplinePointIndex(segmentIndex + 2, pointCount, isLooping);

			for (int32_t sampleIndex = 1; sampleIndex <= samplesPerSegment; sampleIndex++) {
				const float normalizedTime =
					static_cast<float>(sampleIndex) / static_cast<float>(samplesPerSegment);
				previewPositions.push_back(usesSmoothCurve ?
					EvaluateSplineCatmullRom(
						controlPoints[static_cast<size_t>(firstIndex)]->translate,
						controlPoints[static_cast<size_t>(secondIndex)]->translate,
						controlPoints[static_cast<size_t>(thirdIndex)]->translate,
						controlPoints[static_cast<size_t>(fourthIndex)]->translate,
						normalizedTime) :
					controlPoints[static_cast<size_t>(thirdIndex)]->translate);
			}
		}

		return previewPositions;
	}

	CanvasBounds BuildCanvasBounds(
		const std::vector<EditorGameObject*>& controlPoints,
		bool showsSideView) {
		CanvasBounds bounds{};

		if (controlPoints.empty()) {
			return bounds;
		}

		const Vector3& firstPosition = controlPoints.front()->translate;
		bounds.minimumA = showsSideView ? firstPosition.z : firstPosition.x;
		bounds.maximumA = bounds.minimumA;
		bounds.minimumB = showsSideView ? firstPosition.y : firstPosition.z;
		bounds.maximumB = bounds.minimumB;

		for (const EditorGameObject* controlPoint : controlPoints) {
			const float axisA = showsSideView ? controlPoint->translate.z : controlPoint->translate.x;
			const float axisB = showsSideView ? controlPoint->translate.y : controlPoint->translate.z;
			bounds.minimumA = (std::min)(bounds.minimumA, axisA);
			bounds.maximumA = (std::max)(bounds.maximumA, axisA);
			bounds.minimumB = (std::min)(bounds.minimumB, axisB);
			bounds.maximumB = (std::max)(bounds.maximumB, axisB);
		}

		const float rangeA = (std::max)(bounds.maximumA - bounds.minimumA, 10.0f);
		const float rangeB = (std::max)(bounds.maximumB - bounds.minimumB, 10.0f);
		bounds.minimumA -= rangeA * 0.15f;
		bounds.maximumA += rangeA * 0.15f;
		bounds.minimumB -= rangeB * 0.15f;
		bounds.maximumB += rangeB * 0.15f;
		return bounds;
	}

#ifdef USE_IMGUI
	bool DrawStringInput(const char* label, std::string& value) {
		char valueBuffer[256]{};
		strncpy_s(valueBuffer, sizeof(valueBuffer), value.c_str(), _TRUNCATE);

		if (!ImGui::InputText(label, valueBuffer, sizeof(valueBuffer))) {
			return false;
		}

		value = valueBuffer;
		return true;
	}

	ImVec2 AddImVec2(const ImVec2& firstValue, const ImVec2& secondValue) {
		return {firstValue.x + secondValue.x, firstValue.y + secondValue.y};
	}

	ImVec2 ToCanvasPosition(
		const Vector3& position,
		const CanvasBounds& bounds,
		const ImVec2& canvasMinimum,
		const ImVec2& canvasSize,
		bool showsSideView) {
		const float axisA = showsSideView ? position.z : position.x;
		const float axisB = showsSideView ? position.y : position.z;
		const float normalizedA = (axisA - bounds.minimumA) / (bounds.maximumA - bounds.minimumA);
		const float normalizedB = (axisB - bounds.minimumB) / (bounds.maximumB - bounds.minimumB);
		return {
			canvasMinimum.x + normalizedA * canvasSize.x,
			canvasMinimum.y + (1.0f - normalizedB) * canvasSize.y};
	}
#endif

}

void EditorGameplayToolsWindowManager::Initialize() {
	selectedRailPathGameObjectId_ = -1;
	selectedControlPointGameObjectId_ = -1;
	draggingControlPointGameObjectId_ = -1;
	timelineSourceMode_ = 0;
	selectedTimelineGameObjectId_ = -1;
	draggingTimelineGameObjectId_ = -1;
	isDraggingWaveMarker_ = false;
	waveTemplateGameObjectId_ = -1;
	selectedStateGameObjectId_ = -1;
}

void EditorGameplayToolsWindowManager::Update() {
}

void EditorGameplayToolsWindowManager::Draw() {
#ifdef USE_IMGUI
	if (g_isSplineEditorVisible) {
		if (ImGui::Begin("Spline Editor###SplineEditor", &g_isSplineEditorVisible, ImGuiWindowFlags_NoCollapse)) {
			ImGui::TextDisabled("Scene上のSpline制御点を上面・側面から編集します。");
			DrawSplineEditor();
		}
		ImGui::End();
	}

	if (g_isGameplayTimelineWindowVisible) {
		if (ImGui::Begin(
				"Event Timeline###GameplayEventTimeline",
				&g_isGameplayTimelineWindowVisible,
				ImGuiWindowFlags_NoCollapse)) {
			DrawEventTimeline();
		}
		ImGui::End();
	}

	if (g_isStateGraphWindowVisible) {
		if (ImGui::Begin(
				"State Graph###GameplayStateGraph",
				&g_isStateGraphWindowVisible,
				ImGuiWindowFlags_NoCollapse)) {
			DrawStateGraph();
		}
		ImGui::End();
	}

#endif
}

void EditorGameplayToolsWindowManager::DrawSplineEditor() {
#ifdef USE_IMGUI
	EditorGameObject* selectedGameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	EditorComponent* selectedRailMovement = FindComponent(selectedGameObject, EditorComponentType::RailMovement);

	if (selectedRailMovement != nullptr && selectedRailMovement->railPathGameObjectId >= 0) {
		selectedRailPathGameObjectId_ = selectedRailMovement->railPathGameObjectId;
	}
	else if (selectedGameObject != nullptr && IsRailPathCandidate(*selectedGameObject)) {
		selectedRailPathGameObjectId_ = selectedGameObject->id;
	}

	EditorGameObject* railPathGameObject = g_editorScene.FindGameObject(selectedRailPathGameObjectId_);
	const char* railName = railPathGameObject != nullptr ? railPathGameObject->name.c_str() : "未選択";

	if (ImGui::BeginCombo("Spline", railName)) {
		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!IsRailPathCandidate(gameObject)) {
				continue;
			}

			const bool isSelected = gameObject.id == selectedRailPathGameObjectId_;
			if (ImGui::Selectable(gameObject.name.c_str(), isSelected)) {
				selectedRailPathGameObjectId_ = gameObject.id;
				selectedControlPointGameObjectId_ = -1;

				if (selectedRailMovement != nullptr) {
					selectedRailMovement->railPathGameObjectId = gameObject.id;
				}
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("新規Spline", ImVec2(130.0f, 0.0f))) {
		CreateSpline();
	}
	ImGui::SameLine();

	if (ImGui::Button("選択点の次へ追加", ImVec2(150.0f, 0.0f))) {
		AddControlPoint();
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("上面 XZ", !showsSideView_)) {
		showsSideView_ = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("側面 ZY", showsSideView_)) {
		showsSideView_ = true;
	}

	if (selectedGameObject != nullptr && selectedRailMovement != nullptr &&
		g_editorRuntimeManager.IsPlaying()) {
		EditorRailMovementManager& railMovementManager =
			g_editorRuntimeManager.GetRailMovementManager();
		float normalizedProgress = 0.0f;
		ImGui::SeparatorText("Play Preview");

		if (railMovementManager.GetNormalizedProgress(
				selectedGameObject->id,
				normalizedProgress) &&
			ImGui::SliderFloat("進行率", &normalizedProgress, 0.0f, 1.0f, "%.3f")) {
			railMovementManager.SetNormalizedProgress(selectedGameObject->id, normalizedProgress);
		}

		const bool isPaused = railMovementManager.IsPaused(selectedGameObject->id);
		if (ImGui::Button(isPaused ? "再開" : "停止", ImVec2(90.0f, 0.0f))) {
			railMovementManager.SetPaused(selectedGameObject->id, !isPaused);
		}

		ImGui::SameLine();
		if (ImGui::Button("順方向", ImVec2(90.0f, 0.0f))) {
			railMovementManager.SetReverse(selectedGameObject->id, false);
		}

		ImGui::SameLine();
		if (ImGui::Button("逆方向", ImVec2(90.0f, 0.0f))) {
			railMovementManager.SetReverse(selectedGameObject->id, true);
		}
	}

	railPathGameObject = g_editorScene.FindGameObject(selectedRailPathGameObjectId_);
	std::vector<EditorGameObject*> controlPoints = CollectControlPoints(railPathGameObject);
	EditorComponent* previewRailMovement = selectedRailMovement != nullptr ?
		selectedRailMovement : FindRailMovementForPath(selectedRailPathGameObjectId_);

	if (railPathGameObject == nullptr || controlPoints.size() < 2u) {
		ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "2点以上を持つSplineを選択してください。");
		return;
	}

	const float listWidth = 280.0f;
	if (ImGui::BeginChild("RailControlPointList", ImVec2(listWidth, 430.0f), true)) {
		for (size_t pointIndex = 0u; pointIndex < controlPoints.size(); pointIndex++) {
			EditorGameObject* controlPoint = controlPoints[pointIndex];
			char pointLabel[160]{};
			std::snprintf(
				pointLabel,
				_countof(pointLabel),
				"%02d  %s",
				static_cast<int32_t>(pointIndex),
				controlPoint->name.c_str());

			if (ImGui::Selectable(
					pointLabel,
					controlPoint->id == selectedControlPointGameObjectId_)) {
				selectedControlPointGameObjectId_ = controlPoint->id;
				SetSingleSelectedGameObject(controlPoint->id);
			}
		}

		EditorGameObject* selectedPoint = g_editorScene.FindGameObject(selectedControlPointGameObjectId_);
		if (selectedPoint != nullptr) {
			ImGui::Separator();
			if (ImGui::DragFloat3("位置", &selectedPoint->translate.x, 0.1f)) {
				g_editorSceneSynchronizer.Update(g_editorTextureFilePaths, g_selectedPlacedSceneObjectIndex);
			}

			if (controlPoints.size() > 2u && ImGui::Button("選択点を削除", ImVec2(-1.0f, 0.0f))) {
				g_editorScene.PushUndo();
				g_editorScene.DeleteGameObject(selectedPoint->id);
				selectedControlPointGameObjectId_ = -1;
				ClearSelectedGameObjects();
			}
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("RailSplineCanvasPanel", ImVec2(0.0f, 430.0f), true)) {
		const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("RailSplineCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(canvasMinimum, AddImVec2(canvasMinimum, canvasSize), IM_COL32(10, 20, 29, 255));
		drawList->AddRect(canvasMinimum, AddImVec2(canvasMinimum, canvasSize), IM_COL32(63, 109, 132, 255));

		for (int32_t gridIndex = 1; gridIndex < 10; gridIndex++) {
			const float ratio = static_cast<float>(gridIndex) / 10.0f;
			drawList->AddLine(
				{canvasMinimum.x + canvasSize.x * ratio, canvasMinimum.y},
				{canvasMinimum.x + canvasSize.x * ratio, canvasMinimum.y + canvasSize.y},
				IM_COL32(30, 54, 68, 255));
			drawList->AddLine(
				{canvasMinimum.x, canvasMinimum.y + canvasSize.y * ratio},
				{canvasMinimum.x + canvasSize.x, canvasMinimum.y + canvasSize.y * ratio},
				IM_COL32(30, 54, 68, 255));
		}

		const CanvasBounds bounds = BuildCanvasBounds(controlPoints, showsSideView_);
		std::vector<ImVec2> pointPositions;
		pointPositions.reserve(controlPoints.size());
		for (const EditorGameObject* controlPoint : controlPoints) {
			pointPositions.push_back(ToCanvasPosition(
				controlPoint->translate, bounds, canvasMinimum, canvasSize, showsSideView_));
		}

		const std::vector<Vector3> previewPositions = BuildSplinePreviewPositions(
			controlPoints,
			previewRailMovement);
		std::vector<ImVec2> previewCanvasPositions;
		previewCanvasPositions.reserve(previewPositions.size());

		for (const Vector3& previewPosition : previewPositions) {
			previewCanvasPositions.push_back(ToCanvasPosition(
				previewPosition,
				bounds,
				canvasMinimum,
				canvasSize,
				showsSideView_));
		}

		for (size_t pointIndex = 1u; pointIndex < previewCanvasPositions.size(); pointIndex++) {
			drawList->AddLine(
				previewCanvasPositions[pointIndex - 1u],
				previewCanvasPositions[pointIndex],
				IM_COL32(67, 211, 235, 255),
				3.0f);
		}

		for (size_t pointIndex = 0u; pointIndex < pointPositions.size(); pointIndex++) {
			const bool isSelected = controlPoints[pointIndex]->id == selectedControlPointGameObjectId_;
			drawList->AddCircleFilled(
				pointPositions[pointIndex],
				isSelected ? 9.0f : 7.0f,
				isSelected ? IM_COL32(255, 185, 64, 255) : IM_COL32(91, 231, 208, 255));
			drawList->AddText(
				AddImVec2(pointPositions[pointIndex], ImVec2(10.0f, -10.0f)),
				IM_COL32(225, 242, 246, 255),
				std::to_string(pointIndex).c_str());
		}

		const ImVec2 mousePosition = ImGui::GetIO().MousePos;
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			float nearestDistanceSquared = 144.0f;
			for (size_t pointIndex = 0u; pointIndex < pointPositions.size(); pointIndex++) {
				const float dx = mousePosition.x - pointPositions[pointIndex].x;
				const float dy = mousePosition.y - pointPositions[pointIndex].y;
				const float distanceSquared = dx * dx + dy * dy;

				if (distanceSquared <= nearestDistanceSquared) {
					nearestDistanceSquared = distanceSquared;
					draggingControlPointGameObjectId_ = controlPoints[pointIndex]->id;
					selectedControlPointGameObjectId_ = controlPoints[pointIndex]->id;
				}
			}

			if (draggingControlPointGameObjectId_ >= 0) {
				g_editorScene.PushUndo();
				SetSingleSelectedGameObject(draggingControlPointGameObjectId_);
			}
		}

		if (draggingControlPointGameObjectId_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			EditorGameObject* draggedPoint = g_editorScene.FindGameObject(draggingControlPointGameObjectId_);
			if (draggedPoint != nullptr) {
				const float normalizedA = (std::clamp)((mousePosition.x - canvasMinimum.x) / canvasSize.x, 0.0f, 1.0f);
				const float normalizedB = (std::clamp)(1.0f - (mousePosition.y - canvasMinimum.y) / canvasSize.y, 0.0f, 1.0f);
				const float axisA = bounds.minimumA + (bounds.maximumA - bounds.minimumA) * normalizedA;
				const float axisB = bounds.minimumB + (bounds.maximumB - bounds.minimumB) * normalizedB;

				if (showsSideView_) {
					draggedPoint->translate.z = axisA;
					draggedPoint->translate.y = axisB;
				}
				else {
					draggedPoint->translate.x = axisA;
					draggedPoint->translate.z = axisB;
				}
			}
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (draggingControlPointGameObjectId_ >= 0) {
				g_editorSceneSynchronizer.Update(
					g_editorTextureFilePaths,
					g_selectedPlacedSceneObjectIndex);
			}

			draggingControlPointGameObjectId_ = -1;
		}
	}
	ImGui::EndChild();
#endif
}

void EditorGameplayToolsWindowManager::DrawEventTimeline() {
#ifdef USE_IMGUI
	ImGui::TextDisabled(
		"Event Timelineは時刻またはRail進行率と名前付きScript Actionだけを接続します。ゲームルールは実行しません。");
	const char* sourceModes[] = {"経過秒", "Rail進行率"};
	ImGui::SetNextItemWidth(180.0f);
	ImGui::Combo("表示軸", &timelineSourceMode_, sourceModes, 2);

	if (timelineSourceMode_ == 0) {
		ImGui::SetNextItemWidth(180.0f);
		ImGui::DragFloat("表示時間", &timelineDurationSeconds_, 1.0f, 1.0f, 3600.0f, "%.1f 秒");
		timelineDurationSeconds_ = (std::clamp)(timelineDurationSeconds_, 1.0f, 3600.0f);
	}

	if (ImGui::Button("Eventを追加", ImVec2(130.0f, 0.0f))) {
		CreateTimelineEvent();
	}

	ImGui::SameLine();
	if (ImGui::Button("選択ObjectをWave雛形にする", ImVec2(220.0f, 0.0f))) {
		if (g_editorScene.FindGameObject(g_selectedEditorGameObjectId) != nullptr) {
			waveTemplateGameObjectId_ = g_selectedEditorGameObjectId;
		}
	}

	EditorGameObject* waveTemplateGameObject = g_editorScene.FindGameObject(waveTemplateGameObjectId_);
	ImGui::Text(
		"Wave雛形: %s",
		waveTemplateGameObject != nullptr ? waveTemplateGameObject->name.c_str() : "未選択");
	ImGui::SetNextItemWidth(130.0f);
	ImGui::DragInt("Wave個数", &waveCount_, 1.0f, 1, 64);
	waveCount_ = (std::clamp)(waveCount_, 1, 64);
	const char* formationPatterns[] = {"横列", "V字", "円", "グリッド"};
	ImGui::SetNextItemWidth(130.0f);
	ImGui::Combo("初期配置", &waveFormationPattern_, formationPatterns, 4);
	ImGui::SetNextItemWidth(130.0f);
	ImGui::DragFloat("配置間隔", &waveSpacing_, 0.1f, 0.0f, 1000.0f, "%.2f");
	waveSpacing_ = (std::clamp)(waveSpacing_, 0.0f, 1000.0f);

	if (ImGui::Button(
			"Waveを作成",
			ImVec2(130.0f, 0.0f)) &&
		waveTemplateGameObject != nullptr) {
		CreateWaveFromSelection();
	}

	std::vector<TimelineMarker> timelineMarkers;
	for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		EditorComponent* timelineComponent = FindComponent(
			&gameObject,
			EditorComponentType::TimelineEvent);

		if (timelineComponent != nullptr && timelineComponent->timelineSourceMode == timelineSourceMode_) {
			TimelineMarker marker{};
			marker.gameObjectId = gameObject.id;
			marker.component = timelineComponent;
			marker.isWave = false;
			marker.normalizedPosition = timelineSourceMode_ == 0
				? timelineComponent->timelineTriggerValue / timelineDurationSeconds_
				: timelineComponent->timelineTriggerValue;
			marker.normalizedPosition = (std::clamp)(marker.normalizedPosition, 0.0f, 1.0f);
			marker.label = gameObject.name + " / " + timelineComponent->timelineActionName;
			timelineMarkers.push_back(marker);
		}

		EditorComponent* waveComponent = FindComponent(
			&gameObject,
			EditorComponentType::WaveSpawner);

		if (timelineSourceMode_ == 1 && waveComponent != nullptr && waveComponent->waveTriggerMode == 1) {
			TimelineMarker marker{};
			marker.gameObjectId = gameObject.id;
			marker.component = waveComponent;
			marker.isWave = true;
			marker.normalizedPosition = (std::clamp)(waveComponent->waveTriggerValue, 0.0f, 1.0f);
			marker.label = gameObject.name + " / Wave";
			timelineMarkers.push_back(marker);
		}
	}

	ImGui::SeparatorText("Events");
	const float rowHeight = 38.0f;
	const float headerHeight = 42.0f;
	const float canvasHeight = headerHeight +
		rowHeight * static_cast<float>((std::max)(timelineMarkers.size(), static_cast<size_t>(1u)));
	const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize{(std::max)(ImGui::GetContentRegionAvail().x, 320.0f), canvasHeight};
	ImGui::InvisibleButton(
		"GameplayEventTimelineCanvas",
		canvasSize,
		ImGuiButtonFlags_MouseButtonLeft);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 canvasMaximum = AddImVec2(canvasMinimum, canvasSize);
	drawList->AddRectFilled(canvasMinimum, canvasMaximum, IM_COL32(11, 19, 26, 255));
	drawList->AddRect(canvasMinimum, canvasMaximum, IM_COL32(61, 91, 108, 255));
	const float trackMinimumX = canvasMinimum.x + 190.0f;
	const float trackMaximumX = canvasMaximum.x - 18.0f;

	for (int32_t gridIndex = 0; gridIndex <= 10; gridIndex++) {
		const float normalizedGrid = static_cast<float>(gridIndex) / 10.0f;
		const float gridX = trackMinimumX + (trackMaximumX - trackMinimumX) * normalizedGrid;
		drawList->AddLine(
			{gridX, canvasMinimum.y + 24.0f},
			{gridX, canvasMaximum.y},
			IM_COL32(35, 52, 62, 255));
		char gridLabel[32]{};

		if (timelineSourceMode_ == 0) {
			std::snprintf(
				gridLabel,
				_countof(gridLabel),
				"%.1fs",
				timelineDurationSeconds_ * normalizedGrid);
		}
		else {
			std::snprintf(gridLabel, _countof(gridLabel), "%.0f%%", normalizedGrid * 100.0f);
		}

		drawList->AddText({gridX + 3.0f, canvasMinimum.y + 4.0f}, IM_COL32(155, 181, 194, 255), gridLabel);
	}

	for (size_t markerIndex = 0u; markerIndex < timelineMarkers.size(); markerIndex++) {
		const TimelineMarker& marker = timelineMarkers[markerIndex];
		const float markerY = canvasMinimum.y + headerHeight + rowHeight * static_cast<float>(markerIndex);
		const float markerX = trackMinimumX + (trackMaximumX - trackMinimumX) * marker.normalizedPosition;
		const bool isSelected = marker.gameObjectId == selectedTimelineGameObjectId_;
		drawList->AddText(
			{canvasMinimum.x + 8.0f, markerY - 8.0f},
			IM_COL32(220, 232, 237, 255),
			marker.label.c_str());
		drawList->AddLine(
			{trackMinimumX, markerY},
			{trackMaximumX, markerY},
			IM_COL32(42, 62, 73, 255));
		drawList->AddCircleFilled(
			{markerX, markerY},
			isSelected ? 8.0f : 6.0f,
			marker.isWave ? IM_COL32(245, 174, 66, 255) : IM_COL32(67, 211, 235, 255));
	}

	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		float nearestDistanceSquared = 196.0f;

		for (size_t markerIndex = 0u; markerIndex < timelineMarkers.size(); markerIndex++) {
			const TimelineMarker& marker = timelineMarkers[markerIndex];
			const float markerY = canvasMinimum.y + headerHeight + rowHeight * static_cast<float>(markerIndex);
			const float markerX = trackMinimumX + (trackMaximumX - trackMinimumX) * marker.normalizedPosition;
			const float differenceX = mousePosition.x - markerX;
			const float differenceY = mousePosition.y - markerY;
			const float distanceSquared = differenceX * differenceX + differenceY * differenceY;

			if (distanceSquared <= nearestDistanceSquared) {
				nearestDistanceSquared = distanceSquared;
				draggingTimelineGameObjectId_ = marker.gameObjectId;
				selectedTimelineGameObjectId_ = marker.gameObjectId;
				isDraggingWaveMarker_ = marker.isWave;
			}
		}

		if (draggingTimelineGameObjectId_ >= 0) {
			g_editorScene.PushUndo();
			SetSingleSelectedGameObject(draggingTimelineGameObjectId_);
		}
	}

	if (draggingTimelineGameObjectId_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		EditorGameObject* draggedGameObject = g_editorScene.FindGameObject(draggingTimelineGameObjectId_);
		EditorComponent* draggedComponent = FindComponent(
			draggedGameObject,
			isDraggingWaveMarker_
				? EditorComponentType::WaveSpawner
				: EditorComponentType::TimelineEvent);

		if (draggedComponent != nullptr && trackMaximumX > trackMinimumX) {
			const float normalizedPosition = (std::clamp)(
				(mousePosition.x - trackMinimumX) / (trackMaximumX - trackMinimumX),
				0.0f,
				1.0f);

			if (isDraggingWaveMarker_) {
				draggedComponent->waveTriggerValue = normalizedPosition;
			}
			else {
				draggedComponent->timelineTriggerValue = timelineSourceMode_ == 0
					? normalizedPosition * timelineDurationSeconds_
					: normalizedPosition;
			}
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		draggingTimelineGameObjectId_ = -1;
		isDraggingWaveMarker_ = false;
	}

	ImGui::TextDisabled(
		"水色=名前付きEvent、橙色=Wave開始。実際の攻撃・演出・BGM等は受信側Script/Componentが決めます。");
#endif
}

void EditorGameplayToolsWindowManager::DrawStateGraph() {
#ifdef USE_IMGUI
	ImGui::TextDisabled(
		"State Graphは値の区間を状態へ変換し、状態変更時に名前付きScript Actionだけを通知します。");
	EditorGameObject* sceneSelectedGameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	if (FindComponent(sceneSelectedGameObject, EditorComponentType::ThresholdState) != nullptr) {
		selectedStateGameObjectId_ = g_selectedEditorGameObjectId;
	}

	EditorGameObject* selectedStateGameObject = g_editorScene.FindGameObject(selectedStateGameObjectId_);
	const char* selectedStateName = selectedStateGameObject != nullptr
		? selectedStateGameObject->name.c_str()
		: "未選択";

	if (ImGui::BeginCombo("State Graph", selectedStateName)) {
		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (FindComponent(&gameObject, EditorComponentType::ThresholdState) == nullptr) {
				continue;
			}

			const bool isSelected = gameObject.id == selectedStateGameObjectId_;
			if (ImGui::Selectable(gameObject.name.c_str(), isSelected)) {
				selectedStateGameObjectId_ = gameObject.id;
				SetSingleSelectedGameObject(gameObject.id);
			}
		}
		ImGui::EndCombo();
	}

	if (sceneSelectedGameObject != nullptr &&
		FindComponent(sceneSelectedGameObject, EditorComponentType::ThresholdState) == nullptr &&
		ImGui::Button("選択ObjectへThreshold Stateを追加", ImVec2(260.0f, 0.0f))) {
		g_editorScene.PushUndo();
		if (g_editorScene.AddComponent(sceneSelectedGameObject->id, EditorComponentType::ThresholdState)) {
			selectedStateGameObjectId_ = sceneSelectedGameObject->id;
		}
	}

	selectedStateGameObject = g_editorScene.FindGameObject(selectedStateGameObjectId_);
	EditorComponent* stateComponent = FindComponent(
		selectedStateGameObject,
		EditorComponentType::ThresholdState);

	if (stateComponent == nullptr) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			"Threshold Stateを持つGameObjectを選択してください。");
		return;
	}

	const char* stateSourceModes[] = {"Health比率", "Rail進行率"};
	stateComponent->thresholdSourceMode = (std::clamp)(stateComponent->thresholdSourceMode, 0, 1);
	ImGui::SetNextItemWidth(180.0f);
	ImGui::Combo("値Source", &stateComponent->thresholdSourceMode, stateSourceModes, 2);
	ImGui::SetNextItemWidth(180.0f);
	ImGui::DragFloat(
		"State 2 境界",
		&stateComponent->thresholdSecondValue,
		0.01f,
		0.0f,
		1.0f,
		"%.2f");
	ImGui::SetNextItemWidth(180.0f);
	ImGui::DragFloat(
		"State 3 境界",
		&stateComponent->thresholdThirdValue,
		0.01f,
		0.0f,
		1.0f,
		"%.2f");
	stateComponent->thresholdSecondValue = (std::clamp)(stateComponent->thresholdSecondValue, 0.0f, 1.0f);
	stateComponent->thresholdThirdValue = (std::clamp)(stateComponent->thresholdThirdValue, 0.0f, 1.0f);
	DrawStringInput("State 1 Action", stateComponent->thresholdFirstActionName);
	DrawStringInput("State 2 Action", stateComponent->thresholdSecondActionName);
	DrawStringInput("State 3 Action", stateComponent->thresholdThirdActionName);

	const ImVec2 graphMinimum = ImGui::GetCursorScreenPos();
	const ImVec2 graphSize{(std::max)(ImGui::GetContentRegionAvail().x, 560.0f), 230.0f};
	ImGui::InvisibleButton("ThresholdStateGraphCanvas", graphSize);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(graphMinimum, AddImVec2(graphMinimum, graphSize), IM_COL32(11, 19, 26, 255));
	const float nodeWidth = 170.0f;
	const float nodeHeight = 84.0f;
	const float availableNodeSpace = graphSize.x - nodeWidth * 3.0f;
	const float nodeGap = (std::max)(availableNodeSpace / 4.0f, 18.0f);
	const float nodeY = graphMinimum.y + 72.0f;
	const std::string actionNames[] = {
		stateComponent->thresholdFirstActionName,
		stateComponent->thresholdSecondActionName,
		stateComponent->thresholdThirdActionName};

	for (int32_t stateIndex = 0; stateIndex < 3; stateIndex++) {
		const float nodeX = graphMinimum.x + nodeGap +
			static_cast<float>(stateIndex) * (nodeWidth + nodeGap);
		const ImVec2 nodeMinimum{nodeX, nodeY};
		const ImVec2 nodeMaximum{nodeX + nodeWidth, nodeY + nodeHeight};

		if (stateIndex > 0) {
			const float previousNodeX = nodeX - nodeGap;
			drawList->AddLine(
				{previousNodeX, nodeY + nodeHeight * 0.5f},
				{nodeX, nodeY + nodeHeight * 0.5f},
				IM_COL32(118, 155, 171, 255),
				2.0f);
			drawList->AddTriangleFilled(
				{nodeX, nodeY + nodeHeight * 0.5f},
				{nodeX - 9.0f, nodeY + nodeHeight * 0.5f - 5.0f},
				{nodeX - 9.0f, nodeY + nodeHeight * 0.5f + 5.0f},
				IM_COL32(118, 155, 171, 255));
		}

		drawList->AddRectFilled(nodeMinimum, nodeMaximum, IM_COL32(28, 49, 62, 255), 6.0f);
		drawList->AddRect(nodeMinimum, nodeMaximum, IM_COL32(67, 211, 235, 255), 6.0f, 0, 2.0f);
		const std::string stateLabel = "State " + std::to_string(stateIndex + 1);
		drawList->AddText(
			{nodeX + 12.0f, nodeY + 12.0f},
			IM_COL32(242, 247, 249, 255),
			stateLabel.c_str());
		drawList->AddText(
			{nodeX + 12.0f, nodeY + 42.0f},
			IM_COL32(151, 220, 225, 255),
			actionNames[static_cast<size_t>(stateIndex)].c_str());
	}

	const char* directionText = stateComponent->thresholdSourceMode == 0
		? "値が低下すると State 1 -> 2 -> 3"
		: "値が上昇すると State 1 -> 2 -> 3";
	drawList->AddText(
		{graphMinimum.x + 14.0f, graphMinimum.y + 16.0f},
		IM_COL32(174, 196, 207, 255),
		directionText);
	ImGui::TextDisabled(
		"各Actionの意味は受信するC++ Scriptが決めます。Boss、敵、攻撃などの固有概念はGraph側にありません。");
#endif
}

void EditorGameplayToolsWindowManager::CreateTimelineEvent() {
	const int32_t selectedGameObjectId = g_selectedEditorGameObjectId;
	g_editorScene.PushUndo();
	const int32_t eventGameObjectId = g_editorScene.CreateGameObject("Timeline Event");
	g_editorScene.AddComponent(eventGameObjectId, EditorComponentType::TimelineEvent);
	EditorGameObject* eventGameObject = g_editorScene.FindGameObject(eventGameObjectId);
	EditorComponent* eventComponent = FindComponent(
		eventGameObject,
		EditorComponentType::TimelineEvent);

	if (eventComponent != nullptr) {
		eventComponent->timelineSourceMode = timelineSourceMode_;
		eventComponent->timelineSourceGameObjectId = timelineSourceMode_ == 1
			? selectedGameObjectId
			: -1;
		eventComponent->timelineTargetGameObjectId = selectedGameObjectId;
	}

	selectedTimelineGameObjectId_ = eventGameObjectId;
	SetSingleSelectedGameObject(eventGameObjectId);
}

void EditorGameplayToolsWindowManager::CreateWaveFromSelection() {
	const EditorGameObject* templateGameObject = g_editorScene.FindGameObject(waveTemplateGameObjectId_);

	if (templateGameObject == nullptr) {
		return;
	}

	const std::string templateName = templateGameObject->name;
	const int32_t templateGameObjectId = templateGameObject->id;
	Vector3 waveScale = templateGameObject->scale;
	Vector3 waveRotation = templateGameObject->rotate;
	Vector3 wavePosition = templateGameObject->translate;
	g_editorScene.GetWorldTransform(
		templateGameObjectId,
		waveScale,
		waveRotation,
		wavePosition);
	(void)waveScale;
	g_editorScene.PushUndo();
	const int32_t poolTemplateGameObjectId = g_editorScene.DuplicateGameObject(templateGameObjectId);

	if (poolTemplateGameObjectId < 0) {
		return;
	}

	EditorGameObject* poolTemplateGameObject = g_editorScene.FindGameObject(poolTemplateGameObjectId);

	if (poolTemplateGameObject == nullptr) {
		return;
	}

	poolTemplateGameObject->name = templateName + " Wave Template";
	poolTemplateGameObject->isActive = false;
	const int32_t poolGameObjectId = g_editorScene.CreateGameObject(templateName + " Wave Pool");
	g_editorScene.AddComponent(poolGameObjectId, EditorComponentType::ObjectPool);
	EditorComponent* poolComponent = FindComponent(
		g_editorScene.FindGameObject(poolGameObjectId),
		EditorComponentType::ObjectPool);

	if (poolComponent != nullptr) {
		poolComponent->objectPoolTemplateGameObjectId = poolTemplateGameObjectId;
		poolComponent->objectPoolInitialSize = waveCount_;
		poolComponent->objectPoolAllowExpand = false;
	}

	const int32_t waveGameObjectId = g_editorScene.CreateGameObject("Wave");
	g_editorScene.AddComponent(waveGameObjectId, EditorComponentType::WaveSpawner);
	EditorGameObject* waveGameObject = g_editorScene.FindGameObject(waveGameObjectId);

	if (waveGameObject != nullptr) {
		waveGameObject->translate = wavePosition;
		waveGameObject->rotate = waveRotation;
	}

	EditorComponent* waveComponent = FindComponent(
		waveGameObject,
		EditorComponentType::WaveSpawner);

	if (waveComponent != nullptr) {
		waveComponent->waveSpawnSourceMode = 0;
		waveComponent->wavePoolGameObjectId = poolGameObjectId;
		waveComponent->waveSpawnPointGameObjectId = waveGameObjectId;
		waveComponent->waveSpawnCount = waveCount_;
		waveComponent->waveFormationPattern = waveFormationPattern_ + 1;
		waveComponent->waveFormationSpacing = waveSpacing_;
	}

	SetSingleSelectedGameObject(waveGameObjectId);
}

void EditorGameplayToolsWindowManager::CreateSpline() {
	const int32_t selectedFollowerGameObjectId = g_selectedEditorGameObjectId;
	g_editorScene.PushUndo();
	const int32_t railPathGameObjectId = g_editorScene.CreateGameObject("Spline Path");

	for (int32_t pointIndex = 0; pointIndex < 4; pointIndex++) {
		char pointName[32]{};
		std::snprintf(pointName, _countof(pointName), "Point %02d", pointIndex);
		const int32_t pointGameObjectId = g_editorScene.CreateGameObject(pointName);
		g_editorScene.SetParent(pointGameObjectId, railPathGameObjectId);
		EditorGameObject* pointGameObject = g_editorScene.FindGameObject(pointGameObjectId);

		if (pointGameObject != nullptr) {
			pointGameObject->translate = {0.0f, 0.0f, static_cast<float>(pointIndex) * 12.0f};
		}
	}

	EditorGameObject* selectedFollowerGameObject = g_editorScene.FindGameObject(selectedFollowerGameObjectId);
	EditorComponent* railMovementComponent = FindComponent(
		selectedFollowerGameObject,
		EditorComponentType::RailMovement);

	if (railMovementComponent != nullptr) {
		railMovementComponent->railPathGameObjectId = railPathGameObjectId;
	}

	selectedRailPathGameObjectId_ = railPathGameObjectId;
	SetSingleSelectedGameObject(railPathGameObjectId);
}

void EditorGameplayToolsWindowManager::AddControlPoint() {
	EditorGameObject* railPathGameObject = g_editorScene.FindGameObject(selectedRailPathGameObjectId_);
	std::vector<EditorGameObject*> controlPoints = CollectControlPoints(railPathGameObject);

	if (railPathGameObject == nullptr) {
		return;
	}

	const int32_t railPathGameObjectId = railPathGameObject->id;
	size_t insertionIndex = controlPoints.size();

	for (size_t pointIndex = 0u; pointIndex < controlPoints.size(); pointIndex++) {
		if (controlPoints[pointIndex]->id == selectedControlPointGameObjectId_) {
			insertionIndex = pointIndex + 1u;
			break;
		}
	}

	Vector3 newPointPosition{0.0f, 0.0f, 0.0f};

	if (!controlPoints.empty() && insertionIndex < controlPoints.size()) {
		newPointPosition = Multiply(
			0.5f,
			Add(
				controlPoints[insertionIndex - 1u]->translate,
				controlPoints[insertionIndex]->translate));
	}
	else if (controlPoints.size() >= 2u) {
		const Vector3& lastPosition = controlPoints.back()->translate;
		const Vector3& previousPosition = controlPoints[controlPoints.size() - 2u]->translate;
		newPointPosition = Add(lastPosition, Subtract(lastPosition, previousPosition));
	}
	else if (!controlPoints.empty()) {
		newPointPosition = Add(controlPoints.back()->translate, Vector3{0.0f, 0.0f, 12.0f});
	}

	g_editorScene.PushUndo();
	char pointName[32]{};
	std::snprintf(pointName, _countof(pointName), "Point %02d", static_cast<int32_t>(controlPoints.size()));
	const int32_t pointGameObjectId = g_editorScene.CreateGameObject(pointName);
	g_editorScene.SetParent(pointGameObjectId, railPathGameObjectId);
	EditorGameObject* pointGameObject = g_editorScene.FindGameObject(pointGameObjectId);

	if (pointGameObject != nullptr) {
		pointGameObject->translate = newPointPosition;
	}

	railPathGameObject = g_editorScene.FindGameObject(railPathGameObjectId);

	if (railPathGameObject != nullptr && insertionIndex < railPathGameObject->children.size()) {
		auto newPointIterator = std::find(
			railPathGameObject->children.begin(),
			railPathGameObject->children.end(),
			pointGameObjectId);

		if (newPointIterator != railPathGameObject->children.end()) {
			railPathGameObject->children.erase(newPointIterator);
			railPathGameObject->children.insert(
				railPathGameObject->children.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
				pointGameObjectId);
		}
	}

	selectedControlPointGameObjectId_ = pointGameObjectId;
	SetSingleSelectedGameObject(pointGameObjectId);
}
