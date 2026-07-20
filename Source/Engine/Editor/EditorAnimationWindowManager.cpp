#pragma warning(disable : 4189 4514 5045)

#include "EditorAnimationWindowManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

using namespace EditorSharedState;

namespace {
	constexpr int32_t kAnimationPropertyTargetCount =
		static_cast<int32_t>(AnimationPropertyTarget::MaterialEmissionColorB) + 1;
	constexpr float kMinimumClipDuration = 0.01f;
	constexpr float kMinimumTimelineScale = 40.0f;
	constexpr float kMaximumTimelineScale = 500.0f;
	constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
	constexpr float kRadiansToDegrees = 180.0f / 3.14159265358979323846f;

	EditorComponent* FindMaterialComponent(EditorGameObject& gameObject) {
		EditorComponent* renderer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ModelRenderer);

		if (renderer == nullptr) {
			renderer = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::SpriteRenderer);
		}

		return renderer;
	}

	const EditorComponent* FindMaterialComponent(const EditorGameObject& gameObject) {
		const EditorComponent* renderer = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ModelRenderer);

		if (renderer == nullptr) {
			renderer = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::SpriteRenderer);
		}

		return renderer;
	}

	const char* GetWriteModeDisplayName(AnimationPropertyWriteMode writeMode) {
		switch (writeMode) {
		case AnimationPropertyWriteMode::Additive: return "加算";
		case AnimationPropertyWriteMode::Multiply: return "乗算";
		case AnimationPropertyWriteMode::Override:
		default: return "上書き";
		}
	}

	const char* GetInterpolationDisplayName(AnimationCurveInterpolation interpolation) {
		switch (interpolation) {
		case AnimationCurveInterpolation::Step: return "一定 (Step)";
		case AnimationCurveInterpolation::CubicHermite: return "滑らか (Cubic Hermite)";
		case AnimationCurveInterpolation::Linear:
		default: return "直線 (Linear)";
		}
	}
}

void EditorAnimationWindowManager::Initialize() {
	previousUpdateTime_ = std::chrono::steady_clock::now();
	animationClip_.name = "NewAnimationClip";
	animationClip_.durationSeconds = 1.0f;
	animationClip_.sampleRate = 30.0f;
	animationClip_.loop = true;
}

void EditorAnimationWindowManager::Update() {
	const std::chrono::steady_clock::time_point currentUpdateTime = std::chrono::steady_clock::now();
	const float deltaTime = std::chrono::duration<float>(currentUpdateTime - previousUpdateTime_).count();
	previousUpdateTime_ = currentUpdateTime;

	if (!g_isAnimationWindowVisible) {
		EndRecording(true);
		RestorePreview();
		isPreviewPlaying_ = false;
		return;
	}

	SynchronizeSelectedAsset();

	if (isPreviewPlaying_) {
		currentTime_ += (std::clamp)(deltaTime, 0.0f, 0.1f);

		if (currentTime_ > animationClip_.durationSeconds) {
			currentTime_ = animationClip_.loop
				? std::fmod(currentTime_, (std::max)(animationClip_.durationSeconds, kMinimumClipDuration))
				: animationClip_.durationSeconds;
			isPreviewPlaying_ = animationClip_.loop;
		}
	}

	if (isRecording_) {
		UpdateRecording();
	}
	else if (isPreviewActive_) {
		ApplyPreview();
	}
}

