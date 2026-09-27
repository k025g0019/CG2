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
#include <initializer_list>
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
	SynchronizeSelectedGraphAsset();

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

	if (ImGui::BeginTabBar("AnimationWindowTabs")) {
		if (ImGui::BeginTabItem("Clip (.animclip)")) {
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
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Animator Graph (.animgraph)")) {
			DrawAnimatorGraphTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

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

//================================================================
// Animator Graph (.animgraph) 編集
//================================================================

void EditorAnimationWindowManager::SynchronizeSelectedGraphAsset() {
	std::string requestedPath;

	if (EditorAssetUtility::HasExtension(g_selectedAssetPath, ".animgraph")) {
		requestedPath = g_selectedAssetPath;
	}
	else {
		const EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
		if (gameObject != nullptr) {
			const EditorComponent* animatorComponent = EditorComponentUtility::FindComponent(
				*gameObject,
				EditorComponentType::Animator);

			if (animatorComponent != nullptr &&
				EditorAssetUtility::HasExtension(animatorComponent->assetPath, ".animgraph")) {
				requestedPath = animatorComponent->assetPath;
			}
		}
	}

	// 未保存の編集を選択変更で破棄しない。保存するまで現在の Graph を保持する。
	if (!requestedPath.empty() && requestedPath != animationGraphPath_ && !isGraphDirty_) {
		LoadAnimationGraph(requestedPath);
	}

	RefreshGraphClipNames();
}

void EditorAnimationWindowManager::RefreshGraphClipNames() {
	graphClipNames_.clear();
	const EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);

	if (gameObject == nullptr) {
		return;
	}

	// Animator が実行時に Clip を読む経路（ModelRenderer -> SkinnedMeshRenderer -> MeshFilter）と
	// 同じ優先順で Model を特定し、Editor でも同じ Clip 番号を見せる。
	std::string modelAssetPath;
	for (const EditorComponentType componentType : {
			EditorComponentType::ModelRenderer,
			EditorComponentType::SkinnedMeshRenderer,
			EditorComponentType::MeshFilter}) {
		const EditorComponent* component = EditorComponentUtility::FindComponent(*gameObject, componentType);

		if (component != nullptr && !component->assetPath.empty()) {
			modelAssetPath = component->assetPath;
			break;
		}
	}

	if (modelAssetPath.empty()) {
		return;
	}

	const ModelData* modelData = EditorAssetUtility::GetSharedModelAssetData(modelAssetPath, true);
	if (modelData == nullptr) {
		return;
	}

	for (const ModelAnimationClipData& clip : modelData->animationClips) {
		graphClipNames_.push_back(clip.name);
	}
}

bool EditorAnimationWindowManager::LoadAnimationGraph(const std::string& filePath) {
	AnimationGraph loadedGraph{};

	if (!loadedGraph.LoadFromJson(filePath)) {
		// 壊れた .animgraph を選んでも編集中の内容は壊さず、UI 側で理由を出すだけにする。
		animationGraphPath_ = filePath;
		hasGraphLoadFailed_ = true;
		g_editorConsoleMessages.push_back("Animator Graph: 読み込み失敗: " + filePath);
		return false;
	}

	animationGraph_ = std::move(loadedGraph);
	animationGraphPath_ = filePath;
	selectedStateIndex_ = animationGraph_.states.empty() ? -1 : 0;
	selectedTransitionIndex_ = animationGraph_.transitions.empty() ? -1 : 0;
	selectedParameterIndex_ = animationGraph_.parameters.empty() ? -1 : 0;
	isGraphDirty_ = false;
	hasGraphLoadFailed_ = false;
	g_editorConsoleMessages.push_back("Animator Graph: 開きました: " + filePath);
	return true;
}

void EditorAnimationWindowManager::SaveAnimationGraph() {
	if (animationGraphPath_.empty()) {
		g_editorConsoleMessages.push_back("Animator Graph: 保存先 .animgraph が選択されていません");
		return;
	}

	if (animationGraph_.SaveToJson(animationGraphPath_)) {
		isGraphDirty_ = false;
		hasGraphLoadFailed_ = false;
		g_editorConsoleMessages.push_back("Animator Graph: 保存しました: " + animationGraphPath_);
	}
	else {
		g_editorConsoleMessages.push_back("Animator Graph: 保存失敗: " + animationGraphPath_);
	}
}

void EditorAnimationWindowManager::CreateAnimationGraph() {
	const std::filesystem::path animationDirectory = std::filesystem::path("Assets") / "Animation";
	std::error_code fileSystemError;
	std::filesystem::create_directories(animationDirectory, fileSystemError);

	if (fileSystemError) {
		g_editorConsoleMessages.push_back("Animator Graph: Assets/Animation フォルダーを作成できません");
		return;
	}

	std::filesystem::path graphPath = animationDirectory / "NewAnimatorGraph.animgraph";
	int32_t duplicateNumber = 1;

	while (std::filesystem::exists(graphPath)) {
		graphPath = animationDirectory /
			("NewAnimatorGraph_" + std::to_string(duplicateNumber) + ".animgraph");
		++duplicateNumber;
	}

	// Idle 1 State と Speed Parameter だけの、そのまま Play できる最小構成から始める。
	AnimationGraph newGraph{};
	newGraph.entryState = 0;

	AnimationGraphParameter speedParameter{};
	speedParameter.name = "Speed";
	speedParameter.defaultValue.type = AnimatorParameterType::Float;
	newGraph.parameters.push_back(speedParameter);

	AnimationGraphState idleState{};
	idleState.name = "Idle";
	idleState.clipIndex = 0;
	idleState.loop = true;
	newGraph.states.push_back(idleState);

	animationGraph_ = std::move(newGraph);
	animationGraphPath_ = graphPath.generic_string();
	selectedStateIndex_ = 0;
	selectedTransitionIndex_ = -1;
	selectedParameterIndex_ = 0;
	hasGraphLoadFailed_ = false;

	if (!animationGraph_.SaveToJson(animationGraphPath_)) {
		g_editorConsoleMessages.push_back("Animator Graph: 新規 Graph を保存できません: " + animationGraphPath_);
		animationGraphPath_.clear();
		return;
	}

	isGraphDirty_ = false;
	g_selectedAssetPath = animationGraphPath_;
	AssignGraphToSelectedGameObject();
	g_editorConsoleMessages.push_back("Animator Graph: 新規作成しました: " + animationGraphPath_);
}

void EditorAnimationWindowManager::AssignGraphToSelectedGameObject() {
	if (animationGraphPath_.empty() || g_selectedEditorGameObjectId < 0) {
		return;
	}

	EditorGameObject* gameObject = g_editorScene.FindGameObject(g_selectedEditorGameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	if (EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::Animator) == nullptr) {
		g_editorScene.AddComponent(gameObject->id, EditorComponentType::Animator);
	}

	EditorComponent* animatorComponent = EditorComponentUtility::FindComponent(
		*gameObject,
		EditorComponentType::Animator);

	if (animatorComponent != nullptr) {
		animatorComponent->assetPath = animationGraphPath_;
		g_editorConsoleMessages.push_back(
			"Animator Graph: " + gameObject->name + " へ設定しました: " + animationGraphPath_);
	}
}

void EditorAnimationWindowManager::DeleteGraphState(int32_t stateIndex) {
	if (stateIndex < 0 || stateIndex >= static_cast<int32_t>(animationGraph_.states.size())) {
		return;
	}

	// State が 0 個の Graph は読み直せなくなるため、最後の 1 つは消させない。
	if (animationGraph_.states.size() <= 1u) {
		g_editorConsoleMessages.push_back("Animator Graph: State は最低 1 つ必要です");
		return;
	}

	animationGraph_.states.erase(animationGraph_.states.begin() + stateIndex);

	// 削除した State を参照する Transition を落とし、後ろの State を指す番号を 1 つ前へ詰める。
	for (auto transitionIterator = animationGraph_.transitions.begin();
		 transitionIterator != animationGraph_.transitions.end();) {
		if (transitionIterator->sourceState == stateIndex ||
			transitionIterator->destinationState == stateIndex) {
			transitionIterator = animationGraph_.transitions.erase(transitionIterator);
			continue;
		}

		if (transitionIterator->sourceState > stateIndex) {
			transitionIterator->sourceState--;
		}

		if (transitionIterator->destinationState > stateIndex) {
			transitionIterator->destinationState--;
		}

		++transitionIterator;
	}

	if (animationGraph_.entryState == stateIndex) {
		animationGraph_.entryState = 0;
	}
	else if (animationGraph_.entryState > stateIndex) {
		animationGraph_.entryState--;
	}

	selectedStateIndex_ = (std::min)(stateIndex, static_cast<int32_t>(animationGraph_.states.size()) - 1);
	selectedTransitionIndex_ = animationGraph_.transitions.empty() ? -1 : 0;
	isGraphDirty_ = true;
}

void EditorAnimationWindowManager::DrawClipIndexRow(const char* label, int32_t& clipIndex) {
#ifdef USE_IMGUI
	const int32_t clipCount = static_cast<int32_t>(graphClipNames_.size());

	if (clipCount <= 0) {
		// Model 未設定・Clip 無しでも番号だけは編集できるようにする（後から Model を差す運用があるため）。
		if (ImGui::DragInt(label, &clipIndex, 1.0f, 0, 1024)) {
			clipIndex = (std::max)(clipIndex, 0);
			isGraphDirty_ = true;
		}
		return;
	}

	const std::string previewLabel = clipIndex >= 0 && clipIndex < clipCount
		? std::to_string(clipIndex) + ": " + graphClipNames_[static_cast<size_t>(clipIndex)]
		: std::to_string(clipIndex) + ": (Clip が見つかりません)";

	if (ImGui::BeginCombo(label, previewLabel.c_str())) {
		for (int32_t candidateIndex = 0; candidateIndex < clipCount; ++candidateIndex) {
			const std::string itemLabel =
				std::to_string(candidateIndex) + ": " + graphClipNames_[static_cast<size_t>(candidateIndex)];

			if (ImGui::Selectable(itemLabel.c_str(), candidateIndex == clipIndex)) {
				clipIndex = candidateIndex;
				isGraphDirty_ = true;
			}
		}

		ImGui::EndCombo();
	}

	if (clipIndex < 0 || clipIndex >= clipCount) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
			"Clip 番号 %d は Model に存在しません（実行時は Pose なしとして安全に無視されます）",
			clipIndex);
	}