void EditorAnimationWindowManager::Draw() {
#ifdef USE_IMGUI
	if (!g_isAnimationWindowVisible) {
		return;
	}

	const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
	if (!ImGui::Begin("アニメーション###AnimationWindow", &g_isAnimationWindowVisible, windowFlags)) {
		ImGui::End();
		return;
	}

	DrawToolbar();
	ImGui::Separator();

	const float trackPanelWidth = (std::clamp)(ImGui::GetContentRegionAvail().x * 0.32f, 240.0f, 420.0f);
	if (ImGui::BeginChild("AnimationTrackPanel", ImVec2(trackPanelWidth, 330.0f), true)) {
		DrawTrackList();
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("AnimationTimelinePanel", ImVec2(0.0f, 330.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
		DrawTimeline();
	}
	ImGui::EndChild();

	DrawSelectedKeyEditor();
	DrawEventEditor();
	ImGui::End();
#endif
}

void EditorAnimationWindowManager::SynchronizeSelectedAsset() {
	std::string requestedPath;

	if (EditorAssetUtility::HasExtension(g_selectedAssetPath, ".animclip")) {
		requestedPath = g_selectedAssetPath;
	}
	else {
		const EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
		if (gameObject != nullptr) {
			const EditorComponent* animationComponent = EditorComponentUtility::FindComponent(
				*gameObject,
				EditorComponentType::Animation);

			if (animationComponent != nullptr &&
				EditorAssetUtility::HasExtension(animationComponent->assetPath, ".animclip")) {
				requestedPath = animationComponent->assetPath;
			}
		}
	}

	if (!requestedPath.empty() && requestedPath != animationClipPath_ && !isDirty_) {
		LoadAnimationClip(requestedPath);
	}
}

void EditorAnimationWindowManager::CreateAnimationClip() {
	//================================================================
	// Animation Clip の保存先を準備
	//================================================================

	const std::filesystem::path animationDirectory = std::filesystem::path("Assets") / "Animation";
	std::error_code fileSystemError;
	std::filesystem::create_directories(animationDirectory, fileSystemError);

	if (fileSystemError) {
		g_editorConsoleMessages.push_back("Animation: Assets/Animation フォルダーを作成できません");
		return;
	}

	std::filesystem::path animationPath = animationDirectory / "NewAnimationClip.animclip";
	int32_t duplicateNumber = 1;

	while (std::filesystem::exists(animationPath)) {
		animationPath = animationDirectory /
			("NewAnimationClip_" + std::to_string(duplicateNumber) + ".animclip");
		++duplicateNumber;
	}

	//================================================================
	// 空の Clip を作成して選択 GameObject へ設定
	//================================================================

	EndRecording(true);
	isPreviewPlaying_ = false;
	animationClip_ = PropertyAnimationClip{};
	animationClip_.name = animationPath.stem().string();
	animationClip_.durationSeconds = 1.0f;
	animationClip_.sampleRate = 30.0f;
	animationClip_.loop = true;
	animationClipPath_ = animationPath.generic_string();
	currentTime_ = 0.0f;
	selectedTrackIndex_ = -1;
	selectedKeyIndex_ = -1;
	selectedEventIndex_ = -1;

	if (!animationClip_.SaveToJson(animationClipPath_)) {
		g_editorConsoleMessages.push_back("Animation: 新規 Clip を保存できません: " + animationClipPath_);
		animationClipPath_.clear();
		return;
	}

	isDirty_ = false;
	g_selectedAssetPath = animationClipPath_;
	AssignClipToSelectedGameObject();
	g_editorConsoleMessages.push_back("Animation: 新規 Clip を作成しました: " + animationClipPath_);
}

bool EditorAnimationWindowManager::LoadAnimationClip(const std::string& filePath) {
	PropertyAnimationClip loadedClip{};

	if (!loadedClip.LoadFromJson(filePath)) {
		g_editorConsoleMessages.push_back("Animation: Clip 読み込み失敗: " + filePath);
		return false;
	}

	EndRecording(true);
	isPreviewPlaying_ = false;
	animationClip_ = std::move(loadedClip);
	animationClipPath_ = filePath;
	currentTime_ = 0.0f;
	selectedTrackIndex_ = animationClip_.tracks.empty() ? -1 : 0;
	selectedKeyIndex_ = -1;
	selectedEventIndex_ = -1;
	isDirty_ = false;
	g_editorConsoleMessages.push_back("Animation: Clip を開きました: " + filePath);
	return true;
}

void EditorAnimationWindowManager::SaveAnimationClip() {
	if (animationClipPath_.empty()) {
		g_editorConsoleMessages.push_back("Animation: 保存先 .animclip が選択されていません");
		return;
	}

	if (animationClip_.SaveToJson(animationClipPath_)) {
		isDirty_ = false;
		g_editorConsoleMessages.push_back("Animation: Clip を保存しました: " + animationClipPath_);
	}
	else {
		g_editorConsoleMessages.push_back("Animation: Clip 保存失敗: " + animationClipPath_);
	}
}

void EditorAnimationWindowManager::AssignClipToSelectedGameObject() {
	if (animationClipPath_.empty() || g_selectedEditorGameObjectId < 0) {
		return;
	}

	if (!g_editorScene.HasComponent(g_selectedEditorGameObjectId, EditorComponentType::Animation)) {
		g_editorScene.AddComponent(g_selectedEditorGameObjectId, EditorComponentType::Animation);
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	EditorComponent* animationComponent = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Animation)
		: nullptr;

	if (animationComponent != nullptr) {
		animationComponent->assetPath = animationClipPath_;
		animationComponent->animationType = 0;
		g_editorConsoleMessages.push_back("Animation: 選択 GameObject へ Clip を設定しました");
	}
}

void EditorAnimationWindowManager::DrawToolbar() {
#ifdef USE_IMGUI
	ImGui::Text("Clip: %s%s", animationClipPath_.empty() ? "未選択" : animationClipPath_.c_str(), isDirty_ ? " *" : "");
	ImGui::TextDisabled("操作: 時間を選ぶ -> ●自動記録 -> ギズモまたはInspectorで値を変更 -> プレビュー再生");

	if (ImGui::Button("新規Clip", ImVec2(90.0f, 0.0f))) {
		CreateAnimationClip();
	}
	ImGui::SameLine();

	if (ImGui::Button("保存", ImVec2(80.0f, 0.0f))) {
		SaveAnimationClip();
	}
	ImGui::SameLine();

	if (ImGui::Button("再読込", ImVec2(80.0f, 0.0f)) && !animationClipPath_.empty()) {
		LoadAnimationClip(animationClipPath_);
	}
	ImGui::SameLine();

	if (ImGui::Button("選択オブジェクトへ設定", ImVec2(180.0f, 0.0f))) {
		AssignClipToSelectedGameObject();
	}
	ImGui::SameLine();

	if (ImGui::Button(isPreviewPlaying_ ? "一時停止" : "プレビュー再生", ImVec2(120.0f, 0.0f))) {
		EndRecording(false);

		if (!isPreviewActive_) {
			BeginPreview();
		}

		isPreviewPlaying_ = isPreviewActive_ && !isPreviewPlaying_;
	}
	ImGui::SameLine();

	if (ImGui::Button("停止", ImVec2(70.0f, 0.0f))) {
		isPreviewPlaying_ = false;
		EndRecording(false);
		currentTime_ = 0.0f;
		RestorePreview();
	}
	ImGui::SameLine();

	if (isRecording_) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.12f, 0.12f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.18f, 0.18f, 1.0f));
	}

	if (ImGui::Button(isRecording_ ? "● 記録中" : "● 自動記録", ImVec2(110.0f, 0.0f))) {
		if (isRecording_) {
			EndRecording(true);
		}
		else {
			BeginRecording();
		}
	}

	if (isRecording_) {
		ImGui::PopStyleColor(2);
	}
	ImGui::SameLine();

	if (ImGui::Button("現在の姿勢をキー", ImVec2(150.0f, 0.0f))) {
		RecordCurrentTransform();
	}

	if (g_editorRuntimeManager.IsPlaying()) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
			"上部Play中です。自動記録はRuntimeの移動も値変更として記録します。");
	}

	char clipNameBuffer[128]{};
	strncpy_s(clipNameBuffer, animationClip_.name.c_str(), _TRUNCATE);
	if (ImGui::InputText("名前", clipNameBuffer, sizeof(clipNameBuffer))) {
		animationClip_.name = clipNameBuffer;
		isDirty_ = true;
	}

	if (ImGui::DragFloat("長さ (秒)", &animationClip_.durationSeconds, 0.01f, kMinimumClipDuration, 3600.0f, "%.3f")) {
		animationClip_.durationSeconds = (std::max)(animationClip_.durationSeconds, kMinimumClipDuration);
		currentTime_ = (std::min)(currentTime_, animationClip_.durationSeconds);
		isDirty_ = true;
	}

	if (ImGui::DragFloat("サンプルレート", &animationClip_.sampleRate, 1.0f, 1.0f, 240.0f, "%.0f fps")) {
		animationClip_.sampleRate = (std::clamp)(animationClip_.sampleRate, 1.0f, 240.0f);
		isDirty_ = true;
	}

	ImGui::SameLine();
	if (ImGui::Checkbox("ループ", &animationClip_.loop)) {
		isDirty_ = true;
	}

	if (ImGui::SliderFloat("現在時間", &currentTime_, 0.0f, animationClip_.durationSeconds, "%.3f 秒")) {
		if (!isPreviewActive_) {
			BeginPreview();
		}

		ApplyPreview();

		if (isRecording_) {
			EditorGameObject* gameObject = g_editorScene.FindGameObject(recordingGameObjectId_);

			if (gameObject != nullptr) {
				CaptureRecordingValues(*gameObject);
			}
		}
	}
#endif
}

void EditorAnimationWindowManager::DrawTrackList() {
#ifdef USE_IMGUI
	ImGui::TextUnformatted("プロパティトラック");

	if (ImGui::BeginCombo("追加対象", GetAnimationPropertyTargetName(
		static_cast<AnimationPropertyTarget>(addPropertyTargetIndex_)))) {
		for (int32_t targetIndex = 0; targetIndex < kAnimationPropertyTargetCount; ++targetIndex) {
			const bool isSelected = addPropertyTargetIndex_ == targetIndex;
			if (ImGui::Selectable(
				GetAnimationPropertyTargetName(static_cast<AnimationPropertyTarget>(targetIndex)),
				isSelected)) {
				addPropertyTargetIndex_ = targetIndex;
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("トラックを追加", ImVec2(-1.0f, 0.0f))) {
		AddPropertyTrack();
	}

	ImGui::Separator();
	for (int32_t trackIndex = 0; trackIndex < static_cast<int32_t>(animationClip_.tracks.size()); ++trackIndex) {
		const PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(trackIndex)];
		ImGui::PushID(trackIndex);
		const std::string trackLabel = std::string(GetAnimationPropertyTargetName(track.target)) +
			"  [" + GetWriteModeDisplayName(track.writeMode) + "]";

		if (ImGui::Selectable(trackLabel.c_str(), selectedTrackIndex_ == trackIndex)) {
			selectedTrackIndex_ = trackIndex;
			selectedKeyIndex_ = -1;
		}
		ImGui::PopID();
	}

	if (selectedTrackIndex_ >= 0 && selectedTrackIndex_ < static_cast<int32_t>(animationClip_.tracks.size())) {
		PropertyAnimationTrack& selectedTrack = animationClip_.tracks[static_cast<size_t>(selectedTrackIndex_)];
		int32_t writeMode = static_cast<int32_t>(selectedTrack.writeMode);
		const char* writeModeItems[] = {"上書き", "加算", "乗算"};

		if (ImGui::Combo("適用方法", &writeMode, writeModeItems, _countof(writeModeItems))) {
			selectedTrack.writeMode = static_cast<AnimationPropertyWriteMode>(writeMode);
			isDirty_ = true;
		}

		if (ImGui::Button("現在値をキー追加", ImVec2(-1.0f, 0.0f))) {
			const EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
			float currentValue = 0.0f;

			if (gameObject != nullptr && ReadProperty(*gameObject, selectedTrack.target, currentValue)) {
				AddOrUpdateKey(selectedTrackIndex_, currentValue);
			}
		}

		if (ImGui::Button("選択トラックを削除", ImVec2(-1.0f, 0.0f))) {
			animationClip_.tracks.erase(animationClip_.tracks.begin() + selectedTrackIndex_);
			selectedTrackIndex_ = -1;
			selectedKeyIndex_ = -1;
			isDirty_ = true;
		}
	}
#endif
}

void EditorAnimationWindowManager::DrawTimeline() {
#ifdef USE_IMGUI
	ImGui::SetNextItemWidth(160.0f);
	ImGui::SliderFloat("表示倍率", &timelinePixelsPerSecond_, kMinimumTimelineScale, kMaximumTimelineScale, "%.0f px/s");

	const float rowHeight = 30.0f;
	const float rulerHeight = 26.0f;
	const float timelineWidth = (std::max)(
		animationClip_.durationSeconds * timelinePixelsPerSecond_ + 32.0f,
		ImGui::GetContentRegionAvail().x);
	const float timelineHeight = rulerHeight +
		(std::max)(1.0f, static_cast<float>(animationClip_.tracks.size())) * rowHeight;
	const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("AnimationTimelineCanvas", ImVec2(timelineWidth, timelineHeight));
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 canvasEnd{canvasPosition.x + timelineWidth, canvasPosition.y + timelineHeight};
	drawList->AddRectFilled(canvasPosition, canvasEnd, IM_COL32(19, 24, 31, 255));

	const int32_t wholeSecondCount = static_cast<int32_t>(std::ceil(animationClip_.durationSeconds));
	for (int32_t second = 0; second <= wholeSecondCount; ++second) {
		const float x = canvasPosition.x + static_cast<float>(second) * timelinePixelsPerSecond_;
		drawList->AddLine(ImVec2(x, canvasPosition.y), ImVec2(x, canvasEnd.y), IM_COL32(72, 82, 96, 255));

		char secondLabel[32]{};
		sprintf_s(secondLabel, "%.1fs", static_cast<float>(second));
		drawList->AddText(ImVec2(x + 3.0f, canvasPosition.y + 3.0f), IM_COL32(200, 205, 214, 255), secondLabel);
	}

	for (int32_t trackIndex = 0; trackIndex < static_cast<int32_t>(animationClip_.tracks.size()); ++trackIndex) {
		const float rowTop = canvasPosition.y + rulerHeight + static_cast<float>(trackIndex) * rowHeight;
		const ImU32 rowColor = trackIndex == selectedTrackIndex_
			? IM_COL32(42, 64, 88, 255)
			: IM_COL32(27, 33, 42, 255);
		drawList->AddRectFilled(ImVec2(canvasPosition.x, rowTop), ImVec2(canvasEnd.x, rowTop + rowHeight), rowColor);
		drawList->AddLine(ImVec2(canvasPosition.x, rowTop + rowHeight), ImVec2(canvasEnd.x, rowTop + rowHeight), IM_COL32(50, 58, 68, 255));

		const PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(trackIndex)];
		for (int32_t keyIndex = 0; keyIndex < static_cast<int32_t>(track.keyframes.size()); ++keyIndex) {
			const PropertyAnimationKeyframe& keyframe = track.keyframes[static_cast<size_t>(keyIndex)];
			const float keyX = canvasPosition.x + keyframe.time * timelinePixelsPerSecond_;
			const float keyY = rowTop + rowHeight * 0.5f;
			const bool isSelectedKey = trackIndex == selectedTrackIndex_ && keyIndex == selectedKeyIndex_;
			const ImU32 keyColor = isSelectedKey ? IM_COL32(255, 190, 55, 255) : IM_COL32(105, 190, 255, 255);
			const ImVec2 keyPoints[] = {
				ImVec2(keyX, keyY - 6.0f),
				ImVec2(keyX + 6.0f, keyY),
				ImVec2(keyX, keyY + 6.0f),
				ImVec2(keyX - 6.0f, keyY)};
			drawList->AddConvexPolyFilled(keyPoints, 4, keyColor);
		}
	}

	const float playheadX = canvasPosition.x + currentTime_ * timelinePixelsPerSecond_;
	drawList->AddLine(ImVec2(playheadX, canvasPosition.y), ImVec2(playheadX, canvasEnd.y), IM_COL32(255, 80, 70, 255), 2.0f);

	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 mousePosition = ImGui::GetIO().MousePos;
		const float clickedTime = (std::clamp)(
			(mousePosition.x - canvasPosition.x) / timelinePixelsPerSecond_,
			0.0f,
			animationClip_.durationSeconds);
		const int32_t clickedTrack = static_cast<int32_t>((mousePosition.y - canvasPosition.y - rulerHeight) / rowHeight);
		currentTime_ = clickedTime;
		selectedKeyIndex_ = -1;

		if (clickedTrack >= 0 && clickedTrack < static_cast<int32_t>(animationClip_.tracks.size())) {
			selectedTrackIndex_ = clickedTrack;
			const PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(clickedTrack)];

			for (int32_t keyIndex = 0; keyIndex < static_cast<int32_t>(track.keyframes.size()); ++keyIndex) {
				if (std::fabs(track.keyframes[static_cast<size_t>(keyIndex)].time - clickedTime) * timelinePixelsPerSecond_ <= 9.0f) {
					selectedKeyIndex_ = keyIndex;
					currentTime_ = track.keyframes[static_cast<size_t>(keyIndex)].time;
					break;
				}
			}
		}

		if (!isPreviewActive_) {
			BeginPreview();
		}

		ApplyPreview();

		if (isRecording_) {
			EditorGameObject* gameObject = g_editorScene.FindGameObject(recordingGameObjectId_);

			if (gameObject != nullptr) {
				CaptureRecordingValues(*gameObject);
			}
		}
	}

	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
		selectedTrackIndex_ >= 0 && selectedKeyIndex_ >= 0) {
		PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(selectedTrackIndex_)];

		if (selectedKeyIndex_ < static_cast<int32_t>(track.keyframes.size())) {
			const float draggedTime = (std::clamp)(
				(ImGui::GetIO().MousePos.x - canvasPosition.x) / timelinePixelsPerSecond_,
				0.0f,
				animationClip_.durationSeconds);
			track.keyframes[static_cast<size_t>(selectedKeyIndex_)].time = draggedTime;
			currentTime_ = draggedTime;
			isDirty_ = true;
			ApplyPreview();
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && selectedTrackIndex_ >= 0 && selectedKeyIndex_ >= 0) {
		SortSelectedTrack(currentTime_);
	}