#else
	(void)label;
	(void)clipIndex;
#endif
}

void EditorAnimationWindowManager::DrawAnimatorGraphTab() {
#ifdef USE_IMGUI
	DrawGraphToolbar();
	ImGui::Separator();

	const float leftPanelWidth = (std::clamp)(ImGui::GetContentRegionAvail().x * 0.34f, 260.0f, 460.0f);

	if (ImGui::BeginChild("AnimatorGraphLeftPanel", ImVec2(leftPanelWidth, 420.0f), true)) {
		DrawGraphParameterList();
		ImGui::Separator();
		DrawGraphStateList();
		ImGui::Separator();
		DrawGraphTransitionList();
	}
	ImGui::EndChild();
	ImGui::SameLine();

	if (ImGui::BeginChild("AnimatorGraphRightPanel", ImVec2(0.0f, 420.0f), true)) {
		DrawGraphStateEditor();
		ImGui::Separator();
		DrawGraphTransitionEditor();
		ImGui::Separator();
		DrawGraphEventList();
	}
	ImGui::EndChild();
#endif
}

void EditorAnimationWindowManager::DrawGraphEventList() {
#ifdef USE_IMGUI
	if (!ImGui::CollapsingHeader("Animation Event")) {
		return;
	}

	ImGui::TextDisabled("指定 Clip の再生がこの時刻を通過した瞬間に、C++ Script と Effect へ通知します。");

	if (ImGui::Button("イベント追加")) {
		AnimationGraphEvent animationEvent{};
		animationEvent.name = "AnimationEvent";
		animationEvent.clipIndex = selectedStateIndex_ >= 0 &&
			selectedStateIndex_ < static_cast<int32_t>(animationGraph_.states.size())
			? animationGraph_.states[static_cast<size_t>(selectedStateIndex_)].clipIndex
			: 0;
		animationGraph_.events.push_back(animationEvent);
		isGraphDirty_ = true;
	}

	for (int32_t eventIndex = 0;
		 eventIndex < static_cast<int32_t>(animationGraph_.events.size());
		 ++eventIndex) {
		AnimationGraphEvent& animationEvent = animationGraph_.events[static_cast<size_t>(eventIndex)];
		ImGui::PushID(3000 + eventIndex);
		ImGui::Separator();

		char nameBuffer[128]{};
		strncpy_s(nameBuffer, animationEvent.name.c_str(), _TRUNCATE);

		if (ImGui::InputText("イベント名", nameBuffer, sizeof(nameBuffer))) {
			animationEvent.name = nameBuffer;
			isGraphDirty_ = true;
		}

		DrawClipIndexRow("対象 Clip", animationEvent.clipIndex);

		if (ImGui::DragFloat("発火時刻 (秒)", &animationEvent.time, 0.01f, 0.0f, 600.0f)) {
			animationEvent.time = (std::max)(animationEvent.time, 0.0f);
			isGraphDirty_ = true;
		}

		char effectPathBuffer[260]{};
		strncpy_s(effectPathBuffer, animationEvent.effectAssetPath.c_str(), _TRUNCATE);

		if (ImGui::InputText("再生 Effect (.effect)", effectPathBuffer, sizeof(effectPathBuffer))) {
			animationEvent.effectAssetPath = effectPathBuffer;
			isGraphDirty_ = true;
		}

		if (EditorAssetUtility::HasExtension(g_selectedAssetPath, ".effect") &&
			ImGui::Button("選択中 Effect を設定")) {
			animationEvent.effectAssetPath = g_selectedAssetPath;
			isGraphDirty_ = true;
		}

		if (ImGui::DragFloat3("発生位置オフセット", &animationEvent.localOffset.x, 0.01f)) {
			isGraphDirty_ = true;
		}

		if (ImGui::Button("このイベントを削除")) {
			animationGraph_.events.erase(animationGraph_.events.begin() + eventIndex);
			isGraphDirty_ = true;
			ImGui::PopID();
			break;
		}

		ImGui::PopID();
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphToolbar() {
#ifdef USE_IMGUI
	ImGui::Text(
		"Graph: %s%s",
		animationGraphPath_.empty() ? "未選択" : animationGraphPath_.c_str(),
		isGraphDirty_ ? " *" : "");

	if (hasGraphLoadFailed_) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
			"この .animgraph は読み込めませんでした（JSON が壊れている可能性があります）。"
			"新規作成するか、保存で上書きできます。");
	}

	if (ImGui::Button("新規Graph", ImVec2(100.0f, 0.0f))) {
		CreateAnimationGraph();
	}
	ImGui::SameLine();

	if (ImGui::Button("保存", ImVec2(80.0f, 0.0f))) {
		SaveAnimationGraph();
	}
	ImGui::SameLine();

	if (ImGui::Button("再読込", ImVec2(80.0f, 0.0f)) && !animationGraphPath_.empty()) {
		isGraphDirty_ = false;
		LoadAnimationGraph(animationGraphPath_);
	}
	ImGui::SameLine();

	if (ImGui::Button("選択オブジェクトへ設定", ImVec2(180.0f, 0.0f))) {
		AssignGraphToSelectedGameObject();
	}

	// Play 中は実行側の現在 State を並べて出し、条件が意図通り効いているかをその場で確認できるようにする。
	if (g_editorRuntimeManager.IsPlaying() && g_selectedEditorGameObjectId >= 0) {
		const std::string runtimeStateName =
			g_editorRuntimeManager.GetAnimationManager().GetAnimatorStateName(g_selectedEditorGameObjectId);
		ImGui::TextColored(
			ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
			"実行中 State: %s",
			runtimeStateName.empty() ? "(Animator なし)" : runtimeStateName.c_str());
		ImGui::TextDisabled("実行中パラメータの確認と変更は Inspector の Animator から行えます。");
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphParameterList() {
#ifdef USE_IMGUI
	ImGui::TextUnformatted("パラメータ");

	if (ImGui::Button("パラメータ追加", ImVec2(-1.0f, 0.0f))) {
		AnimationGraphParameter parameter{};
		parameter.name = "NewParameter" + std::to_string(animationGraph_.parameters.size());
		parameter.defaultValue.type = AnimatorParameterType::Float;
		animationGraph_.parameters.push_back(parameter);
		selectedParameterIndex_ = static_cast<int32_t>(animationGraph_.parameters.size()) - 1;
		isGraphDirty_ = true;
	}

	for (int32_t parameterIndex = 0;
		 parameterIndex < static_cast<int32_t>(animationGraph_.parameters.size());
		 ++parameterIndex) {
		AnimationGraphParameter& parameter = animationGraph_.parameters[static_cast<size_t>(parameterIndex)];
		ImGui::PushID(parameterIndex);

		const std::string parameterLabel =
			parameter.name + "  [" + GetAnimatorParameterTypeName(parameter.defaultValue.type) + "]";

		if (ImGui::Selectable(parameterLabel.c_str(), selectedParameterIndex_ == parameterIndex)) {
			selectedParameterIndex_ = parameterIndex;
		}

		ImGui::PopID();
	}

	if (selectedParameterIndex_ >= 0 &&
		selectedParameterIndex_ < static_cast<int32_t>(animationGraph_.parameters.size())) {
		AnimationGraphParameter& parameter =
			animationGraph_.parameters[static_cast<size_t>(selectedParameterIndex_)];

		char nameBuffer[128]{};
		strncpy_s(nameBuffer, parameter.name.c_str(), _TRUNCATE);

		if (ImGui::InputText("名前", nameBuffer, sizeof(nameBuffer))) {
			parameter.name = nameBuffer;
			isGraphDirty_ = true;
		}

		int32_t parameterType = static_cast<int32_t>(parameter.defaultValue.type);
		const char* parameterTypeItems[] = {"Float", "Int", "Bool", "Trigger", "Vector2", "Vector3"};

		if (ImGui::Combo("型", &parameterType, parameterTypeItems, _countof(parameterTypeItems))) {
			parameter.defaultValue.type = static_cast<AnimatorParameterType>(parameterType);
			isGraphDirty_ = true;
		}

		switch (parameter.defaultValue.type) {
		case AnimatorParameterType::Int:
			if (ImGui::DragInt("既定値", &parameter.defaultValue.intValue)) {
				isGraphDirty_ = true;
			}
			break;
		case AnimatorParameterType::Bool:
		case AnimatorParameterType::Trigger:
			if (ImGui::Checkbox("既定値", &parameter.defaultValue.boolValue)) {
				isGraphDirty_ = true;
			}
			break;
		case AnimatorParameterType::Vector2:
			if (ImGui::DragFloat2("既定値", &parameter.defaultValue.vector2Value.x, 0.01f)) {
				isGraphDirty_ = true;
			}
			break;
		case AnimatorParameterType::Vector3:
			if (ImGui::DragFloat3("既定値", &parameter.defaultValue.vector3Value.x, 0.01f)) {
				isGraphDirty_ = true;
			}
			break;
		case AnimatorParameterType::Float:
		default:
			if (ImGui::DragFloat("既定値", &parameter.defaultValue.floatValue, 0.01f)) {
				isGraphDirty_ = true;
			}
			break;
		}

		if (ImGui::Button("このパラメータを削除", ImVec2(-1.0f, 0.0f))) {
			animationGraph_.parameters.erase(
				animationGraph_.parameters.begin() + selectedParameterIndex_);
			selectedParameterIndex_ = animationGraph_.parameters.empty()
				? -1
				: (std::min)(selectedParameterIndex_, static_cast<int32_t>(animationGraph_.parameters.size()) - 1);
			isGraphDirty_ = true;
		}
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphStateList() {
#ifdef USE_IMGUI
	ImGui::TextUnformatted("State");

	if (ImGui::Button("State 追加", ImVec2(-1.0f, 0.0f))) {
		AnimationGraphState state{};
		state.name = "NewState" + std::to_string(animationGraph_.states.size());
		state.clipIndex = 0;
		state.loop = true;
		animationGraph_.states.push_back(state);
		selectedStateIndex_ = static_cast<int32_t>(animationGraph_.states.size()) - 1;
		isGraphDirty_ = true;
	}

	for (int32_t stateIndex = 0;
		 stateIndex < static_cast<int32_t>(animationGraph_.states.size());
		 ++stateIndex) {
		const AnimationGraphState& state = animationGraph_.states[static_cast<size_t>(stateIndex)];
		ImGui::PushID(stateIndex);

		const std::string stateLabel = std::to_string(stateIndex) + ": " + state.name +
			(stateIndex == animationGraph_.entryState ? "  [Entry]" : "") +
			"  (" + GetAnimationBlendTreeTypeName(state.blendTreeType) + ")";

		if (ImGui::Selectable(stateLabel.c_str(), selectedStateIndex_ == stateIndex)) {
			selectedStateIndex_ = stateIndex;
		}

		ImGui::PopID();
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphStateEditor() {
#ifdef USE_IMGUI
	if (selectedStateIndex_ < 0 ||
		selectedStateIndex_ >= static_cast<int32_t>(animationGraph_.states.size())) {
		ImGui::TextDisabled("左の一覧から State を選ぶと、ここで Clip と Blend Tree を編集できます。");
		return;
	}

	AnimationGraphState& state = animationGraph_.states[static_cast<size_t>(selectedStateIndex_)];
	ImGui::Text("State %d の設定", selectedStateIndex_);

	char nameBuffer[128]{};
	strncpy_s(nameBuffer, state.name.c_str(), _TRUNCATE);

	if (ImGui::InputText("State 名", nameBuffer, sizeof(nameBuffer))) {
		state.name = nameBuffer;
		isGraphDirty_ = true;
	}

	bool isEntryState = animationGraph_.entryState == selectedStateIndex_;
	if (ImGui::Checkbox("Entry State にする", &isEntryState) && isEntryState) {
		animationGraph_.entryState = selectedStateIndex_;
		isGraphDirty_ = true;
	}

	if (ImGui::DragFloat("再生速度", &state.playbackSpeed, 0.01f, -10.0f, 10.0f)) {
		isGraphDirty_ = true;
	}

	if (ImGui::Checkbox("ループ", &state.loop)) {
		isGraphDirty_ = true;
	}

	int32_t blendTreeType = static_cast<int32_t>(state.blendTreeType);
	const char* blendTreeItems[] = {
		"単一 Clip",
		"1D Blend",
		"2D 方向 Blend",
		"2D 座標 Blend",
		"Direct Blend"};

	if (ImGui::Combo("Blend Tree", &blendTreeType, blendTreeItems, _countof(blendTreeItems))) {
		state.blendTreeType = static_cast<AnimationBlendTreeType>(blendTreeType);
		isGraphDirty_ = true;
	}

	if (state.blendTreeType == AnimationBlendTreeType::Clip) {
		DrawClipIndexRow("再生 Clip", state.clipIndex);
	}
	else {
		char blendParameterBuffer[128]{};

		if (state.blendTreeType == AnimationBlendTreeType::Blend1D) {
			strncpy_s(blendParameterBuffer, state.blendParameter.c_str(), _TRUNCATE);
			if (ImGui::InputText("Blend パラメータ", blendParameterBuffer, sizeof(blendParameterBuffer))) {
				state.blendParameter = blendParameterBuffer;
				isGraphDirty_ = true;
			}
			ImGui::TextDisabled("Sample は X 座標を Blend 値として昇順に評価します。");
		}
		else if (state.blendTreeType == AnimationBlendTreeType::Blend2DDirectional ||
			state.blendTreeType == AnimationBlendTreeType::Blend2DCartesian) {
			strncpy_s(blendParameterBuffer, state.blendParameterX.c_str(), _TRUNCATE);
			if (ImGui::InputText("X パラメータ", blendParameterBuffer, sizeof(blendParameterBuffer))) {
				state.blendParameterX = blendParameterBuffer;
				isGraphDirty_ = true;
			}

			strncpy_s(blendParameterBuffer, state.blendParameterY.c_str(), _TRUNCATE);
			if (ImGui::InputText("Y パラメータ", blendParameterBuffer, sizeof(blendParameterBuffer))) {
				state.blendParameterY = blendParameterBuffer;
				isGraphDirty_ = true;
			}
		}
		else {
			ImGui::TextDisabled("Direct Blend は Sample ごとの Weight パラメータを直接ウェイトとして使います。");
		}

		if (ImGui::Button("Blend Sample 追加")) {
			AnimationBlendSample sample{};
			sample.clipIndex = state.clipIndex;
			state.blendSamples.push_back(sample);
			isGraphDirty_ = true;
		}

		for (int32_t sampleIndex = 0;
			 sampleIndex < static_cast<int32_t>(state.blendSamples.size());
			 ++sampleIndex) {
			AnimationBlendSample& sample = state.blendSamples[static_cast<size_t>(sampleIndex)];
			ImGui::PushID(sampleIndex);
			ImGui::Separator();
			ImGui::Text("Sample %d", sampleIndex);
			DrawClipIndexRow("Clip", sample.clipIndex);

			if (state.blendTreeType == AnimationBlendTreeType::Blend1D) {
				if (ImGui::DragFloat("Blend 位置", &sample.position.x, 0.01f)) {
					isGraphDirty_ = true;
				}
			}
			else if (state.blendTreeType == AnimationBlendTreeType::Direct) {
				char weightParameterBuffer[128]{};
				strncpy_s(weightParameterBuffer, sample.weightParameter.c_str(), _TRUNCATE);

				if (ImGui::InputText("Weight パラメータ", weightParameterBuffer, sizeof(weightParameterBuffer))) {
					sample.weightParameter = weightParameterBuffer;
					isGraphDirty_ = true;
				}
			}
			else {
				if (ImGui::DragFloat2("Blend 位置 (X, Y)", &sample.position.x, 0.01f)) {
					isGraphDirty_ = true;
				}
			}

			if (ImGui::DragFloat("再生速度", &sample.playbackSpeed, 0.01f, -10.0f, 10.0f)) {
				isGraphDirty_ = true;
			}

			if (ImGui::Button("この Sample を削除")) {
				state.blendSamples.erase(state.blendSamples.begin() + sampleIndex);
				isGraphDirty_ = true;
				ImGui::PopID();
				break;
			}

			ImGui::PopID();
		}
	}

	ImGui::Separator();

	if (ImGui::Button("この State を削除", ImVec2(-1.0f, 0.0f))) {
		DeleteGraphState(selectedStateIndex_);
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphTransitionList() {
#ifdef USE_IMGUI
	ImGui::TextUnformatted("Transition");

	const bool canAddTransition = animationGraph_.states.size() >= 1u;

	if (canAddTransition && ImGui::Button("Transition 追加", ImVec2(-1.0f, 0.0f))) {
		AnimationGraphTransition transition{};
		transition.sourceState = (std::max)(selectedStateIndex_, 0);
		transition.destinationState =
			(std::min)(transition.sourceState + 1, static_cast<int32_t>(animationGraph_.states.size()) - 1);
		animationGraph_.transitions.push_back(transition);
		selectedTransitionIndex_ = static_cast<int32_t>(animationGraph_.transitions.size()) - 1;
		isGraphDirty_ = true;
	}

	for (int32_t transitionIndex = 0;
		 transitionIndex < static_cast<int32_t>(animationGraph_.transitions.size());
		 ++transitionIndex) {
		const AnimationGraphTransition& transition =
			animationGraph_.transitions[static_cast<size_t>(transitionIndex)];
		ImGui::PushID(1000 + transitionIndex);

		const auto stateNameOf = [this](int32_t stateIndex) -> std::string {
			if (stateIndex < 0) {
				return "Any State";
			}

			if (stateIndex >= static_cast<int32_t>(animationGraph_.states.size())) {
				return "(不明)";
			}

			return animationGraph_.states[static_cast<size_t>(stateIndex)].name;
		};

		const std::string transitionLabel =
			stateNameOf(transition.sourceState) + " -> " + stateNameOf(transition.destinationState) +
			"  (条件 " + std::to_string(transition.conditions.size()) + ")";

		if (ImGui::Selectable(transitionLabel.c_str(), selectedTransitionIndex_ == transitionIndex)) {
			selectedTransitionIndex_ = transitionIndex;
		}

		ImGui::PopID();
	}
#endif
}

void EditorAnimationWindowManager::DrawGraphTransitionEditor() {
#ifdef USE_IMGUI
	if (selectedTransitionIndex_ < 0 ||
		selectedTransitionIndex_ >= static_cast<int32_t>(animationGraph_.transitions.size())) {
		ImGui::TextDisabled("左の一覧から Transition を選ぶと、ここで遷移条件を編集できます。");
		return;
	}

	AnimationGraphTransition& transition =
		animationGraph_.transitions[static_cast<size_t>(selectedTransitionIndex_)];
	ImGui::Text("Transition %d の設定", selectedTransitionIndex_);

	std::vector<std::string> stateNames;
	stateNames.reserve(animationGraph_.states.size());
	for (int32_t stateIndex = 0;
		 stateIndex < static_cast<int32_t>(animationGraph_.states.size());
		 ++stateIndex) {
		stateNames.push_back(
			std::to_string(stateIndex) + ": " + animationGraph_.states[static_cast<size_t>(stateIndex)].name);
	}

	// 遷移元だけは Any State(-1) を選べる。Runtime は sourceState < 0 を
	// 「どの State からでも遷移可」として扱うため、Jump / Attack をここで表現できる。
	const auto drawStateCombo = [&](const char* label, int32_t& stateIndex, bool allowAnyState) {
		std::string preview = "(不明な State)";

		if (allowAnyState && stateIndex < 0) {
			preview = "Any State (どの State からでも)";
		}
		else if (stateIndex >= 0 && stateIndex < static_cast<int32_t>(stateNames.size())) {
			preview = stateNames[static_cast<size_t>(stateIndex)];
		}

		if (ImGui::BeginCombo(label, preview.c_str())) {
			if (allowAnyState &&
				ImGui::Selectable("Any State (どの State からでも)", stateIndex < 0)) {
				stateIndex = -1;
				isGraphDirty_ = true;
			}

			for (int32_t candidateIndex = 0;
				 candidateIndex < static_cast<int32_t>(stateNames.size());
				 ++candidateIndex) {
				if (ImGui::Selectable(
						stateNames[static_cast<size_t>(candidateIndex)].c_str(),
						candidateIndex == stateIndex)) {
					stateIndex = candidateIndex;
					isGraphDirty_ = true;
				}
			}

			ImGui::EndCombo();
		}
	};

	drawStateCombo("遷移元", transition.sourceState, true);
	drawStateCombo("遷移先", transition.destinationState, false);

	if (ImGui::DragFloat("遷移秒 (Cross Fade)", &transition.blendDuration, 0.01f, 0.0f, 5.0f)) {
		transition.blendDuration = (std::max)(transition.blendDuration, 0.0f);
		isGraphDirty_ = true;
	}

	if (ImGui::Checkbox("Exit Time を使う", &transition.hasExitTime)) {
		isGraphDirty_ = true;
	}

	if (transition.hasExitTime) {
		if (ImGui::DragFloat("Exit Time (再生比率)", &transition.exitTime, 0.01f, 0.0f, 10.0f)) {
			transition.exitTime = (std::max)(transition.exitTime, 0.0f);
			isGraphDirty_ = true;
		}
	}

	if (ImGui::Checkbox("遷移中でも割り込み可能", &transition.canInterrupt)) {
		isGraphDirty_ = true;
	}

	ImGui::Separator();
	ImGui::TextUnformatted("遷移条件 (すべて満たしたときに遷移)");

	if (ImGui::Button("条件を追加")) {
		AnimationTransitionCondition condition{};
		condition.parameterName = animationGraph_.parameters.empty()
			? std::string("Speed")
			: animationGraph_.parameters.front().name;
		transition.conditions.push_back(condition);
		isGraphDirty_ = true;
	}

	for (int32_t conditionIndex = 0;
		 conditionIndex < static_cast<int32_t>(transition.conditions.size());
		 ++conditionIndex) {
		AnimationTransitionCondition& condition =
			transition.conditions[static_cast<size_t>(conditionIndex)];
		ImGui::PushID(2000 + conditionIndex);
		ImGui::Separator();

		// Parameter 名は Graph に宣言済みのものから選ばせ、綴り間違いで条件が効かない事故を防ぐ。
		const std::string parameterPreview =
			condition.parameterName.empty() ? "(未設定)" : condition.parameterName;

		if (ImGui::BeginCombo("パラメータ", parameterPreview.c_str())) {
			for (const AnimationGraphParameter& parameter : animationGraph_.parameters) {
				if (ImGui::Selectable(parameter.name.c_str(), parameter.name == condition.parameterName)) {
					condition.parameterName = parameter.name;
					isGraphDirty_ = true;
				}
			}

			// Inspector 側が常に用意する標準 Parameter も選べるようにする。
			for (const char* standardParameterName : {"MoveX", "MoveY", "Speed"}) {
				if (ImGui::Selectable(standardParameterName, condition.parameterName == standardParameterName)) {
					condition.parameterName = standardParameterName;
					isGraphDirty_ = true;
				}
			}

			ImGui::EndCombo();
		}

		int32_t conditionOperator = static_cast<int32_t>(condition.conditionOperator);
		const char* conditionOperatorItems[] = {
			"より大きい (Greater)",
			"より小さい (Less)",
			"等しい (Equal)",
			"等しくない (NotEqual)",
			"True",
			"False",
			"Trigger 発火 (Triggered)"};

		if (ImGui::Combo("条件", &conditionOperator, conditionOperatorItems, _countof(conditionOperatorItems))) {
			condition.conditionOperator = static_cast<AnimationConditionOperator>(conditionOperator);
			isGraphDirty_ = true;
		}

		if (condition.conditionOperator == AnimationConditionOperator::Greater ||
			condition.conditionOperator == AnimationConditionOperator::Less ||
			condition.conditionOperator == AnimationConditionOperator::Equal ||
			condition.conditionOperator == AnimationConditionOperator::NotEqual) {
			if (ImGui::DragFloat("Float しきい値", &condition.floatThreshold, 0.01f)) {
				isGraphDirty_ = true;
			}

			if (ImGui::DragInt("Int しきい値", &condition.intThreshold)) {
				isGraphDirty_ = true;
			}
		}

		if (ImGui::Button("この条件を削除")) {
			transition.conditions.erase(transition.conditions.begin() + conditionIndex);
			isGraphDirty_ = true;
			ImGui::PopID();
			break;
		}

		ImGui::PopID();
	}

	ImGui::Separator();

	if (ImGui::Button("この Transition を削除", ImVec2(-1.0f, 0.0f))) {
		animationGraph_.transitions.erase(animationGraph_.transitions.begin() + selectedTransitionIndex_);
		selectedTransitionIndex_ = animationGraph_.transitions.empty()
			? -1
			: (std::min)(selectedTransitionIndex_, static_cast<int32_t>(animationGraph_.transitions.size()) - 1);
		isGraphDirty_ = true;
	}
#endif
}