#endif
}

void EditorAnimationWindowManager::DrawSelectedKeyEditor() {
#ifdef USE_IMGUI
	if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= static_cast<int32_t>(animationClip_.tracks.size())) {
		return;
	}

	PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(selectedTrackIndex_)];
	if (selectedKeyIndex_ < 0 || selectedKeyIndex_ >= static_cast<int32_t>(track.keyframes.size())) {
		return;
	}

	if (!ImGui::CollapsingHeader("選択キーフレーム", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	PropertyAnimationKeyframe& keyframe = track.keyframes[static_cast<size_t>(selectedKeyIndex_)];
	if (ImGui::DragFloat("時刻 (秒)", &keyframe.time, 0.01f, 0.0f, animationClip_.durationSeconds, "%.3f")) {
		keyframe.time = (std::clamp)(keyframe.time, 0.0f, animationClip_.durationSeconds);
		currentTime_ = keyframe.time;
		isDirty_ = true;
	}

	if (ImGui::DragFloat("値", &keyframe.value, 0.01f)) {
		isDirty_ = true;
		ApplyPreview();
	}

	if (keyframe.interpolation == AnimationCurveInterpolation::CubicHermite) {
		if (ImGui::DragFloat("入る接線", &keyframe.inTangent, 0.01f)) {
			isDirty_ = true;
		}

		if (ImGui::DragFloat("出る接線", &keyframe.outTangent, 0.01f)) {
			isDirty_ = true;
		}
	}

	int32_t interpolation = static_cast<int32_t>(keyframe.interpolation);
	const char* interpolationItems[] = {
		"一定 (Step)",
		"直線 (Linear)",
		"滑らか (Cubic Hermite)"};
	if (ImGui::Combo("補間", &interpolation, interpolationItems, _countof(interpolationItems))) {
		keyframe.interpolation = static_cast<AnimationCurveInterpolation>(interpolation);
		isDirty_ = true;
	}

	ImGui::TextDisabled("現在: %s", GetInterpolationDisplayName(keyframe.interpolation));
	if (ImGui::Button("このキーを削除")) {
		DeleteSelectedKey();
	}
#endif
}

void EditorAnimationWindowManager::DrawEventEditor() {
#ifdef USE_IMGUI
	if (!ImGui::CollapsingHeader("アニメーションイベント")) {
		return;
	}

	if (ImGui::Button("現在時間へイベント追加")) {
		PropertyAnimationEvent animationEvent{};
		animationEvent.time = currentTime_;
		animationEvent.name = "AnimationEvent";
		animationClip_.events.push_back(animationEvent);
		selectedEventIndex_ = static_cast<int32_t>(animationClip_.events.size()) - 1;
		isDirty_ = true;
	}

	for (int32_t eventIndex = 0; eventIndex < static_cast<int32_t>(animationClip_.events.size()); ++eventIndex) {
		const PropertyAnimationEvent& animationEvent = animationClip_.events[static_cast<size_t>(eventIndex)];
		const std::string label = std::to_string(animationEvent.time) + "s  " + animationEvent.name;
		if (ImGui::Selectable(label.c_str(), selectedEventIndex_ == eventIndex)) {
			selectedEventIndex_ = eventIndex;
			currentTime_ = animationEvent.time;
		}
	}

	if (selectedEventIndex_ < 0 || selectedEventIndex_ >= static_cast<int32_t>(animationClip_.events.size())) {
		return;
	}

	PropertyAnimationEvent& animationEvent = animationClip_.events[static_cast<size_t>(selectedEventIndex_)];
	char eventNameBuffer[128]{};
	char effectPathBuffer[512]{};
	strncpy_s(eventNameBuffer, animationEvent.name.c_str(), _TRUNCATE);
	strncpy_s(effectPathBuffer, animationEvent.effectAssetPath.c_str(), _TRUNCATE);

	if (ImGui::DragFloat("イベント時刻", &animationEvent.time, 0.01f, 0.0f, animationClip_.durationSeconds, "%.3f")) {
		animationEvent.time = (std::clamp)(animationEvent.time, 0.0f, animationClip_.durationSeconds);
		isDirty_ = true;
	}

	if (ImGui::InputText("イベント名", eventNameBuffer, sizeof(eventNameBuffer))) {
		animationEvent.name = eventNameBuffer;
		isDirty_ = true;
	}

	if (ImGui::InputText("Effect パス", effectPathBuffer, sizeof(effectPathBuffer))) {
		animationEvent.effectAssetPath = effectPathBuffer;
		isDirty_ = true;
	}

	float localOffset[] = {
		animationEvent.localOffset.x,
		animationEvent.localOffset.y,
		animationEvent.localOffset.z};
	if (ImGui::DragFloat3("ローカル発生位置", localOffset, 0.01f)) {
		animationEvent.localOffset = {localOffset[0], localOffset[1], localOffset[2]};
		isDirty_ = true;
	}

	if (ImGui::Button("選択イベントを削除")) {
		animationClip_.events.erase(animationClip_.events.begin() + selectedEventIndex_);
		selectedEventIndex_ = -1;
		isDirty_ = true;
	}
#endif
}

void EditorAnimationWindowManager::AddPropertyTrack() {
	const AnimationPropertyTarget target = static_cast<AnimationPropertyTarget>(addPropertyTargetIndex_);
	const auto existingTrackIterator = std::find_if(
		animationClip_.tracks.begin(),
		animationClip_.tracks.end(),
		[target](const PropertyAnimationTrack& track) {
			return track.target == target;
		});

	if (existingTrackIterator != animationClip_.tracks.end()) {
		selectedTrackIndex_ = static_cast<int32_t>(std::distance(animationClip_.tracks.begin(), existingTrackIterator));
		return;
	}

	PropertyAnimationTrack track{};
	track.target = target;
	track.writeMode = AnimationPropertyWriteMode::Override;
	animationClip_.tracks.push_back(track);
	selectedTrackIndex_ = static_cast<int32_t>(animationClip_.tracks.size()) - 1;
	selectedKeyIndex_ = -1;
	isDirty_ = true;
}

int32_t EditorAnimationWindowManager::FindOrCreateTrack(
	AnimationPropertyTarget target,
	float initialValue) {
	const auto existingTrackIterator = std::find_if(
		animationClip_.tracks.begin(),
		animationClip_.tracks.end(),
		[target](const PropertyAnimationTrack& track) {
			return track.target == target;
		});

	int32_t trackIndex = -1;

	if (existingTrackIterator != animationClip_.tracks.end()) {
		trackIndex = static_cast<int32_t>(std::distance(animationClip_.tracks.begin(), existingTrackIterator));
	}
	else {
		PropertyAnimationTrack track{};
		track.target = target;
		track.writeMode = AnimationPropertyWriteMode::Override;
		animationClip_.tracks.push_back(track);
		trackIndex = static_cast<int32_t>(animationClip_.tracks.size()) - 1;
		isDirty_ = true;
	}

	// 途中の時刻から初めて記録した場合でも、0 秒から値が固定されないよう元の姿勢を補う。
	PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(trackIndex)];
	const float keyTolerance = 0.5f / (std::max)(animationClip_.sampleRate, 1.0f);

	if (track.keyframes.empty() && currentTime_ > keyTolerance) {
		PropertyAnimationKeyframe initialKeyframe{};
		initialKeyframe.time = 0.0f;
		initialKeyframe.value = initialValue;
		initialKeyframe.interpolation = AnimationCurveInterpolation::Linear;
		track.keyframes.push_back(initialKeyframe);
		isDirty_ = true;
	}

	return trackIndex;
}

void EditorAnimationWindowManager::AddOrUpdateKey(int32_t trackIndex, float value) {
	if (trackIndex < 0 || trackIndex >= static_cast<int32_t>(animationClip_.tracks.size())) {
		return;
	}

	PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(trackIndex)];
	const float keyTolerance = 0.5f / (std::max)(animationClip_.sampleRate, 1.0f);

	for (int32_t keyIndex = 0; keyIndex < static_cast<int32_t>(track.keyframes.size()); ++keyIndex) {
		PropertyAnimationKeyframe& keyframe = track.keyframes[static_cast<size_t>(keyIndex)];

		if (std::fabs(keyframe.time - currentTime_) <= keyTolerance) {
			keyframe.time = currentTime_;
			keyframe.value = value;
			selectedTrackIndex_ = trackIndex;
			selectedKeyIndex_ = keyIndex;
			isDirty_ = true;
			return;
		}
	}

	PropertyAnimationKeyframe keyframe{};
	keyframe.time = currentTime_;
	keyframe.value = value;
	keyframe.interpolation = AnimationCurveInterpolation::Linear;
	track.keyframes.push_back(keyframe);
	selectedTrackIndex_ = trackIndex;
	SortSelectedTrack(currentTime_);
	isDirty_ = true;
}

void EditorAnimationWindowManager::RecordCurrentTransform() {
	if (animationClipPath_.empty()) {
		g_editorConsoleMessages.push_back("Animation: 先に「新規Clip」を押してください");
		return;
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);

	if (gameObject == nullptr) {
		g_editorConsoleMessages.push_back("Animation: キーを記録する GameObject が選択されていません");
		return;
	}

	if (!isPreviewActive_ || previewGameObjectId_ != gameObject->id) {
		BeginPreview();
		gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	}

	if (gameObject == nullptr) {
		return;
	}

	// Transform の 9 軸を一括で記録する。Light と Material は自動記録で変更した項目だけ作る。
	for (int32_t targetIndex = static_cast<int32_t>(AnimationPropertyTarget::TransformPositionX);
		targetIndex <= static_cast<int32_t>(AnimationPropertyTarget::TransformScaleZ);
		++targetIndex) {
		const AnimationPropertyTarget target = static_cast<AnimationPropertyTarget>(targetIndex);
		float currentValue = 0.0f;
		float initialValue = 0.0f;

		if (!ReadProperty(*gameObject, target, currentValue)) {
			continue;
		}

		if (!ReadProperty(previewBackup_, target, initialValue)) {
			initialValue = currentValue;
		}

		const int32_t trackIndex = FindOrCreateTrack(target, initialValue);
		AddOrUpdateKey(trackIndex, currentValue);
	}

	CaptureRecordingValues(*gameObject);
	SynchronizeRenderedScene();
}

void EditorAnimationWindowManager::DeleteSelectedKey() {
	if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= static_cast<int32_t>(animationClip_.tracks.size())) {
		return;
	}

	PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(selectedTrackIndex_)];
	if (selectedKeyIndex_ < 0 || selectedKeyIndex_ >= static_cast<int32_t>(track.keyframes.size())) {
		return;
	}

	track.keyframes.erase(track.keyframes.begin() + selectedKeyIndex_);
	selectedKeyIndex_ = -1;
	isDirty_ = true;
}

void EditorAnimationWindowManager::SortSelectedTrack(float selectedTime) {
	if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= static_cast<int32_t>(animationClip_.tracks.size())) {
		return;
	}

	PropertyAnimationTrack& track = animationClip_.tracks[static_cast<size_t>(selectedTrackIndex_)];
	std::sort(
		track.keyframes.begin(),
		track.keyframes.end(),
		[](const PropertyAnimationKeyframe& leftKey, const PropertyAnimationKeyframe& rightKey) {
			return leftKey.time < rightKey.time;
		});

	selectedKeyIndex_ = -1;
	for (int32_t keyIndex = 0; keyIndex < static_cast<int32_t>(track.keyframes.size()); ++keyIndex) {
		if (std::fabs(track.keyframes[static_cast<size_t>(keyIndex)].time - selectedTime) <= 0.0001f) {
			selectedKeyIndex_ = keyIndex;
			break;
		}
	}
}

void EditorAnimationWindowManager::BeginRecording() {
	if (animationClipPath_.empty()) {
		g_editorConsoleMessages.push_back("Animation: 先に「新規Clip」を押してください");
		return;
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);

	if (gameObject == nullptr) {
		g_editorConsoleMessages.push_back("Animation: 自動記録する GameObject が選択されていません");
		return;
	}

	isPreviewPlaying_ = false;

	if (!isPreviewActive_ || previewGameObjectId_ != gameObject->id) {
		BeginPreview();
		gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	}

	if (gameObject == nullptr || !isPreviewActive_) {
		return;
	}

	recordingGameObjectId_ = gameObject->id;
	CaptureRecordingValues(*gameObject);
	isRecording_ = true;
	g_editorConsoleMessages.push_back("Animation: 自動記録を開始しました。ギズモまたはInspectorで値を変更してください");
}

void EditorAnimationWindowManager::EndRecording(bool restorePreview) {
	if (isRecording_) {
		g_editorConsoleMessages.push_back("Animation: 自動記録を終了しました");
	}

	isRecording_ = false;
	recordingGameObjectId_ = -1;
	hasRecordedPropertyValue_.fill(false);

	if (restorePreview) {
		RestorePreview();
	}
}

void EditorAnimationWindowManager::CaptureRecordingValues(const EditorGameObject& gameObject) {
	for (size_t targetIndex = 0u; targetIndex < kRecordedPropertyCount; ++targetIndex) {
		float currentValue = 0.0f;
		const AnimationPropertyTarget target = static_cast<AnimationPropertyTarget>(targetIndex);
		hasRecordedPropertyValue_[targetIndex] = ReadProperty(gameObject, target, currentValue);
		recordedPropertyValues_[targetIndex] = currentValue;
	}
}

void EditorAnimationWindowManager::BeginPreview() {
	RestorePreview();
	EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);

	if (gameObject == nullptr) {
		return;
	}

	previewBackup_ = *gameObject;
	previewGameObjectId_ = gameObject->id;
	isPreviewActive_ = true;
	ApplyPreview();
}

void EditorAnimationWindowManager::RestorePreview() {
	if (!isPreviewActive_) {
		return;
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(previewGameObjectId_);
	if (gameObject != nullptr) {
		*gameObject = previewBackup_;
	}

	isPreviewActive_ = false;
	previewGameObjectId_ = -1;
	SynchronizeRenderedScene();
}

void EditorAnimationWindowManager::ApplyPreview() {
	if (!isPreviewActive_) {
		return;
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(previewGameObjectId_);
	if (gameObject == nullptr) {
		isPreviewActive_ = false;
		previewGameObjectId_ = -1;
		return;
	}

	*gameObject = previewBackup_;
	for (const PropertyAnimationTrack& track : animationClip_.tracks) {
		float baseValue = 0.0f;

		if (!ReadProperty(previewBackup_, track.target, baseValue) || track.keyframes.empty()) {
			continue;
		}

		const float sampledValue = animationClip_.SampleTrack(track, currentTime_);
		float outputValue = sampledValue;

		if (track.writeMode == AnimationPropertyWriteMode::Additive) {
			outputValue = baseValue + sampledValue;
		}
		else if (track.writeMode == AnimationPropertyWriteMode::Multiply) {
			outputValue = baseValue * sampledValue;
		}

		WriteProperty(*gameObject, track.target, outputValue);
	}

	SynchronizeRenderedScene();
}

void EditorAnimationWindowManager::UpdateRecording() {
	EditorGameObject* gameObject = g_editorScene.FindGameObject(recordingGameObjectId_);

	if (gameObject == nullptr) {
		EndRecording(true);
		return;
	}

	if (g_selectedEditorGameObjectId != recordingGameObjectId_) {
		g_editorConsoleMessages.push_back("Animation: 選択 GameObject が変わったため自動記録を停止しました");
		EndRecording(true);
		return;
	}

	for (size_t targetIndex = 0u; targetIndex < kRecordedPropertyCount; ++targetIndex) {
		const AnimationPropertyTarget target = static_cast<AnimationPropertyTarget>(targetIndex);
		float currentValue = 0.0f;

		if (!ReadProperty(*gameObject, target, currentValue)) {
			hasRecordedPropertyValue_[targetIndex] = false;
			continue;
		}

		if (!hasRecordedPropertyValue_[targetIndex]) {
			hasRecordedPropertyValue_[targetIndex] = true;
			recordedPropertyValues_[targetIndex] = currentValue;
			continue;
		}

		if (std::fabs(currentValue - recordedPropertyValues_[targetIndex]) > 0.00001f) {
			const int32_t trackIndex = FindOrCreateTrack(target, recordedPropertyValues_[targetIndex]);
			AddOrUpdateKey(trackIndex, currentValue);
			recordedPropertyValues_[targetIndex] = currentValue;
		}
	}
}

bool EditorAnimationWindowManager::ReadProperty(
	const EditorGameObject& gameObject,
	AnimationPropertyTarget target,
	float& value) const {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX: value = gameObject.translate.x; return true;
	case AnimationPropertyTarget::TransformPositionY: value = gameObject.translate.y; return true;
	case AnimationPropertyTarget::TransformPositionZ: value = gameObject.translate.z; return true;
	case AnimationPropertyTarget::TransformRotationXDegrees: value = gameObject.rotate.x * kRadiansToDegrees; return true;
	case AnimationPropertyTarget::TransformRotationYDegrees: value = gameObject.rotate.y * kRadiansToDegrees; return true;
	case AnimationPropertyTarget::TransformRotationZDegrees: value = gameObject.rotate.z * kRadiansToDegrees; return true;
	case AnimationPropertyTarget::TransformScaleX: value = gameObject.scale.x; return true;
	case AnimationPropertyTarget::TransformScaleY: value = gameObject.scale.y; return true;
	case AnimationPropertyTarget::TransformScaleZ: value = gameObject.scale.z; return true;
	default: break;
	}

	if (target == AnimationPropertyTarget::LightIntensity || target == AnimationPropertyTarget::LightRange) {
		const EditorComponent* lightComponent = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Light);

		if (lightComponent == nullptr) {
			return false;
		}

		value = target == AnimationPropertyTarget::LightIntensity
			? lightComponent->intensity
			: lightComponent->colliderRadius;
		return true;
	}

	const EditorComponent* materialComponent = FindMaterialComponent(gameObject);
	if (materialComponent == nullptr) {
		return false;
	}

	switch (target) {
	case AnimationPropertyTarget::MaterialBaseColorR: value = materialComponent->color.x; return true;
	case AnimationPropertyTarget::MaterialBaseColorG: value = materialComponent->color.y; return true;
	case AnimationPropertyTarget::MaterialBaseColorB: value = materialComponent->color.z; return true;
	case AnimationPropertyTarget::MaterialMetallic: value = materialComponent->metallic; return true;
	case AnimationPropertyTarget::MaterialRoughness: value = materialComponent->roughness; return true;
	case AnimationPropertyTarget::MaterialAlpha: value = materialComponent->alpha; return true;
	case AnimationPropertyTarget::MaterialEmissionStrength: value = materialComponent->emissionStrength; return true;
	case AnimationPropertyTarget::MaterialEmissionColorR: value = materialComponent->emissionColor.x; return true;
	case AnimationPropertyTarget::MaterialEmissionColorG: value = materialComponent->emissionColor.y; return true;
	case AnimationPropertyTarget::MaterialEmissionColorB: value = materialComponent->emissionColor.z; return true;
	default: return false;
	}
}

bool EditorAnimationWindowManager::WriteProperty(
	EditorGameObject& gameObject,
	AnimationPropertyTarget target,
	float value) const {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX: gameObject.translate.x = value; return true;
	case AnimationPropertyTarget::TransformPositionY: gameObject.translate.y = value; return true;
	case AnimationPropertyTarget::TransformPositionZ: gameObject.translate.z = value; return true;
	case AnimationPropertyTarget::TransformRotationXDegrees: gameObject.rotate.x = value * kDegreesToRadians; return true;
	case AnimationPropertyTarget::TransformRotationYDegrees: gameObject.rotate.y = value * kDegreesToRadians; return true;
	case AnimationPropertyTarget::TransformRotationZDegrees: gameObject.rotate.z = value * kDegreesToRadians; return true;
	case AnimationPropertyTarget::TransformScaleX: gameObject.scale.x = value; return true;
	case AnimationPropertyTarget::TransformScaleY: gameObject.scale.y = value; return true;
	case AnimationPropertyTarget::TransformScaleZ: gameObject.scale.z = value; return true;
	default: break;
	}

	if (target == AnimationPropertyTarget::LightIntensity || target == AnimationPropertyTarget::LightRange) {
		EditorComponent* lightComponent = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Light);

		if (lightComponent == nullptr) {
			return false;
		}

		if (target == AnimationPropertyTarget::LightIntensity) {
			lightComponent->intensity = value;
		}
		else {
			lightComponent->colliderRadius = value;
		}
		return true;
	}

	EditorComponent* materialComponent = FindMaterialComponent(gameObject);
	if (materialComponent == nullptr) {
		return false;
	}

	switch (target) {
	case AnimationPropertyTarget::MaterialBaseColorR: materialComponent->color.x = value; return true;
	case AnimationPropertyTarget::MaterialBaseColorG: materialComponent->color.y = value; return true;
	case AnimationPropertyTarget::MaterialBaseColorB: materialComponent->color.z = value; return true;
	case AnimationPropertyTarget::MaterialMetallic: materialComponent->metallic = value; return true;
	case AnimationPropertyTarget::MaterialRoughness: materialComponent->roughness = value; return true;
	case AnimationPropertyTarget::MaterialAlpha: materialComponent->alpha = value; return true;
	case AnimationPropertyTarget::MaterialEmissionStrength: materialComponent->emissionStrength = value; return true;
	case AnimationPropertyTarget::MaterialEmissionColorR: materialComponent->emissionColor.x = value; return true;
	case AnimationPropertyTarget::MaterialEmissionColorG: materialComponent->emissionColor.y = value; return true;
	case AnimationPropertyTarget::MaterialEmissionColorB: materialComponent->emissionColor.z = value; return true;
	default: return false;
	}
}

void EditorAnimationWindowManager::SynchronizeRenderedScene() {
	g_editorSceneSynchronizer.Update(g_editorTextureFilePaths, g_selectedPlacedSceneObjectIndex);
}
